GRAVADORESP 1.1 — REDE, WEB E ARMAZENAMENTO
============================================

Esta etapa valida:
- conexão à rede Wi-Fi;
- AP de manutenção;
- mesma página web acessível pelo AP;
- mDNS gravadoresp.local quando conectado a uma rede;
- armazenamento das configurações em Preferences/NVS;
- upload de firmware.bin para o LittleFS;
- validação do cabeçalho 0xE9, tamanho e MD5;
- botão GPIO12 monitorado continuamente;
- funcionamento sem utilizar o LED verde RS-485/GPIO13.

O protocolo de transmissão ArduinoOTA ainda não está incluído. Ele será a
próxima etapa, depois de validar rede, página e upload.

PRIMEIRO UPLOAD
---------------
A tabela de partições é a mesma da etapa anterior. Se você já apagou a flash
e gravou o firmware de diagnóstico com esta tabela, não precisa apagar outra
vez. Use apenas Upload.

ACESSO
------
Quando conseguir conectar à rede salva:
    http://gravadoresp.local
ou o IP mostrado no Monitor Serial.

Quando não conseguir conectar:
    AP: GravadorESP
    senha: gravador123
    página: http://192.168.4.1

A página do modo AP é a mesma página principal. Nela é possível selecionar a
rede, informar a senha e reiniciar o equipamento.

LEDS
----
Wi-Fi vermelho piscando:
    tentando conectar

Wi-Fi verde fixo:
    conectado à rede

Wi-Fi vermelho fixo:
    modo AP de manutenção

Server verde fixo:
    servidor e LittleFS prontos

Server verde piscando:
    recebendo firmware

Server vermelho:
    erro de LittleFS ou upload

RS-485 vermelho:
    pulso curto quando o botão muda de estado

RS-485 verde / GPIO13:
    não utilizado; configurado como INPUT_PULLDOWN

BOTÃO
-----
O botão é monitorado continuamente. Pressionar e soltar deve gerar mensagens
no Monitor Serial, eliminando a dependência de uma janela de 10 segundos.

FIRMWARE.BIN
------------
Use o binário principal da aplicação:
- PlatformIO: .pio/build/<ambiente>/firmware.bin
- Arduino IDE: Sketch > Export Compiled Binary, escolhendo o .ino.bin principal

Não envie bootloader.bin, partitions.bin, merged.bin, spiffs.bin ou
littlefs.bin.

OBSERVAÇÃO SOBRE WIFIMANAGER
----------------------------
Nesta etapa a configuração Wi-Fi foi implementada diretamente na mesma
interface web. Isso garante que a página principal continue disponível no
modo AP, sem disputa entre dois servidores HTTP na porta 80.
