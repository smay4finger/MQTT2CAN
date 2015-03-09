/*
 * Copyright (C) 2015  Stefan May <smay@4finger.net>
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <getopt.h>
#include <sys/ioctl.h>
#include <linux/can.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <mosquitto.h>

int debug = 0;

char* can_interface = NULL;

bool access_read = false;
bool access_write = false;

char* broker_hostname = "localhost";
int broker_port = 1883;
char* broker_username = NULL;
char* broker_password = NULL;

char* mqtt_topic_prefix = NULL;
char* here = NULL;

int can_fd;

static void exithelp(char* progname, int exit_status) {
    fprintf(stderr,
        "%s -i interface [-h hostname] [-p port] [-t topic]\n"
        "  -i interface  CAN interface\n"
        "  -r            read from CAN\n"
        "  -w            write to CAN\n"
        "  -h hostname   MQTT broker host\n"
        "  -p port       MQTT broker port\n"
        "  -t topic      MQTT topic prefix (default is can/hostname/interface)\n"
        "  -d            debug (use multiple times for more debug messages)\n"
        , progname);
    exit(exit_status);
}

void parse_options(int argc, char** argv)
{
    int opt;
    while ( (opt = getopt(argc, argv, "i:rwh:p:t:U:P:u:d")) != -1 ) {
        switch ( opt ) {
        case 'i':
            can_interface = optarg;
            break;
        case 'r':
            access_read = true;
            break;
        case 'w':
            access_write = true;
            break;
        case 'h':
            broker_hostname = optarg;
            break;
        case 'p':
            broker_port = atoi(optarg);
            break;
        case 't':
            mqtt_topic_prefix = optarg;
            break;
        case 'U':
            broker_username = optarg;
            break;
        case 'P':
            broker_password = strdup(optarg);
            while(*optarg) *optarg++ = 'x';
            break;
        case 'u':
            here = optarg;
            break;
        case 'd':
            debug++;
            break;
        default:
            exithelp(argv[0], 0);
        }
    }

    if ( can_interface == NULL ) {
        fprintf(stderr, "please specify CAN interface\n\n");
        exithelp(argv[0], 0);
    }

    char hostname[1024];
    if ( gethostname(hostname, sizeof(hostname)) ) {
        perror("failed to get hostname");
        exit(EXIT_FAILURE);
    }
    for ( char* p = hostname; *p; p++) *p = tolower(*p);

    if ( here == NULL ) {
        size_t len = strlen(hostname) + strlen(":") + strlen(can_interface);
        if ( (here = malloc(len+1)) == NULL ) {
            perror("failed to allocate memory");
            exit(EXIT_FAILURE);
        }
        strcpy(here, hostname);
        strcat(here, ":");
        strcat(here, can_interface);
    }

    if ( mqtt_topic_prefix == NULL ) {
        size_t len = strlen("can/") + strlen(hostname) + strlen("/") + strlen(can_interface);
        if ( (mqtt_topic_prefix = malloc(len+1)) == NULL ) {
            perror("failed to allocate mqtt_topic_prefix");
            exit(EXIT_FAILURE);
        }
        strcpy(mqtt_topic_prefix, "can/");
        strcat(mqtt_topic_prefix, hostname);
        strcat(mqtt_topic_prefix, "/");
        strcat(mqtt_topic_prefix, can_interface);
    }

    if ( !access_write && !access_read ) {
        access_read = access_write = true;
    }

    if ( debug > 2 ) {
        printf("CAN interface %s\n", can_interface);
        printf("using broker at %s:%d\n", broker_hostname, broker_port);
        printf("MQTT topic is %s\n", mqtt_topic_prefix);
    }
}

void debug_frame(struct can_frame* frame, char* direction)
{
    if ( debug < 1 )
        return;

    if ( !(frame->can_id & CAN_ERR_FLAG) ) {
        if ( (frame->can_id & CAN_RTR_FLAG) == 0 ) {
            /* data frame */
            printf("socketcan: %s ID=%X DLC=%d %02X %02X %02X %02X %02X %02X %02X %02X\n",
                direction,
                frame->can_id & CAN_ERR_MASK,
                frame->can_dlc,
                frame->data[0], frame->data[1], frame->data[2], frame->data[3],
                frame->data[4], frame->data[5], frame->data[6], frame->data[7]);
        }
        else {
            /* remote request transmission frame */
            printf("socketcan: %s ID=%X DLC=%d RTR\n",
                direction,
                frame->can_id & CAN_ERR_MASK,
                frame->can_dlc);
        }
    }
    else {
        /* error frame */
    }
}


void mqtt_log_callback(struct mosquitto* mosq, void* userdata, int level, const char* str)
{
mosq = mosq; /* unused */
userdata = userdata; /* unused */
level = level; /* unused */

    if ( debug > 1 ) {
        printf("mosquitto: %s\n", str);
    }
}

