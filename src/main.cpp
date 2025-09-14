#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <PubSubClient.h>
#include <FS.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <config.h>  // ⚠️ WIFI_SSID, WIFI_PASSWORD, MQTT_SERVER, MQTT_PORT, MQTT_USER, MQTT_PASSWORD

// ==================== BROCHES ====================
#define ONE_WIRE_BUS    4
#define RELAY_PIN       26
#define LED_ACTIVE_PIN  25
#define LED_RELAY_PIN   27

// ==================== OBJETS ====================
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

WiFiClient espClient;
PubSubClient mqttClient(espClient);
WebServer server(80);

// ==================== MQTT TOPICS ====================
const char* mqtt_topic_temp    = "ballon/temperature";
const char* mqtt_topic_state   = "ballon/relais/state";
const char* mqtt_topic_command = "ballon/relais/command";
const char* mqtt_topic_status  = "ballon/status";

// ==================== VARIABLES ====================
unsigned long lastMqtt = 0;
bool relayState = false;
bool safetyActive = false;

struct Config {
  int active_start_hour;
  int active_end_hour;
  float temp_max;
  float temp_reset;
};
Config config;

// ==================== NTP ====================
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 3600;
const int daylightOffset_sec = 3600;

// ==================== CONFIG JSON ====================
void loadConfig() {
  if(!SPIFFS.exists("/config.json")) {
    config.active_start_hour = 9;
    config.active_end_hour   = 23;
    config.temp_max          = 70.0;
    config.temp_reset        = 65.0;
    return;
  }
  File file = SPIFFS.open("/config.json", "r");
  DynamicJsonDocument doc(256);
  if(deserializeJson(doc, file) == DeserializationError::Ok) {
    config.active_start_hour = doc["active_start_hour"] | 9;
    config.active_end_hour   = doc["active_end_hour"]   | 23;
    config.temp_max          = doc["temp_max"]          | 70.0;
    config.temp_reset        = doc["temp_reset"]        | 65.0;
  }
  file.close();
}

void saveConfig() {
  File file = SPIFFS.open("/config.json", "w");
  DynamicJsonDocument doc(256);
  doc["active_start_hour"] = config.active_start_hour;
  doc["active_end_hour"]   = config.active_end_hour;
  doc["temp_max"]          = config.temp_max;
  doc["temp_reset"]        = config.temp_reset;
  serializeJsonPretty(doc, file);
  file.close();
}

// ==================== UTILS ====================
bool isActiveHour() {
  time_t now = time(nullptr);
  struct tm *ptm = localtime(&now);
  if (!ptm) return true;
  int hour = ptm->tm_hour;
  if (config.active_start_hour < config.active_end_hour) {
    return (hour >= config.active_start_hour && hour < config.active_end_hour);
  } else {
    return (hour >= config.active_start_hour || hour < config.active_end_hour);
  }
}

// ==================== SÉCURITÉ ====================
void checkSafety() {
  sensors.requestTemperatures();
  float temp = sensors.getTempCByIndex(0);
  if (temp != DEVICE_DISCONNECTED_C) {
    if (temp >= config.temp_max && relayState && !safetyActive) {
      relayState = false;
      digitalWrite(RELAY_PIN, LOW);
      digitalWrite(LED_RELAY_PIN, LOW);
      safetyActive = true;
      mqttClient.publish(mqtt_topic_state, "OFF", true);
      mqttClient.publish(mqtt_topic_status, "SAFETY", true);
    } else if (temp <= config.temp_reset && safetyActive) {
      safetyActive = false;
      mqttClient.publish(mqtt_topic_status, "RESET", true);
    }
  }
}

// ==================== WIFI & MQTT ====================
void setup_wifi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) delay(500);
  Serial.println("✅ WiFi connecté : " + WiFi.localIP().toString());
}

void setupNTP() {
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  while (time(nullptr) < 100000) delay(500);
}

void callbackMQTT(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  msg.trim();
  if (String(topic) == mqtt_topic_command) {
    if (msg == "ON" && !safetyActive && isActiveHour()) {
      relayState = true;
    } else if (msg == "OFF") {
      relayState = false;
    }
    digitalWrite(RELAY_PIN, relayState ? HIGH : LOW);
    digitalWrite(LED_RELAY_PIN, relayState ? HIGH : LOW);
    mqttClient.publish(mqtt_topic_state, relayState ? "ON" : "OFF", true);
  }
}

