#!/bin/sh
### BEGIN INIT INFO
# Provides:          aesdsocket
# Required-Start:    $network
# Required-Stop:     $network
# Default-Start:     2 3 4 5
# Default-Stop:      0 1 6
# Short-Description: aesdsocket daemon
### END INIT INFO

PIDFILE=/var/run/aesdsocket.pid
DAEMON=/usr/bin/aesdsocket

case "$1" in
    start)
        echo "Starting aesdsocket"
        [ -d /var/run ] || mkdir -p /var/run
        start-stop-daemon -S -n aesdsocket -a $DAEMON -p $PIDFILE -- -d
        ;;
    stop)
        echo "Stopping aesdsocket"
        start-stop-daemon -K -n aesdsocket -p $PIDFILE
        ;;
    *)
        echo "Usage: $0 {start|stop}"
        exit 1
esac

exit 0
