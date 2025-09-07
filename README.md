# ESP32 Résistance MQTT

Contrôle d’un relais normalement fermé (NC) via MQTT, lecture de température (DS18B20), synchronisation NTP et gestion d’une fenêtre d’activité journalière (9h → 18h) avec mise en deep sleep en dehors de cette plage.

## Fonctionnalités

- Connexion Wi‑Fi et synchronisation horaire via NTP
- Fenêtre active : 09:00–18:00, deep sleep le reste du temps (≈ 15 h)
- Lecture de la température (DS18B20) et publication sur MQTT toutes les 10 s
- Réception de commandes MQTT `ON`/`OFF` pour piloter le relais NC
- Indicateurs LED : verte (ESP32 actif), rouge (relais activé → circuit ouvert)

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

## MQTT

- Broker (par défaut) : `broker.hivemq.com:1883` (exemple public)
- Authentification : `mqtt_user` / `mqtt_pass` (à adapter ou supprimer côté broker)
- Topics :
  - Souscription commandes relais : `esp32/relais/command` → charge utile `ON` ou `OFF`
  - Publication température : `esp32/temperature` (QoS par défaut, retained true)

Exemples :

```text
Souscrire : esp32/relais/command   (payload: ON | OFF)
Publier   : esp32/temperature      (ex: 22.31)
```

## Comportement horaire et deep sleep

- Au démarrage, l’heure est synchronisée via NTP.
- Si l’heure n’est pas dans [09:00, 18:00), l’ESP32 passe immédiatement en deep sleep jusqu’au lendemain 09:00 (réveil temporisé ≈ 15 h).
- Pendant la plage active, la boucle maintient MQTT et publie la température toutes les 10 s. À 18:00, passage en deep sleep.

## Prérequis

- PlatformIO (VS Code ou CLI)
- Carte plateforme : `espressif32`
- Bibliothèques Arduino :
  - `WiFi` (builtin ESP32)
  - `PubSubClient`
  - `OneWire`
  - `DallasTemperature`
  - `NTPClient`

Ces libs peuvent être ajoutées via PlatformIO (`lib_deps`) ou le gestionnaire de bibliothèques.

## Installation & Build

1. Cloner ce dépôt dans PlatformIO ou copier le dossier du projet.
2. Ouvrir le projet dans VS Code (extension PlatformIO installée).
3. Adapter la configuration dans `src/main.cpp` :
   - `ssid`, `password` : identifiants Wi‑Fi
   - `mqtt_server`, `mqtt_port`, `mqtt_user`, `mqtt_password`
   - `mqtt_topic_subscribe`, `mqtt_topic_publish_temp` (si besoin)
4. Connecter la carte ESP32 et sélectionner le bon port série.
5. Compiler et téléverser via PlatformIO : Build → Upload.
6. Ouvrir le moniteur série à 115200 bauds pour suivre les logs.

## Indicateurs LED

- LED verte (`GPIO 25`) : ON quand l’ESP32 est en phase active
- LED rouge (`GPIO 27`) : ON quand le relais est activé (circuit ouvert)

## Dépannage

- Pas de capteur : vérifier le DS18B20, la résistance 4.7 kΩ et le `GPIO 4`.
- Pas de Wi‑Fi : vérifier `ssid/password`, la couverture et la configuration régionale.
- MQTT non connecté : vérifier l’IP/nom de domaine du broker, le port et les identifiants.
- Heure incorrecte : vérifier l’accès NTP (pare‑feu) et la connectivité Internet.

## Personnalisation

- Plage horaire : adapter la logique dans `setup()` et `loopActive()` (calcul heure via NTP).
- Durée de deep sleep : changer `SLEEP_DURATION_US` (µs) dans `src/main.cpp`.
- Broches : modifier les `#define` (`RELAY_PIN`, `LED_*`, `ONE_WIRE_BUS`).

## Licence

Ce projet est fourni tel quel, sans garantie. Adaptez la licence selon vos besoins.
# esp32-resistance-mqttt
