GRAVADORESP 1.0 — TESTE DE HARDWARE
===================================

O firmware testa:
- seis canais dos três LEDs bicolores;
- botão CH1/RST no GPIO12;
- informações do ESP32, flash e RAM;
- tabela de partições;
- escrita, leitura e exclusão no LittleFS;
- barramento I2C e presença do RTC PCF8563 em 0x51;
- transmissão RS-485 opcional.

PRIMEIRA GRAVAÇÃO
-----------------
Como a tabela de partições será alterada, apague a flash uma vez:

    pio run -t erase

Depois grave normalmente pelo botão Upload do PlatformIO.

O teste usa LittleFS.begin(true): a partição LittleFS será formatada se não
puder ser montada.

MONITOR SERIAL
--------------
Velocidade: 115200 baud

a - todos os testes
l - LEDs
i - chip e memória
p - partições
f - LittleFS
c - I2C/RTC
b - botão
r - transmissão RS-485
s - resumo
m - menu

INTERPRETAÇÃO FINAL DOS LEDS
----------------------------
Wi-Fi verde: RTC encontrado em 0x51
Wi-Fi vermelho: RTC não encontrado

Server verde: LittleFS aprovado
Server vermelho: falha no LittleFS

RS-485 verde: firmware de diagnóstico executando

O teste RS-485 apenas transmite bytes. Para validar A/B, use osciloscópio,
analisador ou outro equipamento RS-485. Não execute em barramento ativo.
