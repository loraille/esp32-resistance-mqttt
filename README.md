# 🌡️ ESP32 Ballon MQTT - Contrôle Intelligent avec Deep Sleep

> **Version 1.2.0**

Contrôle intelligent d'un ballon d'eau chaude avec gestion d'énergie optimisée. Lecture de température (DS18B20), système de sécurité automatique, fenêtre d'activité configurable, **deep sleep** pour économie d'énergie, et interface web moderne responsive avec console MQTT en temps réel.

## ✨ Fonctionnalités

### 🔋 Gestion d'Énergie
- **Deep Sleep Automatique** : L'ESP32 entre en veille profonde hors des heures actives
- **Réveil par Bouton** : GPIO12 permet un réveil manuel pour 5 minutes d'activité
- **Réveil Programmé** : Réveil automatique à l'heure de début configurée
- **Désactivation WiFi/Bluetooth** : En deep sleep, aucune consommation réseau

### 🌐 Interface Web Moderne
- **Design Responsive** : Optimisé mobile et desktop
- **Console MQTT en temps réel** : Visualisation des messages entrants/sortants
- **Affichage de version** : Version firmware visible
- **Contrôle et monitoring** : Température, relais, statut en temps réel
- **Configuration intuitive** : Modification des horaires et seuils de sécurité

### 🔐 Sécurité & Automatisation
- **Sécurité thermique** : Coupure automatique si température > seuil max
- **Fenêtre d'activité** : Contrôle possible uniquement dans la plage horaire configurée
- **Mode réveil manuel** : 5 minutes d'activité forcée même hors horaires
- **Reboot automatique** : Après modification de configuration

### 📡 Intégration MQTT
- **Publication température** : Envoi toutes les 15 secondes
- **Commandes Home Assistant** : Contrôle du relais à distance
- **Logs en temps réel** : Historique des 20 derniers messages MQTT
- **Statut en ligne** : Publication ONLINE/OFFLINE automatique

## 🔧 Matériel Requis

| Composant | Spécification | Quantité |
|-----------|---------------|----------|
| ESP32 | DevKit ou équivalent | 1 |
| DS18B20 | Sonde température 1-Wire | 1 |
| Résistance | 4.7 kΩ (pull-up) | 1 |
| Module relais | Compatible 3.3V ou avec transistor | 1 |
| LED verte | Indicateur activité | 1 |
| LED rouge | Indicateur relais | 1 |
| Résistances | 220Ω-330Ω (pour LEDs) | 2 |
| Bouton poussoir | Réveil manuel | 1 |

## 📟 Branchements (GPIO)

```
DS18B20 (Température)
├─ VCC   → 3V3
├─ GND   → GND
└─ DATA  → GPIO 32 + Pull-up 4.7kΩ vers 3V3

Relais
└─ IN    → GPIO 33

LEDs
├─ LED verte (activité)  → GPIO 26 + résistance 220Ω → GND
└─ LED rouge (relais)    → GPIO 27 + résistance 220Ω → GND

Bouton Réveil
└─ GPIO 12 → Bouton → GND (avec INPUT_PULLUP interne)
```

**Note** : Relais NC (Normalement Fermé) → au repos (LOW), circuit fermé. Activé (HIGH), circuit ouvert.

## 🌐 Interface Web

L'ESP32 héberge une interface web responsive accessible via son adresse IP :

### Pages Disponibles

- **Page principale** (`/`) :
  - Affichage température en temps réel
  - État du relais et statut de sécurité
  - Contrôle manuel du relais (si autorisé)
  - **Console MQTT** : messages entrants (←) et sortants (→) en temps réel
  - Affichage de la version firmware

- **Configuration** (`/config.html`) :
  - Heures de début et fin d'activité
  - Température maximale (seuil de sécurité)
  - Température de réarmement
  - Sauvegarde avec reboot automatique de l'ESP32

### API REST

