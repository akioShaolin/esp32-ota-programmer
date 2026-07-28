#include "OtaSender.h"

EspOtaSender::EspOtaSender() : _server(LOCAL_TCP_PORT) {}

void EspOtaSender::beginServer() {
    _server.begin();
    Serial.printf("[OTA-TX] Servidor TCP de retorno ativo na porta %u.\n",
                  LOCAL_TCP_PORT);
}

bool EspOtaSender::start(const String& targetHost,
                         uint16_t targetPort,
                         const String& password,
                         const String& filePath,
                         const String& fileMd5,
                         uint32_t timeoutSeconds) {
    if (busy()) {
        return false;
    }

    closeTransport();

    _host = targetHost;
    _host.trim();
    _password = password;
    _filePath = filePath;
    _fileMd5 = fileMd5;
    _fileMd5.trim();
    _targetPort = targetPort;
    _timeoutMs = constrain(timeoutSeconds, 2U, 300U) * 1000UL;
    _targetIp = IPAddress();
    _bytesSent = 0;
    _totalBytes = 0;
    _chunkLength = 0;
    _chunkOffset = 0;
    _replyBuffer = "";

    if (WiFi.status() != WL_CONNECTED) {
        fail("O GravadorESP não está conectado a uma rede Wi-Fi.");
        return false;
    }

    if (_host.isEmpty()) {
        fail("Destino OTA não configurado.");
        return false;
    }

    if (_targetPort == 0) {
        fail("Porta OTA inválida.");
        return false;
    }

    if (_fileMd5.length() != 32) {
        fail("MD5 do firmware ausente ou inválido.");
        return false;
    }

    _file = LittleFS.open(_filePath, FILE_READ);
    if (!_file) {
        fail("Não foi possível abrir o firmware armazenado.");
        return false;
    }

    _totalBytes = _file.size();
    if (_totalBytes < 1024) {
        fail("Firmware armazenado é pequeno demais.");
        return false;
    }

    setState(State::Resolving, "Resolvendo o endereço do destino...");
    Serial.printf("[OTA-TX] Início: %s:%u, %u bytes, MD5 %s.\n",
                  _host.c_str(), _targetPort,
                  static_cast<unsigned int>(_totalBytes),
                  _fileMd5.c_str());
    return true;
}

