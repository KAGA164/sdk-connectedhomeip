#!/bin/bash

service dbus start
sleep 1
service avahi-daemon start
/usr/sbin/otbr-agent -I wpan0 spinel+hdlc+uart:///dev/ttyUSB0 &
sleep 1
ot-ctl panid 0x1234
ot-ctl ifconfig up
ot-ctl thread start

if [ "$1" = "responder" ]; then
    gdb -batch -return-child-result -q -ex run -ex bt chip-echo-responder
else
    sleep infinity
fi