| Endpoint | Méthode | Description | Payload |
|----------|---------|-------------|---------|
| `/api/status` | GET | État complet (temp, relais, version, mode) | - |
| `/api/logs` | GET | Historique des 20 derniers messages MQTT | - |
| `/api/config` | GET | Configuration actuelle | - |
| `/api/config` | POST | Sauvegarder config + reboot | `{active_start_hour, active_end_hour, temp_max, temp_reset}` |
| `/api/relay` | POST | Commande relais | `{"command": "ON"}` ou `"OFF"` |

**Exemple réponse `/api/status`** :
```json
{
  "temperature": 45.5,
  "relay": false,
  "status": "Relais OFF",
  "canControl": true,
  "version": "1.2.0",
  "manualMode": false
}
```

**Exemple réponse `/api/logs`** :
```json
{
  "logs": [
    "→ ballon/status : ONLINE",
    "→ ballon/temperature : 45.5",
    "← ballon/relais/command : ON",
    "→ ballon/relais/state : ON"
  ]
}
```

## 📡 Topics MQTT

| Topic | Type | Description | Payload |
|-------|------|-------------|---------|
| `ballon/temperature` | Publish | Température actuelle (toutes les 15s) | `22.3` (float) |
| `ballon/relais/state` | Publish | État du relais | `ON` ou `OFF` |
| `ballon/relais/command` | Subscribe | Commande relais depuis Home Assistant | `ON` ou `OFF` |
| `ballon/safety` | Publish | État sécurité thermique | `SAFETY` ou `OFF` |
| `ballon/status` | Publish | Statut connexion ESP32 | `ONLINE` ou `OFFLINE` |

### Intégration Home Assistant

Exemple de configuration YAML pour Home Assistant :

```yaml
# configuration.yaml
mqtt:
  sensor:
    - name: "Ballon Température"
      state_topic: "ballon/temperature"
      unit_of_measurement: "°C"
      device_class: temperature

    - name: "Ballon Statut"
      state_topic: "ballon/status"

    - name: "Ballon Sécurité"
      state_topic: "ballon/safety"

  switch:
    - name: "Ballon Relais"
      state_topic: "ballon/relais/state"
      command_topic: "ballon/relais/command"
      payload_on: "ON"
      payload_off: "OFF"
```

## ⚙️ Configuration

### 1. Configuration initiale (config.h)

Modifier le fichier `include/config.h` avec vos paramètres réseau :

```cpp
// include/config.h
#ifndef CONFIG_H
#define CONFIG_H

// 🔹 Wi-Fi
#define WIFI_SSID "Votre-SSID"
#define WIFI_PASSWORD "VotreMotDePasse"

// 🔹 MQTT
#define MQTT_SERVER "192.168.1.100"  // IP de votre broker MQTT
#define MQTT_PORT       1883
#define MQTT_USER "mqtt-user"
#define MQTT_PASSWORD "mqtt-password"

// 🔹 Nom d'hôte
#define HOSTNAME        "esp-ballon"

#endif // CONFIG_H
```

### 2. Configuration runtime (via Web)

Accessible via la page `/config.html` :

| Paramètre | Description | Défaut |
|-----------|-------------|--------|
| **Heure début** | Heure de début d'activité (0-24) | 9 |
| **Heure fin** | Heure de fin d'activité (0-24) | 23 |
| **Température max** | Seuil de sécurité (°C) | 70.0 |
| **Température reset** | Seuil de réarmement (°C) | 65.0 |

⚠️ **Important** : La sauvegarde déclenche un reboot automatique de l'ESP32 après 2 secondes.

## 🔋 Modes de Fonctionnement

### Mode Normal (Heures Actives)
- **WiFi/MQTT** : Actifs
- **LED verte** : Allumée en continu
- **Publication MQTT** : Toutes les 15 secondes
- **Contrôle** : Disponible via web et MQTT
- **Transition** : Passage automatique en deep sleep en fin de fenêtre

### Mode Deep Sleep (Hors Heures)
- **WiFi/Bluetooth** : Désactivés
- **Consommation** : Minimale (µA)
- **Publication MQTT** : `OFFLINE` avant sleep
- **Réveil automatique** : À l'heure de début configurée
- **Réveil manuel** : Bouton GPIO12