void EspOtaSender::loop() {
    switch (_state) {
        case State::Idle:
        case State::Success:
        case State::Error:
        case State::Canceled:
            return;

        case State::Resolving: {
            if (!resolveTarget()) {
                fail("Não foi possível resolver o IP do destino.");
                return;
            }

            clearStaleTcpClients();
            _udp.stop();
            if (!_udp.begin(LOCAL_UDP_PORT)) {
                fail("Não foi possível abrir a porta UDP local.");
                return;
            }

            setState(State::SendingInvitation,
                     String("Destino resolvido: ") + _targetIp.toString());
            break;
        }

        case State::SendingInvitation: {
            const String invitation =
                String("0 ") + LOCAL_TCP_PORT + " " + _totalBytes + " " +
                _fileMd5 + "\n";

            if (!sendUdpText(invitation)) {
                fail("Falha ao enviar o convite OTA por UDP.");
                return;
            }

            Serial.printf("[OTA-TX] Convite enviado para %s:%u: %s",
                          _targetIp.toString().c_str(), _targetPort,
                          invitation.c_str());
            setState(State::WaitingInvitationReply,
                     "Convite enviado; aguardando resposta do alvo...");
            break;
        }

        case State::WaitingInvitationReply: {
            String reply;
            if (readUdpReply(reply)) {
                reply.trim();
                Serial.printf("[OTA-TX] Resposta UDP: %s\n", reply.c_str());

                if (reply == "OK") {
                    setState(State::WaitingTcpClient,
                             "Alvo aceitou; aguardando conexão TCP de retorno...");
                    return;
                }

                if (reply.startsWith("AUTH ")) {
                    if (_password.isEmpty()) {
                        fail("O alvo exige senha OTA, mas nenhuma senha foi informada.");
                        return;
                    }

                    const String nonce = reply.substring(5);
                    if (nonce.length() != 32) {
                        fail("Nonce de autenticação inválido.");
                        return;
                    }

                    const String cnonce = md5OfString(
                        _filePath + String(_totalBytes) + _fileMd5 +
                        _targetIp.toString() + String(micros()));
                    const String passMd5 = md5OfString(_password);
                    const String result =
                        md5OfString(passMd5 + ":" + nonce + ":" + cnonce);
                    const String auth =
                        String("200 ") + cnonce + " " + result + "\n";

                    if (!sendUdpText(auth)) {
                        fail("Falha ao enviar a autenticação OTA.");
                        return;
                    }

                    setState(State::WaitingAuthenticationReply,
                             "Autenticação enviada; aguardando confirmação...");
                    return;
                }

                fail(String("Resposta inesperada do alvo: ") + reply);
                return;
            }

            if (timedOut()) {
                fail("O alvo não respondeu ao convite OTA.");
            }
            break;
        }

        case State::WaitingAuthenticationReply: {
            String reply;
            if (readUdpReply(reply)) {
                reply.trim();
                Serial.printf("[OTA-TX] Resposta de autenticação: %s\n",
                              reply.c_str());

                if (reply == "OK") {
                    setState(State::WaitingTcpClient,
                             "Autenticação aceita; aguardando conexão TCP...");
                } else {
                    fail(String("Autenticação recusada: ") + reply);
                }
                return;
            }

            if (timedOut()) {
                fail("Timeout durante a autenticação OTA.");
            }
            break;
        }

        case State::WaitingTcpClient: {
            WiFiClient incoming = _server.available();
            if (incoming) {
                _client = incoming;
                _client.setNoDelay(true);
                _file.seek(0, SeekSet);
                _bytesSent = 0;
                _chunkLength = 0;
                _chunkOffset = 0;
                _replyBuffer = "";
                Serial.printf("[OTA-TX] Alvo conectou de %s.\n",
                              _client.remoteIP().toString().c_str());
                setState(State::SendingChunk, "Enviando firmware...");
                return;
            }

            if (timedOut()) {
                fail("O alvo aceitou o convite, mas não abriu a conexão TCP.");
            }
            break;
        }

        case State::SendingChunk: {
            if (!_client.connected()) {
                fail("A conexão TCP foi encerrada durante o envio.");
                return;
            }

            if (_chunkLength == 0) {
                const size_t remaining = _totalBytes - _bytesSent;
                if (remaining == 0) {
                    setState(State::WaitingFinalResult,
                             "Firmware enviado; aguardando validação final...");
                    return;
                }

                const size_t requested = min(CHUNK_SIZE, remaining);
                _chunkLength = _file.read(_chunk, requested);
                _chunkOffset = 0;

                if (_chunkLength == 0) {
                    fail("Falha ao ler o firmware no LittleFS.");
                    return;
                }
            }

            const size_t remainingInChunk = _chunkLength - _chunkOffset;
            const size_t written =
                _client.write(_chunk + _chunkOffset, remainingInChunk);

            if (written > 0) {
                _chunkOffset += written;
                _stateSince = millis();
            }

            if (_chunkOffset == _chunkLength) {
                _bytesSent += _chunkLength;
                _chunkLength = 0;
                _chunkOffset = 0;
                _replyBuffer = "";
                setState(State::WaitingChunkAck,
                         String("Enviado: ") + progress() + "%");
            } else if (timedOut()) {
                fail("Timeout ao escrever dados na conexão TCP.");
            }
            break;
        }

        case State::WaitingChunkAck:
            processTcpReply(false);
            break;

        case State::WaitingFinalResult:
            processTcpReply(true);
            break;
    }
}

void EspOtaSender::cancel() {
    if (!busy()) {
        return;
    }

    closeTransport();
    setState(State::Canceled, "Transmissão cancelada pelo usuário.");
    Serial.println("[OTA-TX] Cancelado.");
}

bool EspOtaSender::busy() const {
    return _state != State::Idle &&
           _state != State::Success &&
           _state != State::Error &&
           _state != State::Canceled;
}

uint8_t EspOtaSender::progress() const {
    if (_totalBytes == 0) {
        return 0;
    }
    const size_t value = (_bytesSent * 100ULL) / _totalBytes;
    return static_cast<uint8_t>(value > 100 ? 100 : value);
}

String EspOtaSender::stateText() const {
    switch (_state) {
        case State::Idle: return "Aguardando";
        case State::Resolving: return "Resolvendo destino";
        case State::SendingInvitation: return "Enviando convite";
        case State::WaitingInvitationReply: return "Aguardando resposta UDP";
        case State::WaitingAuthenticationReply: return "Autenticando";
        case State::WaitingTcpClient: return "Aguardando TCP";
        case State::SendingChunk: return "Enviando firmware";
        case State::WaitingChunkAck: return "Aguardando confirmação";
        case State::WaitingFinalResult: return "Validando no alvo";
        case State::Success: return "Concluído";
        case State::Error: return "Erro";
        case State::Canceled: return "Cancelado";
        default: return "Desconhecido";
    }
}

