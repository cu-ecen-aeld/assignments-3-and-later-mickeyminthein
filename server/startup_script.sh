#!/bin/sh
### BEGIN INIT INFO
# aesdsocket start stop script 
# Author: Min Min Thein
### END INIT INFO

case "$1" in
    start)
        echo "Starting aesdsocket"
        [ -d /var/run ] || mkdir -p /var/run
        start-stop-daemon -S -n aesdsocket -a /usr/bin/aesdsocket -- -d
        ;;
    stop)
        echo "Stopping aesdsocket"
        start-stop-daemon -K -n aesdsocket 
        ;;
    *)
        echo "Usage: $0 {start|stop}"
        exit 1
esac

exit 0
