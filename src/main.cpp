#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <PubSubClient.h>
#include <FS.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include "config.h"
#include "esp_sleep.h"

// ==================== BROCHES ====================
#define ONE_WIRE_BUS    32
#define RELAY_PIN       33
#define LED_ACTIVE_PIN  26
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
const char* mqtt_topic_safety  = "ballon/safety";
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
  StaticJsonDocument<256> doc;
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
  StaticJsonDocument<256> doc;
  doc["active_start_hour"] = config.active_start_hour;
  doc["active_end_hour"]   = config.active_end_hour;
  doc["temp_max"]          = config.temp_max;
  doc["temp_reset"]        = config.temp_reset;
  serializeJsonPretty(doc, file);
  file.close();
}

// ==================== UTIL ====================
bool isActiveHour() {
  time_t now = time(nullptr);
  struct tm *ptm = localtime(&now);
  if (!ptm) return true;
  int hour = ptm->tm_hour;

  int start = config.active_start_hour;
  int end   = config.active_end_hour;

  if (end == 24) end = 0;

  if (start < end) {
    return (hour >= start && hour < end);
  } else {
    return (hour >= start || hour < end);
  }
}

// ==================== 🔌 FONCTIONS AJOUTÉES ====================

// 🆕 Désactive totalement WiFi et Bluetooth
void disableWireless() {
  Serial.println("📴 Désactivation WiFi & Bluetooth...");
  mqttClient.disconnect();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  btStop();
  Serial.println("📡 WiFi & Bluetooth OFF");
}

// 🆕 Vérifie les connexions réseau/MQTT et publie ONLINE/OFFLINE
void checkConnections() {
  static bool wasOnline = true;
  bool wifiOK = (WiFi.status() == WL_CONNECTED);
  bool mqttOK = mqttClient.connected();

  if (!wifiOK || !mqttOK) {
    if (wasOnline) {
      Serial.println("🔴 Réseau ou MQTT déconnecté -> OFFLINE");
      if (mqttClient.connected())
        mqttClient.publish(mqtt_topic_status, "OFFLINE", true);
      wasOnline = false;
      disableWireless();
    }
  } else {
    if (!wasOnline) {
      Serial.println("🟢 Connexion rétablie -> ONLINE");
      mqttClient.publish(mqtt_topic_status, "ONLINE", true);
      wasOnline = true;
    }
  }
}

// ==================== SÉCURITÉ ====================
void updateSafety() {
  sensors.requestTemperatures();
  float temp = sensors.getTempCByIndex(0);
  if (temp == DEVICE_DISCONNECTED_C) return;

  static float lastTemp = -999;
  if (fabs(temp - lastTemp) >= 0.5) {
    Serial.printf("🌡️ Température mesurée : %.1f °C\n", temp);
    lastTemp = temp;
  }

  if (temp >= config.temp_max) {
    if (!safetyActive) {
      safetyActive = true;
      Serial.printf("🛑 Sécurité activée ! Température = %.1f °C (>= %.1f °C max)\n", temp, config.temp_max);
      mqttClient.publish(mqtt_topic_safety, "SAFETY", true);
    }
  }
  else if (safetyActive && temp <= config.temp_reset) {
    safetyActive = false;
    Serial.printf("✅ Sécurité réarmée ! Température = %.1f °C (<= %.1f °C reset)\n", temp, config.temp_reset);
    mqttClient.publish(mqtt_topic_safety, "OFF", true);
  }
}

// ==================== RELAIS ====================
void updateRelay() {
  bool prevState = relayState;

  if (safetyActive || !isActiveHour()) relayState = false;

  digitalWrite(RELAY_PIN, relayState ? HIGH : LOW);
  digitalWrite(LED_RELAY_PIN, relayState ? HIGH : LOW);

  mqttClient.publish(mqtt_topic_state, relayState ? "ON" : "OFF", true);

  if (relayState != prevState) {
    Serial.printf("⚡ Relais %s\n", relayState ? "ON" : "OFF");
  }
}

