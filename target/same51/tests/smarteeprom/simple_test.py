#!/usr/bin/env python3
# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause

import serial
import serial.tools.list_ports
import time
import sys

def run_test():
    """Simple test runner that just sends ENTER and logs output"""
    
    # Try different ports
    test_ports = ["/dev/cu.usbmodem1102", "/dev/tty.usbmodem1102"]
    
    for port in test_ports:
        try:
            print(f"Trying to connect to {port}...")
            ser = serial.Serial(port, 115200, timeout=1)
            print(f"[ok] Connected to {port}")
            break
        except serial.SerialException as e:
            print(f"[fail] Failed to connect to {port}: {e}")
            continue
    else:
        print("Could not connect to any port")
        return
    
    try:
        print("Waiting 2 seconds for device to settle...")
        time.sleep(2)
        
        print("Sending ENTER to trigger test...")
        ser.write(b"\r\n")
        
        print("Reading output for 60 seconds...")
        start_time = time.time()
        
        while (time.time() - start_time) < 60:  # 60 second timeout
            if ser.in_waiting > 0:
                data = ser.read(ser.in_waiting).decode('utf-8', errors='ignore')
                print(data, end='')
                
                # Check for completion indicators
                if "Test 1a WMODE_MAN:" in data:
                    print("\n" + "="*50)
                    print("BIT PATTERN TEST COMPLETED")
                    print("="*50)
                
                if "TIMEOUT" in data:
                    print("\n" + "!"*50)
                    print("TIMEOUT DETECTED - CHECKING DEBUG OUTPUT")
                    print("!"*50)
            
            time.sleep(0.01)
        
        print("\nTest monitoring completed")
        
    except Exception as e:
        print(f"Error during test: {e}")
    finally:
        ser.close()

if __name__ == "__main__":
    run_test()