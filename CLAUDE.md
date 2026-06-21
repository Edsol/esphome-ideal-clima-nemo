# Ideal Clima Nemo 1000 — Reverse Engineering

## Goal

Integrate the **Ideal Clima Nemo 1000** fancoil into Home Assistant via an **ESP32-C3 Super Mini**, without the original TQCT07 Wi-Fi dongle.

Target features:
- Read state, setpoint, mode, fan speed
- Command power, setpoint, mode, fan
- Home Assistant integration via ESPHome or MQTT

---

## Hardware

| Component | Notes |
|---|---|
| Ideal Clima Nemo 1000 fancoil | Unit to control |
| Display/touch/IR board | Removed and photographed, silkscreen `YK-D-COL F WIFI-D / 20230720 / YK1.2` |
| ESP32-C3 Super Mini | Final target MCU |
| USB-TTL UART | For firmware debug/development |
| Logic analyzer 8ch 24 MHz | Saleae clone, fx2lafw driver |
| Apple Silicon Mac | Development workstation |

---

## Display board

**Front silkscreen:** `YK-D-COL F WIFI-D` / `20230720` / `YK1.2`
**Back silkscreen (sticker):** `25012001 TY18 / YK SN:02309`

Characteristics:
- 2-digit 7-segment display, part number **CLDT-52622CW-3.5** (sticker on the display module)
- Capacitive touch keys with springs
- Indicator LEDs
- IR receiver
- Central SOIC microcontroller (not identified)
- **K1** = ON/OFF key
- **R2** = temperature DOWN key

Reference photos: `board_images/` (2026-06-20)

---

## Connectors

### CN1 — Display board ↔ fancoil main board

4-pin JST connector, **brown/beige**, located on the **left side edge** of the board.

Silkscreen (from front photo):

```
+5V
RX
GND
TX
```

- **This is the physical connector toward the fancoil main board** — the cable from the fancoil arrives here
- Not the point where we connect the ESP32-C3
- +5V power source for the display board

### CN2 — Display board ↔ TQCT07 Wi-Fi module ← **ESP32-C3 connection point**