void mqtt_message_callback(struct mosquitto *mosq, void *userdata, const struct mosquitto_message *message)
{
mosq = mosq; /* unused */
userdata = userdata; /* unused */

    if ( !access_write )
        return; // shouldn't happen

    if ( message->payloadlen == 0 )
        return; // NULL messages are ignored

    int items;

    char** topics;
    int topic_count;
    mosquitto_sub_topic_tokenise(message->topic, &topics, &topic_count);

    struct can_frame frame;
    char* origin = NULL;

    items = sscanf(topics[topic_count-1], "%x", &frame.can_id);
    if ( (frame.can_id & ~CAN_SFF_MASK) != 0 ) {
        frame.can_id |= CAN_EFF_FLAG;
    }
    if ( items == 1 ) {
        items = sscanf(message->payload,
            "%*d.%*d %1hhd %2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx %ms",
                &frame.can_dlc,
                &frame.data[0], &frame.data[1], &frame.data[2], &frame.data[3],
                &frame.data[4], &frame.data[5], &frame.data[6], &frame.data[7],
                &origin);
        if ( items == 9 || ( items == 10 && strcmp(origin, here) ) ) {
            debug_frame(&frame, "TX");
            if ( write(can_fd, &frame, sizeof(frame)) == -1 ) {
                perror("write on CAN socket failed");
                exit(EXIT_FAILURE);
            }
        }
        else {
            items = sscanf(message->payload,
                "%*d.%*d %1hhd RTR %ms",
                &frame.can_dlc,
                &origin);
            if ( items == 2 ) {
                frame.can_id |= CAN_RTR_FLAG;
                debug_frame(&frame, "TX");
                if ( write(can_fd, &frame, sizeof(frame)) == -1 ) {
                    perror("write on CAN socket failed");
                    exit(EXIT_FAILURE);
                }
            }
        }
    }
    free(origin);
    mosquitto_sub_topic_tokens_free(&topics, topic_count);
}

void mqtt_connect_callback(struct mosquitto *mosq, void *userdata, int result)
{
userdata = userdata; /* unused */
    if ( !result ) {
        char topic[2048];
        snprintf(topic, sizeof(topic), "%s/+", mqtt_topic_prefix);
        if ( access_write ) 
            mosquitto_subscribe(mosq, NULL, topic, 0);
    }
}


int main(int argc, char** argv)
{
    parse_options(argc, argv);

    /*
     * opening SocketCAN interface
     */
    can_fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if ( can_fd < 0 ) {
        perror("cannot create CAN socket");
        exit(EXIT_FAILURE);
    }

    struct ifreq ifr;
    strncpy(ifr.ifr_name, can_interface, sizeof(ifr.ifr_name));
    if ( ioctl(can_fd, SIOCGIFINDEX, &ifr) < 0 ) {
        perror("failed to enumerate CAN socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_can addr;
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if ( bind(can_fd, (struct sockaddr *) &addr, sizeof(addr)) < 0 ) {
        perror("failed to bind CAN socket");
        exit(EXIT_FAILURE);
    }

    /*
     * initialize MQTT
     */

    struct mosquitto* mosq;
    mosquitto_lib_init();
    if ( (mosq = mosquitto_new(NULL, true, NULL)) == NULL ) {
        perror("Mosquitto initialization failed");
        exit(EXIT_FAILURE);
    }
    mosquitto_log_callback_set(mosq, mqtt_log_callback);

    mosquitto_connect_callback_set(mosq, mqtt_connect_callback);
    mosquitto_message_callback_set(mosq, mqtt_message_callback);

    if ( mosquitto_username_pw_set(mosq, broker_username, broker_password) ) {
        fprintf(stderr, "mosquitto_username_pw_set failed.\n");
        exit(EXIT_FAILURE);
    }

    if ( mosquitto_connect_bind(mosq, broker_hostname, broker_port, 20 /* keepalive */, NULL) ) {
        fprintf(stderr, "mosquitto_connect_bind failed.\n");
        exit(EXIT_FAILURE);
    }

    if ( mosquitto_loop_start(mosq) ) {
        fprintf(stderr, "mosquitto_loop_start failed.\n");
        exit(EXIT_FAILURE);
    }

    /*
     * main loop
     */
    for(;;) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(can_fd, &rfds);
        if ( select(FD_SETSIZE, &rfds, NULL, NULL, NULL) == -1 ) {
            perror("select on CAN socket failed");
            exit(EXIT_FAILURE);
        }

        if ( FD_ISSET(can_fd, &rfds) ) {
            struct can_frame frame;
            bzero(&frame, sizeof(frame));
            if ( read(can_fd, &frame, sizeof(struct can_frame)) == -1 ) {
                perror("read on CAN socket failed");
                exit(EXIT_FAILURE);
            }

            struct timeval tv;
            if ( ioctl(can_fd, SIOCGSTAMP, &tv) != 0 ) {
                perror("ioctl on CAN socket failed");
                exit(EXIT_FAILURE);
            }

            if ( !access_read )
                continue;

            debug_frame(&frame, "RX");

            if ( frame.can_id & CAN_ERR_FLAG ) {
                /* error frame */
            }
            else {
                char topic[2048];
                snprintf(topic, sizeof(topic), "%s/%x",
                        mqtt_topic_prefix,
                        frame.can_id & CAN_ERR_MASK);

                char message[2048];
                if ( (frame.can_id & CAN_RTR_FLAG) == 0 ) {
                    /* data frame */
                    snprintf(message, sizeof(message),
                        "%ld.%06ld %d %02x%02x%02x%02x%02x%02x%02x%02x %s",
                            tv.tv_sec, 
                            tv.tv_usec,
                            frame.can_dlc,
                            frame.data[0], frame.data[1], frame.data[2], frame.data[3],
                            frame.data[4], frame.data[5], frame.data[6], frame.data[7],
                            here);
                }
                else {
                    /* remote transmission request frame */
                    snprintf(message, sizeof(message),
                        "%ld.%06ld %d RTR %s",
                            tv.tv_sec,
                            tv.tv_usec,
                            frame.can_dlc,
                            here);
                }

                mosquitto_publish(mosq, NULL, topic,
                    strlen(message), message,
                    0,
                    false);
            }
        }
    }

    return 0;
}
