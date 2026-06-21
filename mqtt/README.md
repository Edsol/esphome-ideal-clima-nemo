# MQTT firmware (PlatformIO)

ESP32-C3 firmware that bridges the Ideal Clima Nemo 1000 fancoil to Home Assistant
over MQTT, with automatic HA discovery. Uses the core [`IdealClimaTuya`](../esp32/)
library plus PubSubClient and ArduinoJson.

Prefer [ESPHome](../esphome/) unless you already run an MQTT broker — it needs no
broker and gives a native HA entity. This option exists for existing Mosquitto setups.

## Prerequisites

- [PlatformIO](https://platformio.org/) (CLI or the VS Code extension)
- An MQTT broker reachable on your network (e.g. Mosquitto)
- ESP32-C3 wired to CN2 — see [`../esp32/WIRING.md`](../esp32/WIRING.md)

## Configure credentials

Credentials are kept out of git. Copy the example and fill it in:

```bash
cd mqtt
cp secrets.ini.example secrets.ini
$EDITOR secrets.ini
```

`secrets.ini` is git-ignored. PlatformIO injects its values into the build flags
(`platformio.ini` reads them via `${secrets.*}`).

## Build & flash

```bash
cd mqtt
pio run                 # compile
pio run -t upload       # flash over USB
pio device monitor      # serial log @ 115200
```

## UART pins

Defined in [`src/main.cpp`](src/main.cpp): `PIN_RX = 20`, `PIN_TX = 21`.
Both CN2 data lines run at 3.3V (verified), so they connect directly — no divider.
Adjust the pins if your board reserves GPIO20/21 (on the bare C3 they are the
UART0 / ROM bootloader console; pick other free GPIOs if you hit boot issues).

## Home Assistant

On connect the firmware publishes a discovery config to
`homeassistant/climate/ideal_clima_nemo/config`, so the `climate` entity appears
automatically (assuming MQTT discovery is enabled in HA, which is the default).
