#!/bin/bash

INTERFACE="WTPWLan00"
CLEANED=0

cleanup() {
    if [ "$CLEANED" -eq 1 ]; then
        return
    fi

    CLEANED=1

    echo "[+] Cleanup triggered..."

    if iw dev | grep -q "$INTERFACE"; then
        echo "[+] Removing interface: $INTERFACE"
        iw dev "$INTERFACE" del
    fi

    echo "[+] Cleanup complete"
}

trap cleanup SIGINT SIGTERM EXIT

./WTP ./ &

echo "[+] WTP started"

# Monitor by process name because WTP daemonizes
while true; do
    if ! pgrep -x WTP > /dev/null; then
        echo "[+] WTP process ended"
        cleanup
        break
    fi
    sleep 1
done
