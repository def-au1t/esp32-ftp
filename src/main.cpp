#include <Arduino.h>

int ledPin = 2; //D6 ustawiamy, do którego pinu jest podłączona dioda

// funkcja setap uruchamia się raz przy uruchomieniu
void setup() {
 pinMode(ledPin, OUTPUT); // ustawiamy pin jako wyjście
 Serial.begin(115200);
}

// funkcja loop uruchamia się w nieskończonej pętli
void loop() {
  digitalWrite(ledPin, HIGH);   // włączmy diodę, podajemy stan wysoki
  delay(1000);                  // czekamy sekundę
  Serial.print("Hello");
  Serial.println("!!");
  digitalWrite(ledPin, LOW);    // wyłączamy diodę, podajemy stan niski
  delay(1000);                  // czekamy sekundę
}