### Mode Réveil Manuel (Bouton)
- **Durée** : 5 minutes exactement
- **LED verte** : Clignotante (500ms)
- **WiFi/MQTT** : Actifs pendant 5 minutes
- **Publication MQTT** : Toutes les 15 secondes
- **Contrôle** : Disponible même hors horaires
- **Fin** : Deep sleep automatique après 5 minutes

## 🛡️ Système de Sécurité

### Fonctionnement

1. **Surveillance continue** : Lecture DS18B20 toutes les 15s
2. **Déclenchement** : Si température ≥ `temp_max`
   - Relais coupé immédiatement
   - Publication MQTT : `ballon/safety → SAFETY`
   - LED rouge éteinte
   - Contrôle bloqué (web et MQTT)

3. **Réarmement automatique** : Quand température ≤ `temp_reset`
   - Publication MQTT : `ballon/safety → OFF`
   - Contrôle débloqué

### Priorités

```
Sécurité thermique > Horaires d'activité > Commandes manuelles
```

## 🚀 Installation & Build

### Prérequis

- [PlatformIO](https://platformio.org/) (extension VS Code ou CLI)
- ESP32 DevKit
- Câble USB micro/type-C
- Broker MQTT (Mosquitto, Home Assistant, etc.)

### Étapes

1. **Cloner ou télécharger le projet**

2. **Configurer vos paramètres** dans `include/config.h`

3. **Compiler et téléverser le firmware** :
   ```bash
   pio run -t upload
   ```

4. **Uploader le système de fichiers SPIFFS** (interface web) :
   ```bash
   pio run -t uploadfs
   ```

5. **Moniteur série** (optionnel) :
   ```bash
   pio device monitor
   ```

6. **Accéder à l'interface web** :
   - Ouvrir le moniteur série (115200 bauds)
   - Noter l'adresse IP affichée (ex: `192.168.1.150`)
   - Ouvrir dans un navigateur : `http://192.168.1.150`

### Dépendances (automatiques via platformio.ini)

```ini
lib_deps =
    milesburton/DallasTemperature@^4.0.5
    knolleary/PubSubClient@^2.8
    paulstoffregen/OneWire@^2.3.8
    bblanchon/ArduinoJson@^7.4.2
```

## 📊 Indicateurs LED

| LED | État | Signification |
|-----|------|---------------|
| 🟢 Verte (GPIO 26) | Allumée fixe | Mode normal (heures actives) |
| 🟢 Verte (GPIO 26) | Clignotante (500ms) | Mode réveil manuel (5 min) |
| 🟢 Verte (GPIO 26) | Éteinte | Deep sleep |
| 🔴 Rouge (GPIO 27) | Allumée | Relais activé |
| 🔴 Rouge (GPIO 27) | Éteinte | Relais désactivé |

## 🐛 Dépannage

### Problèmes courants

| Problème | Cause probable | Solution |
|----------|----------------|----------|
| Interface web inaccessible | IP incorrecte | Vérifier IP dans le moniteur série |
| Température `-127°C` | DS18B20 déconnecté | Vérifier câblage + pull-up 4.7kΩ |
| WiFi non connecté | Mauvais SSID/password | Vérifier `config.h` |
| MQTT déconnecté | Broker inaccessible | Vérifier IP broker, port, user/pass |
| Relais ne répond pas | GPIO ou alimentation | Vérifier GPIO 33 et alim module relais |
| Config non sauvegardée | SPIFFS non uploadé | Faire `pio run -t uploadfs` |
| Deep sleep non fonctionnel | Bouton bloqué | Vérifier GPIO12 (doit être HIGH au repos) |
| Logs série absents | Mauvais baud rate | Régler à 115200 bauds |

### Logs utiles

**Moniteur série (115200 bauds)** :
```
========== DÉMARRAGE ==========
🔘 Réveil manuel par le bouton !
📡 Connexion WiFi...
✅ WiFi connecté !
IP : 192.168.1.150
⏳ Synchronisation NTP...
✅ NTP synchronisé
🔌 Tentative connexion MQTT...
✅ MQTT connecté
🌐 Serveur Web démarré
========== SETUP TERMINÉ ==========

🌡️ Température mesurée : 45.5 °C
⚡ Relais ON
⏱️ Fin du mode réveil manuel (5 min écoulées)
📴 Désactivation WiFi & Bluetooth...
📡 WiFi & Bluetooth OFF
```

## 📁 Structure du Projet

```
esp32-resistance-mqtt/
├── src/
│   └── main.cpp              # Code principal ESP32 (version 1.2.0)
├── include/
│   └── config.h              # Configuration WiFi/MQTT (à personnaliser)
├── data/                     # Interface web (SPIFFS)
│   ├── index.html            # Page principale + console MQTT
│   ├── config.html           # Page configuration
│   ├── style.css             # Styles CSS (responsive mobile)
│   └── script.js             # JavaScript (fetch API + logs)
├── platformio.ini            # Configuration PlatformIO
└── README.md                 # Cette documentation
```

## 🎨 Personnalisation

### Modifier les GPIO

Dans `src/main.cpp` (lignes 17-21) :

```cpp
#define ONE_WIRE_BUS    32   // DS18B20
#define RELAY_PIN       33   // Commande relais
#define LED_ACTIVE_PIN  26   // LED verte
#define LED_RELAY_PIN   27   // LED rouge
#define WAKE_BUTTON_PIN 12   // Bouton réveil
```

### Modifier les topics MQTT

Dans `src/main.cpp` (lignes 28-35) :

```cpp
const char* mqtt_topic_temp    = "ballon/temperature";
const char* mqtt_topic_state   = "ballon/relais/state";
const char* mqtt_topic_command = "ballon/relais/command";
const char* mqtt_topic_safety  = "ballon/safety";
const char* mqtt_topic_status  = "ballon/status";
```

### Modifier la durée du réveil manuel

Dans `src/main.cpp` (ligne 340) :

```cpp
wakeEndTime = millis() + 5UL * 60UL * 1000UL; // 5 minutes (modifier le 5)
```

### Personnaliser l'interface web

1. Modifier les fichiers dans `data/` (HTML, CSS, JS)
2. Re-uploader SPIFFS : `pio run -t uploadfs`

## 📝 Console MQTT (Interface Web)

La console affiche en temps réel les 20 derniers messages :

- **← Messages entrants** (cyan) : Commandes reçues depuis Home Assistant
- **→ Messages sortants** (orange) : Publications de l'ESP32

Exemples affichés :
```
→ ballon/status : ONLINE
→ ballon/temperature : 45.5
→ ballon/relais/state : OFF
← ballon/relais/command : ON
→ ballon/relais/state : ON
→ ballon/safety : OFF
```

Rafraîchissement automatique toutes les 3 secondes.

## 🔄 Changelog

### Version 1.2.0 (Actuelle)
- ✅ Deep sleep automatique hors horaires
- ✅ Réveil manuel par bouton (GPIO12) pour 5 min
- ✅ Console MQTT en temps réel dans l'interface web
- ✅ Design responsive mobile
- ✅ Affichage version firmware
- ✅ Reboot automatique après sauvegarde config
- ✅ Logs MQTT (20 derniers messages)
- ✅ Optimisation consommation énergétique

### Version 1.1.0
- Interface web avec configuration
- Sécurité thermique automatique
- Fenêtre d'activité configurable

### Version 1.0.0
- Release initiale
- Contrôle relais via MQTT
- Lecture température DS18B20

## 📄 Licence

Ce projet est fourni tel quel, sans garantie. Libre d'utilisation et modification selon vos besoins.

## 🤝 Contribution

Les contributions sont les bienvenues ! N'hésitez pas à :
- Signaler des bugs
- Proposer des améliorations
- Soumettre des pull requests

---

**Développé pour Home Assistant & ESP32** | Version 1.2.0 | 2025
