#include <Wire.h>
#include <U8g2lib.h>
#include <Ds1302.h>
#include <EEPROM.h>
#include <math.h>

// =====================================================
// BROCHES
// =====================================================
const byte PIN_PPG = A0;

const byte PIN_RTC_RST = 5;
const byte PIN_RTC_CLK = 6;
const byte PIN_RTC_DAT = 7;

const byte PIN_LED_ROUGE = 10;
const byte PIN_LED_VERTE = 11;
const byte PIN_LED_JAUNE = 12;

const byte PIN_BUZZER = 13;

const byte PIN_ENC_A = 2;
const byte PIN_ENC_B = 3;
const byte PIN_ENC_SW = 4;

const byte PIN_BOUTON_SAVE = A1;
const byte PIN_BOUTON_EFFACE = A2;

// =====================================================
// OBJETS
// =====================================================
Ds1302 rtc(PIN_RTC_RST, PIN_RTC_CLK, PIN_RTC_DAT);

U8G2_SSD1306_128X64_NONAME_1_HW_I2C oled1(
  U8G2_R0,
  U8X8_PIN_NONE
);

U8G2_SSD1306_128X64_NONAME_1_SW_I2C oled2(
  U8G2_R0,
  8,                 // SCL OLED 2
  9,                 // SDA OLED 2
  U8X8_PIN_NONE
);

// =====================================================
// VARIABLES RTC
// =====================================================
Ds1302::DateTime dateHeure;

// =====================================================
// VARIABLES SIGNAL PPG
// =====================================================
float signalLisse = 0;
float ligneBase = 0;
float enveloppe = 0;

bool signalAuDessus = false;
unsigned long dernierBattement = 0;

float bpm = 0;
bool nouveauBattement = false;

unsigned long intervalles[5] = {0, 0, 0, 0, 0};
byte nombreIntervalles = 0;

// Courbe PPG
uint16_t courbe[128];
byte positionCourbe = 0;
byte nombrePoints = 0;

// Échelle de la courbe
byte echelleGraphique = 1;

// =====================================================
// VARIABLES TEMPS
// =====================================================
unsigned long dernierEchantillon = 0;
unsigned long dernierAffichage = 0;
unsigned long derniereLectureRTC = 0;
unsigned long dernierSerial = 0;

// =====================================================
// BUZZER
// =====================================================
bool buzzerActif = true;

// =====================================================
// ENCODEUR
// =====================================================
byte ancienEtatEncodeur = 0;

// =====================================================
// EEPROM
// =====================================================
const uint16_t SIGNATURE_EEPROM = 0xCAFE;
const byte NOMBRE_SAUVEGARDES = 10;

struct Enregistrement {
  uint16_t signature;
  uint16_t bpm;
  byte annee;
  byte mois;
  byte jour;
  byte heure;
  byte minute;
  byte seconde;
};

const int TAILLE_ENREGISTREMENT = sizeof(Enregistrement);

// =====================================================
// OUTILS
// =====================================================
void afficherDeuxChiffresOLED(byte valeur) {
  if (valeur < 10) {
    oled1.print('0');
  }
  oled1.print(valeur);
}

void eteindreLEDs() {
  digitalWrite(PIN_LED_ROUGE, LOW);
  digitalWrite(PIN_LED_VERTE, LOW);
  digitalWrite(PIN_LED_JAUNE, LOW);
}

bool bpmValide() {
  return bpm >= 40 && bpm <= 180;
}

// =====================================================
// LEDS
// =====================================================
void gererLEDs() {
  eteindreLEDs();

  if (!bpmValide()) {
    return;
  }

  if (bpm < 60) {
    digitalWrite(PIN_LED_JAUNE, HIGH);
  } else if (bpm <= 100) {
    digitalWrite(PIN_LED_VERTE, HIGH);
  } else {
    digitalWrite(PIN_LED_ROUGE, HIGH);
  }
}

// =====================================================
// BUZZER
// =====================================================
void jouerBipCardiaque() {
  if (!buzzerActif || !bpmValide()) {
    return;
  }

  int frequence;

  if (bpm < 60) {
    frequence = 300;
  } else if (bpm <= 100) {
    frequence = 600;
  } else {
    frequence = 1000;
  }

  tone(PIN_BUZZER, frequence, 60);
}

