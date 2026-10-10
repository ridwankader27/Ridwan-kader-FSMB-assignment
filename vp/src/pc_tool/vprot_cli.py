#!/usr/bin/env python3
"""PC-side tool for the Voltage Protector (USB serial, 115200 baud).
  python vprot_cli.py COM5 monitor            # live V/I/relay stream
  python vprot_cli.py COM5 get                # read settings
  python vprot_cli.py COM5 set OV 245         # change a setting (OV, OC, DELAY, AUTO, RECOVER)
  python vprot_cli.py COM5 reset              # re-close relay after fault cleared
Requires: pip install pyserial
"""
import sys, time, serial

def send(ser, cmd, wait=0.6):
    ser.reset_input_buffer()
    ser.write((cmd + "\n").encode())
    end = time.time() + wait
    while time.time() < end:
        line = ser.readline().decode(errors="ignore").strip()
        if line.startswith(("OK", "ERR")):
            return line
    return "ERR NO_REPLY"

def main():
    if len(sys.argv) < 3:
        print(__doc__); return
    port, action = sys.argv[1], sys.argv[2].lower()
    ser = serial.Serial(port, 115200, timeout=0.2)
    time.sleep(0.3)
    if action == "monitor":
        try:
            while True:
                line = ser.readline().decode(errors="ignore").strip()
                if line.startswith("DATA,"):
                    _, t, v, i, relay, fault = line.split(",")
                    flt = {"0": "-", "1": "OV", "2": "OC", "3": "OV+OC"}[fault]
                    print(f"t={int(t)/1000:8.1f}s  V={float(v):6.1f}  I={float(i):5.2f}  relay={'ON ' if relay=='1' else 'OFF'}  fault={flt}")
                elif line.startswith("EVENT"):
                    print(">>>", line)
        except KeyboardInterrupt:
            pass
    elif action == "get":
        print(send(ser, "STREAM 0")); print(send(ser, "GET")); send(ser, "STREAM 1")
    elif action == "set" and len(sys.argv) == 5:
        print(send(ser, f"SET {sys.argv[3]} {sys.argv[4]}"))
    elif action == "reset":
        print(send(ser, "RESET"))
    else:
        print(__doc__)

if __name__ == "__main__":
    main()
