
/*
  CARDIO ECE - BPM + 2 OLED + EEPROM + RTC

  OLED 1 : SDA A4, SCL A5 (I2C materiel)
  OLED 2 : SDA D8, SCL D9 (I2C logiciel)
  Les deux OLED sont a l'adresse 0x3C.

  WPSE340 : A0
  LEDs : D10, D11, D12
  Buzzer : D13
  Bouton buzzer : D4

  RTC DS1302 : DAT D7, CLK D6, RST D5

  Bouton A1 : sauver la moyenne de 10 BPM
  Bouton A2 : maintenir 2 s pour effacer
  Encodeur : zoom PPG et menu OLED
  Bouton encodeur : buzzer / menu

  Serial Monitor : 115200 bauds
*/

#include <Arduino.h>
#include <stdint.h>
#include <string.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <EEPROM.h>
#include <ThreeWire.h>
#include <RtcDS1302.h>
#include <avr/pgmspace.h>

// Declaration necessaire avant les prototypes automatiques de l'IDE Arduino.
struct MesureMemoire;
byte calculerControle(const MesureMemoire &m);

// --------------------------------------------------
// BROCHES
// --------------------------------------------------

const byte PIN_CAPTEUR = A0;
const byte PIN_LED_ROUGE = 10;
const byte PIN_LED_VERTE = 11;
const byte PIN_LED_JAUNE = 12;
const byte PIN_BUZZER = 13;
const byte PIN_BOUTON_BUZZER = 4;

const byte PIN_SAUVEGARDER = A1;
const byte PIN_EFFACER = A2;

// AJOUT MODULES : encodeur rotatif (A=D2, B=D3, bouton=D4)
const byte PIN_ENCODEUR_A = 2;
const byte PIN_ENCODEUR_B = 3;

// --------------------------------------------------
// PARAMETRES BPM - INCHANGES
// --------------------------------------------------

const unsigned long PERIODE_ECHANTILLONNAGE = 20;
const byte TAILLE_FILTRE = 5;
const byte NOMBRE_INTERVALLES = 5;
const unsigned long INTERVALLE_MINIMUM = 500;
const unsigned long INTERVALLE_MAXIMUM = 1500;
const unsigned long TEMPS_INITIALISATION = 4000;
const int AMPLITUDE_MINIMALE = 30;
const unsigned long TEMPS_PERTE_SIGNAL = 2500;

int valeursFiltrees[TAILLE_FILTRE];
byte positionFiltre = 0;
long sommeFiltre = 0;
bool filtrePret = false;

int minimumSignal = 1023;
int maximumSignal = 0;
unsigned long debutFenetre = 0;
unsigned long debutMesure = 0;
bool mesureInitialisee = false;

bool signalAuDessusDuSeuil = false;
bool premierBattementTrouve = false;
unsigned long dernierBattement = 0;
unsigned long derniereDetectionValide = 0;
unsigned long intervalles[NOMBRE_INTERVALLES];
byte nombreIntervalles = 0;
byte positionIntervalle = 0;

float bpm = 0.0;
int valeurFiltree = 0;
int seuilActuel = 0;
unsigned long prochaineMesure = 0;

unsigned long dernierEtatBouton = HIGH;
unsigned long dernierChangementBouton = 0;
const unsigned long ANTI_REBOND = 40;
bool buzzerActif = true;

// --------------------------------------------------
// OLED - CONFIGURATION DEJA FONCTIONNELLE
// --------------------------------------------------

U8G2_SSD1306_128X64_NONAME_1_HW_I2C oledBPM(
  U8G2_R0, U8X8_PIN_NONE
);

U8G2_SSD1306_128X64_NONAME_1_SW_I2C oledPPG(
  U8G2_R0, 9, 8, U8X8_PIN_NONE
);

const byte POINTS_PPG = 128;
uint16_t historiquePPG[POINTS_PPG];
// Un seul octet par point : coordonnee Y a dessiner (16 a 61).
// Evite le tableau secondaire uint16_t de 256 octets.
byte imagePPG[POINTS_PPG];
byte debutImagePPG = 0;

byte positionPPG = 0;
byte nombrePointsPPG = 0;
byte nombrePointsImage = 0;

int minimumImage = 0;
int maximumImage = 1023;
int seuilImage = 0;

const unsigned long PAUSE_PAGE_OLED = 35;
const unsigned long PERIODE_CYCLE_OLED = 650;
unsigned long prochainPageOLED = 0;
unsigned long dernierCycleOLED = 0;
byte etapeOLED = 0;

// AJOUT MODULES : echelle du graphique et interface OLED
byte echelleGraphique = 1;       // 1 = 20 ms/pixel, 2 = 40 ms/pixel, 4 = 80 ms/pixel
volatile int16_t variationEncodeur = 0;
volatile byte ancienEtatEncodeur = 0;
int16_t accumulateurEncodeur = 0;

enum ModeInterface {
  INTERFACE_NORMALE,
  INTERFACE_MENU,
  INTERFACE_HISTORIQUE
};

