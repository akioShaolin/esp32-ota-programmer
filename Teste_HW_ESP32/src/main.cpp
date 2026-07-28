#include <Arduino.h>
#include <Wire.h>
#include <LittleFS.h>
#include <FS.h>
#include <esp_partition.h>

// GravadorESP 1.0 - diagnóstico do hardware ED100
// LEDs via ULN2003: HIGH = aceso.

namespace Pins {
constexpr uint8_t LED_WIFI_RED = 32;
constexpr uint8_t LED_WIFI_GREEN = 33;
constexpr uint8_t LED_SERVER_RED = 25;
constexpr uint8_t LED_SERVER_GREEN = 26;
constexpr uint8_t LED_RS485_RED = 27;
constexpr uint8_t LED_RS485_GREEN = 13;
constexpr uint8_t BUTTON_RST = 12;
constexpr uint8_t I2C_SDA = 21;
constexpr uint8_t I2C_SCL = 22;
constexpr uint8_t RS485_DIR = 4;
constexpr uint8_t RS485_RX = 16;
constexpr uint8_t RS485_TX = 17;
}

struct BiColorLed {
    uint8_t red;
    uint8_t green;
};

constexpr BiColorLed LED_WIFI{Pins::LED_WIFI_RED, Pins::LED_WIFI_GREEN};
constexpr BiColorLed LED_SERVER{Pins::LED_SERVER_RED, Pins::LED_SERVER_GREEN};
constexpr BiColorLed LED_RS485{Pins::LED_RS485_RED, Pins::LED_RS485_GREEN};

enum class LedColor : uint8_t { Off, Red, Green, Both };

bool littleFsOk = false;
bool rtcFound = false;
bool buttonWasPressed = false;
bool lastButtonState = HIGH;
uint32_t lastButtonChangeMs = 0;

void setLed(const BiColorLed &led, LedColor color) {
    digitalWrite(led.red,
                 (color == LedColor::Red || color == LedColor::Both) ? HIGH : LOW);
    digitalWrite(led.green,
                 (color == LedColor::Green || color == LedColor::Both) ? HIGH : LOW);
}

void setAllLeds(LedColor color) {
    setLed(LED_WIFI, color);
    setLed(LED_SERVER, color);
    setLed(LED_RS485, color);
}

void initializeHardware() {
    pinMode(LED_WIFI.red, OUTPUT);
    pinMode(LED_WIFI.green, OUTPUT);
    pinMode(LED_SERVER.red, OUTPUT);
    pinMode(LED_SERVER.green, OUTPUT);
    pinMode(LED_RS485.red, OUTPUT);
    pinMode(LED_RS485.green, OUTPUT);
    setAllLeds(LedColor::Off);

    // A placa já possui pull-up externo de 10 kOhm.
    pinMode(Pins::BUTTON_RST, INPUT);

    pinMode(Pins::RS485_DIR, OUTPUT);
    digitalWrite(Pins::RS485_DIR, LOW);

    Wire.begin(Pins::I2C_SDA, Pins::I2C_SCL);
}

void printSeparator() {
    Serial.println(F("------------------------------------------------------------"));
}

void printHumanBytes(const char *label, uint64_t bytes) {
    Serial.printf("%-30s %10llu bytes  (%7.2f MiB)\n",
                  label,
                  static_cast<unsigned long long>(bytes),
                  static_cast<double>(bytes) / (1024.0 * 1024.0));
}

void printChipAndMemoryInfo() {
    printSeparator();
    Serial.println(F("INFORMACOES DO ESP32"));
    printSeparator();

    Serial.printf("Modelo:                       %s\n", ESP.getChipModel());
    Serial.printf("Revisao:                      %u\n", ESP.getChipRevision());
    Serial.printf("Nucleos:                      %u\n", ESP.getChipCores());
    Serial.printf("Frequencia da CPU:            %u MHz\n", ESP.getCpuFreqMHz());
    printHumanBytes("Flash detectada:", ESP.getFlashChipSize());
    Serial.printf("Frequencia da flash:          %u MHz\n",
                  ESP.getFlashChipSpeed() / 1000000U);
    printHumanBytes("Firmware atual:", ESP.getSketchSize());
    printHumanBytes("Espaco livre para app:", ESP.getFreeSketchSpace());
    printHumanBytes("Heap total:", ESP.getHeapSize());
    printHumanBytes("Heap livre:", ESP.getFreeHeap());
    printHumanBytes("Menor heap livre:", ESP.getMinFreeHeap());
    printHumanBytes("Maior bloco alocavel:", ESP.getMaxAllocHeap());

    const uint32_t psramSize = ESP.getPsramSize();
    printHumanBytes("PSRAM:", psramSize);
    if (psramSize > 0) {
        printHumanBytes("PSRAM livre:", ESP.getFreePsram());
    }
}

