#pragma once
// IdealClimaTuya — Tuya MCU protocol for the Ideal Clima Nemo 1000 fancoil (CN2).
// Framework-agnostic: uses an Arduino Stream (HardwareSerial). Works on the
// ESP32-C3 both in an Arduino/PlatformIO sketch and as an ESPHome external component.
//
// We emulate the Wi-Fi module. The display board is the Tuya MCU.
// 9600 8N1. Checksum = sum of ALL bytes (incl. 55 AA) & 0xFF.
// Module->MCU commands: version 0x00. MCU->module responses: version 0x03.
//
// Typical use:
//   IdealClimaTuya fc(Serial1);
//   fc.begin();                  // start handshake
//   ... loop() ...  fc.loop();   // handles heartbeat + parsing (call often)
//   fc.setTemp(22); fc.setPower(true); fc.setMode(MODE_COOL); fc.setFan(FAN_AUTO);
//   if (fc.changed()) { ...read fc.power(), fc.targetTemp(), fc.currentTemp(), ... }

#include <Arduino.h>

enum TuyaMode { MODE_COOL = 0, MODE_HEAT = 1, MODE_DEHU = 2, MODE_FAN = 3 };
enum TuyaFan  { FAN_SUPERLOW = 0, FAN_LOW = 1, FAN_MEDIUM = 2, FAN_HIGH = 3, FAN_AUTO = 4 };

class IdealClimaTuya {
 public:
  explicit IdealClimaTuya(Stream &serial) : _s(serial) {}

  // Start: runs the handshake (heartbeat, product, working mode, net status, query DP).
  void begin();
  // Call continuously from loop(): periodic heartbeat + parsing of incoming frames.
  void loop();

  // Commands (DP). Return immediately; state updates when the MCU confirms.
  void setPower(bool on);
  void setTemp(int celsius);
  void setMode(TuyaMode m);
  void setFan(TuyaFan f);

  // Current state (updated from the MCU's 0x07 reports).
  bool      power()       const { return _power; }
  int       targetTemp()  const { return _target; }
  int       currentTemp() const { return _current; }
  TuyaMode  mode()        const { return _mode; }
  TuyaFan   fan()         const { return _fan; }
  uint8_t   fault()       const { return _fault; }
  bool      online()      const { return _online; }

  // true exactly once after a state change (consumes the flag).
  bool changed() { bool c = _changed; _changed = false; return c; }

 private:
  Stream &_s;

  // state
  bool     _power = false;
  int      _target = 0, _current = 0;
  TuyaMode _mode = MODE_COOL;
  TuyaFan  _fan  = FAN_AUTO;
  uint8_t  _fault = 0;
  bool     _online = false;
  bool     _changed = false;

  // heartbeat timing
  unsigned long _lastHb = 0;
  static const unsigned long HB_PERIOD = 1000;  // ms

  // RX buffer
  uint8_t _buf[64];
  size_t  _len = 0;

  // helpers
  void sendFrame(uint8_t ver, uint8_t cmd, const uint8_t *data, uint8_t dlen);
  void sendCmd06(const uint8_t *data, uint8_t dlen);
  void handleFrame(const uint8_t *f, size_t n);
  void parseStatus(const uint8_t *f, size_t n);
};
