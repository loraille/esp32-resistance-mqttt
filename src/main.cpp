#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <PubSubClient.h>
#include <FS.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include "config.h"
#include "esp_sleep.h"

// ==================== VERSION ====================
#define FIRMWARE_VERSION "1.3.0"

// ==================== BROCHES ====================
#define ONE_WIRE_BUS    32
#define RELAY_PIN       33
#define LED_ACTIVE_PIN  26
#define LED_RELAY_PIN   27
#define WAKE_BUTTON_PIN 12   // Bouton de réveil (GPIO12)

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
bool manualWakeMode = false;
unsigned long wakeEndTime = 0;

struct Config {
  int active_start_hour;
  int active_end_hour;
  float temp_max;
  float temp_reset;
};
Config config;

// ==================== LOGS CONSOLE ====================
#define MAX_CONSOLE_LOGS 50
String consoleLogs[MAX_CONSOLE_LOGS];
int consoleLogIndex = 0;

void addLog(String log) {
  // Ajouter au buffer circulaire
  consoleLogs[consoleLogIndex] = log;
  consoleLogIndex = (consoleLogIndex + 1) % MAX_CONSOLE_LOGS;

  // Afficher aussi sur Serial
  Serial.println(log);
}

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
  if (start < end) return (hour >= start && hour < end);
  else return (hour >= start || hour < end);
}

void setupNTP() {
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  addLog("⏳ Synchronisation NTP...");
  int attempts = 0;
  while (time(nullptr) < 100000 && attempts < 20) {
    delay(500);
    attempts++;
  }
  if (time(nullptr) >= 100000) {
    addLog("✅ NTP synchronisé");
  } else {
    addLog("⚠️ NTP timeout");
  }
}

// ==================== OTA ====================
void setupOTA() {
  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.setPassword("ballon123"); // Mot de passe pour l'OTA
  
  ArduinoOTA.onStart([]() {
    addLog("🚀 Mise à jour OTA démarrée");
    digitalWrite(LED_ACTIVE_PIN, HIGH);
  });
  
  ArduinoOTA.onEnd([]() {
    addLog("✅ Mise à jour OTA terminée");
    digitalWrite(LED_ACTIVE_PIN, LOW);
  });
  
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    char buf[50];
    sprintf(buf, "📦 Progression OTA: %u%%", (progress / (total / 100)));
    addLog(buf);
  });
  
  ArduinoOTA.onError([](ota_error_t error) {
    char buf[50];
    sprintf(buf, "❌ Erreur OTA: %u", error);
    addLog(buf);
  });
  
  ArduinoOTA.begin();
  addLog("🔄 OTA activé - Hostname: " + String(HOSTNAME));
}

// ==================== CONNEXION ====================
void disableWireless() {
  addLog("📴 Désactivation WiFi & Bluetooth...");
  if (mqttClient.connected()) {
    mqttClient.publish(mqtt_topic_status, "OFFLINE", true);
    addLog("→ MQTT: ballon/status = OFFLINE");
    mqttClient.loop();
    delay(100);
    mqttClient.disconnect();
  }
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  btStop();
  addLog("📡 WiFi & Bluetooth OFF");
}

// ==================== SÉCURITÉ ====================
void updateSafety() {
  sensors.requestTemperatures();
  float temp = sensors.getTempCByIndex(0);
  if (temp == DEVICE_DISCONNECTED_C) return;

  static float lastTemp = -999;
  if (fabs(temp - lastTemp) >= 0.5) {
    char buf[50];
    sprintf(buf, "🌡️ Température mesurée : %.1f °C", temp);
    addLog(buf);
    lastTemp = temp;
  }

  if (temp >= config.temp_max) {
    if (!safetyActive) {
      safetyActive = true;
      char buf[60];
      sprintf(buf, "🛑 Sécurité activée ! Température = %.1f °C", temp);
      addLog(buf);
      if (mqttClient.connected()) {
        mqttClient.publish(mqtt_topic_safety, "SAFETY", true);
        addLog("→ MQTT: ballon/safety = SAFETY");
      }
    }
  }
  else if (safetyActive && temp <= config.temp_reset) {
    safetyActive = false;
    char buf[60];
    sprintf(buf, "✅ Sécurité réarmée ! Température = %.1f °C", temp);
    addLog(buf);
    if (mqttClient.connected()) {
      mqttClient.publish(mqtt_topic_safety, "OFF", true);
      addLog("→ MQTT: ballon/safety = OFF");
    }
  }
}