ModeInterface modeInterface = INTERFACE_NORMALE;
byte choixMenu = 0;
byte indexHistoriqueOLED = 0;
bool langueAnglaise = false;

bool boutonInterfaceEnfonce = false;
bool actionLongueEffectuee = false;
bool lectureInterfacePrecedente = HIGH;
bool lectureInterfaceStable = HIGH;
unsigned long debutBoutonInterface = 0;
unsigned long dernierChangementInterface = 0;
const unsigned long DUREE_MENU = 1000;
const unsigned long ANTI_REBOND_INTERFACE = 40;

RtcDateTime heureOLED(2000, 1, 1, 0, 0, 0);
bool heureOLEDValide = false;
unsigned long prochaineLectureRTC = 0;

void gererBoutonEtMenu(unsigned long maintenant);

// --------------------------------------------------
// RTC DS1302
// --------------------------------------------------

ThreeWire filRTC(7, 6, 5);
RtcDS1302<ThreeWire> rtc(filRTC);

// --------------------------------------------------
// AJOUT MODULES : ENCODEUR ET RTC POUR L'INTERFACE
// --------------------------------------------------

const int8_t TABLE_TRANSITIONS_ENCODEUR[16] = {
  0, -1, 1, 0,
  1, 0, 0, -1,
 -1, 0, 0, 1,
  0, 1, -1, 0
};

byte lireEtatEncodeur() {
  return (digitalRead(PIN_ENCODEUR_A) << 1) |
         digitalRead(PIN_ENCODEUR_B);
}

void interruptionEncodeur() {
  byte nouvelEtat = lireEtatEncodeur();
  byte index = (ancienEtatEncodeur << 2) | nouvelEtat;
  variationEncodeur += TABLE_TRANSITIONS_ENCODEUR[index];
  ancienEtatEncodeur = nouvelEtat;
}

void mettreAJourHeureOLED(unsigned long maintenant) {
  if ((long)(maintenant - prochaineLectureRTC) < 0) return;

  prochaineLectureRTC = maintenant + 500;

  if (rtc.IsDateTimeValid()) {
    heureOLED = rtc.GetDateTime();
    heureOLEDValide = true;
  } else {
    heureOLEDValide = false;
  }
}

// --------------------------------------------------
// EEPROM
// --------------------------------------------------

struct MesureMemoire {
  uint16_t bpmDixieme;
  byte annee;
  byte mois;
  byte jour;
  byte heure;
  byte minute;
  byte seconde;
  byte controle;
};

const byte MAGIC_EEPROM_1 = 0xCE;
const byte MAGIC_EEPROM_2 = 0x42;
const byte VERSION_EEPROM = 1;
const int DEBUT_DONNEES = 4;

byte nombreSauvegardes = 0;
byte capaciteEEPROM = 0;

bool collecteActive = false;
byte compteurBPM = 0;
float sommeBPM = 0;

bool ancienEtatA1 = HIGH;
bool ancienEtatA2 = HIGH;
unsigned long dernierAppuiA1 = 0;
unsigned long debutAppuiA2 = 0;
bool effacementFait = false;

bool consultationActive = false;
byte indexConsultation = 0;
unsigned long dernierAffichageHistorique = 0;

// --------------------------------------------------
// FONCTIONS EEPROM
// --------------------------------------------------

byte calculerControle(const MesureMemoire &m) {
  return (byte)(
    m.bpmDixieme ^
    (m.bpmDixieme >> 8) ^
    m.annee ^ m.mois ^ m.jour ^
    m.heure ^ m.minute ^ m.seconde ^ 0xA5
  );
}

void initialiserMemoire() {
  capaciteEEPROM =
    (EEPROM.length() - DEBUT_DONNEES) /
    sizeof(MesureMemoire);

  if (EEPROM.read(0) != MAGIC_EEPROM_1 ||
      EEPROM.read(1) != MAGIC_EEPROM_2 ||
      EEPROM.read(2) != VERSION_EEPROM ||
      EEPROM.read(3) > capaciteEEPROM) {

    EEPROM.update(3, 0);
    EEPROM.update(2, VERSION_EEPROM);
    EEPROM.update(1, MAGIC_EEPROM_2);
    EEPROM.update(0, MAGIC_EEPROM_1);
  }

  nombreSauvegardes = EEPROM.read(3);

  Serial.print(F("EEPROM : "));
  Serial.print(nombreSauvegardes);
  Serial.print('/');
  Serial.println(capaciteEEPROM);
}

void reinitialiserCollecte() {
  compteurBPM = 0;
  sommeBPM = 0;
}

