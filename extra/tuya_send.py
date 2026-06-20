#!/usr/bin/env python3
"""
Tuya MCU controller — Ideal Clima Nemo 1000 fancoil via CN2.

We emulate the TQCT07 Wi-Fi module. The display board is the Tuya MCU.

USB-TTL -> CN2 wiring (full-duplex):
  RXD -> pin 2 (T)
  TXD -> pin 3 (R)
  GND -> pin 5 (GND)
Baud: 9600 8N1.

PROTOCOL (see CLAUDE.md for details):
  - checksum = sum of ALL bytes including the 55 AA header, & 0xFF
  - module->MCU: version 0x00 ; MCU->module: version 0x03
  - a handshake + continuous heartbeat is required before 06 commands are applied

Usage:
  python3 tuya_send.py                # demo: handshake + set temp 25
  python3 tuya_send.py power on
  python3 tuya_send.py temp 22
  python3 tuya_send.py mode cool
  python3 tuya_send.py fan medium
"""
import serial
import sys
import threading
import time

PORT = "/dev/tty.usbserial-210"
BAUD = 9600

# DP
DP_POWER, DP_TEMP, DP_CUR_TEMP, DP_MODE, DP_FAN, DP_FAULT = 1, 2, 3, 4, 5, 6
MODE = {"cool": 0, "heat": 1, "dehu": 2, "fan": 3}
FAN  = {"superlow": 0, "low": 1, "medium": 2, "high": 3, "auto": 4}
MODE_R = {v: k for k, v in MODE.items()}
FAN_R  = {v: k for k, v in FAN.items()}


def frame(ver, cmd, data=b""):
    f = bytes([0x55, 0xAA, ver, cmd, (len(data) >> 8) & 0xFF, len(data) & 0xFF]) + data
    return f + bytes([sum(f) & 0xFF])      # checksum INCLUDES the header


def dp_bool(dp, v):  return bytes([dp, 0x01, 0x00, 0x01, 1 if v else 0])
def dp_int(dp, v):   return bytes([dp, 0x02, 0x00, 0x04, (v>>24)&0xFF,(v>>16)&0xFF,(v>>8)&0xFF,v&0xFF])
def dp_enum(dp, v):  return bytes([dp, 0x04, 0x00, 0x01, v])


class Fancoil:
    def __init__(self, port=PORT):
        self.ser = serial.Serial(port, BAUD, timeout=0.05)
        self._stop = False
        self._hb_thread = None

    def close(self):
        self._stop = True
        if self._hb_thread:
            self._hb_thread.join(timeout=2)
        self.ser.close()

    def _send(self, f):
        self.ser.write(f)

    def handshake(self):
        self.ser.reset_input_buffer()
        for f in (frame(0x00, 0x00),                       # heartbeat
                  frame(0x00, 0x01),                       # query product info
                  frame(0x00, 0x02),                       # query working mode
                  frame(0x00, 0x03, bytes([0x04])),        # network status = cloud
                  frame(0x00, 0x08)):                      # query DP status
            self._send(f)
            time.sleep(0.3)
            self.ser.read(self.ser.in_waiting)

    def start_heartbeat(self, period=1.0):
        hb = frame(0x00, 0x00)
        def loop():
            while not self._stop:
                self._send(hb)
                time.sleep(period)
        self._hb_thread = threading.Thread(target=loop, daemon=True)
        self._hb_thread.start()

    # commands (version 0x00, module->MCU)
    def power(self, on):      self._send(frame(0x00, 0x06, dp_bool(DP_POWER, on)))
    def set_temp(self, c):    self._send(frame(0x00, 0x06, dp_int(DP_TEMP, c)))
    def set_mode(self, m):    self._send(frame(0x00, 0x06, dp_enum(DP_MODE, MODE[m])))
    def set_fan(self, s):     self._send(frame(0x00, 0x06, dp_enum(DP_FAN, FAN[s])))

    def read_loop(self, secs):
        buf = bytearray(); t = time.time() + secs
        while time.time() < t:
            buf += self.ser.read(self.ser.in_waiting or 1)
            while True:
                i = buf.find(b"\x55\xaa")
                if i < 0: break
                if i: del buf[:i]
                if len(buf) < 6: break
                total = 6 + ((buf[4]<<8)|buf[5]) + 1
                if len(buf) < total: break
                yield bytes(buf[:total]); del buf[:total]


def decode(fr):
    if len(fr) < 7 or fr[3] != 0x07: return None
    dp, val = fr[6], fr[10:-1]
    if dp == DP_POWER: return f"power={'ON' if val[-1] else 'OFF'}"
    if dp == DP_TEMP:  return f"target={int.from_bytes(val,'big')}C"
    if dp == DP_CUR_TEMP: return f"ambient={int.from_bytes(val,'big')}C"
    if dp == DP_MODE:  return f"mode={MODE_R.get(val[-1], val[-1])}"
    if dp == DP_FAN:   return f"fan={FAN_R.get(val[-1], val[-1])}"
    if dp == DP_FAULT: return f"fault={val.hex()}"
    return f"DP{dp}={val.hex()}"


def main():
    fc = Fancoil()
    try:
        print("Handshake...")
        fc.handshake()
        fc.start_heartbeat()
        time.sleep(0.5)

        args = sys.argv[1:]
        if not args:
            print("Demo: set temp 25")
            fc.set_temp(25)
        elif args[0] == "power":
            fc.power(args[1].lower() in ("on", "1", "true"))
        elif args[0] == "temp":
            fc.set_temp(int(args[1]))
        elif args[0] == "mode":
            fc.set_mode(args[1])
        elif args[0] == "fan":
            fc.set_fan(args[1])
        else:
            print(f"Unknown command: {args}")

        print("MCU confirmations (3s):")
        for fr in fc.read_loop(3):
            d = decode(fr)
            if d: print(f"  {fr.hex(' ').upper()}  {d}")
    finally:
        fc.close()


if __name__ == "__main__":
    main()
