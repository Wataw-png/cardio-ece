#include <Wire.h>
#include <U8g2lib.h>
#include <Ds1302.h>
#include <math.h>

// =========================
// Broches
// =========================
const byte PIN_PPG = A0;

const byte PIN_RTC_RST = 5;
const byte PIN_RTC_CLK = 6;
const byte PIN_RTC_DAT = 7;

// =========================
// RTC DS1302
// =========================
Ds1302 rtc(PIN_RTC_RST, PIN_RTC_CLK, PIN_RTC_DAT);

// =========================
// OLED
// OLED 1 : I2C matériel
// OLED 2 : I2C logiciel
// =========================
U8G2_SSD1306_128X64_NONAME_1_HW_I2C oled1(
  U8G2_R0,
  U8X8_PIN_NONE
);

U8G2_SSD1306_128X64_NONAME_1_SW_I2C oled2(
  U8G2_R0,
  8,                 // SCL sur D8
  9,                 // SDA sur D9
  U8X8_PIN_NONE
);

// =========================
// Variables PPG
// =========================
float signalLisse = 0;
float ligneBase = 0;
float enveloppe = 0;

bool signalAuDessus = false;

unsigned long dernierBattement = 0;
unsigned long debutMesure = 0;

float bpm = 0;

// Intervalles entre battements
unsigned long intervalles[5] = {0, 0, 0, 0, 0};
byte nombreIntervalles = 0;

// Courbe PPG
uint16_t courbe[128];
byte positionCourbe = 0;
byte nombrePoints = 0;

// Temporisations
unsigned long dernierEchantillon = 0;
unsigned long dernierAffichage = 0;
unsigned long derniereLectureRTC = 0;

// Dernière date/heure connue
Ds1302::DateTime maintenant;

// =========================
// Fonctions d'affichage
// =========================
void afficherDeuxChiffres(byte valeur) {
  if (valeur < 10) {
    Serial.print('0');
  }
  Serial.print(valeur);
}

void afficherBpmOLED1() {
  if (bpm >= 40 && bpm <= 180) {
    oled1.print("BPM : ");
    oled1.print((int)(bpm + 0.5));
  } else {
    oled1.print("BPM : --");
  }
}

void afficherBpmOLED2() {
  if (bpm >= 40 && bpm <= 180) {
    oled2.print("BPM ");
    oled2.print((int)(bpm + 0.5));
  } else {
    oled2.print("BPM --");
  }
}