void enregistrerDansEEPROM(float moyenne) {
  if (nombreSauvegardes >= capaciteEEPROM) {
    Serial.println(F("EEPROM pleine"));
    return;
  }

  MesureMemoire m = {};
  m.bpmDixieme =
    (uint16_t)(moyenne * 10.0f + 0.5f);

  // Recuperation de l'heure du RTC.
  // Aucune remise a l'heure automatique.

  if (rtc.IsDateTimeValid()) {
    RtcDateTime t = rtc.GetDateTime();

    if (t.Year() >= 2024 &&
        t.Year() <= 2099 &&
        t.Month() >= 1 &&
        t.Month() <= 12 &&
        t.Day() >= 1 &&
        t.Day() <= 31) {

      m.annee = t.Year() - 2000;
      m.mois = t.Month();
      m.jour = t.Day();
      m.heure = t.Hour();
      m.minute = t.Minute();
      m.seconde = t.Second();
    }
  }

  m.controle = calculerControle(m);

  int adresse =
    DEBUT_DONNEES +
    nombreSauvegardes * sizeof(MesureMemoire);

  EEPROM.put(adresse, m);

  nombreSauvegardes++;
  EEPROM.update(3, nombreSauvegardes);

  Serial.print(F("EEPROM : BPM moyen = "));
  Serial.print(moyenne, 1);
  Serial.print(F(" ; enregistrements = "));
  Serial.println(nombreSauvegardes);

  if (m.mois == 0) {
    Serial.println(
      F("ATTENTION : date RTC indisponible")
    );
  }
}

bool bpmValide();

void ajouterMesureSauvegarde() {
  if (!collecteActive || !bpmValide()) return;

  sommeBPM += bpm;
  compteurBPM++;

  if (compteurBPM == 10) {
    collecteActive = false;

    enregistrerDansEEPROM(
      sommeBPM / 10.0f
    );

    reinitialiserCollecte();
  }
}

// --------------------------------------------------
// LEDS - CODE D'ORIGINE
// --------------------------------------------------

void eteindreToutesLesLEDs() {
  digitalWrite(PIN_LED_ROUGE, LOW);
  digitalWrite(PIN_LED_VERTE, LOW);
  digitalWrite(PIN_LED_JAUNE, LOW);
}

void afficherEtatAvecLEDs() {
  eteindreToutesLesLEDs();

  if (bpm < 40 || bpm > 180) return;

  if (bpm < 60) {
    digitalWrite(PIN_LED_JAUNE, HIGH);
  } else if (bpm <= 100) {
    digitalWrite(PIN_LED_VERTE, HIGH);
  } else {
    digitalWrite(PIN_LED_ROUGE, HIGH);
  }
}

// --------------------------------------------------
// BUZZER - CODE D'ORIGINE
// --------------------------------------------------

int frequenceBuzzer() {
  if (bpm < 60) return 300;
  if (bpm <= 100) return 600;
  return 1000;
}

void jouerBip() {
  if (!buzzerActif) return;
  if (bpm < 40 || bpm > 180) return;

  tone(PIN_BUZZER, frequenceBuzzer(), 60);
}

// --------------------------------------------------
// BPM - CODE D'ORIGINE
// --------------------------------------------------

void remettreBPMAZero() {
  bpm = 0.0;
  nombreIntervalles = 0;
  positionIntervalle = 0;
  premierBattementTrouve = false;
  dernierBattement = 0;
  derniereDetectionValide = 0;
  eteindreToutesLesLEDs();
  noTone(PIN_BUZZER);

  reinitialiserCollecte();
}

bool bpmValide() {
  return bpm >= 40.0 && bpm <= 180.0;
}

float calculerBPM() {
  if (nombreIntervalles == 0) return 0.0;

  float bpmTries[NOMBRE_INTERVALLES];

  for (byte i = 0; i < nombreIntervalles; i++) {
    bpmTries[i] = 60000.0 / intervalles[i];
  }

  for (byte i = 0; i < nombreIntervalles - 1; i++) {
    for (byte j = i + 1; j < nombreIntervalles; j++) {
      if (bpmTries[j] < bpmTries[i]) {
        float temporaire = bpmTries[i];
        bpmTries[i] = bpmTries[j];
        bpmTries[j] = temporaire;
      }
    }
  }

  return bpmTries[nombreIntervalles / 2];
}

// --------------------------------------------------
// FILTRE - CODE D'ORIGINE
// --------------------------------------------------

void initialiserFiltre(int premiereValeur) {
  sommeFiltre = 0;
  positionFiltre = 0;

  for (byte i = 0; i < TAILLE_FILTRE; i++) {
    valeursFiltrees[i] = premiereValeur;
    sommeFiltre += premiereValeur;
  }

  filtrePret = true;
}

int filtrerValeur(int valeurBrute) {
  if (!filtrePret) initialiserFiltre(valeurBrute);

  sommeFiltre -= valeursFiltrees[positionFiltre];
  valeursFiltrees[positionFiltre] = valeurBrute;
  sommeFiltre += valeurBrute;
  positionFiltre++;

  if (positionFiltre >= TAILLE_FILTRE) {
    positionFiltre = 0;
  }

  return sommeFiltre / TAILLE_FILTRE;
}

// --------------------------------------------------
// DETECTION BPM - CODE D'ORIGINE
// --------------------------------------------------