// ==================== WIFI & MQTT ====================
void setup_wifi() {
  Serial.println("📡 Connexion WiFi...");

  WiFi.disconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 20000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ WiFi connecté !");
    Serial.print("   • IP locale : ");
    Serial.println(WiFi.localIP());
    Serial.print("   • Hostname  : ");
    Serial.println(WiFi.getHostname());
    Serial.print("   • RSSI      : ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
  } else {
    Serial.println("\n❌ Impossible de se connecter au WiFi !");
  }
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
    if (msg == "ON") relayState = true;
    else if (msg == "OFF") relayState = false;
    updateRelay();
  }
}

void reconnectMQTT() {
  while (!mqttClient.connected()) {
    if (mqttClient.connect(HOSTNAME, MQTT_USER, MQTT_PASSWORD)) {
      mqttClient.subscribe(mqtt_topic_command);
      mqttClient.publish(mqtt_topic_status, "ONLINE", true);
      updateRelay();
      mqttClient.publish(mqtt_topic_safety, safetyActive ? "SAFETY" : "OFF", true);
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
    StaticJsonDocument<256> doc;
    doc["temperature"] = (temp == DEVICE_DISCONNECTED_C ? -127 : temp);
    doc["relay"] = relayState;
    doc["status"] = safetyActive ? "Sécurité active" : (relayState ? "Relais ON" : "Relais OFF");
    doc["canControl"] = (!safetyActive && isActiveHour());
    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
  });

  server.on("/api/relay", HTTP_POST, []() {
    StaticJsonDocument<128> doc;
    deserializeJson(doc, server.arg("plain"));
    String cmd = doc["command"];
    if (cmd == "ON") relayState = true;
    if (cmd == "OFF") relayState = false;
    updateRelay();
    server.send(200, "application/json", "{\"ok\":true}");
  });

  server.on("/api/config", HTTP_GET, []() {
    StaticJsonDocument<256> doc;
    doc["active_start_hour"] = config.active_start_hour;
    doc["active_end_hour"] = config.active_end_hour;
    doc["temp_max"] = config.temp_max;
    doc["temp_reset"] = config.temp_reset;
    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
  });

  server.on("/api/config", HTTP_POST, []() {
    StaticJsonDocument<256> doc;
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

  updateSafety();
  updateRelay();

  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setCallback(callbackMQTT);

  setupApi();
  server.begin();

  if (!isActiveHour()) {
    Serial.println("🌙 Pas dans les heures actives -> deep-sleep immédiat");

    time_t now = time(nullptr);
    struct tm *ptm = localtime(&now);
    int hour = ptm->tm_hour;
    int minutes = ptm->tm_min;
    int seconds = ptm->tm_sec;

    int start = config.active_start_hour;
    int end   = config.active_end_hour;

    int nextWakeHour = start;
    if (hour >= end) nextWakeHour = start + 24;

    int hoursToWake = nextWakeHour - hour;
    int secondsToWake = hoursToWake * 3600 - (minutes * 60 + seconds);
    if (secondsToWake < 60) secondsToWake = 60;

    Serial.printf("⏱️ Réveil dans %d secondes\n", secondsToWake);
    disableWireless();   // 🆕
    esp_sleep_enable_timer_wakeup((uint64_t)secondsToWake * 1000000ULL);
    esp_deep_sleep_start();
  }
}

// ==================== LOOP ====================
void loop() {
  if (!mqttClient.connected()) reconnectMQTT();
  mqttClient.loop();
  server.handleClient();

  digitalWrite(LED_ACTIVE_PIN, isActiveHour() ? HIGH : LOW);

  checkConnections(); // 🆕

  if (millis() - lastMqtt > 15000) {
    sensors.requestTemperatures();
    float temp = sensors.getTempCByIndex(0);
    if (temp != DEVICE_DISCONNECTED_C) {
      Serial.printf("🌡️ Envoi MQTT : %.1f °C\n", temp);
      mqttClient.publish(mqtt_topic_temp, String(temp, 1).c_str(), true);
    } else {
      Serial.println("⚠️ Capteur de température non détecté !");
    }
    lastMqtt = millis();
  }

  updateSafety();
  updateRelay();
}