void EspOtaSender::setState(State next, const String& message) {
    _state = next;
    _message = message;
    _stateSince = millis();
}

void EspOtaSender::fail(const String& message) {
    closeTransport();
    setState(State::Error, message);
    Serial.printf("[OTA-TX][ERRO] %s\n", message.c_str());
}

void EspOtaSender::succeed() {
    _bytesSent = _totalBytes;
    closeTransport();
    setState(State::Success,
             "Firmware gravado e validado pelo ESP remoto.");
    Serial.println("[OTA-TX] Gravação concluída com sucesso.");
}

void EspOtaSender::closeTransport() {
    if (_client) {
        _client.stop();
    }
    if (_file) {
        _file.close();
    }
    _udp.stop();
    _chunkLength = 0;
    _chunkOffset = 0;
    _replyBuffer = "";
}

bool EspOtaSender::timedOut(uint32_t customTimeoutMs) const {
    const uint32_t limit = customTimeoutMs ? customTimeoutMs : _timeoutMs;
    return millis() - _stateSince >= limit;
}

bool EspOtaSender::resolveTarget() {
    if (_targetIp.fromString(_host.c_str())) {
        return true;
    }

    String lookup = _host;
    if (lookup.endsWith(".local")) {
        lookup.remove(lookup.length() - 6);
        char hostBuffer[64];
        lookup.toCharArray(hostBuffer, sizeof(hostBuffer));
        _targetIp = MDNS.queryHost(hostBuffer, 3000);
        return _targetIp[0] != 0 || _targetIp[1] != 0 ||
               _targetIp[2] != 0 || _targetIp[3] != 0;
    }

    return WiFi.hostByName(_host.c_str(), _targetIp) == 1 &&
           (_targetIp[0] != 0 || _targetIp[1] != 0 ||
            _targetIp[2] != 0 || _targetIp[3] != 0);
}

bool EspOtaSender::sendUdpText(const String& text) {
    if (!_udp.beginPacket(_targetIp, _targetPort)) {
        return false;
    }
    _udp.write(reinterpret_cast<const uint8_t*>(text.c_str()), text.length());
    return _udp.endPacket() == 1;
}

bool EspOtaSender::readUdpReply(String& reply) {
    const int packetSize = _udp.parsePacket();
    if (packetSize <= 0) {
        return false;
    }

    reply = "";
    while (_udp.available()) {
        reply += static_cast<char>(_udp.read());
    }
    return true;
}

void EspOtaSender::clearStaleTcpClients() {
    while (true) {
        WiFiClient stale = _server.available();
        if (!stale) {
            break;
        }
        stale.stop();
    }
}

void EspOtaSender::processTcpReply(bool finalStage) {
    bool receivedAny = false;

    while (_client.available()) {
        receivedAny = true;
        _replyBuffer += static_cast<char>(_client.read());
        if (_replyBuffer.length() > 160) {
            _replyBuffer.remove(0, _replyBuffer.length() - 160);
        }
    }

    if (_replyBuffer.indexOf('E') >= 0) {
        fail(String("O alvo retornou erro: ") + _replyBuffer);
        return;
    }

    if (_replyBuffer.indexOf('O') >= 0) {
        succeed();
        return;
    }

    if (!finalStage && receivedAny) {
        _replyBuffer = "";
        setState(State::SendingChunk,
                 String("Confirmação recebida; progresso ") + progress() + "%");
        return;
    }

    if (!_client.connected() && !_client.available()) {
        fail(finalStage
                 ? "O alvo encerrou a conexão antes da confirmação final."
                 : "O alvo encerrou a conexão durante a transferência.");
        return;
    }

    const uint32_t timeout =
        finalStage ? (_timeoutMs > 60000UL ? _timeoutMs : 60000UL)
                   : _timeoutMs;
    if (timedOut(timeout)) {
        fail(finalStage
                 ? "Timeout aguardando o resultado final do alvo."
                 : "Timeout aguardando confirmação de bloco.");
    }
}

String EspOtaSender::md5OfString(const String& text) {
    MD5Builder md5;
    md5.begin();
    md5.add(text);
    md5.calculate();
    return md5.toString();
}