void enregistrerBattement(unsigned long maintenant) {
  if (!premierBattementTrouve) {
    premierBattementTrouve = true;
    dernierBattement = maintenant;
    return;
  }

  unsigned long intervalle =
    maintenant - dernierBattement;

  if (intervalle < INTERVALLE_MINIMUM) return;

  if (intervalle > INTERVALLE_MAXIMUM) {
    nombreIntervalles = 0;
    positionIntervalle = 0;
    bpm = 0.0;
    reinitialiserCollecte();
    dernierBattement = maintenant;
    return;
  }

  intervalles[positionIntervalle] = intervalle;
  positionIntervalle++;

  if (positionIntervalle >= NOMBRE_INTERVALLES) {
    positionIntervalle = 0;
  }

  if (nombreIntervalles < NOMBRE_INTERVALLES) {
    nombreIntervalles++;
  }

  dernierBattement = maintenant;
  derniereDetectionValide = maintenant;
  bpm = calculerBPM();

  afficherEtatAvecLEDs();
  jouerBip();

  // Ajout uniquement pour la sauvegarde
  ajouterMesureSauvegarde();
}

// --------------------------------------------------
// MEMORISATION SIGNAL PPG
// --------------------------------------------------

void memoriserPPG() {
  historiquePPG[positionPPG] = valeurFiltree;
  positionPPG++;

  if (positionPPG >= POINTS_PPG) {
    positionPPG = 0;
  }

  if (nombrePointsPPG < POINTS_PPG) {
    nombrePointsPPG++;
  }
}

// --------------------------------------------------
// TRAITEMENT DU CAPTEUR - INCHANGE
// --------------------------------------------------

void traiterUneMesure(unsigned long maintenant) {
  int valeurBrute = analogRead(PIN_CAPTEUR);
  valeurFiltree = filtrerValeur(valeurBrute);

  if (!mesureInitialisee) {
    debutMesure = maintenant;
    debutFenetre = maintenant;
    minimumSignal = valeurFiltree;
    maximumSignal = valeurFiltree;
    mesureInitialisee = true;
  }

  if (maintenant - debutFenetre >= 4000) {
    minimumSignal = valeurFiltree;
    maximumSignal = valeurFiltree;
    debutFenetre = maintenant;
  }

  if (valeurFiltree < minimumSignal) {
    minimumSignal = valeurFiltree;
  }

  if (valeurFiltree > maximumSignal) {
    maximumSignal = valeurFiltree;
  }

  int amplitude = maximumSignal - minimumSignal;

  seuilActuel =
    minimumSignal + (long)amplitude * 65 / 100;

  bool auDessus =
    amplitude > AMPLITUDE_MINIMALE &&
    valeurFiltree > seuilActuel;

  bool detectionAutorisee =
    maintenant - debutMesure >= TEMPS_INITIALISATION;

  if (nombreIntervalles > 0 &&
      maintenant - derniereDetectionValide >
      TEMPS_PERTE_SIGNAL) {
    remettreBPMAZero();
  }

  if (detectionAutorisee &&
      auDessus &&
      !signalAuDessusDuSeuil) {
    enregistrerBattement(maintenant);
  }

  signalAuDessusDuSeuil = auDessus;
  memoriserPPG();
}

// --------------------------------------------------
// BOUTON BUZZER - COMPATIBILITE
// --------------------------------------------------

void gererBoutonBuzzer() {
  // L'ancienne fonction est conservée pour compatibilité.
  // La boucle principale utilise maintenant la version non bloquante.
  gererBoutonEtMenu(millis());
}

// --------------------------------------------------
// AJOUT MODULES : INTERFACE ENCODEUR / MENU OLED
// --------------------------------------------------

void effacerHistoriqueDepuisMenu() {
  EEPROM.update(3, 0);
  nombreSauvegardes = 0;
  consultationActive = false;
  collecteActive = false;
  reinitialiserCollecte();
  indexHistoriqueOLED = 0;
}

void appliquerRotationInterface(int direction) {
  if (modeInterface == INTERFACE_NORMALE) {
    if (direction > 0 && echelleGraphique < 4) {
      echelleGraphique *= 2;
    } else if (direction < 0 && echelleGraphique > 1) {
      echelleGraphique /= 2;
    }
    return;
  }

  if (modeInterface == INTERFACE_MENU) {
    if (direction > 0 && choixMenu < 3) choixMenu++;
    if (direction < 0 && choixMenu > 0) choixMenu--;
    return;
  }

  if (nombreSauvegardes == 0) {
    indexHistoriqueOLED = 0;
    return;
  }

  if (direction > 0 &&
      indexHistoriqueOLED + 1 < nombreSauvegardes) {
    indexHistoriqueOLED++;
  }

  if (direction < 0 && indexHistoriqueOLED > 0) {
    indexHistoriqueOLED--;
  }
}

void gererEncodeur() {
  int16_t variation;

  noInterrupts();
  variation = variationEncodeur;
  variationEncodeur = 0;
  interrupts();

  // Un cran complet correspond généralement à quatre transitions.
  // On conserve les transitions incomplètes entre deux passages.
  accumulateurEncodeur += variation;

  while (accumulateurEncodeur >= 4) {
    appliquerRotationInterface(1);
    accumulateurEncodeur -= 4;
  }

  while (accumulateurEncodeur <= -4) {
    appliquerRotationInterface(-1);
    accumulateurEncodeur += 4;
  }
}