// =====================================================
// TRAITEMENT DU SIGNAL PPG
// =====================================================
void traiterSignalPPG(uint16_t valeurBrute) {
  if (ligneBase == 0) {
    signalLisse = valeurBrute;
    ligneBase = valeurBrute;
  }

  // Filtrage passe-bas
  signalLisse = 0.75 * signalLisse + 0.25 * valeurBrute;

  // Suivi de la ligne de base
  ligneBase = 0.99 * ligneBase + 0.01 * signalLisse;

  // Composante pulsatile
  float composanteAC = signalLisse - ligneBase;

  // Enveloppe du signal
  enveloppe = 0.95 * enveloppe + 0.05 * fabs(composanteAC);

  // Seuil adaptatif
  float seuil = enveloppe * 0.55;

  if (seuil < 18) {
    seuil = 18;
  }

  bool auDessus = composanteAC > seuil;
  unsigned long maintenant = millis();

  // Détection d'un front montant
  if (auDessus && !signalAuDessus) {
    unsigned long intervalle = maintenant - dernierBattement;

    if (dernierBattement == 0 || intervalle > 350) {
      if (dernierBattement != 0 &&
          intervalle >= 400 &&
          intervalle <= 1500) {

        intervalles[nombreIntervalles % 5] = intervalle;

        if (nombreIntervalles < 5) {
          nombreIntervalles++;
        }

        unsigned long somme = 0;

        for (byte i = 0; i < nombreIntervalles; i++) {
          somme += intervalles[i];
        }

        float moyenne = (float)somme / nombreIntervalles;
        bpm = 60000.0 / moyenne;

        nouveauBattement = true;
      }

      dernierBattement = maintenant;
    }
  }

  signalAuDessus = auDessus;

  // Enregistrement de la courbe
  courbe[positionCourbe] = valeurBrute;
  positionCourbe++;

  if (positionCourbe >= 128) {
    positionCourbe = 0;
  }

  if (nombrePoints < 128) {
    nombrePoints++;
  }
}

// =====================================================
// ENCODEUR
// =====================================================
byte lireEtatEncodeur() {
  byte a = digitalRead(PIN_ENC_A);
  byte b = digitalRead(PIN_ENC_B);

  return (a << 1) | b;
}

void gererEncodeur() {
  static const int8_t tableTransitions[16] = {
    0, -1, 1, 0,
    1, 0, 0, -1,
   -1, 0, 0, 1,
    0, 1, -1, 0
  };

  byte nouvelEtat = lireEtatEncodeur();

  if (nouvelEtat != ancienEtatEncodeur) {
    byte index = (ancienEtatEncodeur << 2) | nouvelEtat;
    int variation = tableTransitions[index];

    if (variation > 0 && echelleGraphique < 4) {
      echelleGraphique++;
    }

    if (variation < 0 && echelleGraphique > 1) {
      echelleGraphique--;
    }

    ancienEtatEncodeur = nouvelEtat;
  }
}

// =====================================================
// EEPROM
// =====================================================
bool emplacementValide(byte index) {
  Enregistrement rec;

  int adresse = index * TAILLE_ENREGISTREMENT;
  EEPROM.get(adresse, rec);

  return rec.signature == SIGNATURE_EEPROM;
}

void sauvegarderMesure() {
  if (!bpmValide()) {
    Serial.println(F("Sauvegarde impossible : BPM invalide"));
    return;
  }

  Enregistrement rec;

  rec.signature = SIGNATURE_EEPROM;
  rec.bpm = (uint16_t)(bpm + 0.5);
  rec.annee = dateHeure.year;
  rec.mois = dateHeure.month;
  rec.jour = dateHeure.day;
  rec.heure = dateHeure.hour;
  rec.minute = dateHeure.minute;
  rec.seconde = dateHeure.second;

  byte emplacement = 0;

  for (byte i = 0; i < NOMBRE_SAUVEGARDES; i++) {
    if (!emplacementValide(i)) {
      emplacement = i;
      break;
    }
  }

  int adresse = emplacement * TAILLE_ENREGISTREMENT;
  EEPROM.put(adresse, rec);

  Serial.print(F("Mesure sauvegardee dans EEPROM, emplacement "));
  Serial.println(emplacement);
}

void effacerEEPROM() {
  for (byte i = 0; i < NOMBRE_SAUVEGARDES; i++) {
    int adresse = i * TAILLE_ENREGISTREMENT;
    EEPROM.update(adresse, 0);
    EEPROM.update(adresse + 1, 0);
  }

  Serial.println(F("EEPROM effacee"));
}

