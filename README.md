MQTT2CAN
========

MQTT messaging gateway for SocketCAN 

Example for bridging CAN traffic
--------------------------------

You can bridge a CAN interface to another machine by using the -t option. Here is an example:

    sudo modprobe vcan
    sudo ip link add dev vcan0 type vcan
    sudo ip link vcan0 up
    mqtt2can -h [broker] -t can/[otherhost]/can0 -i vcan0
