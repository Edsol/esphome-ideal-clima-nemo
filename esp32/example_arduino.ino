// ESP32-C3 example — Ideal Clima Nemo 1000 fancoil control via CN2.
// Uses the IdealClimaTuya library (put .h and .cpp in the same folder as the sketch).
//
// UART: uses Serial1 on two free ESP32-C3 GPIOs.
//   ESP RX  <- CN2 pin2 (T)   (direct, 3.3V)
//   ESP TX  -> CN2 pin3 (R)   (direct, 3.3V)
//   GND shared with CN2 pin5
//
// Both CN2 data lines run at 3.3V (verified), so they connect directly to the
// ESP32-C3 GPIOs — no divider, no level shifter. See WIRING.md.

#include "IdealClimaTuya.h"

// Pick two free GPIOs (avoid the boot/strapping pins). C3 Super Mini example:
static const int PIN_RX = 20;   // receives from CN2-T (direct, 3.3V)
static const int PIN_TX = 21;   // transmits to CN2-R (direct, 3.3V)

IdealClimaTuya fc(Serial1);

void setup() {
  Serial.begin(115200);                       // USB log
  Serial1.begin(9600, SERIAL_8N1, PIN_RX, PIN_TX);
  delay(200);
  fc.begin();                                 // handshake
  Serial.println("IdealClimaTuya started");
}

unsigned long lastDemo = 0;

void loop() {
  fc.loop();                                  // heartbeat + parsing (call often)

  if (fc.changed()) {
    Serial.printf("STATE  power=%d target=%dC ambient=%dC mode=%d fan=%d fault=%u online=%d\n",
                  fc.power(), fc.targetTemp(), fc.currentTemp(),
                  fc.mode(), fc.fan(), fc.fault(), fc.online());
  }

  // Demo: every 30s set 22C cool auto (remove in production)
  if (millis() - lastDemo > 30000) {
    lastDemo = millis();
    fc.setPower(true);
    fc.setMode(MODE_COOL);
    fc.setFan(FAN_AUTO);
    fc.setTemp(22);
    Serial.println("Demo: power on, cool, auto, 22C");
  }
}
