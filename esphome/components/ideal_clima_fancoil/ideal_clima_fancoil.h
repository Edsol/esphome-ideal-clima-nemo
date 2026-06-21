#pragma once
// ESPHome component for Ideal Clima Nemo fancoils via CN2 (Tuya MCU).
// Protocol: 9600 8N1, 55 AA header, checksum = sum of ALL bytes & 0xFF.
// Module->MCU commands use version 0x00, MCU->module responses use version 0x03.
// Startup requires a handshake plus a continuous heartbeat (~1s).

#include <string>

#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/number/number.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/uart/uart.h"

namespace esphome {
namespace ideal_clima_fancoil {

// DP
static const uint8_t DP_POWER = 0x01, DP_TEMP = 0x02, DP_CUR_TEMP = 0x03,
                     DP_MODE = 0x04, DP_FAN = 0x05, DP_FAULT = 0x06;
// Tuya mode / fan (raw DP bytes, verified on the device)
enum { TM_COOL = 0, TM_HEAT = 1, TM_DEHU = 2, TM_FAN = 3 };
enum { TF_SUPERLOW = 0, TF_LOW = 1, TF_MEDIUM = 2, TF_HIGH = 3, TF_AUTO = 4 };

enum LimitId {
  LIMIT_COOL_MIN = 0,
  LIMIT_COOL_MAX,
  LIMIT_HEAT_MIN,
  LIMIT_HEAT_MAX,
  LIMIT_DRY_MIN,
  LIMIT_DRY_MAX,
  LIMIT_FAN_MIN,
  LIMIT_FAN_MAX,
  LIMIT_COUNT,
};

class IdealClimaFancoil;

struct IdealClimaStoredLimits {
  uint32_t magic;
  float values[LIMIT_COUNT];
};

class IdealClimaLimitNumber : public number::Number {
 public:
  IdealClimaLimitNumber(IdealClimaFancoil *parent, uint8_t limit_id) : parent_(parent), limit_id_(limit_id) {}

 protected:
  void control(float value) override;

  IdealClimaFancoil *parent_;
  uint8_t limit_id_;
};

class IdealClimaDebugSwitch : public switch_::Switch {
 public:
  explicit IdealClimaDebugSwitch(IdealClimaFancoil *parent) : parent_(parent) {}

 protected:
  void write_state(bool state) override;

  IdealClimaFancoil *parent_;
};

class IdealClimaFancoil : public climate::Climate,
                          public Component,
                          public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  climate::ClimateTraits traits() override;
  void set_model_power(const std::string &model_power) { this->model_power_ = model_power; }
  void set_limit_number(uint8_t limit_id, number::Number *number);
  void set_limit_default(uint8_t limit_id, float value);
  void set_limit_value(uint8_t limit_id, float value);
  void set_debug_switch(switch_::Switch *debug_switch);
  void set_debug_logging(bool enabled);

 protected:
  void control(const climate::ClimateCall &call) override;

  // protocol
  void send_frame_(uint8_t ver, uint8_t cmd, const uint8_t *data, uint8_t dlen);
  void send_cmd06_(const uint8_t *data, uint8_t dlen);
  void do_handshake_();
  void handle_frame_(const uint8_t *f, size_t n);
  void parse_status_(const uint8_t *f, size_t n);
  void publish_from_state_();
  int clamp_target_for_mode_(int c) const;
  uint8_t active_mode_() const;
  void restore_limit_values_();
  void save_limit_values_();
  void publish_limit_numbers_();
  void handle_target_confirmation_(int confirmed);
  void restore_confirmed_target_();

  // DP setters
  void set_power_(bool on);
  void set_target_(int c);
  void set_mode_(uint8_t m);
  void set_fan_(uint8_t f);

  // raw state
  bool power_ = false;
  int target_ = 22, current_ = 0;
  uint8_t tmode_ = TM_COOL, tfan_ = TF_AUTO, fault_ = 0;
  std::string model_power_ = "1000";
  float limits_[LIMIT_COUNT] = {8, 40, 0, 40, 0, 40, 0, 40};
  number::Number *limit_numbers_[LIMIT_COUNT] = {};
  switch_::Switch *debug_switch_{nullptr};
  ESPPreferenceObject limits_pref_;
  bool debug_enabled_ = false;
  bool target_pending_ = false;
  int pending_target_ = 0;
  int confirmed_target_ = 22;
  uint32_t pending_target_since_ = 0;

  uint32_t last_hb_ = 0;
  uint8_t buf_[64];
  size_t len_ = 0;
};

}  // namespace ideal_clima_fancoil
}  // namespace esphome
