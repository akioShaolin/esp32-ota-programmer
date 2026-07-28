#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>

constexpr char AP_SSID[] = "ESP-OTA-ALVO";
constexpr char AP_PASSWORD[] = "12345678";

constexpr uint8_t LED_PIN = LED_BUILTIN;

// Na maioria dos ESP8266, o LED integrado é ativo em LOW.
constexpr uint8_t LED_ON = LOW;
constexpr uint8_t LED_OFF = HIGH;

uint8_t clientesAnteriores = 0;

void configurarAccessPoint() {
    WiFi.mode(WIFI_AP);

    const bool iniciou = WiFi.softAP(
        AP_SSID,
        AP_PASSWORD
    );

    if (!iniciou) {
        Serial.println("[WiFi] Falha ao criar o access point.");
        return;
    }

    Serial.println();
    Serial.println("[WiFi] Access point criado.");
    Serial.printf("[WiFi] SSID: %s\n", AP_SSID);
    Serial.printf("[WiFi] Senha: %s\n", AP_PASSWORD);
    Serial.printf(
        "[WiFi] IP: %s\n",
        WiFi.softAPIP().toString().c_str()
    );
}

void configurarOTA() {
    ArduinoOTA.setHostname("esp-ota-alvo");
    ArduinoOTA.setPort(8266);

    ArduinoOTA.onStart([]() {
        Serial.println();
        Serial.println("[OTA] Gravação iniciada.");
    });

    ArduinoOTA.onProgress([](
        unsigned int progresso,
        unsigned int total
    ) {
        const unsigned int percentual =
            static_cast<unsigned int>(
                (static_cast<uint64_t>(progresso) * 100U) / total
            );

        Serial.printf(
            "[OTA] Progresso: %u%%\r",
            percentual
        );
    });

    ArduinoOTA.onEnd([]() {
        Serial.println();
        Serial.println("[OTA] Gravação concluída.");
    });

    ArduinoOTA.onError([](ota_error_t erro) {
        Serial.printf("\n[OTA] Erro %u: ", erro);

        switch (erro) {
            case OTA_AUTH_ERROR:
                Serial.println("falha de autenticação");
                break;

            case OTA_BEGIN_ERROR:
                Serial.println("falha ao iniciar");
                break;

            case OTA_CONNECT_ERROR:
                Serial.println("falha de conexão");
                break;

            case OTA_RECEIVE_ERROR:
                Serial.println("falha durante o recebimento");
                break;

            case OTA_END_ERROR:
                Serial.println("falha ao finalizar");
                break;

            default:
                Serial.println("erro desconhecido");
                break;
        }
    });

    ArduinoOTA.begin();

    Serial.println("[OTA] ArduinoOTA iniciado.");
    Serial.println("[OTA] Destino: 192.168.4.1");
    Serial.println("[OTA] Porta: 8266");
}

void atualizarLedPorClientes() {
    const uint8_t clientes = WiFi.softAPgetStationNum();

    digitalWrite(
        LED_PIN,
        clientes > 0 ? LED_ON : LED_OFF
    );

    if (clientes != clientesAnteriores) {
        clientesAnteriores = clientes;

        Serial.printf(
            "[WiFi] Clientes conectados: %u\n",
            clientes
        );
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LED_OFF);

    configurarAccessPoint();
    configurarOTA();
}

void loop() {
    ArduinoOTA.handle();
    atualizarLedPorClientes();

    delay(2);
}