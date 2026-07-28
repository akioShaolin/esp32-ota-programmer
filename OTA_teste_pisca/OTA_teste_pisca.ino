#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>

const char* ssid = "Shaolin";
const char* password = "12345678";

constexpr uint8_t LED_PIN = LED_BUILTIN;
constexpr unsigned long INTERVALO_LED = 500;  // troca a cada 500 ms = 1 pisca/s

unsigned long ultimoTempoLed = 0;
bool estadoLed = false;

void setup() {
    Serial.begin(115200);

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
    }

    ArduinoOTA.setHostname("esp-ota-teste");

    ArduinoOTA.onStart([]() {
        Serial.println("OTA iniciado");
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Progresso: %u%%\r",
                      static_cast<unsigned int>(
                          (static_cast<uint64_t>(progress) * 100U) / total
                      ));
    });

    ArduinoOTA.onEnd([]() {
        Serial.println("\nOTA concluído");
    });

    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("\nErro OTA: %u\n", error);
    });

    ArduinoOTA.begin();

    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
}

void loop() {
    const unsigned long agora = millis();

    if (agora - ultimoTempoLed >= INTERVALO_LED) {
        ultimoTempoLed = agora;
        estadoLed = !estadoLed;
        digitalWrite(LED_PIN, estadoLed ? HIGH : LOW);
    }
    
    ArduinoOTA.handle();

}