void validerChoixMenu() {
  if (modeInterface == INTERFACE_MENU) {
    if (choixMenu == 0) {
      modeInterface = INTERFACE_HISTORIQUE;
      indexHistoriqueOLED = nombreSauvegardes > 0
        ? nombreSauvegardes - 1
        : 0;
    } else if (choixMenu == 1) {
      effacerHistoriqueDepuisMenu();
    } else if (choixMenu == 2) {
      langueAnglaise = !langueAnglaise;
    } else {
      modeInterface = INTERFACE_NORMALE;
    }
  } else if (modeInterface == INTERFACE_HISTORIQUE) {
    modeInterface = INTERFACE_MENU;
  }
}

void gererBoutonEtMenu(unsigned long maintenant) {
  bool lecture = digitalRead(PIN_BOUTON_BUZZER);

  if (lecture != lectureInterfacePrecedente) {
    dernierChangementInterface = maintenant;
    lectureInterfacePrecedente = lecture;
  }

  if (lecture != lectureInterfaceStable &&
      maintenant - dernierChangementInterface >=
      ANTI_REBOND_INTERFACE) {

    lectureInterfaceStable = lecture;

    if (lectureInterfaceStable == LOW) {
      boutonInterfaceEnfonce = true;
      actionLongueEffectuee = false;
      debutBoutonInterface = maintenant;
    } else if (boutonInterfaceEnfonce) {
      if (!actionLongueEffectuee) {
        if (modeInterface == INTERFACE_NORMALE) {
          buzzerActif = !buzzerActif;
          noTone(PIN_BUZZER);
        } else {
          validerChoixMenu();
        }
      }

      boutonInterfaceEnfonce = false;
      actionLongueEffectuee = false;
    }
  }

  if (lectureInterfaceStable == LOW &&
      boutonInterfaceEnfonce &&
      !actionLongueEffectuee &&
      maintenant - debutBoutonInterface >= DUREE_MENU) {

    actionLongueEffectuee = true;

    if (modeInterface == INTERFACE_NORMALE) {
      modeInterface = INTERFACE_MENU;
      choixMenu = 0;
    } else {
      modeInterface = INTERFACE_NORMALE;
    }
  }
}

// --------------------------------------------------
// SERIAL - INCHANGE
// --------------------------------------------------

void afficherTrace(unsigned long maintenant) {
  Serial.print(maintenant);
  Serial.print('\t');
  Serial.print(valeurFiltree);
  Serial.print('\t');
  Serial.print(seuilActuel);
  Serial.print('\t');
  Serial.println(bpm, 2);
}

// --------------------------------------------------
// BOUTONS EEPROM
// --------------------------------------------------

void gererBoutonsEEPROM(unsigned long maintenant) {
  bool a1 = digitalRead(PIN_SAUVEGARDER);

  if (ancienEtatA1 == HIGH &&
      a1 == LOW &&
      maintenant - dernierAppuiA1 >= 180) {

    dernierAppuiA1 = maintenant;

    if (!collecteActive) {
      if (nombreSauvegardes >= capaciteEEPROM) {
        Serial.println(
          F("EEPROM pleine : maintenir A2 pour effacer")
        );
      } else {
        collecteActive = true;
        reinitialiserCollecte();

        Serial.println(
          F("A1 : acquisition de 10 BPM valides...")
        );
      }
    }
  }

  ancienEtatA1 = a1;

  bool a2 = digitalRead(PIN_EFFACER);

  if (ancienEtatA2 == HIGH && a2 == LOW) {
    debutAppuiA2 = maintenant;
    effacementFait = false;
  }

  if (a2 == LOW &&
      !effacementFait &&
      maintenant - debutAppuiA2 >= 2000) {

    // Effacement logique de l'historique
    EEPROM.update(3, 0);

    nombreSauvegardes = 0;
    consultationActive = false;
    collecteActive = false;
    reinitialiserCollecte();
    effacementFait = true;

    Serial.println(
      F("A2 : historique EEPROM efface")
    );
  }

  ancienEtatA2 = a2;
}

// --------------------------------------------------
// CONSULTATION EEPROM DANS LE SERIAL MONITOR
// --------------------------------------------------

void afficherDeuxChiffres(byte nombre) {
  if (nombre < 10) Serial.print('0');
  Serial.print(nombre);
}