// ==================== RELAIS ====================
void updateRelay(bool forcePublish = false) {
  bool prevState = relayState;
  bool actualState = relayState;

  // Forcer OFF si sécurité active OU hors heures actives (sauf mode réveil manuel)
  if (safetyActive || (!isActiveHour() && !manualWakeMode)) {
    actualState = false;
  }

  digitalWrite(RELAY_PIN, actualState ? HIGH : LOW);
  digitalWrite(LED_RELAY_PIN, actualState ? HIGH : LOW);

  // Publier MQTT si l'état a changé OU si forcePublish = true
  if (actualState != prevState || forcePublish) {
    String state = actualState ? "ON" : "OFF";

    if (actualState != prevState) {
      addLog("⚡ Relais " + state);
    }

    if (mqttClient.connected()) {
      mqttClient.publish(mqtt_topic_state, state.c_str(), true);
      if (forcePublish && actualState == prevState) {
        addLog("→ MQTT: ballon/relais/state = " + state + " (confirmation)");
      } else {
        addLog("→ MQTT: ballon/relais/state = " + state);
      }
    }
  }

  // Mettre à jour relayState avec l'état réel
  relayState = actualState;
}

// ==================== WIFI & MQTT ====================
void setup_wifi() {
  addLog("📡 Connexion WiFi...");
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
    addLog("✅ WiFi connecté !");
    addLog("IP : " + WiFi.localIP().toString());
  } else {
    addLog("❌ Impossible de se connecter au WiFi !");
  }
}

void callbackMQTT(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  msg.trim();

  addLog("← MQTT: " + String(topic) + " = " + msg);

  if (String(topic) == mqtt_topic_command) {
    // Vérifier si la commande peut être exécutée
    bool canControl = !safetyActive && (isActiveHour() || manualWakeMode);

    if (msg == "ON") {
      if (canControl) {
        relayState = true;
      } else {
        addLog("⚠️ Commande ON rejetée (hors horaires ou sécurité active)");
        relayState = false; // Forcer à OFF
      }
    } else if (msg == "OFF") {
      relayState = false;
    }

    // TOUJOURS publier l'état après une commande MQTT (même si pas de changement)
    updateRelay(true); // forcePublish = true
  }
}

void reconnectMQTT() {
  if (!mqttClient.connected()) {
    addLog("🔌 Tentative connexion MQTT...");
    if (mqttClient.connect(HOSTNAME, MQTT_USER, MQTT_PASSWORD)) {
      mqttClient.subscribe(mqtt_topic_command);
      mqttClient.publish(mqtt_topic_status, "ONLINE", true);
      addLog("→ MQTT: ballon/status = ONLINE");

      String safetyMsg = safetyActive ? "SAFETY" : "OFF";
      mqttClient.publish(mqtt_topic_safety, safetyMsg.c_str(), true);
      addLog("→ MQTT: ballon/safety = " + safetyMsg);

      updateRelay();
      addLog("✅ MQTT connecté");
    } else {
      char buf[50];
      sprintf(buf, "❌ MQTT échec, code=%d", mqttClient.state());
      addLog(buf);
    }
  }
}

