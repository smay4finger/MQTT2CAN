/*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <getopt.h>
#include <sys/ioctl.h>
#include <linux/can.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <mosquitto.h>

int debug = 0;

char* can_interface = NULL;
char* broker_hostname = "localhost";
int broker_port = 1883;

char* mqtt_topic_prefix = NULL;
int mqtt_qos = 0;
bool mqtt_retain = false;

int can_fd;

void parse_options(int argc, char** argv)
{
    int opt;
    while ( (opt = getopt(argc, argv, "i:h:p:t:q:rd")) != -1 ) {
        switch ( opt ) {
        case 'i':
            can_interface = optarg;
            break;
        case 'b':
            broker_hostname = optarg;
            break;
        case 'p':
            broker_port = atoi(optarg);
            break;
        case 't':
            mqtt_topic_prefix = optarg;
            break;
        case 'q':
            mqtt_qos = atoi(optarg);
            if ( mqtt_qos < 0 || mqtt_qos > 2 ) {
                fprintf(stderr, "MQTT QoS must be between 0 and 2\n");
                exit(EXIT_FAILURE);
            }
            break;
        case 'r':
            mqtt_retain = true;
            break;
        case 'd':
            debug++;
            break;
        default:
            fprintf(stderr, 
                "%s -i [CAN interface] [-h [hostname]] [-p [port]] [-t [topic]]\n"
                "  -q [QoS]     the QoS of the MQTT messages\n"
                "  -r           published MQTT messages will be retained by the broker\n"
                "  -d           debug (use multiple times for more debug messages)\n"
                , argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    if ( can_interface == NULL ) {
        fprintf(stderr, "please specify CAN interface\n");
        exit(EXIT_FAILURE);
    }

    if ( mqtt_topic_prefix == NULL ) {
        char hostname[1024];
        if ( gethostname(hostname, sizeof(hostname)) ) {
            perror("failed to get hostname");
            exit(EXIT_FAILURE);
        }
        mqtt_topic_prefix = malloc(strlen("can/") + strlen(hostname) + 1 + strlen(can_interface));
        strcpy(mqtt_topic_prefix, "can/");
        strcat(mqtt_topic_prefix, hostname);
        strcat(mqtt_topic_prefix, "/");
        strcat(mqtt_topic_prefix, can_interface);
    }

    if ( debug > 2 ) {
        printf("CAN interface %s\n", can_interface);
        printf("using broker at %s:%d\n", broker_hostname, broker_port);
        printf("MQTT topic is %s\n", mqtt_topic_prefix);
    }
}

void debug_frame(struct can_frame* frame, char* direction)
{
    if ( debug > 0 ) {
        if ( !(frame->can_id & CAN_ERR_FLAG) ) {
            if ( !(frame->can_id & CAN_RTR_FLAG) ) {
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
message = message; /* unused */

    if ( message->payloadlen ) {
        struct can_frame frame;

        char* saveptr;
        char* token;
        token = strtok_r(message->topic, "/", &saveptr);
        while ( token != NULL ) {
            if ( strcmp(token, "tx") == 0 ) {
                char* id_token = strtok_r(NULL, "/", &saveptr);
                if ( id_token == NULL ) {
                    printf("malformed message topic\n");
                    return;
                }
                frame.can_id = strtol(id_token, NULL, 16);
                break;
            }
            token = strtok_r(NULL, "/", &saveptr);
        }
        if ( strcmp(token, "tx") != 0 ) {
            printf("malformed message topic\n");
            return;
        }

        int message_items = sscanf(message->payload, 
            "%1hhd %2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx", 
                &frame.can_dlc, 
                &frame.data[0], &frame.data[1], &frame.data[2], &frame.data[3],
                &frame.data[4], &frame.data[5], &frame.data[6], &frame.data[7]);
        if ( message_items != 9 ) {
            printf("malformed message payload\n");
            return;
        }

        debug_frame(&frame, "TX");
        if ( write(can_fd, &frame, sizeof(frame)) == -1 ) {
            perror("write on CAN socket failed");
            exit(EXIT_FAILURE);
        }
    }
    else {
        /* null message */
    }
}

void mqtt_connect_callback(struct mosquitto *mosq, void *userdata, int result)
{
mosq = mosq; /* unused */
userdata = userdata; /* unused */
    if ( !result ) {
        char topic[2048];
        strncpy(topic, mqtt_topic_prefix, sizeof(topic));
        strncat(topic, "/tx/+", sizeof(topic));
        mosquitto_subscribe(mosq, NULL, topic, mqtt_qos);
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

    if ( mosquitto_connect_async(mosq, broker_hostname, broker_port, 20 /* keepalive */) ) {
        perror("Mosquitto connect failed");
        exit(EXIT_FAILURE);
    }

    if ( mosquitto_loop_start(mosq) ) {
        perror("Mosquitto loop start failed");
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
            debug_frame(&frame, "RX");

            if ( frame.can_id & CAN_ERR_FLAG ) {
                /* error frame */
            }
            else {
                char topic[2048];
                snprintf(topic, sizeof(topic),
                    "%s/rx/%x",
                        mqtt_topic_prefix,
                        frame.can_id & CAN_ERR_MASK);

                char message[2048];
                if ( !(frame.can_id & CAN_RTR_FLAG) ) {
                    /* data frame */
                    snprintf(message, sizeof(message),
                        "%d %02x%02x%02x%02x%02x%02x%02x%02x",
                            frame.can_dlc,
                            frame.data[0], frame.data[1], frame.data[2], frame.data[3],
                            frame.data[4], frame.data[5], frame.data[6], frame.data[7]);
                }
                else {
                    /* remote transmission request frame */
                    snprintf(message, sizeof(message),
                        "%d RTR",
                            frame.can_dlc);
                }

                mosquitto_publish(mosq, NULL, topic,
                    strlen(message), message,
                    mqtt_qos,
                    mqtt_retain);
            }
        }
    }

    return 0;
}
