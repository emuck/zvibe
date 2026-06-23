#!/usr/bin/env python3
# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause

import serial
import serial.tools.list_ports
import time
import sys
import platform

def detect_sam_e51_serial_port():
    """Detects the SAM E51 Curiosity Nano serial port across different platforms."""
    system = platform.system().lower()
    ports = serial.tools.list_ports.comports()
    
    sam_e51_vid_pids = [
        (0x03eb, 0x2111),  # Programming interface
        (0x03eb, 0x2175),  # CMSIS-DAP debug interface
    ]
    
    print(f"Platform: {platform.system()}")
    
    for port in ports:
        # Check VID:PID for Microchip SAM E51 Curiosity Nano
        if port.vid and port.pid and (port.vid, port.pid) in sam_e51_vid_pids:
            return port.device
    
    # Platform defaults if not detected
    if system == "darwin":  # macOS
        return "/dev/cu.usbmodem14201"  # Common default
    elif system == "linux":
        return "/dev/ttyACM0"
    else:
        return None

def run_smarteeprom_test():
    """Run the SmartEEPROM test via UART"""
    port = detect_sam_e51_serial_port()
    if not port:
        print("Could not detect serial port")
        sys.exit(1)
    
    print(f"Connecting to {port}...")
    print("Once connected, press ENTER to start the SmartEEPROM test")
    print("Press Ctrl+C to exit")
    print("="*60)
    
    try:
        # Try to connect with proper parameters
        ser = serial.Serial(
            port=port, 
            baudrate=115200,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=0.1,
            xonxoff=False,
            rtscts=False,
            dsrdtr=False
        )
        
        print(f"[ok] Connected to {port} at 115200 baud")
        print("\nWaiting for device output...")
        
        # Interactive mode
        while True:
            # Check for incoming data
            if ser.in_waiting > 0:
                data = ser.read(ser.in_waiting).decode('utf-8', errors='ignore')
                print(data, end='')  # Print without adding newline
            
            # Check for user input (non-blocking on Unix)
            try:
                import select
                if select.select([sys.stdin], [], [], 0) == ([sys.stdin], [], []):
                    user_input = sys.stdin.readline().strip()
                    if user_input:
                        ser.write(f"{user_input}\r\n".encode('utf-8'))
                    else:
                        # Just ENTER pressed - send carriage return
                        ser.write(b"\r\n")
            except ImportError:
                # Windows doesn't have select, use simpler approach
                pass
            
            time.sleep(0.01)  # Small delay to prevent excessive CPU usage
            
    except serial.SerialException as e:
        print(f"Serial error: {e}")
        print("\nTips:")
        print("- Close any other applications using the serial port")
        print("- Try unplugging and reconnecting the USB cable")
        print("- On macOS, try: sudo pkill -f usbmodem")
        sys.exit(1)
    except KeyboardInterrupt:
        print("\nTest stopped by user")
    finally:
        if 'ser' in locals():
            ser.close()
            print("Serial connection closed")

if __name__ == "__main__":
    run_smarteeprom_test()