// =====================================================
// BOUTONS
// =====================================================
void gererBoutons() {
  static bool ancienEtatSW = HIGH;
  static bool ancienEtatSave = HIGH;
  static bool ancienEtatEfface = HIGH;

  static unsigned long dernierAppuiSW = 0;
  static unsigned long dernierAppuiSave = 0;
  static unsigned long dernierAppuiEfface = 0;

  unsigned long maintenant = millis();

  bool etatSW = digitalRead(PIN_ENC_SW);
  bool etatSave = digitalRead(PIN_BOUTON_SAVE);
  bool etatEfface = digitalRead(PIN_BOUTON_EFFACE);

  // Bouton encodeur : active/désactive le buzzer
  if (etatSW == LOW &&
      ancienEtatSW == HIGH &&
      maintenant - dernierAppuiSW > 250) {

    buzzerActif = !buzzerActif;
    noTone(PIN_BUZZER);

    Serial.print(F("Buzzer : "));
    Serial.println(buzzerActif ? F("ACTIF") : F("DESACTIVE"));

    dernierAppuiSW = maintenant;
  }

  // Bouton A1 : sauvegarde EEPROM
  if (etatSave == LOW &&
      ancienEtatSave == HIGH &&
      maintenant - dernierAppuiSave > 250) {

    sauvegarderMesure();
    dernierAppuiSave = maintenant;
  }

  // Bouton A2 : effacement EEPROM
  if (etatEfface == LOW &&
      ancienEtatEfface == HIGH &&
      maintenant - dernierAppuiEfface > 250) {

    effacerEEPROM();
    dernierAppuiEfface = maintenant;
  }

  ancienEtatSW = etatSW;
  ancienEtatSave = etatSave;
  ancienEtatEfface = etatEfface;
}

// =====================================================
// OLED 1 : DATE, HEURE, BPM
// =====================================================
void afficherOLED1() {
  oled1.firstPage();

  do {
    oled1.setFont(u8g2_font_6x10_tf);

    oled1.drawStr(0, 9, "CARDIO - MONITEUR");

    oled1.setCursor(0, 22);
    oled1.print("Date: 20");
    if (dateHeure.year < 10) oled1.print('0');
    oled1.print(dateHeure.year);
    oled1.print('-');
    afficherDeuxChiffresOLED(dateHeure.month);
    oled1.print('-');
    afficherDeuxChiffresOLED(dateHeure.day);

    oled1.setCursor(0, 35);
    oled1.print("Heure: ");
    afficherDeuxChiffresOLED(dateHeure.hour);
    oled1.print(':');
    afficherDeuxChiffresOLED(dateHeure.minute);
    oled1.print(':');
    afficherDeuxChiffresOLED(dateHeure.second);

    oled1.setCursor(0, 49);
    oled1.print("BPM: ");

    if (bpmValide()) {
      oled1.print((int)(bpm + 0.5));
    } else {
      oled1.print("--");
    }

    oled1.setCursor(0, 62);
    oled1.print("Buzzer: ");
    oled1.print(buzzerActif ? "ON" : "OFF");

  } while (oled1.nextPage());
}

// =====================================================
// OLED 2 : COURBE PPG
// =====================================================
void afficherOLED2() {
  oled2.firstPage();

  do {
    oled2.setFont(u8g2_font_6x10_tf);

    oled2.drawStr(0, 9, "SIGNAL PPG");

    oled2.setCursor(78, 9);
    oled2.print("BPM ");

    if (bpmValide()) {
      oled2.print((int)(bpm + 0.5));
    } else {
      oled2.print("--");
    }

    oled2.drawLine(0, 57, 127, 57);

    if (nombrePoints >= 2) {
      uint16_t minimum = 1023;
      uint16_t maximum = 0;

      for (byte i = 0; i < nombrePoints; i++) {
        byte index = (positionCourbe + i) % 128;

        if (courbe[index] < minimum) minimum = courbe[index];
        if (courbe[index] > maximum) maximum = courbe[index];
      }

      uint16_t centre = (minimum + maximum) / 2;
      uint16_t demiAmplitude = (maximum - minimum) / 2;

      if (demiAmplitude < 5) {
        demiAmplitude = 5;
      }

      demiAmplitude = demiAmplitude / echelleGraphique;

      if (demiAmplitude < 3) {
        demiAmplitude = 3;
      }

      for (byte x = 1; x < nombrePoints && x < 128; x++) {
        byte index1 = (positionCourbe + x - 1) % 128;
        byte index2 = (positionCourbe + x) % 128;

        int y1 = map(
          courbe[index1],
          centre - demiAmplitude,
          centre + demiAmplitude,
          52,
          14
        );

        int y2 = map(
          courbe[index2],
          centre - demiAmplitude,
          centre + demiAmplitude,
          52,
          14
        );

        y1 = constrain(y1, 14, 52);
        y2 = constrain(y2, 14, 52);

        oled2.drawLine(x - 1, y1, x, y2);
      }
    }

  } while (oled2.nextPage());
}

