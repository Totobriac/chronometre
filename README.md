# ⏱️ Chronomètre & Horloge Scénique Animée (ESP32)

Système d'horloge scénique interactive à aiguille unique, piloté par un microcontrôleur ESP32 et un moteur pas-à-pas démultiplié. 

Le dispositif propose un calage manuel au zéro, un mode chronomètre régulier de 60 secondes et un mode d'animation comique infini débutant par une phase trompeuse normale.

---

## 🛠️ Matériel requis

* **Microcontrôleur :** ESP32 Dev Module (ESP-WROOM-32).
* **Moteur pas-à-pas :** 28BYJ-48 5 V (4 096 demi-pas par tour complet).
* **Driver moteur :** Carte ULN2003 (avec cavalier d'alimentation fermé).
* **Boutons :** 3 boutons-poussoirs momentanés normalement ouverts (NO).
* **Alimentation :** Pack 4× piles AA (~5 V à 6 V) câblé en parallèle avec interrupteur général ON/OFF.

---

## 🔌 Câblage & Brochage

### 1. Driver Moteur ULN2003 $\rightarrow$ ESP32
| Broche ULN2003 | Broche ESP32 |
| :--- | :--- |
| **IN1** | `GPIO 13` |
| **IN2** | `GPIO 12` |
| **IN3** | `GPIO 14` |
| **IN4** | `GPIO 27` |
| **+ (5-12V)** | Ligne `+` 6 V (après interrupteur) |
| **- (GND)** | Ligne `-` 6 V (Masse commune ESP32 `GND`) |

> **Note logicielle :** Séquence demi-pas `AccelStepper::HALF4WIRE` configurée en `IN1, IN3, IN2, IN4`.

### 2. Boutons-poussoirs (câblage `INPUT_PULLUP`)
| Fonction | Broche ESP32 | Autre pôle du bouton |
| :--- | :--- | :--- |
| **Calage (JOG)** | `GPIO 21` | `GND` |
| **Mode Normal (60 s)** | `GPIO 18` | `GND` |
| **Mode Foufou (Infini)** | `GPIO 19` | `GND` |

### 3. Schéma d'alimentation (Parallèle)
* **Pôle positif (+ 6 V) :** Piles $\rightarrow$ Interrupteur ON/OFF $\rightarrow$ division en « Y » vers `VIN` (ESP32) et `+` (ULN2003).
* **Pôle négatif (- Masse) :** Piles $\rightarrow$ division en « Y » vers `GND` (ESP32) et `-` (ULN2003).

---

## 🎮 Modes de fonctionnement

### 1. Calage manuel (Bouton JOG — `GPIO 21`)
* **Maintien appuyé :** L'aiguille avance par petits pas réguliers (~1 seconde de cadran toutes les 100 ms).
* **Relâchement :** Arrêt instantané du moteur, réinitialisation de la position actuelle comme le point zéro absolu (midi / 0 s), puis extinction des bobines.

### 2. Mode Chronomètre Normal (Bouton — `GPIO 18`)
* **Au repos :** Retour rapide au point zéro si nécessaire, puis décompte précis de 60 secondes (1 saut de pas chaque seconde réelle, 1 tour complet de 4 096 demi-pas en 60 s).
* **Fin de tour :** Arrêt net à midi et coupure de l'alimentation des bobines pour économiser les piles.
* **Pendant la rotation :** Un appui sur n'importe quel bouton interrompt le décompte et ramène l'aiguille à midi.

### 3. Mode Foufou Infini (Bouton — `GPIO 19`)
* **Phase 1 (Trompe-l'œil) :** Les **14 premières secondes** se déroulent comme un chronomètre parfaitement normal et régulier.
* **Phase 2 (Animation infinie) :** Dès la 15ᵉ seconde, l'aiguille devient imprévisible sans limite de temps (accélérations brutales, freinages, hésitations, reculs comiques et arrêts nets).
* **Arrêt :** Un appui sur n'importe quel bouton interrompt l'animation et ramène immédiatement l'aiguille à midi via le chemin le plus court.

### 4. Gestion d'énergie
* Désactivation logicielle systématique des 4 bobines du moteur (`digitalWrite(LOW)`) à l'état de repos (`IDLE`) pour éviter toute chauffe et prolonger la durée de vie des piles.

---

## 💻 Environnement & Compilation

Projet développé avec **PlatformIO** sur VS Code.

### Fichier `platformio.ini`
```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
lib_deps =
    waspinator/AccelStepper @ ^1.64
