
/*
  CARDIO ECE - TEST UNITAIRE RTC DS1302

  RST -> D5
  CLK -> D6
  DAT -> D7

  Test :
  - Communication RTC
  - Lecture de la date et de l'heure
  - Verification du fonctionnement de l'horloge
  - Conservation de l'heure apres coupure

  Moniteur serie : 115200 bauds
*/

#include <Arduino.h>
#include <ThreeWire.h>
#include <RtcDS1302.h>

// Broches
const byte PIN_RST = 5;
const byte PIN_CLK = 6;
const byte PIN_DAT = 7;

// Initialisation RTC
ThreeWire liaisonRTC(PIN_DAT, PIN_CLK, PIN_RST);
RtcDS1302<ThreeWire> rtc(liaisonRTC);

// Temporisation
unsigned long derniereLecture = 0;
const unsigned long PERIODE_LECTURE = 1000;

// Affichage avec deux chiffres
void afficherDeuxChiffres(int valeur) {
  if (valeur < 10) {
    Serial.print('0');
  }
  Serial.print(valeur);
}

// Initialisation
void setup() {

  Serial.begin(115200);

  rtc.Begin();

  Serial.println();
  Serial.println(F("===== TEST RTC DS1302 ====="));

  // Verification de l'horloge
  if (rtc.IsDateTimeValid()) {
    Serial.println(F("Date RTC : VALIDE"));
  } else {
    Serial.println(F("ATTENTION : date RTC invalide"));
  }

  if (rtc.GetIsRunning()) {
    Serial.println(F("Oscillateur RTC : ACTIF"));
  } else {
    Serial.println(F("ERREUR : oscillateur RTC arrete"));
  }

  Serial.println(F("--------------------------"));
  Serial.println(F("Lecture toutes les secondes"));
  Serial.println();
}

// Boucle
void loop() {

  unsigned long maintenant = millis();

  if (maintenant - derniereLecture >= PERIODE_LECTURE) {

    derniereLecture = maintenant;

    // Lecture de la date et de l'heure
    RtcDateTime dateRTC = rtc.GetDateTime();

    Serial.print(F("Date : "));

    afficherDeuxChiffres(dateRTC.Day());
    Serial.print('/');

    afficherDeuxChiffres(dateRTC.Month());
    Serial.print('/');

    Serial.print(dateRTC.Year());

    Serial.print(F(" | Heure : "));

    afficherDeuxChiffres(dateRTC.Hour());
    Serial.print(':');

    afficherDeuxChiffres(dateRTC.Minute());
    Serial.print(':');

    afficherDeuxChiffres(dateRTC.Second());

    Serial.println();
  }
}
