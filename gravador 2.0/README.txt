GRAVADORESP 1.2 — TRANSMISSOR ARDUINO OTA
==========================================

Esta versão implementa o transmissor compatível com o fluxo do espota.py:
1. resolve IP/hostname/mDNS;
2. abre servidor TCP local na porta 32320;
3. envia convite UDP ao alvo;
4. trata autenticação MD5 opcional;
5. aguarda a conexão TCP de retorno;
6. envia /firmware.bin em blocos de 1460 bytes;
7. aguarda a confirmação de cada bloco e o resultado final OK.

USO
---
1. Conecte GravadorESP e alvo à mesma rede.
2. No alvo, ArduinoOTA.begin() e ArduinoOTA.handle() devem estar ativos.
3. Salve o IP/hostname e a porta do alvo:
   - ESP8266: 8266
   - ESP32: 3232
4. Envie o firmware.bin para o GravadorESP.
5. Clique em "Gravar ESP remoto".
6. Acompanhe estado e progresso na página.

BOTÃO
-----
Toque curto: inicia a gravação usando o destino e firmware salvos.
Pressionado por 3 segundos durante a transmissão: cancela.

LEDS
----
Wi-Fi: mantém os estados da versão anterior.
Server: servidor/LittleFS e upload HTTP.
RS-485 vermelho: pisca lento durante negociação, rápido durante envio,
firma por 5 segundos no sucesso e pisca lento em erro.
RS-485 verde/GPIO13: não utilizado.

LIMITES DA PRIMEIRA VERSÃO DO TRANSMISSOR
------------------------------------------
- envia somente firmware de aplicação (comando U_FLASH = 0);
- não envia imagem de filesystem;
- use IP direto no primeiro teste;
- o alvo precisa aceitar ArduinoOTA e ter espaço de partição OTA;
- os dois dispositivos precisam conseguir comunicar-se diretamente na rede.

A tabela de partições não mudou. Não é necessário apagar a flash novamente.
