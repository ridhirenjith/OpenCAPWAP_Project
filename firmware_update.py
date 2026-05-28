#!/usr/bin/env python3
import os
import tarfile
import tempfile
import subprocess
import shutil
import time

def create_cup_package(wtp_name, wtp_location, version="3.0"):
    print(f"Creating CUP package...")
    print(f"  WTP Name: {wtp_name}")
    print(f"  WTP Location: {wtp_location}")
    print(f"  Version: {version}")
    
    tmpdir = tempfile.mkdtemp()
    wtp_dir = os.path.join(tmpdir, "WTP")
    os.makedirs(wtp_dir)
    
    with open('/home/vvsa/openCAPWAP/config.wtp', 'r') as f:
        config = f.read()
    
    lines = config.split('\n')
    new_lines = []
    for line in lines:
        if line.startswith('</WTP_NAME>'):
            new_lines.append(f'</WTP_NAME> {wtp_name}')
        elif line.startswith('</WTP_LOCATION>'):
            new_lines.append(f'</WTP_LOCATION> {wtp_location}')
        else:
            new_lines.append(line)
    
    new_config = '\n'.join(new_lines)
    
    with open(os.path.join(wtp_dir, 'config.wtp'), 'w') as f:
        f.write(new_config)
    
    shutil.copy('/home/vvsa/openCAPWAP/WTP', wtp_dir)
    
    cud_content = f"version {version}\n"
    with open(os.path.join(tmpdir, 'update.cud'), 'w') as f:
        f.write(cud_content)
    
    cup_path = '/tmp/dynamic_firmware.tar.gz'
    with tarfile.open(cup_path, 'w:gz') as tar:
        for item in os.listdir(tmpdir):
            tar.add(os.path.join(tmpdir, item), arcname=item)
    
    shutil.rmtree(tmpdir)
    print(f"CUP package created: {cup_path}")
    return cup_path

def run_wua(cup_path):
    print(f"\nRunning WUA with {cup_path}...")
    result = subprocess.run(
        ['sudo', '/home/vvsa/openCAPWAP/WUA', cup_path],
        capture_output=True,
        text=True
    )
    print("WUA completed!")

def check_wua_log():
    print("\n--- WUA Log (last 10 lines) ---")
    with open('/var/log/wua.log', 'r') as f:
        lines = f.readlines()
        for line in lines[-10:]:
            print(line.strip())

print("=== Dynamic Firmware Update ===")
print("No config file editing needed!\n")

cup_path = create_cup_package(
    wtp_name="DYNAMIC-WTP-v3",
    wtp_location="Dynamic Update Lab Floor 1",
    version="3.0"
)

run_wua(cup_path)
time.sleep(1)
check_wua_log()

print("\n=== Done! ===")
print("Check config.wtp for updated values:")
os.system("grep 'WTP_NAME\\|WTP_LOCATION' /home/vvsa/openCAPWAP/config.wtp")
