#include <Arduino.h>
#include <AccelStepper.h>

// Broches de commande du driver ULN2003
#define IN1 13
#define IN2 12
#define IN3 14
#define IN4 27

// Broches des boutons poussoirs
#define BTN_NORMAL 18
#define BTN_RANDOM 19
#define BTN_JOG    21

#define STEPS_PER_REV 2048
#define STEPS_PER_SEC (STEPS_PER_REV / 60) // ~34 pas par seconde

// Ordre requis pour le 28BYJ-48 : IN1, IN3, IN2, IN4
AccelStepper stepper(AccelStepper::HALF4WIRE, IN1, IN3, IN2, IN4);

enum Mode { IDLE, NORMAL, RANDOM_MODE, JOGGING };
Mode currentMode = IDLE;

unsigned long modeStartTime = 0;
unsigned long lastTickTime = 0;
unsigned long lastRandomActionTime = 0;
unsigned long lastJogStepTime = 0;
int secondsElapsed = 0;

// Coupe le courant dans les 4 bobines pour économiser les piles
void powerOffCoils() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
}

// Ramène l'aiguille à 0 si elle s'est arrêtée en cours de route
void returnToZero() {
  if (stepper.currentPosition() != 0) {
    Serial.println("Retour rapide a zero...");
    stepper.setMaxSpeed(950.0);
    stepper.setAcceleration(900.0);
    stepper.moveTo(0);
    
    while (stepper.distanceToGo() != 0) {
      stepper.run();
    }
    delay(150);
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n--- ESP32 CHRONOMETRE PRET ---");
  Serial.println("En attente d'un appui bouton (Normal: 18, Foufou: 19, Jog: 21)...");

  pinMode(BTN_NORMAL, INPUT_PULLUP);
  pinMode(BTN_RANDOM, INPUT_PULLUP);
  pinMode(BTN_JOG, INPUT_PULLUP);

  stepper.setMaxSpeed(800.0);
  stepper.setAcceleration(500.0);
  stepper.setCurrentPosition(0);
  powerOffCoils();
}

void loop() {
  unsigned long now = millis();

  // --- 1. BOUTON DE CALAGE MANUEL (JOG) ---
  if (digitalRead(BTN_JOG) == LOW) {
    if (currentMode != JOGGING) {
      Serial.println("-> Calage manuel en cours (Bouton JOG maintenu)");
      currentMode = JOGGING;
    }

    if (now - lastJogStepTime >= 120) {
      lastJogStepTime = now;
      stepper.move(STEPS_PER_SEC);
    }
    stepper.run();
    return;
  } 
  else if (currentMode == JOGGING) {
    while (stepper.distanceToGo() != 0) {
      stepper.run();
    }
    stepper.setCurrentPosition(0);
    powerOffCoils();
    currentMode = IDLE;
    Serial.println("-> Calage termine : Nouveau point ZERO defini.");
    delay(200);
    return;
  }

  // --- 2. ARRÊT EN COURS DE ROUTE (Si on appuie pendant un tour) ---
  if (currentMode != IDLE && (digitalRead(BTN_NORMAL) == LOW || digitalRead(BTN_RANDOM) == LOW)) {
    Serial.println("-> Interruption demandee ! Arret et retour a zero.");
    delay(50);
    returnToZero();
    powerOffCoils();
    currentMode = IDLE;
    return;
  }

  // --- 3. DÉMARRAGE DES MODES AU REPOS ---
  if (currentMode == IDLE) {
    if (digitalRead(BTN_NORMAL) == LOW) {
      delay(50);
      Serial.println("-> Lancement du MODE NORMAL (60 secondes)");
      returnToZero();
      
      stepper.setMaxSpeed(700.0);
      stepper.setAcceleration(400.0);
      modeStartTime = millis();
      lastTickTime = modeStartTime;
      secondsElapsed = 0;
      currentMode = NORMAL;
    } 
    else if (digitalRead(BTN_RANDOM) == LOW) {
      delay(50);
      Serial.println("-> Lancement du MODE FOUFOU (60 secondes aleatoires)");
      returnToZero();
      
      modeStartTime = millis();
      lastRandomActionTime = modeStartTime;
      currentMode = RANDOM_MODE;
    }
  }

  // --- 4. EXÉCUTION DU MODE NORMAL ---
  if (currentMode == NORMAL) {
    if (now - lastTickTime >= 1000) {
      lastTickTime += 1000;
      secondsElapsed++;
      
      long target = (long)secondsElapsed * STEPS_PER_REV / 60;
      stepper.moveTo(target);
      Serial.printf("Seconde : %d/60 | Cible pas : %ld\n", secondsElapsed, target);
    }

    stepper.run();

    if (secondsElapsed >= 60 && stepper.distanceToGo() == 0) {
      Serial.println("-> Fin du tour normal : 60 secondes ecoulees.");
      stepper.setCurrentPosition(0);
      powerOffCoils();
      currentMode = IDLE;
    }
  }

  // --- 5. EXÉCUTION DU MODE FOUFOU ---
  if (currentMode == RANDOM_MODE) {
    unsigned long elapsed = now - modeStartTime;

    if (now - lastRandomActionTime > (unsigned long)random(200, 800)) {
      lastRandomActionTime = now;

      long progress = (elapsed * STEPS_PER_REV) / 60000;
      long jitter = random(-80, 150);
      stepper.setMaxSpeed(random(400, 900));
      stepper.moveTo(progress + jitter);
    }

    stepper.run();

    if (elapsed >= 60000) {
      Serial.println("-> Fin du tour foufou : Retour a midi.");
      stepper.setMaxSpeed(850);
      stepper.moveTo(STEPS_PER_REV);
      while (stepper.distanceToGo() != 0) {
        stepper.run();
      }
      stepper.setCurrentPosition(0);
      powerOffCoils();
      currentMode = IDLE;
    }
  }
}