#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <PubSubClient.h>
#include <config.h>  // ⚠️ WIFI_SSID, WIFI_PASSWORD, MQTT_SERVER, MQTT_PORT, MQTT_USER, MQTT_PASSWORD

// ==================== BROCHES ====================
#define ONE_WIRE_BUS    4
#define RELAY_PIN       26
#define LED_ACTIVE_PIN  25
#define LED_RELAY_PIN   27

// ==================== PARAMÈTRES ====================
const int ACTIVE_START_HOUR = 9;   // Début plage horaire
const int ACTIVE_END_HOUR   = 23;  // Fin plage horaire
const float TEMP_MAX = 70.0;       // Coupure à 70°C
const float TEMP_RESET = 65.0;     // Réarmement sous 65°C

// ==================== OBJETS ====================
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

WiFiClient espClient;
PubSubClient mqttClient(espClient);
WebServer server(80);

// ==================== TOPICS MQTT ====================
const char* mqtt_topic_temp    = "ballon/temperature";
const char* mqtt_topic_state   = "ballon/relais/state";
const char* mqtt_topic_command = "ballon/relais/command";
const char* mqtt_topic_status  = "ballon/status";

// ==================== VARIABLES ====================
unsigned long lastMqtt = 0;
bool relayState = false;
bool safetyActive = false;

// ==================== NTP ====================
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 3600;      // +1h
const int daylightOffset_sec = 3600;  // heure d'été

// ==================== UTILS ====================
bool isActiveHour() {
  time_t now = time(nullptr);
  struct tm *ptm = localtime(&now);
  if (!ptm) return true; // si pas d'heure, on considère actif
  int hour = ptm->tm_hour;
  if (ACTIVE_START_HOUR < ACTIVE_END_HOUR) {
    return (hour >= ACTIVE_START_HOUR && hour < ACTIVE_END_HOUR);
  } else {
    return (hour >= ACTIVE_START_HOUR || hour < ACTIVE_END_HOUR);
  }
}

String getTimeStr() {
  time_t now = time(nullptr);
  struct tm *ptm = localtime(&now);
  if (!ptm) return "--:--";
  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", ptm->tm_hour, ptm->tm_min);
  return String(buf);
}

