const byte CAPTEUR = A0;

const unsigned long PERIODE = 20;
const byte TAILLE_FILTRE = 5;
const byte NB_INTERValles = 5;

int buffer[TAILLE_FILTRE];
byte indiceFiltre = 0;
long somme = 0;
bool filtreInitialise = false;

unsigned long prochaineMesure;
unsigned long debutMesure;
unsigned long debutFenetre;

int minimumSignal = 1023;
int maximumSignal = 0;

unsigned long dernierBattement = 0;
bool precedentAuDessus = false;

unsigned long intervalles[NB_INTERValles];
byte nombreIntervalles = 0;
byte indiceIntervalle = 0;

float calculerBPM() {
  if (nombreIntervalles == 0) {
    return 0;
  }

  float valeurs[NB_INTERValles];

  for (byte i = 0; i < nombreIntervalles; i++) {
    valeurs[i] = 60000.0 / intervalles[i];
  }

  for (byte i = 0; i < nombreIntervalles - 1; i++) {
    for (byte j = i + 1; j < nombreIntervalles; j++) {
      if (valeurs[j] < valeurs[i]) {
        float temporaire = valeurs[i];
        valeurs[i] = valeurs[j];
        valeurs[j] = temporaire;
      }
    }
  }

  return valeurs[nombreIntervalles / 2];
}

void setup() {
  Serial.begin(115200);

  prochaineMesure = millis();
  debutMesure = millis();
  debutFenetre = millis();
}

void loop() {
  unsigned long maintenant = millis();

  if ((long)(maintenant - prochaineMesure) < 0) {
    return;
  }

  prochaineMesure += PERIODE;

  int brut = analogRead(CAPTEUR);

  if (!filtreInitialise) {
    for (byte i = 0; i < TAILLE_FILTRE; i++) {
      buffer[i] = brut;
    }

    somme = (long)brut * TAILLE_FILTRE;
    filtreInitialise = true;
  } else {
    somme -= buffer[indiceFiltre];
    buffer[indiceFiltre] = brut;
    somme += brut;

    indiceFiltre++;
    if (indiceFiltre >= TAILLE_FILTRE) {
      indiceFiltre = 0;
    }
  }

  int filtre = somme / TAILLE_FILTRE;

  if (maintenant - debutFenetre >= 4000) {
    minimumSignal = filtre;
    maximumSignal = filtre;
    debutFenetre = maintenant;
  }

  if (filtre < minimumSignal) minimumSignal = filtre;
  if (filtre > maximumSignal) maximumSignal = filtre;

  int amplitude = maximumSignal - minimumSignal;
  int seuil = minimumSignal + 0.65 * amplitude;

  bool auDessus = amplitude > 30 && filtre > seuil;
  bool detectionAutorisee = maintenant - debutMesure >= 4000;

  if (detectionAutorisee && auDessus && !precedentAuDessus) {
    if (dernierBattement == 0) {
      dernierBattement = maintenant;
    } else {
      unsigned long intervalle = maintenant - dernierBattement;

      if (intervalle >= 500 && intervalle <= 1500) {
        intervalles[indiceIntervalle] = intervalle;

        indiceIntervalle++;
        if (indiceIntervalle >= NB_INTERValles) {
          indiceIntervalle = 0;
        }

        if (nombreIntervalles < NB_INTERValles) {
          nombreIntervalles++;
        }

        dernierBattement = maintenant;
      } else if (intervalle > 1500) {
        dernierBattement = maintenant;
      }
    }
  }

  precedentAuDessus = auDessus;

  float bpm = calculerBPM();

  Serial.print(filtre);
  Serial.print('\t');
  Serial.print(seuil);
  Serial.print('\t');
  Serial.println(bpm, 2);
}