void gererConsultationEEPROM(unsigned long maintenant) {
  while (Serial.available() > 0) {
    char commande = Serial.read();

    if (commande == 'H' || commande == 'h') {
      consultationActive = true;
      indexConsultation = 0;
      dernierAffichageHistorique = maintenant - 200;

      Serial.print(F("HISTORIQUE EEPROM : "));
      Serial.println(nombreSauvegardes);
    }
  }

  // Lecture progressive pour ne pas bloquer le BPM.
  if (!consultationActive ||
      maintenant - dernierAffichageHistorique < 150) {
    return;
  }

  dernierAffichageHistorique = maintenant;

  if (indexConsultation >= nombreSauvegardes) {
    consultationActive = false;
    Serial.println(F("FIN HISTORIQUE"));
    return;
  }

  MesureMemoire m;

  EEPROM.get(
    DEBUT_DONNEES +
    indexConsultation * sizeof(MesureMemoire),
    m
  );

  Serial.print('#');
  Serial.print(indexConsultation + 1);

  if (m.controle != calculerControle(m)) {
    Serial.println(
      F(" : ENREGISTREMENT ENDOMMAGE")
    );
  } else {
    Serial.print(F(" : "));
    Serial.print(m.bpmDixieme / 10.0f, 1);
    Serial.print(F(" BPM | "));

    if (m.mois == 0) {
      Serial.println(F("date indisponible"));
    } else {
      afficherDeuxChiffres(m.jour);
      Serial.print('/');
      afficherDeuxChiffres(m.mois);
      Serial.print(F("/20"));
      afficherDeuxChiffres(m.annee);
      Serial.print(' ');
      afficherDeuxChiffres(m.heure);
      Serial.print(':');
      afficherDeuxChiffres(m.minute);
      Serial.print(':');
      afficherDeuxChiffres(m.seconde);
      Serial.println();
    }
  }

  indexConsultation++;
}

// --------------------------------------------------
// OLED 1 : BPM + HEURE + MENU - MODULE AJOUTE
// --------------------------------------------------

// Les textes fixes restent dans la Flash, et non dans la RAM.
void dessinerTexteFlash(U8G2 &ecran, byte x, byte y,
                        const __FlashStringHelper *texte) {
  char tampon[24];
  strncpy_P(tampon, reinterpret_cast<PGM_P>(texte), sizeof(tampon) - 1);
  tampon[sizeof(tampon) - 1] = '\0';
  ecran.drawStr(x, y, tampon);
}

void dessinerMenuOLED() {
  oledBPM.setFont(u8g2_font_6x10_tf);
  dessinerTexteFlash(oledBPM, 0, 9,
                     langueAnglaise ? F("CARDIO MENU") : F("MENU CARDIO"));
  oledBPM.drawHLine(0, 11, 128);

  dessinerTexteFlash(oledBPM, 0, 22, choixMenu == 0 ? F(">") : F(" "));
  dessinerTexteFlash(oledBPM, 8, 22,
                     langueAnglaise ? F("History") : F("Historique"));
  dessinerTexteFlash(oledBPM, 0, 34, choixMenu == 1 ? F(">") : F(" "));
  dessinerTexteFlash(oledBPM, 8, 34,
                     langueAnglaise ? F("Erase EEPROM") : F("Effacer EEPROM"));
  dessinerTexteFlash(oledBPM, 0, 46, choixMenu == 2 ? F(">") : F(" "));
  dessinerTexteFlash(oledBPM, 8, 46,
                     langueAnglaise ? F("Language: EN") : F("Langue : FR"));
  dessinerTexteFlash(oledBPM, 0, 58, choixMenu == 3 ? F(">") : F(" "));
  dessinerTexteFlash(oledBPM, 8, 58,
                     langueAnglaise ? F("Exit") : F("Quitter"));
}

void dessinerHistoriqueOLED() {
  oledBPM.setFont(u8g2_font_6x10_tf);
  dessinerTexteFlash(oledBPM, 0, 9,
    langueAnglaise ? F("EEPROM HISTORY") : F("HISTORIQUE EEPROM"));
  oledBPM.drawHLine(0, 11, 128);

  if (nombreSauvegardes == 0) {
    dessinerTexteFlash(oledBPM, 10, 34,
      langueAnglaise ? F("No measurement") : F("Aucune mesure"));
    dessinerTexteFlash(oledBPM, 0, 63,
      langueAnglaise ? F("Press = back") : F("Appuyer = retour"));
    return;
  }

  MesureMemoire m;
  EEPROM.get(
    DEBUT_DONNEES + indexHistoriqueOLED * sizeof(MesureMemoire),
    m
  );

  char ligne[26];
  char bpmTexte[20];

  snprintf_P(
    ligne, sizeof(ligne),
    langueAnglaise ? PSTR("Record %u/%u") : PSTR("Mesure %u/%u"),
    (unsigned)(indexHistoriqueOLED + 1),
    (unsigned)nombreSauvegardes
  );
  oledBPM.drawStr(0, 23, ligne);

  if (m.controle != calculerControle(m)) {
    dessinerTexteFlash(oledBPM, 0, 40,
      langueAnglaise ? F("Damaged record") : F("Donnee endommagee"));
  } else {
    snprintf_P(
      bpmTexte, sizeof(bpmTexte),
      langueAnglaise ? PSTR("BPM: %u.%u") : PSTR("BPM : %u.%u"),
      (unsigned)(m.bpmDixieme / 10),
      (unsigned)(m.bpmDixieme % 10)
    );
    oledBPM.drawStr(0, 39, bpmTexte);

    snprintf_P(
      ligne, sizeof(ligne),
      PSTR("%02u/%02u/20%02u %02u:%02u"),
      (unsigned)m.jour,
      (unsigned)m.mois,
      (unsigned)m.annee,
      (unsigned)m.heure,
      (unsigned)m.minute
    );
    oledBPM.drawStr(0, 52, ligne);
  }

  dessinerTexteFlash(oledBPM, 0, 63,
    langueAnglaise ? F("Turn = record") : F("Tourner = mesure"));
}

