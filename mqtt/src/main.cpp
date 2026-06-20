// MQTT firmware — Ideal Clima Nemo 1000 fancoil (ESP32-C3).
// Exposes a climate entity to Home Assistant via MQTT discovery.
//
// UART to CN2: ESP RX <- CN2 pin2 (T), ESP TX -> CN2 pin3 (R), common GND.
//              Both lines run at 3.3V (verified), direct connection. See esp32/WIRING.md.

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "IdealClimaTuya.h"

// --- GPIO UART (ESP32-C3) ---
static const int PIN_RX = 20;   // <- CN2-T (direct, 3.3V)
static const int PIN_TX = 21;   // -> CN2-R

// --- MQTT topics ---
static const char *DEV_ID = "ideal_clima_nemo";
static const char *T_AVAIL   = "ideal_clima/nemo/availability";
static const char *T_MODE_S  = "ideal_clima/nemo/mode/state";
static const char *T_MODE_C  = "ideal_clima/nemo/mode/set";
static const char *T_TEMP_S  = "ideal_clima/nemo/temp/state";
static const char *T_TEMP_C  = "ideal_clima/nemo/temp/set";
static const char *T_CUR_S   = "ideal_clima/nemo/current/state";
static const char *T_FAN_S   = "ideal_clima/nemo/fan/state";
static const char *T_FAN_C   = "ideal_clima/nemo/fan/set";

WiFiClient net;
PubSubClient mqtt(net);
IdealClimaTuya fc(Serial1);

const char *mode_to_str() {
  if (!fc.power()) return "off";
  switch (fc.mode()) {
    case MODE_COOL: return "cool";
    case MODE_HEAT: return "heat";
    case MODE_DEHU: return "dry";
    case MODE_FAN:  return "fan_only";
  }
  return "off";
}
const char *fan_to_str() {
  switch (fc.fan()) {
    case FAN_SUPERLOW: return "superlow";
    case FAN_LOW:      return "low";
    case FAN_MEDIUM:   return "medium";
    case FAN_HIGH:     return "high";
    case FAN_AUTO:     return "auto";
  }
  return "auto";
}

void publish_discovery() {
  JsonDocument doc;
  doc["name"] = "Fancoil Nemo 1000";
  doc["unique_id"] = DEV_ID;
  doc["availability_topic"] = T_AVAIL;
  doc["modes"][0] = "off"; doc["modes"][1] = "cool"; doc["modes"][2] = "heat";
  doc["modes"][3] = "dry"; doc["modes"][4] = "fan_only";
  doc["mode_state_topic"] = T_MODE_S;
  doc["mode_command_topic"] = T_MODE_C;
  doc["temperature_state_topic"] = T_TEMP_S;
  doc["temperature_command_topic"] = T_TEMP_C;
  doc["current_temperature_topic"] = T_CUR_S;
  doc["min_temp"] = 0; doc["max_temp"] = 40; doc["temp_step"] = 1;
  const char *fans[] = {"superlow", "low", "medium", "high", "auto"};
  for (int i = 0; i < 5; i++) doc["fan_modes"][i] = fans[i];
  doc["fan_mode_state_topic"] = T_FAN_S;
  doc["fan_mode_command_topic"] = T_FAN_C;
  JsonObject dev = doc["device"].to<JsonObject>();
  dev["identifiers"][0] = DEV_ID;
  dev["name"] = "Ideal Clima Nemo 1000";
  dev["model"] = "Nemo 1000"; dev["manufacturer"] = "Ideal Clima";

  char buf[1024];
  size_t n = serializeJson(doc, buf);
  mqtt.publish("homeassistant/climate/ideal_clima_nemo/config", (uint8_t *)buf, n, true);
}

void publish_state() {
  mqtt.publish(T_MODE_S, mode_to_str(), true);
  mqtt.publish(T_FAN_S, fan_to_str(), true);
  char tmp[8];
  snprintf(tmp, sizeof(tmp), "%d", fc.targetTemp());  mqtt.publish(T_TEMP_S, tmp, true);
  snprintf(tmp, sizeof(tmp), "%d", fc.currentTemp()); mqtt.publish(T_CUR_S, tmp, true);
}

void on_message(char *topic, uint8_t *payload, unsigned int len) {
  String t = topic, p;
  for (unsigned i = 0; i < len; i++) p += (char)payload[i];

  if (t == T_MODE_C) {
    if (p == "off") fc.setPower(false);
    else {
      fc.setPower(true);
      if (p == "cool") fc.setMode(MODE_COOL);
      else if (p == "heat") fc.setMode(MODE_HEAT);
      else if (p == "dry") fc.setMode(MODE_DEHU);
      else if (p == "fan_only") fc.setMode(MODE_FAN);
    }
  } else if (t == T_TEMP_C) {
    fc.setTemp((int)roundf(p.toFloat()));
  } else if (t == T_FAN_C) {
    if (p == "superlow") fc.setFan(FAN_SUPERLOW);
    else if (p == "low") fc.setFan(FAN_LOW);
    else if (p == "medium") fc.setFan(FAN_MEDIUM);
    else if (p == "high") fc.setFan(FAN_HIGH);
    else if (p == "auto") fc.setFan(FAN_AUTO);
  }
}

void mqtt_reconnect() {
  while (!mqtt.connected()) {
    if (mqtt.connect(DEV_ID, MQTT_USER, MQTT_PASS, T_AVAIL, 0, true, "offline")) {
      mqtt.publish(T_AVAIL, "online", true);
      publish_discovery();
      mqtt.subscribe(T_MODE_C);
      mqtt.subscribe(T_TEMP_C);
      mqtt.subscribe(T_FAN_C);
      publish_state();
    } else {
      delay(2000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial1.begin(9600, SERIAL_8N1, PIN_RX, PIN_TX);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) { delay(300); }
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setBufferSize(1024);
  mqtt.setCallback(on_message);
  fc.begin();
}

void loop() {
  if (!mqtt.connected()) mqtt_reconnect();
  mqtt.loop();
  fc.loop();
  if (fc.changed()) publish_state();
}
