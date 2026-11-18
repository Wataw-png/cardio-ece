#include <Ds1302.h>

const int PIN_RST = 5;
const int PIN_CLK = 6;
const int PIN_DAT = 7;

Ds1302 rtc(PIN_RST, PIN_CLK, PIN_DAT);

// Mets true pour régler l'heure une seule fois.
// Remets false après le premier téléversement.
const bool REGLER_HEURE = false;

void afficherDeuxChiffres(int valeur) {
  if (valeur < 10) {
    Serial.print('0');
  }
  Serial.print(valeur);
}

void setup() {
  Serial.begin(9600);
  rtc.init();

  if (REGLER_HEURE) {
    Ds1302::DateTime dateHeure = {
      .year = 26,
      .month = Ds1302::MONTH_OCT,
      .day = 6,
      .hour = 2,
      .minute = 03,
      .second = 0,
      .dow = Ds1302::DOW_TUE
    };

    rtc.setDateTime(&dateHeure);
  }
}

void loop() {
  Ds1302::DateTime maintenant;
  rtc.getDateTime(&maintenant);

  static byte derniereSeconde = 255;

  if (maintenant.second != derniereSeconde) {
    derniereSeconde = maintenant.second;

    Serial.print("20");
    afficherDeuxChiffres(maintenant.year);
    Serial.print("-");

    afficherDeuxChiffres(maintenant.month);
    Serial.print("-");

    afficherDeuxChiffres(maintenant.day);
    Serial.print(" ");

    afficherDeuxChiffres(maintenant.hour);
    Serial.print(":");

    afficherDeuxChiffres(maintenant.minute);
    Serial.print(":");

    afficherDeuxChiffres(maintenant.second);
    Serial.println();
  }

  delay(100);
}