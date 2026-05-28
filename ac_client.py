#!/usr/bin/env python3
import socket
import struct

AC_HOST = '127.0.0.1'
AC_PORT = 1235

# Message types
QUIT_MSG = 0
LIST_MSG = 1
CONF_UPDATE_MSG = 2

# Message element types
MSG_ELEMENT_TYPE_OFDM = 1
MSG_ELEMENT_TYPE_ADD_WLAN = 4

def connect():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect((AC_HOST, AC_PORT))
    # Read connection OK response
    data = s.recv(4)
    conn_status = struct.unpack('!i', data)[0]
    print(f"Connection status: {conn_status}")
    return s

def list_wtps(s):
    print("\n--- Listing connected WTPs ---")
    # Send LIST_MSG (1 byte)
    s.send(struct.pack('B', LIST_MSG))
    
    # Read response - first 4 bytes = number of active WTPs
    data = s.recv(1024)
    if len(data) < 4:
        print("No WTPs connected")
        return
    
    num_wtps = struct.unpack('!i', data[:4])[0]
    print(f"Active WTPs: {num_wtps}")
    
    if num_wtps > 0:
        offset = 4
        for i in range(num_wtps):
            wtp_id = struct.unpack('!i', data[offset:offset+4])[0]
            offset += 4
            name_len = struct.unpack('!i', data[offset:offset+4])[0]
            offset += 4
            wtp_name = data[offset:offset+name_len].decode('utf-8')
            offset += name_len
            print(f"  WTP ID: {wtp_id}, Name: {wtp_name}")

def send_ofdm_config(s, wtp_index, channel, band):
    print(f"\n--- Sending OFDM config to WTP {wtp_index} ---")
    print(f"    Channel: {channel}, Band: {band}")
    
    # Build CONF_UPDATE_MSG
    # cmd_msg (1 byte) + msg_elem (1 byte) + wtp_index (4 bytes) + payload
    msg_elem = MSG_ELEMENT_TYPE_OFDM
    
    # OFDM payload: radio_id(1) + reserved(1) + channel(1) + band(1) + TI_threshold(4)
    radio_id = 0
    reserved = 0
    ti_threshold = 0
    
    payload = struct.pack('BBBBI', 
                         radio_id, 
                         reserved, 
                         channel, 
                         band, 
                         ti_threshold)
    
    wtp_index_net = struct.pack('!i', wtp_index)
    
    msg = struct.pack('BB', CONF_UPDATE_MSG, msg_elem) + wtp_index_net + payload
    s.send(msg)
    print("OFDM config sent successfully!")

def quit_connection(s):
    s.send(struct.pack('B', QUIT_MSG))
    s.close()
    print("\nDisconnected from AC")

# Main
print("=== AC Management Client ===")
print(f"Connecting to AC at {AC_HOST}:{AC_PORT}")

try:
    s = connect()
    list_wtps(s)
    
    # Send OFDM config update to WTP 0
    # Channel 6, Band 1
    send_ofdm_config(s, 0, 6, 1)
    
    quit_connection(s)
except Exception as e:
    print(f"Error: {e}")