void dessinerEcranBPM() {
  if (modeInterface == INTERFACE_MENU) {
    dessinerMenuOLED();
    return;
  }

  if (modeInterface == INTERFACE_HISTORIQUE) {
    dessinerHistoriqueOLED();
    return;
  }

  char ligneDate[22];
  char ligneHeure[22];
  char texteBPM[8];
  char texteZoom[20];

  oledBPM.setFont(u8g2_font_6x10_tf);
  dessinerTexteFlash(oledBPM, 0, 9, F("CARDIO ECE"));
  oledBPM.drawHLine(0, 11, 128);

  if (heureOLEDValide) {
    snprintf_P(
      ligneDate, sizeof(ligneDate),
      langueAnglaise ? PSTR("Date: %02u/%02u/20%02u") : PSTR("Date : %02u/%02u/20%02u"),
      (unsigned)heureOLED.Day(),
      (unsigned)heureOLED.Month(),
      (unsigned)(heureOLED.Year() % 100)
    );

    snprintf_P(
      ligneHeure, sizeof(ligneHeure),
      langueAnglaise ? PSTR("Time: %02u:%02u:%02u") : PSTR("Heure : %02u:%02u:%02u"),
      (unsigned)heureOLED.Hour(),
      (unsigned)heureOLED.Minute(),
      (unsigned)heureOLED.Second()
    );
  } else {
    strcpy_P(
      ligneDate,
      langueAnglaise ? PSTR("Date: unavailable") : PSTR("Date : indisponible")
    );
    strcpy_P(
      ligneHeure,
      langueAnglaise ? PSTR("Time: unavailable") : PSTR("Heure : indisponible")
    );
  }

  oledBPM.drawStr(0, 22, ligneDate);
  oledBPM.drawStr(0, 33, ligneHeure);

  dessinerTexteFlash(oledBPM, 75, 44,
    bpmValide() ?
      (langueAnglaise ?
        (bpm < 60 ? F("LOW") : (bpm <= 100 ? F("NORMAL") : F("HIGH"))) :
        (bpm < 60 ? F("BAS") : (bpm <= 100 ? F("NORMAL") : F("ELEVE")))) :
      F("--")
  );

  oledBPM.setFont(u8g2_font_logisoso20_tn);

  if (bpmValide()) {
    snprintf_P(
      texteBPM, sizeof(texteBPM),
      PSTR("%d"), (int)(bpm + 0.5f)
    );
  } else {
    strcpy_P(texteBPM, PSTR("--"));
  }

  oledBPM.drawStr(0, 61, texteBPM);
  oledBPM.setFont(u8g2_font_6x10_tf);

  snprintf_P(
    texteZoom, sizeof(texteZoom),
    langueAnglaise ? PSTR("Sound:%s Zx%u") : PSTR("Son:%s Zx%u"),
    buzzerActif ? "ON" : "OFF",
    (unsigned)echelleGraphique
  );
  oledBPM.drawStr(76, 61, texteZoom);
}

// --------------------------------------------------
// OLED 2 : PPG + ECHELLE ENCODEUR - MODULE AJOUTE
// --------------------------------------------------

// On lit la meme suite de valeurs que le code initial.
uint16_t lirePointImagePPG(byte numero) {
  byte indiceRelatif = debutImagePPG + numero * echelleGraphique;
  byte indice = (nombrePointsPPG == POINTS_PPG)
    ? (byte)((positionPPG + indiceRelatif) % POINTS_PPG)
    : indiceRelatif;
  return historiquePPG[indice];
}

void preparerImagePPG() {
  byte pas = echelleGraphique;

  if (nombrePointsPPG == 0) {
    nombrePointsImage = 0;
  } else {
    nombrePointsImage = (nombrePointsPPG + pas - 1) / pas;
  }

  minimumImage = 1023;
  maximumImage = 0;
  seuilImage = seuilActuel;

  if (nombrePointsImage > 0) {
    debutImagePPG = nombrePointsPPG - 1 -
      (nombrePointsImage - 1) * pas;
  } else {
    debutImagePPG = 0;
  }

  for (byte i = 0; i < nombrePointsImage; i++) {
    uint16_t valeur = lirePointImagePPG(i);

    if (valeur < minimumImage) minimumImage = valeur;
    if (valeur > maximumImage) maximumImage = valeur;
  }

  if (maximumImage - minimumImage < 10)
    maximumImage = minimumImage + 10;

  // Figer l'image a chaque cycle OLED, avec une seule coordonnee Y par point.
  int plage = maximumImage - minimumImage;
  for (byte i = 0; i < nombrePointsImage; i++) {
    imagePPG[i] = 61 -
      (long)(lirePointImagePPG(i) - minimumImage) * 45 / plage;
  }
}