// ==================== API JSON ====================
void setupApi() {
  // Fichiers statiques
  server.serveStatic("/", SPIFFS, "/index.html");
  server.serveStatic("/config.html", SPIFFS, "/config.html");
  server.serveStatic("/style.css", SPIFFS, "/style.css");
  server.serveStatic("/script.js", SPIFFS, "/script.js");

  // GET config
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

  // POST config
  server.on("/api/config", HTTP_POST, []() {
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, server.arg("plain"));
    if(error){
      Serial.print("❌ JSON Error: "); Serial.println(error.c_str());
      server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
      return;
    }

    config.active_start_hour = doc["active_start_hour"];
    config.active_end_hour   = doc["active_end_hour"];
    config.temp_max          = doc["temp_max"];
    config.temp_reset        = doc["temp_reset"];
    saveConfig();

    Serial.println("💾 Config sauvegardée, reboot dans 2 secondes...");
    server.send(200, "application/json", "{\"saved\":true,\"reboot\":true}");

    delay(2000);
    ESP.restart();
  });

  // GET status
  server.on("/api/status", HTTP_GET, []() {
    sensors.requestTemperatures();
    float temp = sensors.getTempCByIndex(0);
    StaticJsonDocument<512> doc;
    doc["temperature"] = (temp == DEVICE_DISCONNECTED_C ? -127 : temp);
    doc["relay"] = relayState;
    doc["status"] = safetyActive ? "Sécurité active" : (relayState ? "Relais ON" : "Relais OFF");
    doc["canControl"] = (!safetyActive && (isActiveHour() || manualWakeMode));
    doc["version"] = FIRMWARE_VERSION;
    doc["manualMode"] = manualWakeMode;
    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
  });

  // GET logs console
  server.on("/api/logs", HTTP_GET, []() {
    StaticJsonDocument<4096> doc;
    JsonArray logs = doc.createNestedArray("logs");

    // Récupérer les logs dans l'ordre chronologique
    for (int i = 0; i < MAX_CONSOLE_LOGS; i++) {
      int idx = (consoleLogIndex + i) % MAX_CONSOLE_LOGS;
      if (consoleLogs[idx].length() > 0) {
        logs.add(consoleLogs[idx]);
      }
    }

    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
  });

  // POST relay
  server.on("/api/relay", HTTP_POST, []() {
    StaticJsonDocument<128> doc;
    DeserializationError error = deserializeJson(doc, server.arg("plain"));
    if(error){
      addLog("❌ JSON Error relay: " + String(error.c_str()));
      server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
      return;
    }

    String cmd = doc["command"];
    bool canControl = !safetyActive && (isActiveHour() || manualWakeMode);

    if(cmd == "ON") {
      if (canControl) {
        relayState = true;
        addLog("🌐 Commande WEB: ON acceptée");
      } else {
        addLog("⚠️ Commande WEB ON rejetée (hors horaires ou sécurité active)");
        relayState = false;
      }
    } else if(cmd == "OFF") {
      relayState = false;
      addLog("🌐 Commande WEB: OFF");
    }

    // Toujours publier l'état MQTT après commande web
    updateRelay(true);
    server.send(200, "application/json", "{\"ok\":true}");
  });

  // Route non trouvée
  server.onNotFound([]() {
    Serial.printf("⚠️ Requête inconnue : %s\n", server.uri().c_str());
    server.send(404, "text/plain", "Not found");
  });

  server.begin();
  Serial.println("🌐 Serveur Web démarré");
}


// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  delay(500);
  addLog("\n========== DÉMARRAGE ==========");

  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_ACTIVE_PIN, OUTPUT);
  pinMode(LED_RELAY_PIN, OUTPUT);
  pinMode(WAKE_BUTTON_PIN, INPUT_PULLUP);

  digitalWrite(RELAY_PIN, LOW);
  digitalWrite(LED_ACTIVE_PIN, LOW);
  digitalWrite(LED_RELAY_PIN, LOW);

  if (!SPIFFS.begin(true)) {
    addLog("❌ SPIFFS init failed");
    return;
  }

  loadConfig();

  // Vérifier la cause du réveil
  esp_sleep_wakeup_cause_t wakeupReason = esp_sleep_get_wakeup_cause();
  bool wakeupButton = (wakeupReason == ESP_SLEEP_WAKEUP_EXT0);

  if (wakeupButton) {
    addLog("🔘 Réveil manuel par le bouton !");
    manualWakeMode = true;
    wakeEndTime = millis() + 5UL * 60UL * 1000UL; // 5 minutes
  } else {
    addLog("⏰ Réveil programmé ou premier démarrage");
  }

  setup_wifi();
  setupNTP();
  setupOTA();
  sensors.begin();
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setCallback(callbackMQTT);
  setupApi();

  // Connexion MQTT initiale
  reconnectMQTT();

  updateSafety();
  updateRelay();

  // Si hors heures actives ET pas de réveil manuel -> deep sleep
  if (!isActiveHour() && !manualWakeMode) {
    addLog("🌙 Hors heures actives -> deep sleep immédiat");

    time_t now = time(nullptr);
    struct tm *ptm = localtime(&now);
    int hour = ptm->tm_hour;
    int minutes = ptm->tm_min;
    int seconds = ptm->tm_sec;
    int start = config.active_start_hour;
    int end = config.active_end_hour;

    int nextWakeHour = start;
    if (hour >= end) nextWakeHour = start + 24;

    int hoursToWake = nextWakeHour - hour;
    int secondsToWake = hoursToWake * 3600 - (minutes * 60 + seconds);
    if (secondsToWake < 60) secondsToWake = 60;

    char buf[80];
    sprintf(buf, "⏱️ Réveil auto dans %d secondes (%.1f heures)", secondsToWake, secondsToWake / 3600.0);
    addLog(buf);

    disableWireless();
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_12, 0);
    esp_sleep_enable_timer_wakeup((uint64_t)secondsToWake * 1000000ULL);
    esp_deep_sleep_start();
  }

  // Activer le wake-up par bouton pour le prochain deep sleep
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_12, 0);

  addLog("========== SETUP TERMINÉ ==========");
}