const char *partitionTypeName(esp_partition_type_t type) {
    switch (type) {
        case ESP_PARTITION_TYPE_APP: return "APP";
        case ESP_PARTITION_TYPE_DATA: return "DATA";
        default: return "OUTRO";
    }
}

void printPartitionTable() {
    printSeparator();
    Serial.println(F("TABELA DE PARTICOES"));
    printSeparator();
    Serial.println(F("Label           Tipo   Sub   Offset      Tamanho"));
    Serial.println(F("--------------- ------ ----- ----------- -----------"));

    esp_partition_iterator_t iterator =
        esp_partition_find(ESP_PARTITION_TYPE_ANY,
                           ESP_PARTITION_SUBTYPE_ANY,
                           nullptr);

    while (iterator != nullptr) {
        const esp_partition_t *partition = esp_partition_get(iterator);
        Serial.printf("%-15s %-6s 0x%02X  0x%08lX  %8lu KiB\n",
                      partition->label,
                      partitionTypeName(partition->type),
                      static_cast<unsigned int>(partition->subtype),
                      static_cast<unsigned long>(partition->address),
                      static_cast<unsigned long>(partition->size / 1024U));
        iterator = esp_partition_next(iterator);
    }

    esp_partition_iterator_release(iterator);
}

void runLedSequence() {
    printSeparator();
    Serial.println(F("TESTE VISUAL DOS LEDS"));
    Serial.println(F("Cada indicador deve acender vermelho, verde e as duas cores."));
    printSeparator();

    struct LedTestItem {
        const char *name;
        BiColorLed led;
    };

    const LedTestItem items[] = {
        {"Wi-Fi", LED_WIFI},
        {"Server", LED_SERVER},
        {"RS-485/OTA", LED_RS485},
    };

    setAllLeds(LedColor::Off);

    for (const auto &item : items) {
        Serial.printf("%s: vermelho\n", item.name);
        setLed(item.led, LedColor::Red);
        delay(700);

        Serial.printf("%s: verde\n", item.name);
        setLed(item.led, LedColor::Green);
        delay(700);

        Serial.printf("%s: vermelho + verde\n", item.name);
        setLed(item.led, LedColor::Both);
        delay(700);

        setLed(item.led, LedColor::Off);
        delay(250);
    }

    Serial.println(F("Teste visual finalizado."));
}

bool mountAndTestLittleFS() {
    printSeparator();
    Serial.println(F("TESTE DO LITTLEFS"));
    printSeparator();

    if (!LittleFS.begin(true, "/littlefs", 10, "littlefs")) {
        Serial.println(F("[ERRO] Nao foi possivel montar ou formatar o LittleFS."));
        return false;
    }

    printHumanBytes("LittleFS total:", LittleFS.totalBytes());
    printHumanBytes("LittleFS usado:", LittleFS.usedBytes());
    printHumanBytes("LittleFS livre:", LittleFS.totalBytes() - LittleFS.usedBytes());

    constexpr const char *testPath = "/teste_hardware.bin";
    constexpr size_t testSize = 4096;

    LittleFS.remove(testPath);
    File output = LittleFS.open(testPath, FILE_WRITE);
    if (!output) {
        Serial.println(F("[ERRO] Nao foi possivel criar o arquivo de teste."));
        return false;
    }

    for (size_t i = 0; i < testSize; ++i) {
        const uint8_t expected = static_cast<uint8_t>((i * 31U + 7U) & 0xFFU);
        if (output.write(&expected, 1) != 1) {
            Serial.println(F("[ERRO] Falha durante a escrita."));
            output.close();
            LittleFS.remove(testPath);
            return false;
        }
    }
    output.close();

    File input = LittleFS.open(testPath, FILE_READ);
    if (!input) {
        Serial.println(F("[ERRO] Nao foi possivel reabrir o arquivo."));
        LittleFS.remove(testPath);
        return false;
    }

    if (input.size() != testSize) {
        Serial.printf("[ERRO] Tamanho incorreto: %u bytes.\n",
                      static_cast<unsigned int>(input.size()));
        input.close();
        LittleFS.remove(testPath);
        return false;
    }

    for (size_t i = 0; i < testSize; ++i) {
        const int received = input.read();
        const uint8_t expected = static_cast<uint8_t>((i * 31U + 7U) & 0xFFU);
        if (received < 0 || static_cast<uint8_t>(received) != expected) {
            Serial.printf("[ERRO] Dado incorreto na posicao %u.\n",
                          static_cast<unsigned int>(i));
            input.close();
            LittleFS.remove(testPath);
            return false;
        }
    }

    input.close();
    LittleFS.remove(testPath);
    Serial.println(F("[OK] Escrita, leitura, comparacao e exclusao funcionando."));
    return true;
}

