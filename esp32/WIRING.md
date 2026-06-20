# ESP32-C3 ↔ CN2 wiring — direct connection, no extra components

**Verified on the real device (2026-06-20):** both CN2 data lines (T and R) operate
at **3.3V logic**, not 5V. So they connect **directly** to the ESP32-C3 GPIOs —
no voltage divider, no level shifter, no resistors.

```
CN2 pin2 (T, board TX) ──► ESP RX   : direct (3.3V, safe for the GPIO)
CN2 pin3 (R, board RX) ◄── ESP TX   : direct (3.3V accepted by the MCU)
CN2 pin5 (GND)         ──── ESP GND  : common ground (mandatory)
CN2 pin1 (+5V)         ──── opt. ESP power (see below)
CN2 pin4 (S)           ──── DO NOT connect (leave high/floating)
```

## 1. RX line (board T → ESP) — direct ✓

The T line idles and drives at **3.3V**, within the ESP32-C3 GPIO range. Connect it
straight to the ESP RX pin. No divider needed.

## 2. TX line (ESP → board R) — direct ✓

The ESP32-C3 TX output at 3.3V is read correctly by the board MCU as a logic high
(its VIH threshold is ≤ 3.3V). Connect it straight to CN2-R. No level shifter, no
transistor needed.

## 3. Powering the ESP32-C3

CN2 pin1 provides **5V**. You can power the ESP32-C3 from the **5V** pin (it feeds
the onboard 3.3V regulator). Do NOT feed the GPIOs at 5V. Alternatively power the
ESP from USB during development (ALWAYS keeping a common ground with CN2 pin5).

## Minimal summary (confirmed)
- **RX**: ESP RX ← CN2-T **direct** (3.3V, safe)
- **TX**: ESP TX → CN2-R **direct** (3.3V accepted by the MCU)
- Common **GND** (CN2 pin5 ↔ ESP GND)
- **Pin S** not connected
- No dividers, no level shifters, no extra components.
