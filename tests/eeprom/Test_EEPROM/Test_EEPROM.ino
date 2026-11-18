#include <EEPROM.h>

const int ADRESSE_EEPROM = 0;
const uint16_t SIGNATURE = 0xCAFE;

struct Mesure {
  uint16_t signature;
  byte jour;
  byte mois;
  byte annee;
  byte heure;
  byte minute;
  uint16_t bpm;
};

void afficherMesure(const Mesure& mesure) {
  Serial.print("Mesure : ");

  if (mesure.jour < 10) Serial.print('0');
  Serial.print(mesure.jour);
  Serial.print('/');

  if (mesure.mois < 10) Serial.print('0');
  Serial.print(mesure.mois);
  Serial.print("/20");
  Serial.print(mesure.annee);

  Serial.print(" ");
  if (mesure.heure < 10) Serial.print('0');
  Serial.print(mesure.heure);
  Serial.print(':');

  if (mesure.minute < 10) Serial.print('0');
  Serial.print(mesure.minute);

  Serial.print(" - ");
  Serial.print(mesure.bpm);
  Serial.println(" BPM");
}

void ecrireMesureTest() {
  Mesure mesure;

  mesure.signature = SIGNATURE;
  mesure.jour = 7;
  mesure.mois = 10;
  mesure.annee = 26;
  mesure.heure = 16;
  mesure.minute = 45;
  mesure.bpm = 72;

  EEPROM.put(ADRESSE_EEPROM, mesure);

  Serial.println("Mesure ecrite dans l'EEPROM.");
  afficherMesure(mesure);
}

void lireMesure() {
  Mesure mesure;
  EEPROM.get(ADRESSE_EEPROM, mesure);

  if (mesure.signature != SIGNATURE) {
    Serial.println("Aucune mesure valide trouvee.");
  } else {
    Serial.println("Mesure lue depuis l'EEPROM :");
    afficherMesure(mesure);
  }
}

void effacerEEPROM() {
  Mesure mesure;
  mesure.signature = 0;
  EEPROM.put(ADRESSE_EEPROM, mesure);

  Serial.println("EEPROM effacee.");
}

void setup() {
  Serial.begin(9600);
  delay(500);

  Serial.println("=== TEST EEPROM ===");
  lireMesure();

  Serial.println();
  Serial.println("Commandes disponibles :");
  Serial.println("w : ecrire une mesure test");
  Serial.println("r : lire la mesure");
  Serial.println("e : effacer la mesure");
}

void loop() {
  if (Serial.available()) {
    char commande = Serial.read();

    if (commande == 'w') {
      ecrireMesureTest();
    } 
    else if (commande == 'r') {
      lireMesure();
    } 
    else if (commande == 'e') {
      effacerEEPROM();
    }
  }
}