bool scanI2cBus() {
    printSeparator();
    Serial.println(F("VARREDURA I2C"));
    printSeparator();

    uint8_t foundCount = 0;
    bool foundPcf8563 = false;

    for (uint8_t address = 1; address < 127; ++address) {
        Wire.beginTransmission(address);
        const uint8_t error = Wire.endTransmission();

        if (error == 0) {
            Serial.printf("Dispositivo encontrado em 0x%02X", address);
            if (address == 0x51) {
                Serial.print(F("  <- PCF8563 esperado"));
                foundPcf8563 = true;
            }
            Serial.println();
            ++foundCount;
        } else if (error == 4) {
            Serial.printf("Erro desconhecido no endereco 0x%02X\n", address);
        }
    }

    if (foundCount == 0) {
        Serial.println(F("[AVISO] Nenhum dispositivo I2C respondeu."));
    }

    if (foundPcf8563) {
        Serial.println(F("[OK] RTC PCF8563 respondeu no endereco 0x51."));
    } else {
        Serial.println(F("[AVISO] RTC PCF8563 nao respondeu em 0x51."));
    }

    return foundPcf8563;
}

void waitForButtonTest(uint32_t timeoutMs = 10000) {
    printSeparator();
    Serial.println(F("TESTE DO BOTAO CH1/RST - GPIO12"));
    Serial.printf("Pressione o botao nos proximos %lu segundos...\n",
                  static_cast<unsigned long>(timeoutMs / 1000U));
    printSeparator();

    const uint32_t start = millis();
    while (millis() - start < timeoutMs) {
        if (digitalRead(Pins::BUTTON_RST) == LOW) {
            buttonWasPressed = true;
            Serial.println(F("[OK] Botao detectado em nivel LOW."));
            setAllLeds(LedColor::Green);
            while (digitalRead(Pins::BUTTON_RST) == LOW) {
                delay(10);
            }
            delay(300);
            return;
        }
        delay(10);
    }

    Serial.println(F("[AVISO] Botao nao foi pressionado. Pode testar depois."));
}

void showFinalStatus() {
    setLed(LED_WIFI, rtcFound ? LedColor::Green : LedColor::Red);
    setLed(LED_SERVER, littleFsOk ? LedColor::Green : LedColor::Red);
    setLed(LED_RS485, LedColor::Green);

    printSeparator();
    Serial.println(F("RESUMO VISUAL"));
    Serial.printf("Wi-Fi LED:   %s (RTC/I2C)\n", rtcFound ? "VERDE" : "VERMELHO");
    Serial.printf("Server LED:  %s (LittleFS)\n", littleFsOk ? "VERDE" : "VERMELHO");
    Serial.println(F("RS-485 LED:  VERDE (programa executando)"));
    Serial.printf("Botao:       %s\n", buttonWasPressed ? "TESTADO" : "AINDA NAO TESTADO");
    printSeparator();
}

