#!/bin/bash
mkdir -p /var/log
cd /openCAPWAP
echo "=== Network interfaces ==="
ip addr show
echo "=== Starting AC ==="
./AC /openCAPWAP/
EXIT=$?
echo "=== AC exited with code $EXIT ==="
ls -la /var/log/
cat /var/log/ac.log.txt 2>/dev/null || echo "No log file created"
sleep infinity
