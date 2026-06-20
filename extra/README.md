# Extra — PC tools

Host-side tools used during reverse engineering and for quick bench testing
from a computer with a USB-TTL adapter. Not needed for normal ESP32-C3 use.

Requires Python 3 and `pyserial` (`pip install pyserial`). Edit the `PORT`
constant at the top of each script to match your serial device.

## `monitor.py`
Passive sniffer. Listens on CN2-T (display TX) and decodes the Tuya `55 AA`
frames in real time.

Wiring: `RXD -> CN2 pin2 (T)`, `GND -> CN2 pin5`. Baud 9600 8N1.

```
python3 monitor.py
```

## `tuya_send.py`
Active controller. Performs the handshake, keeps the heartbeat alive, and sends
commands (power / temp / mode / fan).

Wiring (full-duplex): `RXD -> CN2 pin2 (T)`, `TXD -> CN2 pin3 (R)`, `GND -> CN2 pin5`.

```
python3 tuya_send.py                # demo: handshake + set temp 25
python3 tuya_send.py power on
python3 tuya_send.py temp 22
python3 tuya_send.py mode cool
python3 tuya_send.py fan medium
```
