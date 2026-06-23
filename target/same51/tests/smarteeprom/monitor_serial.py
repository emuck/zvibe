#!/usr/bin/env python3
# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause

import serial
import serial.tools.list_ports
import time
import sys
import platform

def detect_sam_e51_serial_port():
    """
    Detects the SAM E51 Curiosity Nano serial port across different platforms.
    """
    system = platform.system().lower()
    ports = serial.tools.list_ports.comports()
    
    sam_e51_vid_pids = [
        (0x03eb, 0x2111),  # Programming interface
        (0x03eb, 0x2175),  # CMSIS-DAP debug interface
    ]
    
    print(f"Platform: {platform.system()}")
    print("Available serial ports:")
    
    sam_e51_candidates = []
    
    for port in ports:
        print(f"  {port.device} - {port.description}")
        
        # Check VID:PID for Microchip SAM E51 Curiosity Nano
        if port.vid and port.pid and (port.vid, port.pid) in sam_e51_vid_pids:
            sam_e51_candidates.append(port.device)
            print(f"    [ok] SAM E51 candidate: {port.device}")
    
    if len(sam_e51_candidates) >= 1:
        return sam_e51_candidates[0]
    else:
        print("No SAM E51 found. Using platform defaults...")
        if system == "darwin":  # macOS
            return "/dev/cu.usbmodem14201"  # Common default
        elif system == "linux":
            return "/dev/ttyACM0"
        else:
            return None

def monitor_serial():
    """Monitor serial output from SmartEEPROM test"""
    port = detect_sam_e51_serial_port()
    if not port:
        print("Could not detect serial port")
        sys.exit(1)
    
    print(f"\nMonitoring serial output on {port}...")
    print("Press Ctrl+C to stop monitoring\n")
    print("="*60)
    
    try:
        ser = serial.Serial(port, 115200, timeout=1)
        
        while True:
            line = ser.readline().decode('utf-8', errors='ignore')
            if line:
                print(line, end='')  # Don't add extra newline since line already has one
            
    except serial.SerialException as e:
        print(f"Serial error: {e}")
        sys.exit(1)
    except KeyboardInterrupt:
        print("\nMonitoring stopped by user")
    finally:
        if 'ser' in locals():
            ser.close()

if __name__ == "__main__":
    monitor_serial()