5-pin **JST 1.25mm** connector, **white**, located on the **bottom-right edge** of the board (extension tab). A pre-crimped JST 1.25mm 5-pin pigtail mates with it ([example](https://it.aliexpress.com/item/1005010705677089.html)).

Silkscreen (confirmed from close-up photos):

```
+5V
T
R
S
GND
```

- T = TX display → Wi-Fi module
- R = RX display ← Wi-Fi module
- S = auxiliary signal (status/reset/enable/module presence — TBD)
- **We connect the ESP32-C3 here** emulating the TQCT07 dongle
- With nothing connected: the bus may be silent or waiting for a handshake

### Architecture

```
Fancoil main board  ──CN1──>  Display board  <──CN2──>  TQCT07 / ESP32-C3
```

---

## Assumed protocol

The TQCT07 dongle is a Tuya module. The protocol on CN2 is probably **Tuya MCU UART**:
- Frame header: `55 AA` (or variants `AA 55`, `A5`, `5A`)
- Baud rates to try: `9600`, `19200`, `38400`, `57600`, `115200`
- Format: 8N1, idle high

Known Tuya DPs (from [tuya-local ideal_clima_fancoil.yaml](https://github.com/deltaclock/tuya-local/blob/8030830581c33e005efabf82cae12e87e1719da9/custom_components/tuya_local/devices/ideal_clima_fancoil.yaml)):

| DP | Type | Description | Values |
|---|---|---|---|
| 1 | bool | Power | `false`=off / `true`=on |
| 2 | int 0–40 | Target temperature (°C) | — |
| 3 | int −20..50 | Current temperature (°C) | — |
| 4 | string | Mode | `cool` / `heat` / `dehu` / `fan` |
| 5 | string | Fan speed | `superlow` / `low` / `medium` / `high` / `auto` |
| 6 | bitfield | Fault / diagnostic | 0 = no fault |

**Note:** the DPs describe the semantics over the network; the electrical protocol on CN2 (or CN1) must be verified with real sniffing.

- **Tuya product ID:** `5dgguakbmhwzwiko`

---

## Software tools

### sigrok-cli (working)

```bash
sigrok-cli --scan
# fx2lafw - Saleae Logic with 8 channels: D0 D1 D2 D3 D4 D5 D6 D7
```

### PulseView — DO NOT use

Crashes at startup on macOS Apple Silicon:
- `EXC_BAD_ACCESS / SIGKILL` — Code Signature Invalid
- codesign attempts failed (ambiguous Qt/Python bundle)

---

## Logic analyzer mapping

The physical 10-pin connector has this layout:

```
Top row:     CH2  CH4  CH6  CH8  GND
Bottom row:  CH1  CH3  CH5  CH7  CLK
```

sigrok mapping:

| Physical | sigrok |
|---|---|
| CH1 | D0 |
| CH2 | D1 |
| CH3 | D2 |
| CH4 | D3 |
| CH5 | D4 |
| CH6 | D5 |
| CH7 | D6 |
| CH8 | D7 |

Do not connect PWR/CLK to the fancoil.

---

## Safety notes

- **Never connect +5V from CN1/CN2 to the logic analyzer**
- Measure with a multimeter (referenced to GND) before connecting:
  - CN1: TX, RX, +5V
  - CN2: T, R, S, +5V
- The FX2 clone logic analyzer typically tolerates 5 V, verify your model
- **ESP32-C3 GPIOs are NOT 5V tolerant.** Measure the line voltage first. On the Nemo 1000
  the CN2 data lines (T, R) measured at **3.3V**, so a direct connection is safe — but if
  your unit measures 5 V on a line, use a resistor divider or level shifter on it.

---

## Step 1 — Sniff CN2 (passive listening)

Before connecting the ESP32-C3, sniff CN2 with the logic analyzer to figure out the baud rate, protocol and the role of pin S.

### Connection

```
Logic analyzer GND      →  GND (CN2 pin 5)
Logic analyzer CH1 (D0) →  T   (CN2 pin 2, display TX)
Logic analyzer CH2 (D1) →  R   (CN2 pin 3, display RX)
Logic analyzer CH3 (D2) →  S   (CN2 pin 4, auxiliary signal)
```

Do not connect +5V.

### Preliminary multimeter measurements

Before connecting, check referenced to GND:
- Pin T: idle voltage (expected ~3.3 V or ~5 V idle UART)
- Pin R: idle voltage
- Pin S: idle voltage (figure out if it's already driven or floating)
- Pin +5V: confirm power

### Capture

```bash
sigrok-cli \
  --driver fx2lafw \
  --config samplerate=1m \
  --time 15s \
  --channels D0,D1,D2 \
  --output-file ~/nemo_cn2_15s.sr
```

During the capture, perform actions spaced apart (~2 s each):
1. Power on
2. Temperature +
3. Temperature −
4. Change fan speed
5. Change mode

### Decoding

```bash
for b in 9600 19200 38400 57600 115200; do
  echo "=== T (D0) baud $b ==="
  sigrok-cli --input-file ~/nemo_cn2_15s.sr -P uart:rx=D0:baudrate=$b
  echo "=== R (D1) baud $b ==="
  sigrok-cli --input-file ~/nemo_cn2_15s.sr -P uart:rx=D1:baudrate=$b
done
```

### What to look for in the output

- Traffic on T, R or both (even without a module connected there could be announcements/polling)
- State of S: constant high, low, or pulsing
- Repeated frames (Tuya MCU heartbeat ~every 1 s)
- `55 AA` header
- Bytes that change with each action

---

## Step 2 — Connect the ESP32-C3 to CN2

Once the protocol is known, connect the ESP32-C3 emulating the TQCT07.

### Wiring diagram

> ✅ On the Nemo 1000 the T/R lines measured at 3.3V → direct connection, no divider needed.
> Still measure first on your unit: if a line is at 5 V, use a divider or level shifter (the ESP32-C3 GPIOs are NOT 5V tolerant).

```
CN2 GND  →  ESP32-C3 GND
CN2 T    →  ESP32-C3 GPIO RX  (T = display TX, goes into ESP RX)
CN2 R    →  ESP32-C3 GPIO TX  (R = display RX, comes out of ESP TX)
CN2 S    →  TBD after sniffing (pull-up/pull-down/GPIO)
CN2 +5V  →  ESP32-C3 5V pin (optional, consider powering from USB)
```

---

## Integration plan

**Chosen approach: emulate the TQCT07 on CN2 with an ESP32-C3**

CN1 is the connector toward the fancoil main board (fixed cable) — leave it alone.
CN2 is the optional Wi-Fi module connector — that's our access point.

Phases:
1. Passive CN2 sniff → figure out baud rate, protocol, role of pin S
2. Connect the ESP32-C3 to CN2 as a TQCT07 replacement
3. Implement ESPHome/MQTT firmware

Fallback if CN2 yields nothing:
- **IR emulation** with an IR LED from the ESP32-C3 (simple, no real state)
- **0-10 V interface** if supported by the model (basic fan drive)

---

## CN2 sniffing results (2026-06-20)

### Confirmed parameters
- **Baud rate: 9600** 8N1
- **Protocol: Tuya MCU UART** — `55 AA` header confirmed
- **Traffic direction:** only D0 (T = display→module), D1 (R) silent without a module connected
- **Transmission trigger:** the display board sends a frame for each state change (not continuous polling)

### Frame structure
```
55 AA | ver(03) | cmd(07) | len_hi len_lo | dp_id | dp_type | dp_len_hi dp_len_lo | value... | checksum
```
- cmd `07` = status report (MCU → module)
- ver `03` = protocol version

### DPs confirmed by real sniffing (COMPLETE map)

| DP | Type | Type byte | Example | Value | Notes |
|---|---|---|---|---|---|
| 01 | bool | `01` | `00` / `01` | OFF / ON | power |
| 02 | int 4 bytes | `02` | `00 00 00 14` | 20°C | setpoint |
| 03 | int 4 bytes | `02` | `00 00 00 1A` | 26°C | **ambient temp (sensor)** — emitted only at cold boot |
| 04 | enum 1 byte | `04` | `00` | mode cool | |
| 05 | enum 1 byte | `04` | `04` | fan auto | |
| 06 | bitmap | `05` | `03` / `00` | fault | **diagnostics** — emitted at cold boot |

**Important (cold boot):** DP3 (ambient temp) and DP6 (fault) are transmitted ONLY after a real MCU reset (unplugging from the wall socket), when the board does a full state dump. The ON/OFF key does NOT reset the MCU. To read them on the fly you have to capture the post-boot sequence.

### DP1 (power) mapping confirmed
| Byte | State |
|---|---|
| `00` | Off |
| `01` | On |

### DP4 (mode) mapping confirmed
| Byte | Tuya | Display icon |
|---|---|---|
| `00` | cool | Snowflake |
| `01` | heat | Sun |
| `02` | dehu | Recirculation |
| `03` | fan | Fan only |

### DP5 (fan speed) mapping — from first capture
| Byte | Tuya |
|---|---|
| `00` | superlow |
| `04` | auto |

### Capture files
- `~/nemo_cn2_15s.sr` — first capture, 3 frames
- `~/nemo_cn2_tasti.sr` — capture with key presses, mode and temp confirmed

### DP1 (power), confirmed via ON/OFF key
- `55 AA 03 07 00 05 01 01 00 01 00 11` → OFF
- `55 AA 03 07 00 05 01 01 00 01 01 12` → ON

---

## CN2-R write test (2026-06-20) — FAILED

Setup: CH340 USB-TTL module (5V jumper), device `/dev/tty.usbserial-210`.
Working read wiring: `RXD→T(pin2)`, `TXD→R(pin3)`, `GND→pin5`.

**Loopback TXD↔RXD: OK** → the module transmits and receives correctly.
**Reading CN2-T: OK** → all state changes read in real time at 9600 8N1.

**Writing CN2-R: no effect.** Tried and ignored by the display:
- cmd `06` (set DP) v03 — temp/mode/fan → no effect, no response
- cmd `07` (replicating the display format) → no effect
- Heartbeat `00` v00 and v03 → no response
- Query product `01`, working mode `02`, query DP `08` (v00 and v03) → no response

**Pin S:**
- Idle: ~5.2V (pulled high), like T and R
- Pulled to GND → the display **stops transmitting** (read disabled). So S must stay HIGH. It is not the command enabler.

**Boot capture (power-cycle):** the display only sends the initial state (DP2 temp, DP1 power). **No heartbeat, no query.**

### ⚠️ The early "writing impossible" conclusions were WRONG
The failure was due to **3 bugs of ours**, not a board limitation. The board **IS** a standard Tuya MCU and accepts commands on CN2-R. See the "WORKING PROTOCOL" section below.

---

## ✅ WORKING PROTOCOL — CN2 control (2026-06-20)

The board is a **standard Tuya MCU**. On CN2 we play the role of the **Wi-Fi module**.
Reading AND writing work. The three errors that blocked writing were:

### 1. Checksum — MUST include the `55 AA` header
```python
checksum = sum(all_bytes_from_55_to_end_of_data) & 0xFF
```
Verification on a real frame `55 AA 03 07 00 05 01 01 00 01 01 12`:
`0x55+0xAA+03+07+00+05+01+01+00+01+01 = 274 → &0xFF = 0x12` ✓
(The bug was `sum(frame[2:])` which excluded the header → checksum off by −1, discarded.)

### 2. Version in byte 3
- **Module → MCU** (heartbeat, query, `06` commands): version **`00`** → `55 AA 00 ...`
- **MCU → module** (responses, status report `07`): version **`03`** → `55 AA 03 ...`
(The bug was sending the `06` commands with version `03`.)

### 3. Mandatory handshake before commands + continuous heartbeat
The MCU ignores commands until it considers you "module online". Sequence:
```
Module→MCU  55 AA 00 00 00 00 FF              heartbeat
  MCU→Mod   55 AA 03 00 00 01 00/01 ..         response (00=first, 01=normal)
Module→MCU  55 AA 00 01 00 00 00               query product info
  MCU→Mod   55 AA 03 01 00 2A {"p":"5dgguakbmhwzwiko","v":...}   ASCII JSON
Module→MCU  55 AA 00 02 00 00 01               query working mode
  MCU→Mod   55 AA 03 02 00 02 0C 0D 1F         GPIO LED=0x0C reset=0x0D
Module→MCU  55 AA 00 03 00 01 04 07            network status = cloud connected (no ACK)
Module→MCU  55 AA 00 08 00 00 07               query DP status → full state dump
```
Then a **heartbeat every ~1s** to stay online, and the `06` commands get applied.

### Set DP command (examples, version 00)
```
Set temp 25°C:  55 AA 00 06 00 08 02 02 00 04 00 00 00 19 2E   (DP2 int)
Power ON:       55 AA 00 06 00 05 01 01 00 01 01  <chk>          (DP1 bool)
Mode cool:      55 AA 00 06 00 05 04 04 00 01 00  <chk>          (DP4 enum)
Fan medium:     55 AA 00 06 00 05 05 04 00 01 02  <chk>          (DP5 enum)
```
Confirmation: the MCU replies with a status report `07` containing the new DP value.

**All verified on the physical device (2026-06-20):** power on/off, set temp, mode cool, fan medium — the display follows the commands.

### Tools
- `extra/monitor.py` — passive listening/decoding (RXD→T, GND only)
- `extra/tuya_send.py` — controller (handshake + commands)
- Full-duplex control wiring: `RXD→pin2(T)`, `TXD→pin3(R)`, `GND→pin5`

---

## References

- [tuya-local ideal_clima_fancoil.yaml](https://github.com/deltaclock/tuya-local/blob/8030830581c33e005efabf82cae12e87e1719da9/custom_components/tuya_local/devices/ideal_clima_fancoil.yaml)
- Tuya MCU UART protocol: frame `55 AA ver cmd len_hi len_lo data... checksum`
- **Checksum = sum of ALL bytes (including 55 AA) & 0xFF**