// ==================== HTML ====================
String buildHtmlPage(float temperature) {
  String html = R"=====(<!DOCTYPE html>
<html lang="fr">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Ballon</title>
<style>
body { font-family: 'Segoe UI', sans-serif; text-align: center; background: #f4f6f9; margin: 0; padding: 20px; }
.card { max-width: 400px; margin: auto; background: white; border-radius: 12px; box-shadow: 0 4px 15px rgba(0,0,0,0.1); padding: 30px; }
h1 { color: #1a73e8; margin-bottom: 20px; }
.temp { font-size: 3em; font-weight: bold; color: #e74c3c; margin: 15px 0; }
.status { font-size: 1.3em; margin: 20px 0; }
.dot { display: inline-block; width: 12px; height: 12px; border-radius: 50%; margin-right: 8px; }
.dot-green { background: #27ae60; }
.dot-red { background: #e74c3c; }
.btn { display: inline-block; margin: 10px; padding: 12px 25px; border-radius: 8px; text-decoration: none; color: white; }
.on { background: #27ae60; }
.off { background: #e74c3c; }
.safety { color: #e74c3c; font-weight: bold; }
footer { margin-top: 20px; font-size: 0.9em; color: #7f8c8d; }
</style>
<meta http-equiv="refresh" content="5">
</head>
<body>
<div class="card">
<h1>🌡️ Ballon</h1>
<div class="temp">)=====";

  if (temperature == DEVICE_DISCONNECTED_C) {
    html += "<span style='color:red'>Capteur non détecté ❌</span>";
  } else {
    html += String(temperature, 1) + " °C";
  }

  html += "</div><div class='status'>";

  if (safetyActive) html += "<span class='dot dot-red'></span> 🚨 Sécurité >70°C !";
  else if (!isActiveHour()) html += "<span class='dot dot-red'></span> ⏰ Hors plage horaire";
  else if (relayState) html += "<span class='dot dot-red'></span> Résistance ACTIVÉE";
  else html += "<span class='dot dot-green'></span> Résistance OFF";

  html += "</div>";

  if (!safetyActive && isActiveHour()) {
    if (relayState) html += "<a href='/off' class='btn off'>❌ Éteindre</a>";
    else html += "<a href='/on' class='btn on'>🔥 Allumer</a>";
  } else html += "<p class='safety'>Commande bloquée</p>";

  html += "<p>Heure actuelle : " + getTimeStr() + "</p>";
  html += "<footer>Plage horaire : " + String(ACTIVE_START_HOUR) + "h → " + String(ACTIVE_END_HOUR) + "h<br>MAJ auto toutes les 5s</footer></div></body></html>";
  return html;
}

// ==================== HANDLERS WEB ====================
void handleRoot() {
  sensors.requestTemperatures();
  float temp = sensors.getTempCByIndex(0);
  server.send(200, "text/html", buildHtmlPage(temp));
}

void handleOn() {
  if (!safetyActive && isActiveHour()) {
    relayState = true;
    digitalWrite(RELAY_PIN, HIGH);
    digitalWrite(LED_RELAY_PIN, HIGH);
    mqttClient.publish(mqtt_topic_state, "ON", true);
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleOff() {
  relayState = false;
  digitalWrite(RELAY_PIN, LOW);
  digitalWrite(LED_RELAY_PIN, LOW);
  mqttClient.publish(mqtt_topic_state, "OFF", true);
  server.sendHeader("Location", "/");
  server.send(303);
}

// ==================== SÉCURITÉ ====================
void checkSafety() {
  sensors.requestTemperatures();
  float temp = sensors.getTempCByIndex(0);
  if (temp != DEVICE_DISCONNECTED_C) {
    if (temp >= TEMP_MAX && relayState && !safetyActive) {
      relayState = false;
      digitalWrite(RELAY_PIN, LOW);
      digitalWrite(LED_RELAY_PIN, LOW);
      safetyActive = true;
      mqttClient.publish(mqtt_topic_state, "OFF", true);
      mqttClient.publish(mqtt_topic_status, "SAFETY", true);
      Serial.println("🛑 SÉCURITÉ : >70°C ! Coupure relais");
    } else if (temp <= TEMP_RESET && safetyActive) {
      safetyActive = false;
      mqttClient.publish(mqtt_topic_status, "RESET", true);
      Serial.println("✅ SÉCURITÉ : <65°C. Réarmement possible");
    }
  }
}

// ==================== WIFI & MQTT ====================
void setup_wifi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connexion Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n✅ Wi-Fi connecté !");
  Serial.print("📍 IP : http://");
  Serial.println(WiFi.localIP());
}

void setupNTP() {
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  Serial.print("⏱️ Synchronisation NTP");
  time_t now = time(nullptr);
  while (now < 8 * 3600 * 2) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println("\n✅ Heure NTP synchronisée !");
}

void callbackMQTT(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  msg.trim();

  if (String(topic) == mqtt_topic_command) {
    if (msg == "ON") handleOn();
    else if (msg == "OFF") handleOff();
  }
}

void reconnectMQTT() {
  while (!mqttClient.connected()) {
    Serial.print("Connexion MQTT...");
    if (mqttClient.connect("ESP32Ballon", MQTT_USER, MQTT_PASSWORD)) {
      Serial.println("✅ connecté !");
      mqttClient.subscribe(mqtt_topic_command);
      mqttClient.publish(mqtt_topic_status, "ONLINE", true);
      mqttClient.publish(mqtt_topic_state, relayState ? "ON" : "OFF", true);
    } else {
      Serial.print("❌ échec, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" → nouvel essai dans 5s");
      delay(5000);
    }
  }
}

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("\n🚀 ESP32 - Ballon Température + Relais + Sécurité");

  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_ACTIVE_PIN, OUTPUT);
  pinMode(LED_RELAY_PIN, OUTPUT);

  digitalWrite(RELAY_PIN, LOW);
  digitalWrite(LED_ACTIVE_PIN, LOW);  // LED verte dynamique
  digitalWrite(LED_RELAY_PIN, LOW);

  sensors.begin();
  setup_wifi();
  setupNTP();

  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setCallback(callbackMQTT);

  server.on("/", handleRoot);
  server.on("/on", handleOn);
  server.on("/off", handleOff);
  server.begin();
  Serial.println("🌍 Serveur web démarré");
}

// ==================== LOOP ====================
void loop() {
  if (!mqttClient.connected()) reconnectMQTT();
  mqttClient.loop();
  server.handleClient();

  // LED verte selon plage horaire
  digitalWrite(LED_ACTIVE_PIN, isActiveHour() ? HIGH : LOW);

  // Publier température toutes les 15s
  if (millis() - lastMqtt > 15000) {
    sensors.requestTemperatures();
    float temp = sensors.getTempCByIndex(0);
    if (temp != DEVICE_DISCONNECTED_C) {
      mqttClient.publish(mqtt_topic_temp, String(temp, 1).c_str(), true);
      Serial.printf("📡 Température publiée : %.1f °C\n", temp);
    }
    lastMqtt = millis();
  }

  // Vérification sécurité
  checkSafety();

  // Forcer relais OFF hors plage horaire
  if (!isActiveHour() && relayState) {
    handleOff();
    Serial.println("⏰ Hors plage horaire → Relais coupé");
  }
}
