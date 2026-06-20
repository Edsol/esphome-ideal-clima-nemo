#!/usr/bin/env python3
"""
Continuous CN2 monitor — Ideal Clima Nemo 1000.
Listens on the T line (display TX) and prints/decodes the Tuya 55 AA frames.
Stop with Ctrl+C.

Wiring: RXD -> CN2 pin2 (T), GND -> CN2 pin5. Baud 9600 8N1.
"""
import serial
import sys
import time

PORT = "/dev/tty.usbserial-210"
BAUD = 9600

MODE = {0x00: "cool", 0x01: "heat", 0x02: "dehu", 0x03: "fan"}
FAN  = {0x00: "superlow", 0x01: "low", 0x02: "medium", 0x03: "high", 0x04: "auto"}


def decode(frame: bytes) -> str:
    # frame: 55 AA ver cmd len_hi len_lo data... chk
    if len(frame) < 7 or frame[0] != 0x55 or frame[1] != 0xAA:
        return ""
    cmd = frame[3]
    data = frame[6:-1]
    if cmd == 0x07 and len(data) >= 4:
        dp, typ = data[0], data[1]
        val = data[4:]
        if dp == 0x01:
            return f"POWER = {'ON' if val and val[-1] else 'OFF'}"
        if dp == 0x02:
            return f"TEMP target = {int.from_bytes(val, 'big')} C"
        if dp == 0x03:
            return f"TEMP ambient = {int.from_bytes(val, 'big')} C"
        if dp == 0x04:
            return f"MODE = {MODE.get(val[-1], hex(val[-1]))}"
        if dp == 0x05:
            return f"FAN = {FAN.get(val[-1], hex(val[-1]))}"
        if dp == 0x06:
            return f"FAULT = {val.hex(' ')}"
        return f"DP{dp} type{typ} = {val.hex(' ')}"
    return f"cmd {cmd:#04x}"


def main():
    print(f"Monitor on {PORT} @ {BAUD} — press the fancoil keys. Ctrl+C to quit.\n")
    with serial.Serial(PORT, BAUD, timeout=0.05) as ser:
        ser.reset_input_buffer()
        buf = bytearray()
        while True:
            d = ser.read(ser.in_waiting or 1)
            if not d:
                continue
            buf += d
            # find complete frames: 55 AA ... using the length field
            while True:
                i = buf.find(b"\x55\xaa")
                if i < 0:
                    if len(buf) > 64:
                        del buf[:-1]
                    break
                if i > 0:
                    del buf[:i]
                if len(buf) < 6:
                    break
                length = (buf[4] << 8) | buf[5]
                total = 6 + length + 1
                if len(buf) < total:
                    break
                frame = bytes(buf[:total])
                del buf[:total]
                ts = time.strftime("%H:%M:%S")
                info = decode(frame)
                print(f"[{ts}] {frame.hex(' ').upper()}   {info}")


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nMonitor stopped.")
