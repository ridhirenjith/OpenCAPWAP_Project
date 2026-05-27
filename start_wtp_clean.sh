#!/bin/bash

INTERFACE="WTPWLan00"
CLEANED=0

cleanup() {
    if [ "$CLEANED" -eq 1 ]; then
        return
    fi

    CLEANED=1

    echo "[+] Cleanup triggered..."

    pkill -f "./WTP"

    if iw dev | grep -q "$INTERFACE"; then
        echo "[+] Removing interface: $INTERFACE"
        iw dev "$INTERFACE" del
    fi

    echo "[+] Cleanup complete"
}

trap cleanup SIGINT SIGTERM EXIT

# Start WTP
./WTP ./ &

echo "[+] WTP started"

# Monitor daemonized WTP process
while true; do
    if ! pgrep -x WTP > /dev/null; then
        echo "[+] WTP process ended"
        cleanup
        break
    fi
    sleep 1
done
