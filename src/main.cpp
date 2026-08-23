/*
  =====================================================================
  CHRONOMETRE / HORLOGE SCENIQUE ANIMEE - ESP32 + 28BYJ-48 + ULN2003
  =====================================================================

  Materiel :
    - ESP32 Dev Module (ESP-WROOM-32)
    - Moteur pas-a-pas 28BYJ-48 (4096 demi-pas/tour via reducteur)
    - Driver ULN2003
    - 3 boutons poussoirs (INPUT_PULLUP, cables vers GND)

  Cablage :
    GPIO13 -> IN1   GPIO12 -> IN2   GPIO14 -> IN3   GPIO27 -> IN4
    GPIO21 -> Bouton JOG (calage manuel du zero)
    GPIO18 -> Bouton Chrono Normal
    GPIO19 -> Bouton Mode Foufou

  Bibliotheque requise (Gestionnaire de bibliotheques Arduino) :
    - AccelStepper (Mike McCauley)

  Remarque sur le sequencement du moteur :
    Le connecteur ULN2003 est IN1-IN2-IN3-IN4, mais la sequence demi-pas
    a 4 fils attendue par AccelStepper est IN1, IN3, IN2, IN4. On cable
    donc le constructeur AccelStepper avec l'ordre (IN1, IN3, IN2, IN4).
  =====================================================================
*/

#include <AccelStepper.h>

// ---------------------------------------------------------------------
// CONFIGURATION MATERIELLE
// ---------------------------------------------------------------------
#define PIN_IN1 13
#define PIN_IN2 12
#define PIN_IN3 14
#define PIN_IN4 27

#define PIN_BTN_JOG    21
#define PIN_BTN_NORMAL 18
#define PIN_BTN_FOUFOU 19

// Ordre AccelStepper : IN1, IN3, IN2, IN4 (voir remarque ci-dessus)
AccelStepper stepper(AccelStepper::HALF4WIRE, PIN_IN1, PIN_IN3, PIN_IN2, PIN_IN4);

// ---------------------------------------------------------------------
// CONSTANTES DE FONCTIONNEMENT
// ---------------------------------------------------------------------
const long   STEPS_PER_REV      = 4096;   // demi-pas pour un tour complet (60 s)
const long   NORMAL_DURATION_S  = 60;     // duree du chrono normal
const long   FOUFOU_INTRO_S     = 14;     // phase trompeuse initiale du mode Foufou

const unsigned long TICK_MS     = 1000;   // 1 "seconde de cadran" reelle
const unsigned long JOG_TICK_MS = 100;    // cadence des saccades de calage manuel
const long   JOG_STEP           = STEPS_PER_REV / 60; // ~68 demi-pas = 1 s de cadran

const float  JOG_MAX_SPEED    = 900.0;
const float  JOG_ACCEL        = 4000.0;
const float  NORMAL_MAX_SPEED = 220.0;
const float  NORMAL_ACCEL     = 600.0;
const float  RETURN_MAX_SPEED = 1300.0;
const float  RETURN_ACCEL     = 3500.0;

const unsigned long DEBOUNCE_MS = 25;

// ---------------------------------------------------------------------
// GESTION DES BOUTONS (anti-rebond simple)
// ---------------------------------------------------------------------
struct Bouton {
  uint8_t pin;
  bool etatStable;        // true = appuye
  bool dernierEtatBrut;
  unsigned long dernierChangement;
};

Bouton btnJog, btnNormal, btnFoufou;

void initBouton(Bouton &b, uint8_t pin) {
  b.pin = pin;
  pinMode(pin, INPUT_PULLUP);
  bool niveau = (digitalRead(pin) == LOW);
  b.etatStable = niveau;
  b.dernierEtatBrut = niveau;
  b.dernierChangement = millis();
}