void sendRs485TestFrame() {
    printSeparator();
    Serial.println(F("TESTE BASICO DE TRANSMISSAO RS-485"));
    Serial.println(F("Envia 32 bytes. Use osciloscopio, analisador ou outro receptor."));
    Serial.println(F("Nao execute com a placa ligada a um barramento em operacao."));
    printSeparator();

    Serial2.begin(9600, SERIAL_8N1, Pins::RS485_RX, Pins::RS485_TX);

    const uint8_t frame[] = {
        0x55, 0xAA, 0x00, 0xFF, 0x11, 0x22, 0x33, 0x44,
        0x55, 0xAA, 0x00, 0xFF, 0x11, 0x22, 0x33, 0x44,
        0x55, 0xAA, 0x00, 0xFF, 0x11, 0x22, 0x33, 0x44,
        0x55, 0xAA, 0x00, 0xFF, 0x11, 0x22, 0x33, 0x44
    };

    setLed(LED_RS485, LedColor::Green);
    digitalWrite(Pins::RS485_DIR, HIGH);
    delayMicroseconds(100);
    Serial2.write(frame, sizeof(frame));
    Serial2.flush();
    delayMicroseconds(200);
    digitalWrite(Pins::RS485_DIR, LOW);
    setLed(LED_RS485, LedColor::Off);

    Serial.printf("[OK] %u bytes enviados pela UART2.\n",
                  static_cast<unsigned int>(sizeof(frame)));
    showFinalStatus();
}

void printMenu() {
    Serial.println();
    printSeparator();
    Serial.println(F("MENU DO DIAGNOSTICO"));
    printSeparator();
    Serial.println(F("a - executar novamente todos os testes"));
    Serial.println(F("l - testar LEDs"));
    Serial.println(F("i - informacoes do chip e memoria"));
    Serial.println(F("p - listar tabela de particoes"));
    Serial.println(F("f - testar LittleFS"));
    Serial.println(F("c - varrer I2C e procurar RTC"));
    Serial.println(F("b - aguardar teste do botao"));
    Serial.println(F("r - enviar quadro de teste RS-485"));
    Serial.println(F("s - mostrar resumo visual"));
    Serial.println(F("m - mostrar este menu"));
    printSeparator();
}

void runAllTests() {
    setAllLeds(LedColor::Off);
    runLedSequence();
    printChipAndMemoryInfo();
    printPartitionTable();
    littleFsOk = mountAndTestLittleFS();
    rtcFound = scanI2cBus();
    waitForButtonTest();
    showFinalStatus();
}

void monitorButton() {
    const bool currentState = digitalRead(Pins::BUTTON_RST);
    if (currentState != lastButtonState && millis() - lastButtonChangeMs >= 30) {
        lastButtonChangeMs = millis();
        lastButtonState = currentState;
        if (currentState == LOW) {
            buttonWasPressed = true;
            Serial.println(F("[BOTAO] CH1/RST pressionado."));
        } else {
            Serial.println(F("[BOTAO] CH1/RST solto."));
            showFinalStatus();
        }
    }
}

void handleSerialCommand() {
    if (!Serial.available()) return;

    const char command = static_cast<char>(tolower(Serial.read()));
    if (command == '\r' || command == '\n' || command == ' ') return;

    switch (command) {
        case 'a': runAllTests(); break;
        case 'l': runLedSequence(); showFinalStatus(); break;
        case 'i': printChipAndMemoryInfo(); break;
        case 'p': printPartitionTable(); break;
        case 'f': littleFsOk = mountAndTestLittleFS(); showFinalStatus(); break;
        case 'c': rtcFound = scanI2cBus(); showFinalStatus(); break;
        case 'b': waitForButtonTest(); showFinalStatus(); break;
        case 'r': sendRs485TestFrame(); break;
        case 's': showFinalStatus(); break;
        case 'm':
        case '?': printMenu(); break;
        default:
            Serial.printf("Comando desconhecido: '%c'. Digite m para o menu.\n", command);
            break;
    }
}

void setup() {
    Serial.begin(115200);
    delay(1200);
    initializeHardware();

    Serial.println();
    Serial.println(F("============================================================"));
    Serial.println(F(" GravadorESP 1.0 - Diagnostico do hardware"));
    Serial.println(F("============================================================"));

    runAllTests();
    printMenu();
    lastButtonState = digitalRead(Pins::BUTTON_RST);
}

void loop() {
    monitorButton();
    handleSerialCommand();
    delay(2);
}