// =========================
// Détection du rythme
// =========================
void traiterSignalPPG(uint16_t valeurBrute) {
  if (debutMesure == 0) {
    debutMesure = millis();
    signalLisse = valeurBrute;
    ligneBase = valeurBrute;
  }

  // Filtre passe-bas simple
  signalLisse = 0.75 * signalLisse + 0.25 * valeurBrute;

  // Suivi lent de la ligne de base
  ligneBase = 0.99 * ligneBase + 0.01 * signalLisse;

  // Composante pulsatile
  float composanteAC = signalLisse - ligneBase;

  // Estimation de l'amplitude du signal
  enveloppe = 0.95 * enveloppe + 0.05 * fabs(composanteAC);

  // Seuil adaptatif
  float seuil = enveloppe * 0.55;

  if (seuil < 18) {
    seuil = 18;
  }

  bool auDessus = composanteAC > seuil;
  unsigned long maintenantMillis = millis();

  // Détection d'un front montant
  if (auDessus && !signalAuDessus) {
    unsigned long tempsDepuisDernierBattement =
      maintenantMillis - dernierBattement;

    // Temps réfractaire pour éviter les doubles détections
    if (dernierBattement == 0 || tempsDepuisDernierBattement > 350) {

      if (dernierBattement != 0 &&
          tempsDepuisDernierBattement >= 400 &&
          tempsDepuisDernierBattement <= 1500) {

        intervalles[nombreIntervalles % 5] =
          tempsDepuisDernierBattement;

        if (nombreIntervalles < 5) {
          nombreIntervalles++;
        }

        unsigned long somme = 0;

        for (byte i = 0; i < nombreIntervalles; i++) {
          somme += intervalles[i];
        }

        float intervalleMoyen =
          (float)somme / nombreIntervalles;

        bpm = 60000.0 / intervalleMoyen;
      }

      dernierBattement = maintenantMillis;
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

// =========================
// Affichage OLED 1
// =========================
void afficherOLED1() {
  oled1.firstPage();

  do {
    oled1.setFont(u8g2_font_6x10_tf);

    oled1.drawStr(0, 10, "CARDIO - MONITEUR");

    oled1.setCursor(0, 25);
    oled1.print("Date : 20");

    if (maintenant.year < 10) {
      oled1.print('0');
    }
    oled1.print(maintenant.year);

    oled1.print("-");
    if (maintenant.month < 10) {
      oled1.print('0');
    }
    oled1.print(maintenant.month);

    oled1.print("-");
    if (maintenant.day < 10) {
      oled1.print('0');
    }
    oled1.print(maintenant.day);

    oled1.setCursor(0, 39);
    oled1.print("Heure : ");

    if (maintenant.hour < 10) {
      oled1.print('0');
    }
    oled1.print(maintenant.hour);
    oled1.print(":");

    if (maintenant.minute < 10) {
      oled1.print('0');
    }
    oled1.print(maintenant.minute);
    oled1.print(":");

    if (maintenant.second < 10) {
      oled1.print('0');
    }
    oled1.print(maintenant.second);

    oled1.setCursor(0, 55);
    afficherBpmOLED1();

  } while (oled1.nextPage());
}

// =========================
// Affichage OLED 2
// =========================
void afficherOLED2() {
  oled2.firstPage();

  do {
    oled2.setFont(u8g2_font_6x10_tf);

    oled2.drawStr(0, 9, "SIGNAL PPG");

    oled2.setCursor(78, 9);
    afficherBpmOLED2();

    // Ligne horizontale de référence
    oled2.drawLine(0, 57, 127, 57);

    if (nombrePoints >= 2) {
      uint16_t minimum = 1023;
      uint16_t maximum = 0;

      for (byte i = 0; i < nombrePoints; i++) {
        byte index = (positionCourbe + i) % 128;

        if (courbe[index] < minimum) {
          minimum = courbe[index];
        }

        if (courbe[index] > maximum) {
          maximum = courbe[index];
        }
      }

      if (maximum - minimum < 10) {
        maximum = minimum + 10;
      }

      for (byte x = 1; x < nombrePoints && x < 128; x++) {
        byte index1 = (positionCourbe + x - 1) % 128;
        byte index2 = (positionCourbe + x) % 128;

        int y1 = map(courbe[index1], minimum, maximum, 52, 14);
        int y2 = map(courbe[index2], minimum, maximum, 52, 14);

        oled2.drawLine(x - 1, y1, x, y2);
      }
    }

  } while (oled2.nextPage());
}

// =========================
// Affichage série
// =========================
void afficherSerial() {
  Serial.print("20");

  if (maintenant.year < 10) {
    Serial.print('0');
  }
  Serial.print(maintenant.year);

  Serial.print("-");

  if (maintenant.month < 10) {
    Serial.print('0');
  }
  Serial.print(maintenant.month);

  Serial.print("-");

  if (maintenant.day < 10) {
    Serial.print('0');
  }
  Serial.print(maintenant.day);

  Serial.print(" ");

  if (maintenant.hour < 10) {
    Serial.print('0');
  }
  Serial.print(maintenant.hour);

  Serial.print(":");

  if (maintenant.minute < 10) {
    Serial.print('0');
  }
  Serial.print(maintenant.minute);

  Serial.print(":");

  if (maintenant.second < 10) {
    Serial.print('0');
  }
  Serial.print(maintenant.second);

  Serial.print(" | BPM : ");

  if (bpm >= 40 && bpm <= 180) {
    Serial.println((int)(bpm + 0.5));
  } else {
    Serial.println("--");
  }
}

// =========================
// Initialisation
// =========================
void setup() {
  Serial.begin(9600);

  pinMode(PIN_PPG, INPUT);

  // Initialisation RTC
  rtc.init();

  // Initialisation des deux bus OLED
  Wire.begin();
  Wire.setClock(100000);

  oled1.setI2CAddress(0x3C * 2);
  oled1.begin();

  oled2.setI2CAddress(0x3C * 2);
  oled2.begin();

  oled1.setContrast(180);
  oled2.setContrast(180);

  oled1.clearBuffer();
  oled1.sendBuffer();

  oled2.clearBuffer();
  oled2.sendBuffer();

  // Initialisation de la courbe
  for (byte i = 0; i < 128; i++) {
    courbe[i] = 512;
  }

  Serial.println("Systeme cardio initialise");
  Serial.println("WPSE340 : A0");
  Serial.println("RTC : RST D5 | CLK D6 | DAT D7");
  Serial.println("OLED 1 : SDA A4 | SCL A5");
  Serial.println("OLED 2 : SDA D9 | SCL D8");
}

// =========================
// Boucle principale
// =========================
void loop() {
  unsigned long tempsActuel = millis();

  // Lecture du capteur toutes les 20 ms
  if (tempsActuel - dernierEchantillon >= 20) {
    dernierEchantillon = tempsActuel;

    uint16_t valeurPPG = analogRead(PIN_PPG);
    traiterSignalPPG(valeurPPG);
  }

  // Lecture RTC deux fois par seconde
  if (tempsActuel - derniereLectureRTC >= 500) {
    derniereLectureRTC = tempsActuel;

    rtc.getDateTime(&maintenant);
  }

  // Rafraîchissement des écrans toutes les 100 ms
  if (tempsActuel - dernierAffichage >= 100) {
    dernierAffichage = tempsActuel;

    afficherOLED1();
    afficherOLED2();

    afficherSerial();
  }
}