// Met a jour l'etat d'un bouton et renvoie les fronts (appui / relachement)
void majBouton(Bouton &b, bool &frontAppui, bool &frontRelache) {
  frontAppui = false;
  frontRelache = false;
  bool niveau = (digitalRead(b.pin) == LOW);

  if (niveau != b.dernierEtatBrut) {
    b.dernierEtatBrut = niveau;
    b.dernierChangement = millis();
  }

  if ((millis() - b.dernierChangement) > DEBOUNCE_MS && niveau != b.etatStable) {
    b.etatStable = niveau;
    if (niveau) frontAppui = true;
    else        frontRelache = true;
  }
}

// ---------------------------------------------------------------------
// MACHINE A ETATS PRINCIPALE
// ---------------------------------------------------------------------
enum Mode {
  MODE_IDLE,
  MODE_JOG,
  MODE_NORMAL_RUN,
  MODE_FOUFOU_INTRO,
  MODE_FOUFOU_CHAOS,
  MODE_RETURN_ZERO
};

enum ApresRetour {
  APRES_RIEN,
  APRES_DEMARRER_NORMAL,
  APRES_DEMARRER_FOUFOU
};

Mode currentMode = MODE_IDLE;
ApresRetour apresRetour = APRES_RIEN;

// Variables du mode JOG
unsigned long jogDernierTick = 0;

// Variables communes chrono normal / intro Foufou (avance seconde par seconde)
long startPositionTick = 0;   // position (multiple de STEPS_PER_REV) au debut du cycle
unsigned long chronoDebutMs = 0;
long secondeCourante = -1;    // derniere seconde "posee" (pour eviter les recalculs inutiles)

// Variables du mode Foufou chaotique
bool  chaosActionEnCours = false;
int   chaosTypeAction = 0;
unsigned long chaosFinPauseMs = 0;

// ---------------------------------------------------------------------
// UTILITAIRES MOTEUR
// ---------------------------------------------------------------------

// Coupe le courant dans les 4 bobines (economie d'energie / anti-echauffement)
void couperBobines() {
  stepper.disableOutputs();
}

// Calcule la cible (multiple de STEPS_PER_REV) pour revenir a midi
// par le chemin le plus court, et lance le mouvement de retour rapide.
void demarrerRetourZero() {
  long pos = stepper.currentPosition();
  long reste = pos % STEPS_PER_REV;
  if (reste < 0) reste += STEPS_PER_REV; // normalisation si position negative

  long cible;
  if (reste <= STEPS_PER_REV / 2) {
    cible = pos - reste;                 // on recule jusqu'au multiple inferieur
  } else {
    cible = pos + (STEPS_PER_REV - reste); // on avance jusqu'au multiple superieur
  }

  stepper.setMaxSpeed(RETURN_MAX_SPEED);
  stepper.setAcceleration(RETURN_ACCEL);
  stepper.moveTo(cible);
  currentMode = MODE_RETURN_ZERO;
}

// Demarrage du chrono normal (aiguille supposee deja a midi)
void demarrerChronoNormal() {
  stepper.setMaxSpeed(NORMAL_MAX_SPEED);
  stepper.setAcceleration(NORMAL_ACCEL);
  startPositionTick = stepper.currentPosition();
  chronoDebutMs = millis();
  secondeCourante = -1;
  currentMode = MODE_NORMAL_RUN;
}

// Demarrage de la phase d'introduction (trompeuse) du mode Foufou
void demarrerFoufouIntro() {
  stepper.setMaxSpeed(NORMAL_MAX_SPEED);
  stepper.setAcceleration(NORMAL_ACCEL);
  startPositionTick = stepper.currentPosition();
  chronoDebutMs = millis();
  secondeCourante = -1;
  currentMode = MODE_FOUFOU_INTRO;
}

// Bascule vers la phase chaotique infinie
void demarrerFoufouChaos() {
  chaosActionEnCours = false;
  currentMode = MODE_FOUFOU_CHAOS;
}