// =====================================================
// MONITEUR SERIE
// =====================================================
void afficherSerial() {
  Serial.print("20");

  if (dateHeure.year < 10) Serial.print('0');
  Serial.print(dateHeure.year);
  Serial.print('-');

  if (dateHeure.month < 10) Serial.print('0');
  Serial.print(dateHeure.month);
  Serial.print('-');

  if (dateHeure.day < 10) Serial.print('0');
  Serial.print(dateHeure.day);
  Serial.print(' ');

  if (dateHeure.hour < 10) Serial.print('0');
  Serial.print(dateHeure.hour);
  Serial.print(':');

  if (dateHeure.minute < 10) Serial.print('0');
  Serial.print(dateHeure.minute);
  Serial.print(':');

  if (dateHeure.second < 10) Serial.print('0');
  Serial.print(dateHeure.second);

  Serial.print(" | BPM: ");

  if (bpmValide()) {
    Serial.println((int)(bpm + 0.5));
  } else {
    Serial.println("--");
  }
}

// =====================================================
// SETUP
// =====================================================
void setup() {
  Serial.begin(9600);

  pinMode(PIN_PPG, INPUT);

  pinMode(PIN_LED_ROUGE, OUTPUT);
  pinMode(PIN_LED_VERTE, OUTPUT);
  pinMode(PIN_LED_JAUNE, OUTPUT);

  pinMode(PIN_BUZZER, OUTPUT);

  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);
  pinMode(PIN_ENC_SW, INPUT_PULLUP);

  pinMode(PIN_BOUTON_SAVE, INPUT_PULLUP);
  pinMode(PIN_BOUTON_EFFACE, INPUT_PULLUP);

  eteindreLEDs();
  noTone(PIN_BUZZER);

  rtc.init();

  Wire.begin();
  Wire.setClock(100000);

  oled1.setI2CAddress(0x3C * 2);
  oled1.begin();
  oled1.setContrast(180);

  oled2.setI2CAddress(0x3C * 2);
  oled2.begin();
  oled2.setContrast(180);

  for (byte i = 0; i < 128; i++) {
    courbe[i] = 512;
  }

  ancienEtatEncodeur = lireEtatEncodeur();

  Serial.println(F("SYSTEME CARDIO INITIALISE"));
  Serial.println(F("A1 : sauvegarder une mesure"));
  Serial.println(F("A2 : effacer l'EEPROM"));
  Serial.println(F("Bouton encodeur : activer/desactiver le buzzer"));
}

// =====================================================
// LOOP
// =====================================================
void loop() {
  unsigned long maintenant = millis();

  // Échantillonnage du PPG toutes les 20 ms
  if (maintenant - dernierEchantillon >= 20) {
    dernierEchantillon = maintenant;

    uint16_t valeur = analogRead(PIN_PPG);
    traiterSignalPPG(valeur);
  }

  // Lecture RTC
  if (maintenant - derniereLectureRTC >= 500) {
    derniereLectureRTC = maintenant;
    rtc.getDateTime(&dateHeure);
  }

  // Gestion de l'encodeur et des boutons
  gererEncodeur();
  gererBoutons();

  // LED et buzzer lors d'un nouveau battement
  if (nouveauBattement) {
    nouveauBattement = false;

    gererLEDs();
    jouerBipCardiaque();
  }

  // Affichage OLED
  if (maintenant - dernierAffichage >= 150) {
    dernierAffichage = maintenant;

    afficherOLED1();
    afficherOLED2();
  }

  // Affichage série une fois par seconde
  if (maintenant - dernierSerial >= 1000) {
    dernierSerial = maintenant;
    afficherSerial();
  }
}