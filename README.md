# ESP32 Ballon MQTT

Contrôle intelligent d'un ballon d'eau chaude via MQTT et interface web. Lecture de température (DS18B20), système de sécurité automatique, fenêtre d'activité configurable et interface web pour le contrôle et la configuration.

## Fonctionnalités

- **Interface Web** : Contrôle et monitoring via navigateur
- **API REST** : Endpoints pour contrôle et configuration
- **Système de sécurité** : Coupure automatique si température > seuil max
- **Configuration web** : Heures actives, températures de sécurité configurables
- **MQTT** : Publication température et réception commandes
- **Fenêtre d'activité** : Contrôle possible uniquement dans la plage horaire configurée
- **Indicateurs LED** : verte (ESP32 actif), rouge (relais activé)

## Matériel

- ESP32 (DevKit ou équivalent)
- Sonde de température DS18B20 (1‑Wire)
- Résistance de pull‑up 4.7 kΩ entre `DATA` (DS18B20) et `3V3`
- Module relais (logique adaptée 3.3 V ou transistor si nécessaire)
- 2 LEDs (verte, rouge) + résistances série

## Câblage (GPIO par défaut)

- `ONE_WIRE_BUS` → `GPIO 4` (DATA DS18B20, + pull‑up 4.7 kΩ → 3V3)
- `RELAY_PIN` → `GPIO 26` (commande relais)
- `LED_ACTIVE_PIN` → `GPIO 25` (LED verte)
- `LED_RELAY_PIN` → `GPIO 27` (LED rouge)

Relais NC : au repos (LOW), le circuit est fermé. Quand activé (HIGH), le circuit s’ouvre.

## Interface Web

L'ESP32 héberge une interface web accessible via son adresse IP :

- **Page principale** (`/`) : Monitoring température, statut relais, contrôle
- **Configuration** (`/config.html`) : Paramétrage heures actives et températures de sécurité
- **API REST** :
  - `GET /api/status` : État température, relais, statut sécurité
  - `POST /api/relay` : Commande relais (`{"command": "ON"}` ou `"OFF"`)
  - `GET /api/config` : Configuration actuelle
  - `POST /api/config` : Sauvegarde nouvelle configuration

## MQTT

- **Broker** : Configuré dans `include/config.h`
- **Topics** :
  - `ballon/temperature` : Publication température (toutes les 15s)
  - `ballon/relais/state` : État du relais (`ON`/`OFF`)
  - `ballon/relais/command` : Commandes relais (`ON`/`OFF`)
  - `ballon/status` : Statut système (`ONLINE`/`SAFETY`/`RESET`)

Exemples :

```text
Souscrire : ballon/relais/command   (payload: ON | OFF)
Publier   : ballon/temperature      (ex: 22.3)
Publier   : ballon/status           (ONLINE | SAFETY | RESET)
```

## Configuration

### Configuration initiale

1. **Wi-Fi et MQTT** : Modifier `include/config.h` avec vos paramètres
2. **Interface web** : Accéder à l'IP de l'ESP32 pour configurer :
   - **Heures actives** : Plage horaire où le contrôle est autorisé (défaut: 9h-23h)
   - **Température max** : Seuil de sécurité (défaut: 70°C)
   - **Température reset** : Seuil de réarmement (défaut: 65°C)

### Système de sécurité

- **Coupure automatique** : Si température ≥ seuil max, le relais se coupe automatiquement
- **Réarmement** : Quand température ≤ seuil reset, le système se réarme
- **Blocage commandes** : Pendant la sécurité active, les commandes sont bloquées

### Comportement horaire

- **Synchronisation NTP** : Heure automatique via `pool.ntp.org`
- **Fenêtre active** : Contrôle possible uniquement dans la plage configurée
- **Hors plage** : Le relais se coupe automatiquement si allumé

## Prérequis

- **PlatformIO** (VS Code ou CLI)
- **Carte** : ESP32 (DevKit ou équivalent)
- **Bibliothèques** (automatiquement installées via `platformio.ini`) :
  - `DallasTemperature` (capteur DS18B20)
  - `PubSubClient` (MQTT)
  - `OneWire` (protocole 1-Wire)
  - `ArduinoJson` (API REST)

## Installation & Build

1. **Cloner le projet** dans PlatformIO
2. **Configuration** : Modifier `include/config.h` :
   ```cpp
   #define WIFI_SSID "votre_wifi"
   #define WIFI_PASSWORD "votre_mot_de_passe"
   #define MQTT_SERVER "192.168.1.xxx"
   #define MQTT_USER "votre_user"
   #define MQTT_PASSWORD "votre_password"
   ```
3. **Upload** : Compiler et téléverser via PlatformIO
4. **Upload SPIFFS** : Téléverser les fichiers web (`data/` → SPIFFS)
5. **Test** : Accéder à l'IP de l'ESP32 dans un navigateur

## Indicateurs LED

- LED verte (`GPIO 25`) : ON quand l’ESP32 est en phase active
- LED rouge (`GPIO 27`) : ON quand le relais est activé (circuit ouvert)

## Dépannage

### Problèmes courants

- **Interface web inaccessible** : Vérifier l'IP de l'ESP32 dans le moniteur série
- **Température -127°C** : Vérifier le DS18B20, résistance 4.7kΩ et GPIO 4
- **Wi-Fi non connecté** : Vérifier `WIFI_SSID`/`WIFI_PASSWORD` dans `config.h`
- **MQTT déconnecté** : Vérifier `MQTT_SERVER`, port et identifiants
- **Relais ne répond pas** : Vérifier GPIO 26 et alimentation du module relais
- **Configuration non sauvegardée** : Vérifier que SPIFFS est initialisé

### Logs utiles

- Moniteur série à 115200 bauds
- Messages de statut MQTT sur `ballon/status`
- LED verte : ESP32 actif, LED rouge : relais activé

## Personnalisation

### Broches GPIO

Modifier dans `src/main.cpp` :

```cpp
#define ONE_WIRE_BUS    4    // DS18B20
#define RELAY_PIN       26   // Commande relais
#define LED_ACTIVE_PIN  25   // LED verte
#define LED_RELAY_PIN   27   // LED rouge
```

### Topics MQTT

Modifier dans `src/main.cpp` :

```cpp
const char* mqtt_topic_temp    = "ballon/temperature";
const char* mqtt_topic_state   = "ballon/relais/state";
const char* mqtt_topic_command = "ballon/relais/command";
const char* mqtt_topic_status  = "ballon/status";
```

### Interface web

- Modifier les fichiers dans `data/` (HTML, CSS, JS)
- Re-uploader SPIFFS après modification

## Structure du projet

```
esp32-resistance-mqtt/
├── src/
│   └── main.cpp              # Code principal ESP32
├── include/
│   └── config.h              # Configuration Wi-Fi/MQTT
├── data/                     # Interface web (SPIFFS)
│   ├── index.html            # Page principale
│   ├── config.html           # Page configuration
│   ├── style.css             # Styles CSS
│   └── script.js             # JavaScript
├── platformio.ini            # Configuration PlatformIO
└── README.md                 # Documentation
```

## Licence

Ce projet est fourni tel quel, sans garantie. Adaptez la licence selon vos besoins.
