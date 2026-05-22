#!/bin/bash
mkdir -p /var/log
cd /openCAPWAP

# Get the hwsim phy index from settings
PHY=$(grep RADIO_PHY_NAME_0 /openCAPWAP/settings.wtp.txt | sed 's/.*>//')
echo "Using phy: $PHY"

echo "Waiting for AC to be ready..."
sleep 5

echo "Starting WTP..."
./WTP /openCAPWAP/ &
WTP_PID=$!
sleep 2
tail -f /var/log/wtp1.txt &
wait $WTP_PID
