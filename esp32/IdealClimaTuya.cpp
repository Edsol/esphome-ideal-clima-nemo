#include "IdealClimaTuya.h"

// ---- send frame ------------------------------------------------------------

void IdealClimaTuya::sendFrame(uint8_t ver, uint8_t cmd,
                               const uint8_t *data, uint8_t dlen) {
  uint8_t hdr[6] = {0x55, 0xAA, ver, cmd, 0x00, dlen};
  uint16_t sum = 0;
  for (int i = 0; i < 6; i++) { _s.write(hdr[i]); sum += hdr[i]; }
  for (uint8_t i = 0; i < dlen; i++) { _s.write(data[i]); sum += data[i]; }
  _s.write((uint8_t)(sum & 0xFF));   // checksum INCLUDES the header
}

void IdealClimaTuya::sendCmd06(const uint8_t *data, uint8_t dlen) {
  sendFrame(0x00, 0x06, data, dlen);   // version 0x00, module->MCU
}

// ---- handshake -------------------------------------------------------------

void IdealClimaTuya::begin() {
  sendFrame(0x00, 0x00, nullptr, 0);             // heartbeat
  delay(50);
  sendFrame(0x00, 0x01, nullptr, 0);             // query product info
  delay(50);
  sendFrame(0x00, 0x02, nullptr, 0);             // query working mode
  delay(50);
  uint8_t net = 0x04;                            // network status = cloud connected
  sendFrame(0x00, 0x03, &net, 1);
  delay(50);
  sendFrame(0x00, 0x08, nullptr, 0);             // query DP status (full dump)
  _lastHb = millis();
}

// ---- commands --------------------------------------------------------------

void IdealClimaTuya::setPower(bool on) {
  uint8_t d[5] = {0x01, 0x01, 0x00, 0x01, (uint8_t)(on ? 1 : 0)};
  sendCmd06(d, 5);
}
void IdealClimaTuya::setTemp(int c) {
  uint8_t d[8] = {0x02, 0x02, 0x00, 0x04,
                  (uint8_t)((c >> 24) & 0xFF), (uint8_t)((c >> 16) & 0xFF),
                  (uint8_t)((c >> 8) & 0xFF),  (uint8_t)(c & 0xFF)};
  sendCmd06(d, 8);
}
void IdealClimaTuya::setMode(TuyaMode m) {
  uint8_t d[5] = {0x04, 0x04, 0x00, 0x01, (uint8_t)m};
  sendCmd06(d, 5);
}
void IdealClimaTuya::setFan(TuyaFan f) {
  uint8_t d[5] = {0x05, 0x04, 0x00, 0x01, (uint8_t)f};
  sendCmd06(d, 5);
}

// ---- loop / RX -------------------------------------------------------------

void IdealClimaTuya::loop() {
  // periodic heartbeat
  unsigned long now = millis();
  if (now - _lastHb >= HB_PERIOD) {
    sendFrame(0x00, 0x00, nullptr, 0);
    _lastHb = now;
  }

  // accumulate bytes and extract 55 AA frames ...
  while (_s.available()) {
    uint8_t b = _s.read();
    if (_len == 0 && b != 0x55) continue;
    if (_len == 1 && b != 0xAA) { _len = (b == 0x55) ? 1 : 0; continue; }
    if (_len < sizeof(_buf)) _buf[_len++] = b;
    else { _len = 0; continue; }

    if (_len >= 6) {
      size_t total = 6 + _buf[5] + 1;            // header+len + data + checksum
      if (total <= sizeof(_buf) && _len >= total) {
        // verify checksum
        uint16_t sum = 0;
        for (size_t i = 0; i < total - 1; i++) sum += _buf[i];
        if ((sum & 0xFF) == _buf[total - 1]) handleFrame(_buf, total);
        _len = 0;
      } else if (total > sizeof(_buf)) {
        _len = 0;                                // frame too long, drop
      }
    }
  }
}

void IdealClimaTuya::handleFrame(const uint8_t *f, size_t n) {
  uint8_t cmd = f[3];
  if (cmd == 0x00) { _online = true; }           // heartbeat response
  else if (cmd == 0x07) parseStatus(f, n);       // status report
}

void IdealClimaTuya::parseStatus(const uint8_t *f, size_t n) {
  if (n < 12) return;                            // 6 hdr + dp,type,len(2) + >=1 val + chk
  uint8_t dp  = f[6];
  const uint8_t *v = f + 10;                     // value after dp,type,len_hi,len_lo
  size_t vlen = (n - 1) - 10;
  bool ch = true;
  switch (dp) {
    case 0x01: _power   = v[vlen - 1]; break;
    case 0x02: _target  = ((int)v[0]<<24)|((int)v[1]<<16)|((int)v[2]<<8)|v[3]; break;
    case 0x03: _current = ((int)v[0]<<24)|((int)v[1]<<16)|((int)v[2]<<8)|v[3]; break;
    case 0x04: _mode    = (TuyaMode)v[vlen - 1]; break;
    case 0x05: _fan     = (TuyaFan)v[vlen - 1]; break;
    case 0x06: _fault   = v[vlen - 1]; break;
    default:   ch = false; break;
  }
  if (ch) _changed = true;
}