void executerAnimationChaotique();

// ---------------------------------------------------------------------
// SETUP
// ---------------------------------------------------------------------
void setup() {
  initBouton(btnJog, PIN_BTN_JOG);
  initBouton(btnNormal, PIN_BTN_NORMAL);
  initBouton(btnFoufou, PIN_BTN_FOUFOU);

  stepper.setMaxSpeed(NORMAL_MAX_SPEED);
  stepper.setAcceleration(NORMAL_ACCEL);
  stepper.setCurrentPosition(0);

  couperBobines();
  randomSeed(analogRead(0)); // graine aleatoire pour le mode Foufou

  currentMode = MODE_IDLE;
}

// ---------------------------------------------------------------------
// LOOP
// ---------------------------------------------------------------------
void loop() {
  bool frontAppuiJog, frontRelacheJog;
  bool frontAppuiNormal, frontRelacheNormal;
  bool frontAppuiFoufou, frontRelacheFoufou;

  majBouton(btnJog, frontAppuiJog, frontRelacheJog);
  majBouton(btnNormal, frontAppuiNormal, frontRelacheNormal);
  majBouton(btnFoufou, frontAppuiFoufou, frontRelacheFoufou);

  bool nImporteQuelFrontAppui = frontAppuiJog || frontAppuiNormal || frontAppuiFoufou;

  switch (currentMode) {

    // -----------------------------------------------------------------
    case MODE_IDLE: {
      couperBobines(); // s'assure que les bobines restent au repos

      if (btnJog.etatStable) {
        // Entree en mode calage manuel
        stepper.setMaxSpeed(JOG_MAX_SPEED);
        stepper.setAcceleration(JOG_ACCEL);
        jogDernierTick = millis();
        currentMode = MODE_JOG;
      }
      else if (frontAppuiNormal) {
        if (stepper.currentPosition() % STEPS_PER_REV != 0) {
          apresRetour = APRES_DEMARRER_NORMAL;
          demarrerRetourZero();
        } else {
          demarrerChronoNormal();
        }
      }
      else if (frontAppuiFoufou) {
        if (stepper.currentPosition() % STEPS_PER_REV != 0) {
          apresRetour = APRES_DEMARRER_FOUFOU;
          demarrerRetourZero();
        } else {
          demarrerFoufouIntro();
        }
      }
      break;
    }

    // -----------------------------------------------------------------
    case MODE_JOG: {
      if (btnJog.etatStable) {
        // Maintien : saccades regulieres ~1 s de cadran toutes les 100 ms
        if (millis() - jogDernierTick >= JOG_TICK_MS) {
          jogDernierTick += JOG_TICK_MS;
          stepper.move(JOG_STEP);
        }
        stepper.run();
      } else {
        // Relachement : arret instantane + zero absolu + coupure bobines
        stepper.moveTo(stepper.currentPosition()); // annule tout mouvement en attente
        stepper.setCurrentPosition(0);
        couperBobines();
        currentMode = MODE_IDLE;
      }
      break;
    }

    // -----------------------------------------------------------------
    case MODE_NORMAL_RUN: {
      if (nImporteQuelFrontAppui) {
        apresRetour = APRES_RIEN;
        demarrerRetourZero();
        break;
      }

      unsigned long elapsedMs = millis() - chronoDebutMs;
      long seconde = elapsedMs / TICK_MS;
      if (seconde > NORMAL_DURATION_S) seconde = NORMAL_DURATION_S;

      if (seconde != secondeCourante) {
        secondeCourante = seconde;
        long cible = startPositionTick + round((float)seconde * STEPS_PER_REV / NORMAL_DURATION_S);
        stepper.moveTo(cible);
      }
      stepper.run();

      if (seconde >= NORMAL_DURATION_S && stepper.distanceToGo() == 0) {
        // Tour complet termine : on est exactement a midi
        stepper.setCurrentPosition(0);
        couperBobines();
        currentMode = MODE_IDLE;
      }
      break;
    }

    // -----------------------------------------------------------------
    case MODE_FOUFOU_INTRO: {
      if (nImporteQuelFrontAppui) {
        apresRetour = APRES_RIEN;
        demarrerRetourZero();
        break;
      }

      unsigned long elapsedMs = millis() - chronoDebutMs;
      long seconde = elapsedMs / TICK_MS;
      if (seconde > FOUFOU_INTRO_S) seconde = FOUFOU_INTRO_S;

      if (seconde != secondeCourante) {
        secondeCourante = seconde;
        // Meme cadence que le chrono normal (1 tour en 60 s) pour l'effet
        // de surprise : rien ne distingue cette phase d'un chrono classique.
        long cible = startPositionTick + round((float)seconde * STEPS_PER_REV / NORMAL_DURATION_S);
        stepper.moveTo(cible);
      }
      stepper.run();

      if (seconde >= FOUFOU_INTRO_S && stepper.distanceToGo() == 0) {
        demarrerFoufouChaos();
      }
      break;
    }

    // -----------------------------------------------------------------
    case MODE_FOUFOU_CHAOS: {
      if (nImporteQuelFrontAppui) {
        apresRetour = APRES_RIEN;
        demarrerRetourZero();
        break;
      }
      executerAnimationChaotique();
      stepper.run();
      break;
    }

    // -----------------------------------------------------------------
    case MODE_RETURN_ZERO: {
      stepper.run();
      if (stepper.distanceToGo() == 0) {
        stepper.setCurrentPosition(0);
        couperBobines();

        switch (apresRetour) {
          case APRES_DEMARRER_NORMAL:
            demarrerChronoNormal();
            break;
          case APRES_DEMARRER_FOUFOU:
            demarrerFoufouIntro();
            break;
          default:
            currentMode = MODE_IDLE;
            break;
        }
        apresRetour = APRES_RIEN;
      }
      break;
    }
  }
}

