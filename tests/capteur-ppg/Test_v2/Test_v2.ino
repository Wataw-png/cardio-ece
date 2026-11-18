const byte CAPTEUR = A0;
const unsigned long PERIODE = 20;
const byte N = 5;

int valeurs[N] = {0};
byte indice = 0;
long somme = 0;

unsigned long prochaineMesure = 0;
unsigned long debutFenetre = 0;
unsigned long dernierBattement = 0;

int minimumSignal = 1023;
int maximumSignal = 0;
bool precedentAuDessus = false;
float bpm = 0;

void setup() {
  Serial.begin(115200);
  prochaineMesure = millis();
  debutFenetre = millis();
}

void loop() {
  unsigned long maintenant = millis();

  if ((long)(maintenant - prochaineMesure) < 0) {
    return;
  }

  prochaineMesure += PERIODE;

  int brut = analogRead(CAPTEUR);

  somme -= valeurs[indice];
  valeurs[indice] = brut;
  somme += brut;
  indice = (indice + 1) % N;

  int filtre = somme / N;

  if (maintenant - debutFenetre >= 4000) {
    minimumSignal = filtre;
    maximumSignal = filtre;
    debutFenetre = maintenant;
  }

  if (filtre < minimumSignal) minimumSignal = filtre;
  if (filtre > maximumSignal) maximumSignal = filtre;

  int amplitude = maximumSignal - minimumSignal;
  int seuil = minimumSignal + 0.65 * amplitude;

  bool auDessus = amplitude > 20 && filtre > seuil;

  if (auDessus && !precedentAuDessus &&
      maintenant - dernierBattement >= 500) {

    if (dernierBattement != 0) {
      unsigned long intervalle = maintenant - dernierBattement;

      if (intervalle >= 500 && intervalle <= 2000) {
        bpm = 60000.0 / intervalle;
      }
    }

    dernierBattement = maintenant;
  }

  precedentAuDessus = auDessus;

  Serial.print(filtre);
  Serial.print('\t');
  Serial.print(seuil);
  Serial.print('\t');
  Serial.println(bpm);
}