void dessinerEcranPPG() {
  char titre[24];

  snprintf_P(
    titre, sizeof(titre),
    PSTR("PPG %u ms/pixel"),
    (unsigned)(20 * echelleGraphique)
  );

  oledPPG.setFont(u8g2_font_6x12_tf);
  oledPPG.drawStr(0, 10, titre);
  oledPPG.drawHLine(0, 13, 128);

  if (nombrePointsImage < 2) {
    dessinerTexteFlash(oledPPG, 15, 36, F("Attente signal..."));
    return;
  }

  int plage = maximumImage - minimumImage;

  if (seuilImage >= minimumImage &&
      seuilImage <= maximumImage) {
    int ys =
      61 -
      (long)(seuilImage - minimumImage) *
      45 / plage;

    for (byte x = 0; x < 128; x += 8)
      oledPPG.drawHLine(x, ys, 3);
  }

  int departX = 128 - nombrePointsImage;

  for (byte i = 1; i < nombrePointsImage; i++) {
    oledPPG.drawLine(
      departX + i - 1, imagePPG[i - 1],
      departX + i, imagePPG[i]
    );
  }
}

// --------------------------------------------------
// RAFRAICHISSEMENT OLED - INCHANGE
// --------------------------------------------------

void gererOLED(unsigned long maintenant) {
  if ((long)(maintenant - prochainPageOLED) < 0)
    return;

  prochainPageOLED =
    maintenant + PAUSE_PAGE_OLED;

  if (etapeOLED == 0) {
    if (maintenant - dernierCycleOLED <
        PERIODE_CYCLE_OLED)
      return;

    dernierCycleOLED = maintenant;
    oledBPM.firstPage();
    etapeOLED = 1;
  }

  if (etapeOLED == 1) {
    dessinerEcranBPM();

    if (!oledBPM.nextPage()) {
      preparerImagePPG();
      oledPPG.firstPage();
      etapeOLED = 2;
    }
  } else if (etapeOLED == 2) {
    dessinerEcranPPG();

    if (!oledPPG.nextPage())
      etapeOLED = 0;
  }
}

// --------------------------------------------------
// SETUP
// --------------------------------------------------

void setup() {
  Serial.begin(115200);

  pinMode(PIN_CAPTEUR, INPUT);
  pinMode(PIN_LED_ROUGE, OUTPUT);
  pinMode(PIN_LED_VERTE, OUTPUT);
  pinMode(PIN_LED_JAUNE, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_BOUTON_BUZZER, INPUT_PULLUP);

  // AJOUT MODULES : encodeur rotatif
  pinMode(PIN_ENCODEUR_A, INPUT_PULLUP);
  pinMode(PIN_ENCODEUR_B, INPUT_PULLUP);

  pinMode(PIN_SAUVEGARDER, INPUT_PULLUP);
  pinMode(PIN_EFFACER, INPUT_PULLUP);

  eteindreToutesLesLEDs();
  noTone(PIN_BUZZER);

  Wire.begin();
  oledBPM.begin();
  oledPPG.begin();

  // RTC : on ne modifie pas l'heure deja reglee
  rtc.Begin();

  heureOLEDValide = false;
  prochaineLectureRTC = 0;
  mettreAJourHeureOLED(millis());

  ancienEtatEncodeur = lireEtatEncodeur();
  attachInterrupt(
    digitalPinToInterrupt(PIN_ENCODEUR_A),
    interruptionEncodeur,
    CHANGE
  );
  attachInterrupt(
    digitalPinToInterrupt(PIN_ENCODEUR_B),
    interruptionEncodeur,
    CHANGE
  );

  prochaineMesure = millis();
  debutMesure = 0;
  debutFenetre = 0;
  prochainPageOLED = millis();
  dernierCycleOLED =
    millis() - PERIODE_CYCLE_OLED;

  Serial.println(F("TEST BPM INITIALISE"));
  Serial.println(
    F("Format : temps_ms signal_filtre seuil BPM")
  );
  Serial.println(F("A0 : WPSE340"));
  Serial.println(
    F("D4 : activer/desactiver le buzzer")
  );
  Serial.println(
    F("OLED 1 : A4/A5 ; OLED 2 : D8/D9")
  );

  initialiserMemoire();

  Serial.println(
    F("A1 : sauver moyenne de 10 BPM")
  );
  Serial.println(
    F("A2 2 secondes : effacer EEPROM ; H : consulter")
  );
  Serial.println(
    F("Encodeur : zoom PPG ; appui long : menu OLED")
  );
}

// --------------------------------------------------
// LOOP
// --------------------------------------------------

void loop() {
  unsigned long maintenant = millis();

  if ((long)(maintenant - prochaineMesure) >= 0) {
    prochaineMesure += PERIODE_ECHANTILLONNAGE;

    traiterUneMesure(maintenant);
    //afficherTrace(maintenant);
  }

  // AJOUT MODULES : interface non bloquante
  gererEncodeur();
  gererBoutonEtMenu(maintenant);
  mettreAJourHeureOLED(maintenant);
  gererBoutonsEEPROM(millis());
  gererConsultationEEPROM(millis());
  gererOLED(millis());
}
