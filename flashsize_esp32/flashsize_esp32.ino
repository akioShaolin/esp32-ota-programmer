#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  delay(1500);

  uint32_t flashBytes = ESP.getFlashChipSize();

  Serial.println("\n=== Memoria Flash ===");
  Serial.printf("Flash detectada: %lu bytes\n", flashBytes);
  Serial.printf("Flash detectada: %.2f MB\n",
                flashBytes / (1024.0 * 1024.0));

  Serial.printf("Frequencia: %lu MHz\n",
                ESP.getFlashChipSpeed() / 1000000);

  Serial.printf("Tamanho do firmware atual: %.2f MB\n",
                ESP.getSketchSize() / (1024.0 * 1024.0));

  Serial.printf("Espaco maximo para firmware: %.2f MB\n",
                ESP.getFreeSketchSpace() / (1024.0 * 1024.0));
}

void loop() {
}