// ---------------------------------------------------------------------
// ANIMATION CHAOTIQUE (mode Foufou, phase infinie)
// ---------------------------------------------------------------------
// Alterne aleatoirement entre 4 types d'actions comiques :
//   0 = accelerations fulgurantes (avance rapide)
//   1 = ralentissements (avance lente et hesitante)
//   2 = reculs comiques (petit mouvement arriere)
//   3 = arrets nets (pause franche)
void executerAnimationChaotique() {
  if (!chaosActionEnCours) {
    chaosTypeAction = random(0, 4);

    switch (chaosTypeAction) {
      case 0: { // Accelerations fulgurantes
        float vitesse = random(400, 2000);
        long  pas     = random(300, 1500);
        stepper.setMaxSpeed(vitesse);
        stepper.setAcceleration(vitesse * 6.0);
        stepper.move(pas);
        break;
      }
      case 1: { // Ralentissements
        float vitesse = random(20, 60);
        long  pas     = random(30, 150);
        stepper.setMaxSpeed(vitesse);
        stepper.setAcceleration(150.0);
        stepper.move(pas);
        break;
      }
      case 2: { // Reculs comiques
        float vitesse = random(150, 500);
        long  pas     = random(50, 400);
        stepper.setMaxSpeed(vitesse);
        stepper.setAcceleration(vitesse * 4.0);
        stepper.move(-pas);
        break;
      }
      case 3: { // Arrets nets
        stepper.moveTo(stepper.currentPosition()); // stoppe immediatement
        chaosFinPauseMs = millis() + random(200, 900);
        break;
      }
    }
    chaosActionEnCours = true;
  } else {
    // Fin de l'action en cours ?
    if (chaosTypeAction == 3) {
      if (millis() >= chaosFinPauseMs) chaosActionEnCours = false;
    } else {
      if (stepper.distanceToGo() == 0) chaosActionEnCours = false;
    }
  }
}