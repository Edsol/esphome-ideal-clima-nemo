#include "ideal_clima_fancoil.h"
#include "esphome/core/log.h"
#include <algorithm>
#include <cstring>

namespace esphome {
namespace ideal_clima_fancoil {

static const char *const TAG = "ideal_clima_fancoil";
static const uint32_t HB_PERIOD = 1000;  // ms
static const uint32_t TARGET_CONFIRM_TIMEOUT = 5000;  // ms
static const uint32_t LIMITS_PREF_MAGIC = 0x1CFA1001;

static void log_hex_(int level, const char *prefix, const uint8_t *data, size_t len) {
  char hex[3 * 64 + 1];
  size_t pos = 0;
  size_t max_len = len > 64 ? 64 : len;
  for (size_t i = 0; i < max_len && pos + 3 < sizeof(hex); i++) {
    pos += snprintf(hex + pos, sizeof(hex) - pos, "%02X%s", data[i], i + 1 < max_len ? " " : "");
  }
  if (len > max_len && pos + 4 < sizeof(hex)) {
    snprintf(hex + pos, sizeof(hex) - pos, "...");
  }
  ESP_LOG_LEVEL(level, TAG, "%s len=%u: %s", prefix, (unsigned) len, hex);
}

// ---- ESPHome lifecycle -----------------------------------------------------

void IdealClimaFancoil::setup() {
  this->limits_pref_ = global_preferences->make_preference<IdealClimaStoredLimits>(
      this->get_preference_hash() ^ 0x61A71C5UL, true);
  this->restore_limit_values_();
  publish_limit_numbers_();

  if (this->debug_switch_ != nullptr) {
    auto initial_debug = this->debug_switch_->get_initial_state_with_restore_mode();
    this->debug_enabled_ = initial_debug.value_or(false);
    this->debug_switch_->publish_state(this->debug_enabled_);
  }

  if (this->debug_enabled_) ESP_LOGD(TAG, "Setup: starting Tuya MCU handshake on fancoil UART");
  do_handshake_();
}

void IdealClimaLimitNumber::control(float value) {
  this->parent_->set_limit_value(this->limit_id_, value);
}

void IdealClimaDebugSwitch::write_state(bool state) {
  this->parent_->set_debug_logging(state);
}

void IdealClimaFancoil::set_limit_number(uint8_t limit_id, number::Number *number) {
  if (limit_id < LIMIT_COUNT) this->limit_numbers_[limit_id] = number;
}

void IdealClimaFancoil::set_limit_default(uint8_t limit_id, float value) {
  if (limit_id < LIMIT_COUNT) this->limits_[limit_id] = value;
}

void IdealClimaFancoil::set_debug_switch(switch_::Switch *debug_switch) {
  this->debug_switch_ = debug_switch;
}

void IdealClimaFancoil::set_debug_logging(bool enabled) {
  this->debug_enabled_ = enabled;
  ESP_LOGCONFIG(TAG, "Protocol debug logging %s", enabled ? "enabled" : "disabled");
  if (this->debug_switch_ != nullptr) this->debug_switch_->publish_state(enabled);
}

void IdealClimaFancoil::set_limit_value(uint8_t limit_id, float value) {
  if (limit_id >= LIMIT_COUNT) return;

  bool is_min = (limit_id % 2) == 0;
  uint8_t paired_id = is_min ? limit_id + 1 : limit_id - 1;
  if (is_min && value > this->limits_[paired_id]) {
    ESP_LOGW(TAG, "Limit update rejected: min %.0f > max %.0f", value, this->limits_[paired_id]);
    this->limit_numbers_[limit_id]->publish_state(this->limits_[limit_id]);
    return;
  }
  if (!is_min && value < this->limits_[paired_id]) {
    ESP_LOGW(TAG, "Limit update rejected: max %.0f < min %.0f", value, this->limits_[paired_id]);
    this->limit_numbers_[limit_id]->publish_state(this->limits_[limit_id]);
    return;
  }

  this->limits_[limit_id] = value;
  if (this->debug_enabled_) ESP_LOGD(TAG, "Limit %u set to %.0f C", limit_id, value);
  this->limit_numbers_[limit_id]->publish_state(value);
  this->save_limit_values_();
}

void IdealClimaFancoil::restore_limit_values_() {
  IdealClimaStoredLimits stored{};
  if (!this->limits_pref_.load(&stored) || stored.magic != LIMITS_PREF_MAGIC) return;
  memcpy(this->limits_, stored.values, sizeof(this->limits_));
  ESP_LOGCONFIG(TAG, "Restored temperature limits from preferences");
}

void IdealClimaFancoil::save_limit_values_() {
  IdealClimaStoredLimits stored{};
  stored.magic = LIMITS_PREF_MAGIC;
  memcpy(stored.values, this->limits_, sizeof(this->limits_));
  if (!this->limits_pref_.save(&stored)) {
    ESP_LOGW(TAG, "Failed to save temperature limits");
  }
}

void IdealClimaFancoil::publish_limit_numbers_() {
  for (uint8_t i = 0; i < LIMIT_COUNT; i++) {
    if (this->limit_numbers_[i] != nullptr) this->limit_numbers_[i]->publish_state(this->limits_[i]);
  }
}

void IdealClimaFancoil::do_handshake_() {
  if (this->debug_enabled_) ESP_LOGD(TAG, "Handshake: heartbeat, product info, working mode, network status, DP query");
  if (this->debug_enabled_) ESP_LOGD(TAG, "Handshake TX: heartbeat");
  send_frame_(0x00, 0x00, nullptr, 0);              // heartbeat
  delay(50);
  if (this->debug_enabled_) ESP_LOGD(TAG, "Handshake TX: query product info");
  send_frame_(0x00, 0x01, nullptr, 0);              // query product info
  delay(50);
  if (this->debug_enabled_) ESP_LOGD(TAG, "Handshake TX: query working mode");
  send_frame_(0x00, 0x02, nullptr, 0);              // query working mode
  delay(50);
  uint8_t net = 0x04;                               // network status = cloud
  if (this->debug_enabled_) ESP_LOGD(TAG, "Handshake TX: network status cloud");
  send_frame_(0x00, 0x03, &net, 1);
  delay(50);
  if (this->debug_enabled_) ESP_LOGD(TAG, "Handshake TX: query DP status");
  send_frame_(0x00, 0x08, nullptr, 0);              // query DP status
  last_hb_ = millis();
}

void IdealClimaFancoil::loop() {
  uint32_t now = millis();
  if (now - last_hb_ >= HB_PERIOD) {
    ESP_LOGVV(TAG, "TX heartbeat");
    send_frame_(0x00, 0x00, nullptr, 0);
    last_hb_ = now;
  }

  if (this->target_pending_ && now - this->pending_target_since_ >= TARGET_CONFIRM_TIMEOUT) {
    ESP_LOGW(TAG, "Target %d C not confirmed by fancoil after %ums; restoring %d C in HA",
             this->pending_target_, TARGET_CONFIRM_TIMEOUT, this->confirmed_target_);
    this->target_pending_ = false;
    this->restore_confirmed_target_();
  }

  while (available()) {
    uint8_t b = read();
    ESP_LOGVV(TAG, "RX byte 0x%02X", b);
    if (len_ == 0 && b != 0x55) {
      ESP_LOGVV(TAG, "RX skip byte before header: 0x%02X", b);
      continue;
    }
    if (len_ == 1 && b != 0xAA) {
      ESP_LOGVV(TAG, "RX bad second header byte: 0x%02X", b);
      len_ = (b == 0x55) ? 1 : 0;
      continue;
    }
    if (len_ < sizeof(buf_)) {
      buf_[len_++] = b;
    } else {
      ESP_LOGW(TAG, "RX buffer overflow, dropping partial frame");
      len_ = 0;
      continue;
    }

    if (len_ >= 6) {
      size_t payload_len = ((size_t) buf_[4] << 8) | buf_[5];
      size_t total = 6 + payload_len + 1;
      if (total <= sizeof(buf_) && len_ >= total) {
        uint16_t sum = 0;
        for (size_t i = 0; i < total - 1; i++) sum += buf_[i];
        if ((sum & 0xFF) == buf_[total - 1]) {
          handle_frame_(buf_, total);
        } else {
          ESP_LOGW(TAG, "RX checksum mismatch cmd=0x%02X len=%u expected=0x%02X got=0x%02X",
                   buf_[3], (unsigned) total, (uint8_t) (sum & 0xFF), buf_[total - 1]);
          log_hex_(ESPHOME_LOG_LEVEL_WARN, "RX bad frame", buf_, total);
        }
        len_ = 0;
      } else if (total > sizeof(buf_)) {
        ESP_LOGW(TAG, "RX frame too long payload=%u total=%u, dropping", (unsigned) payload_len, (unsigned) total);
        len_ = 0;
      }
    }
  }
}

// ---- protocol TX -----------------------------------------------------------

void IdealClimaFancoil::send_frame_(uint8_t ver, uint8_t cmd,
                                    const uint8_t *data, uint8_t dlen) {
  if (this->parent_ == nullptr) {
    ESP_LOGE(TAG, "TX failed: UART parent is null");
    return;
  }

  uint8_t frame[6 + 64 + 1];
  uint8_t hdr[6] = {0x55, 0xAA, ver, cmd, 0x00, dlen};
  size_t frame_len = 6 + dlen + 1;
  uint16_t sum = 0;

  for (int i = 0; i < 6; i++) {
    frame[i] = hdr[i];
    sum += hdr[i];
  }
  for (uint8_t i = 0; i < dlen; i++) {
    frame[6 + i] = data[i];
    sum += data[i];
  }
  frame[6 + dlen] = (uint8_t) (sum & 0xFF);

  log_hex_(ESPHOME_LOG_LEVEL_VERY_VERBOSE, "TX frame", frame, frame_len);
  this->write_array(frame, frame_len);
}

void IdealClimaFancoil::send_cmd06_(const uint8_t *data, uint8_t dlen) {
  send_frame_(0x00, 0x06, data, dlen);
}

uint8_t IdealClimaFancoil::active_mode_() const {
  if (!this->power_) return this->tmode_;
  switch (this->mode) {
    case climate::CLIMATE_MODE_COOL: return TM_COOL;
    case climate::CLIMATE_MODE_HEAT: return TM_HEAT;
    case climate::CLIMATE_MODE_DRY: return TM_DEHU;
    case climate::CLIMATE_MODE_FAN_ONLY: return TM_FAN;
    default: return this->tmode_;
  }
}

int IdealClimaFancoil::clamp_target_for_mode_(int c) const {
  uint8_t min_id = LIMIT_COOL_MIN;
  uint8_t max_id = LIMIT_COOL_MAX;
  switch (this->active_mode_()) {
    case TM_COOL:
      min_id = LIMIT_COOL_MIN;
      max_id = LIMIT_COOL_MAX;
      break;
    case TM_HEAT:
      min_id = LIMIT_HEAT_MIN;
      max_id = LIMIT_HEAT_MAX;
      break;
    case TM_DEHU:
      min_id = LIMIT_DRY_MIN;
      max_id = LIMIT_DRY_MAX;
      break;
    case TM_FAN:
      min_id = LIMIT_FAN_MIN;
      max_id = LIMIT_FAN_MAX;
      break;
  }

  int min_temp = (int) lroundf(this->limits_[min_id]);
  int max_temp = (int) lroundf(this->limits_[max_id]);
  return std::max(min_temp, std::min(max_temp, c));
}

void IdealClimaFancoil::set_power_(bool on) {
  if (this->debug_enabled_) ESP_LOGD(TAG, "Command: set power %s", on ? "ON" : "OFF");
  uint8_t d[5] = {DP_POWER, 0x01, 0x00, 0x01, (uint8_t)(on ? 1 : 0)};
  send_cmd06_(d, 5);
}
void IdealClimaFancoil::set_target_(int c) {
  if (this->debug_enabled_) ESP_LOGD(TAG, "Command: set target %d C and wait for fancoil confirmation", c);
  this->target_pending_ = true;
  this->pending_target_ = c;
  this->pending_target_since_ = millis();
  uint8_t d[8] = {DP_TEMP, 0x02, 0x00, 0x04,
                  (uint8_t)((c >> 24) & 0xFF), (uint8_t)((c >> 16) & 0xFF),
                  (uint8_t)((c >> 8) & 0xFF),  (uint8_t)(c & 0xFF)};
  send_cmd06_(d, 8);
}
void IdealClimaFancoil::set_mode_(uint8_t m) {
  if (this->debug_enabled_) ESP_LOGD(TAG, "Command: set mode raw=%u", m);
  uint8_t d[5] = {DP_MODE, 0x04, 0x00, 0x01, m};
  send_cmd06_(d, 5);
}
void IdealClimaFancoil::set_fan_(uint8_t f) {
  if (this->debug_enabled_) ESP_LOGD(TAG, "Command: set fan raw=%u", f);
  uint8_t d[5] = {DP_FAN, 0x04, 0x00, 0x01, f};
  send_cmd06_(d, 5);
}

// ---- protocol RX -----------------------------------------------------------

void IdealClimaFancoil::handle_frame_(const uint8_t *f, size_t n) {
  if (f[3] == 0x00) {
    ESP_LOGVV(TAG, "RX heartbeat response");
    log_hex_(ESPHOME_LOG_LEVEL_VERY_VERBOSE, "RX frame", f, n);
  } else if (f[3] == 0x07) {
    if (this->debug_enabled_) ESP_LOGD(TAG, "RX status frame len=%u", (unsigned) n);
    log_hex_(ESPHOME_LOG_LEVEL_VERY_VERBOSE, "RX frame", f, n);
    parse_status_(f, n);
  }
  else if (f[3] == 0x01) {
    if (this->debug_enabled_) ESP_LOGD(TAG, "RX product info response");
  } else if (f[3] == 0x02) {
    if (this->debug_enabled_) ESP_LOGD(TAG, "RX working mode response");
  } else if (this->debug_enabled_) {
    ESP_LOGD(TAG, "RX unhandled cmd=0x%02X", f[3]);
  }
}

void IdealClimaFancoil::parse_status_(const uint8_t *f, size_t n) {
  const uint8_t *data = f + 6;
  const uint8_t *end = f + n - 1;  // exclude checksum
  bool changed = false;

  while (data + 4 <= end) {
    uint8_t dp = data[0];
    uint16_t vlen = ((uint16_t) data[2] << 8) | data[3];
    const uint8_t *v = data + 4;
    if (v + vlen > end) {
      ESP_LOGW(TAG, "RX truncated DP%u payload len=%u", dp, vlen);
      break;
    }

    bool dp_changed = true;
    switch (dp) {
      case DP_POWER:
        if (vlen >= 1) power_ = v[vlen - 1];
        else dp_changed = false;
        break;
      case DP_TEMP:
        if (vlen == 4) {
          target_ = ((int)v[0]<<24)|((int)v[1]<<16)|((int)v[2]<<8)|v[3];
          handle_target_confirmation_(target_);
        } else {
          dp_changed = false;
        }
        break;
      case DP_CUR_TEMP:
        if (vlen == 4) current_ = ((int)v[0]<<24)|((int)v[1]<<16)|((int)v[2]<<8)|v[3];
        else dp_changed = false;
        break;
      case DP_MODE:
        if (vlen >= 1) tmode_ = v[vlen - 1];
        else dp_changed = false;
        break;
      case DP_FAN:
        if (vlen >= 1) tfan_ = v[vlen - 1];
        else dp_changed = false;
        break;
      case DP_FAULT:
        if (vlen >= 1) fault_ = v[vlen - 1];
        else dp_changed = false;
        break;
      default:
        dp_changed = false;
        break;
    }

    if (dp_changed) {
      changed = true;
      log_hex_(ESPHOME_LOG_LEVEL_VERY_VERBOSE, "RX DP value", v, vlen);
      if (this->debug_enabled_) {
        ESP_LOGD(TAG, "RX DP%u type=0x%02X len=%u -> power=%d target=%dC ambient=%dC mode=%u fan=%u fault=%u",
                 dp, data[1], vlen, power_, target_, current_, tmode_, tfan_, fault_);
      }
    } else if (this->debug_enabled_) {
      ESP_LOGD(TAG, "RX DP%u type=0x%02X len=%u ignored", dp, data[1], vlen);
    }
    data = v + vlen;
  }

  if (changed) publish_from_state_();
}

void IdealClimaFancoil::handle_target_confirmation_(int confirmed) {
  this->confirmed_target_ = confirmed;
  if (!this->target_pending_) return;

  if (confirmed == this->pending_target_) {
    if (this->debug_enabled_) ESP_LOGD(TAG, "Target %d C confirmed by fancoil", confirmed);
  } else {
    ESP_LOGW(TAG, "Fancoil answered target %d C after request %d C; HA will follow fancoil",
             confirmed, this->pending_target_);
  }
  this->target_pending_ = false;
}

void IdealClimaFancoil::restore_confirmed_target_() {
  this->target_temperature = this->confirmed_target_;
  this->publish_state();
}

// ---- state <-> climate mapping ---------------------------------------------

void IdealClimaFancoil::publish_from_state_() {
  if (!power_) {
    this->mode = climate::CLIMATE_MODE_OFF;
  } else {
    switch (tmode_) {
      case TM_COOL: this->mode = climate::CLIMATE_MODE_COOL; break;
      case TM_HEAT: this->mode = climate::CLIMATE_MODE_HEAT; break;
      case TM_DEHU: this->mode = climate::CLIMATE_MODE_DRY; break;
      case TM_FAN:  this->mode = climate::CLIMATE_MODE_FAN_ONLY; break;
    }
  }
  this->target_temperature = target_;
  this->current_temperature = current_;

  switch (tfan_) {
    case TF_SUPERLOW: this->set_custom_fan_mode_("superlow"); break;
    case TF_LOW:      this->clear_custom_fan_mode_(); this->fan_mode = climate::CLIMATE_FAN_LOW; break;
    case TF_MEDIUM:   this->clear_custom_fan_mode_(); this->fan_mode = climate::CLIMATE_FAN_MEDIUM; break;
    case TF_HIGH:     this->clear_custom_fan_mode_(); this->fan_mode = climate::CLIMATE_FAN_HIGH; break;
    case TF_AUTO:     this->clear_custom_fan_mode_(); this->fan_mode = climate::CLIMATE_FAN_AUTO; break;
  }
  if (this->debug_enabled_) {
    ESP_LOGD(TAG, "Publish climate state: mode=%d target=%.1f current=%.1f fan=%s custom_fan=%s",
             this->mode, this->target_temperature, this->current_temperature,
             this->fan_mode.has_value() ? LOG_STR_ARG(climate::climate_fan_mode_to_string(*this->fan_mode)) : "none",
             this->get_custom_fan_mode() != nullptr ? this->get_custom_fan_mode() : "none");
  }
  this->publish_state();
}

// ---- commands from Home Assistant ------------------------------------------

void IdealClimaFancoil::control(const climate::ClimateCall &call) {
  if (this->debug_enabled_) ESP_LOGD(TAG, "Climate control call received");
  if (call.get_mode().has_value()) {
    climate::ClimateMode m = *call.get_mode();
    if (this->debug_enabled_) ESP_LOGD(TAG, "Climate call mode=%s", LOG_STR_ARG(climate::climate_mode_to_string(m)));
    if (m == climate::CLIMATE_MODE_OFF) {
      set_power_(false);
    } else {
      set_power_(true);
      switch (m) {
        case climate::CLIMATE_MODE_COOL:     set_mode_(TM_COOL); break;
        case climate::CLIMATE_MODE_HEAT:     set_mode_(TM_HEAT); break;
        case climate::CLIMATE_MODE_DRY:      set_mode_(TM_DEHU); break;
        case climate::CLIMATE_MODE_FAN_ONLY: set_mode_(TM_FAN);  break;
        default: break;
      }
    }
  }

  if (call.get_target_temperature().has_value()) {
    float target = *call.get_target_temperature();
    if (this->debug_enabled_) ESP_LOGD(TAG, "Climate call target=%.1f", target);
    set_target_((int)lroundf(target));
  }

  if (call.get_fan_mode().has_value()) {
    if (this->debug_enabled_) {
      ESP_LOGD(TAG, "Climate call fan=%s", LOG_STR_ARG(climate::climate_fan_mode_to_string(*call.get_fan_mode())));
    }
    switch (*call.get_fan_mode()) {
      case climate::CLIMATE_FAN_LOW:    set_fan_(TF_LOW); break;
      case climate::CLIMATE_FAN_MEDIUM: set_fan_(TF_MEDIUM); break;
      case climate::CLIMATE_FAN_HIGH:   set_fan_(TF_HIGH); break;
      case climate::CLIMATE_FAN_AUTO:   set_fan_(TF_AUTO); break;
      default: break;
    }
  } else if (call.has_custom_fan_mode()) {
    if (this->debug_enabled_) ESP_LOGD(TAG, "Climate call custom fan=%s", call.get_custom_fan_mode());
    if (strcmp(call.get_custom_fan_mode(), "superlow") == 0) set_fan_(TF_SUPERLOW);
  }
}

climate::ClimateTraits IdealClimaFancoil::traits() {
  auto t = climate::ClimateTraits();
  t.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE);
  t.set_supported_modes({
      climate::CLIMATE_MODE_OFF,
      climate::CLIMATE_MODE_COOL,
      climate::CLIMATE_MODE_HEAT,
      climate::CLIMATE_MODE_DRY,
      climate::CLIMATE_MODE_FAN_ONLY,
  });
  t.set_supported_fan_modes({
      climate::CLIMATE_FAN_LOW,
      climate::CLIMATE_FAN_MEDIUM,
      climate::CLIMATE_FAN_HIGH,
      climate::CLIMATE_FAN_AUTO,
  });
  t.set_supported_custom_fan_modes({"superlow"});
  t.set_visual_min_temperature(8);
  t.set_visual_max_temperature(30);
  t.set_visual_temperature_step(1);
  return t;
}

void IdealClimaFancoil::dump_config() {
  ESP_LOGCONFIG(TAG, "Ideal Clima Nemo %s fancoil (Tuya MCU on UART)", this->model_power_.c_str());
  this->check_uart_settings(9600);
}

}  // namespace ideal_clima_fancoil
}  // namespace esphome
