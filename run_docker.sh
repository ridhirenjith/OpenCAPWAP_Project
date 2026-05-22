#!/bin/bash
# Check dependencies
echo "=== Checking dependencies ==="
if ! command -v docker &> /dev/null; then
    echo "Docker not found! Installing..."
    curl -fsSL https://get.docker.com | sudo sh
fi

if ! modinfo mac80211_hwsim &> /dev/null; then
    echo "ERROR: mac80211_hwsim kernel module not available on this system"
    echo "Install with: sudo apt install linux-modules-extra-$(uname -r)"
    exit 1
fi

echo "All dependencies OK!"
set -e

echo "=== Cleaning up old containers and interfaces ==="
sudo docker rm -f capwap-ac capwap-wtp 2>/dev/null || true
for iface in $(iw dev 2>/dev/null | grep "Interface WTP" | awk '{print $2}'); do
    sudo iw dev $iface del 2>/dev/null
done

echo "=== Setting up mac80211_hwsim ==="
sudo modprobe -r mac80211_hwsim 2>/dev/null || true
sudo modprobe mac80211_hwsim radios=2

HWSIM_PHY=$(iw dev | grep -B1 "Interface wlan1" | grep "phy#" | sed 's/phy#//' | tr -d '\t ')
echo "Virtual WiFi phy: phy${HWSIM_PHY}"

echo "=== Starting AC container ==="
sudo docker compose up -d ac
sleep 3
echo "AC log:"
sudo docker exec capwap-ac cat /var/log/ac.log.txt 2>/dev/null | tail -3

echo "=== Starting WTP container ==="
sudo docker run -d --privileged --pid=host \
  -v /lib/modules:/lib/modules -v /dev:/dev \
  --network opencapwap_capwap-net \
  --name capwap-wtp opencapwap-wtp sleep infinity

WTP_PID=$(sudo docker inspect --format '{{.State.Pid}}' capwap-wtp)
echo "Moving phy${HWSIM_PHY} into WTP container (PID: $WTP_PID)..."
sudo iw phy phy${HWSIM_PHY} set netns $WTP_PID

echo "=== Updating WTP phy config ==="
sudo docker exec capwap-wtp bash -c \
  "sed -i 's|<RADIO_PHY_NAME_0>.*|<RADIO_PHY_NAME_0>${HWSIM_PHY}|' /openCAPWAP/settings.wtp.txt"

echo "=== Starting WTP process ==="
sudo docker exec -d capwap-wtp bash -c \
  "mkdir -p /var/log && cd /openCAPWAP && ./WTP /openCAPWAP/"

echo "=== Waiting for handshake (15s) ==="
sleep 15

echo ""
echo "=== WTP LOG ==="
sudo docker exec capwap-wtp cat /var/log/wtp1.txt | tail -15

echo ""
echo "=== AC LOG ==="
sudo docker exec capwap-ac cat /var/log/ac.log.txt | tail -10

echo ""
echo "=== CAPWAP is running! ==="
echo "Watch logs:  sudo docker exec capwap-ac tail -f /var/log/ac.log.txt"
echo "Wireshark:   sudo wireshark (select br-xxxx interface, filter: udp.port==5246||udp.port==5247)"
echo "Stop:        sudo docker rm -f capwap-ac capwap-wtp"