void reconnectMQTT() {
  while (!mqttClient.connected()) {
    if (mqttClient.connect("ESP32Ballon", MQTT_USER, MQTT_PASSWORD)) {
      mqttClient.subscribe(mqtt_topic_command);
      mqttClient.publish(mqtt_topic_status, "ONLINE", true);
      mqttClient.publish(mqtt_topic_state, relayState ? "ON" : "OFF", true);
    } else {
      delay(5000);
    }
  }
}

// ==================== API JSON ====================
void setupApi() {
  server.serveStatic("/", SPIFFS, "/index.html");
  server.serveStatic("/config.html", SPIFFS, "/config.html");
  server.serveStatic("/style.css", SPIFFS, "/style.css");
  server.serveStatic("/script.js", SPIFFS, "/script.js");

  server.on("/api/status", HTTP_GET, []() {
    sensors.requestTemperatures();
    float temp = sensors.getTempCByIndex(0);
    DynamicJsonDocument doc(256);
    doc["temperature"] = (temp == DEVICE_DISCONNECTED_C ? -127 : temp);
    doc["relay"] = relayState;
    doc["status"] = safetyActive ? "Sécurité active" : (relayState ? "Relais ON" : "Relais OFF");
    doc["canControl"] = (!safetyActive && isActiveHour());
    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
  });

  server.on("/api/relay", HTTP_POST, []() {
    DynamicJsonDocument doc(128);
    deserializeJson(doc, server.arg("plain"));
    String cmd = doc["command"];
    if (cmd == "ON" && !safetyActive && isActiveHour()) relayState = true;
    if (cmd == "OFF") relayState = false;
    digitalWrite(RELAY_PIN, relayState ? HIGH : LOW);
    digitalWrite(LED_RELAY_PIN, relayState ? HIGH : LOW);
    mqttClient.publish(mqtt_topic_state, relayState ? "ON" : "OFF", true);
    server.send(200, "application/json", "{\"ok\":true}");
  });

  server.on("/api/config", HTTP_GET, []() {
    DynamicJsonDocument doc(256);
    doc["active_start_hour"] = config.active_start_hour;
    doc["active_end_hour"] = config.active_end_hour;
    doc["temp_max"] = config.temp_max;
    doc["temp_reset"] = config.temp_reset;
    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
  });

  server.on("/api/config", HTTP_POST, []() {
    DynamicJsonDocument doc(256);
    deserializeJson(doc, server.arg("plain"));
    config.active_start_hour = doc["active_start_hour"];
    config.active_end_hour = doc["active_end_hour"];
    config.temp_max = doc["temp_max"];
    config.temp_reset = doc["temp_reset"];
    saveConfig();
    server.send(200, "application/json", "{\"saved\":true}");
  });
}

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_ACTIVE_PIN, OUTPUT);
  pinMode(LED_RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  if(!SPIFFS.begin(true)) {
    Serial.println("❌ SPIFFS init failed");
    return;
  }
  loadConfig();
  setup_wifi();
  setupNTP();

  sensors.begin();

  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setCallback(callbackMQTT);

  setupApi();
  server.begin();
}

// ==================== LOOP ====================
void loop() {
  if (!mqttClient.connected()) reconnectMQTT();
  mqttClient.loop();
  server.handleClient();

  digitalWrite(LED_ACTIVE_PIN, isActiveHour() ? HIGH : LOW);

  if (millis() - lastMqtt > 15000) {
    sensors.requestTemperatures();
    float temp = sensors.getTempCByIndex(0);
    if (temp != DEVICE_DISCONNECTED_C) {
      mqttClient.publish(mqtt_topic_temp, String(temp,1).c_str(), true);
    }
    lastMqtt = millis();
  }

  checkSafety();

  if (!isActiveHour() && relayState) {
    relayState = false;
    digitalWrite(RELAY_PIN, LOW);
    digitalWrite(LED_RELAY_PIN, LOW);
    mqttClient.publish(mqtt_topic_state, "OFF", true);
  }
}
