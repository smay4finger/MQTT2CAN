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

int main(int argc, char** argv)
{

    int opt;
    char* can_interface = NULL;
    char* broker_hostname = "localhost";
    int broker_port = 1883;

    char* mqtt_topic_prefix = NULL;

    while ( (opt = getopt(argc, argv, "i:h:p:t:")) != -1 ) {
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
        default:
            fprintf(stderr, "%s -i [CAN interface] [-h [hostname]] [-p [port]] [-t [topic]]\n", argv[0]);
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

#if 0
    printf("CAN interface %s\n", can_interface);
    printf("using broker at %s:%d\n", broker_hostname, broker_port);
    printf("MQTT topic is %s\n", mqtt_topic_prefix);
#endif

    /*
     * opening SocketCAN interface
     */
    int can_fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
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

#if 0
    mosquitto_connect_callback_set(mosq, mqtt_connect_callback);
    mosquitto_message_callback_set(mosq, mqtt_message_callback);
#endif

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
            if ( read(can_fd, &frame, sizeof(struct can_frame)) == -1 ) {
                perror("read on CAN socket failed");
                exit(EXIT_FAILURE);
            }
            
            char message[2048];
            snprintf(message, sizeof(message),
                "%d %02x%02x%02x%02x%02x%02x%02x%02x",
                    frame.can_dlc,
                    frame.data[0], frame.data[1], frame.data[2], frame.data[3],
                    frame.data[4], frame.data[5], frame.data[6], frame.data[7]);

            char topic[2048];
            snprintf(topic, sizeof(topic),
                "%s/%x",
                    mqtt_topic_prefix,
                    frame.can_id);

            mosquitto_publish(mosq, NULL, topic,
                strlen(message), message,
                0 /* qos */,
                false /* retain */);
        }
    }

    return 0;
}
