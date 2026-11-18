const int pinCapteur = A0;

void setup() {
  Serial.begin(115200);
}

void loop() {
  Serial.println(analogRead(pinCapteur));
  delay(20);
}