// ==================== LOOP ====================
void loop() {
  ArduinoOTA.handle();
  mqttClient.loop();
  server.handleClient();

  // ===== MODE RÉVEIL MANUEL =====
  if (manualWakeMode) {
    // LED verte clignotante
    static unsigned long lastLedToggle = 0;
    if (millis() - lastLedToggle >= 500) {
      digitalWrite(LED_ACTIVE_PIN, !digitalRead(LED_ACTIVE_PIN));
      lastLedToggle = millis();
    }

    // Reconnexion MQTT si nécessaire
    if (!mqttClient.connected()) {
      reconnectMQTT();
    }

    // Envoi MQTT toutes les 15 secondes
    if (millis() - lastMqtt >= 15000) {
      sensors.requestTemperatures();
      float temp = sensors.getTempCByIndex(0);
      if (temp != DEVICE_DISCONNECTED_C && mqttClient.connected()) {
        mqttClient.publish(mqtt_topic_temp, String(temp, 1).c_str(), true);
        addLog("→ MQTT: ballon/temperature = " + String(temp, 1));
      }
      updateSafety();
      updateRelay();
      lastMqtt = millis();
    }

    // Vérifier fin du mode manuel (5 minutes)
    if (millis() >= wakeEndTime) {
      addLog("⏱️ Fin du mode réveil manuel (5 min écoulées)");
      manualWakeMode = false;
      disableWireless();
      esp_sleep_enable_ext0_wakeup(GPIO_NUM_12, 0);
      esp_deep_sleep_start();
    }

    return;
  }

  // ===== MODE NORMAL (heures actives) =====
  digitalWrite(LED_ACTIVE_PIN, HIGH);

  if (!mqttClient.connected()) {
    reconnectMQTT();
  }

  // Envoi MQTT toutes les 15 secondes
  if (millis() - lastMqtt >= 15000) {
    sensors.requestTemperatures();
    float temp = sensors.getTempCByIndex(0);
    if (temp != DEVICE_DISCONNECTED_C && mqttClient.connected()) {
      mqttClient.publish(mqtt_topic_temp, String(temp, 1).c_str(), true);
      addLog("→ MQTT: ballon/temperature = " + String(temp, 1));
    }
    lastMqtt = millis();
  }

  updateSafety();
  updateRelay();

  // Vérifier si on doit passer en deep sleep
  if (!isActiveHour()) {
    addLog("🌙 Hors heures actives -> passage en deep sleep");
    disableWireless();

    time_t now = time(nullptr);
    struct tm *ptm = localtime(&now);
    int hour = ptm->tm_hour;
    int minutes = ptm->tm_min;
    int seconds = ptm->tm_sec;
    int start = config.active_start_hour;
    int end = config.active_end_hour;

    int nextWakeHour = start;
    if (hour >= end) nextWakeHour = start + 24;

    int hoursToWake = nextWakeHour - hour;
    int secondsToWake = hoursToWake * 3600 - (minutes * 60 + seconds);
    if (secondsToWake < 60) secondsToWake = 60;

    char buf[80];
    sprintf(buf, "⏱️ Réveil auto dans %d secondes (%.1f heures)", secondsToWake, secondsToWake / 3600.0);
    addLog(buf);

    esp_sleep_enable_ext0_wakeup(GPIO_NUM_12, 0);
    esp_sleep_enable_timer_wakeup((uint64_t)secondsToWake * 1000000ULL);
    esp_deep_sleep_start();
  }
}
