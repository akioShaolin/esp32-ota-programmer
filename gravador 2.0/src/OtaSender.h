#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <WiFiClient.h>
#include <WiFiServer.h>
#include <LittleFS.h>
#include <ESPmDNS.h>
#include <MD5Builder.h>

class EspOtaSender {
public:
    enum class State : uint8_t {
        Idle,
        Resolving,
        SendingInvitation,
        WaitingInvitationReply,
        WaitingAuthenticationReply,
        WaitingTcpClient,
        SendingChunk,
        WaitingChunkAck,
        WaitingFinalResult,
        Success,
        Error,
        Canceled
    };

    static constexpr uint16_t LOCAL_TCP_PORT = 32320;
    static constexpr uint16_t LOCAL_UDP_PORT = 32321;

    EspOtaSender();

    void beginServer();

    bool start(const String& targetHost,
               uint16_t targetPort,
               const String& password,
               const String& filePath,
               const String& fileMd5,
               uint32_t timeoutSeconds);

    void loop();
    void cancel();

    bool busy() const;
    State state() const { return _state; }
    uint8_t progress() const;
    size_t bytesSent() const { return _bytesSent; }
    size_t totalBytes() const { return _totalBytes; }
    const String& message() const { return _message; }
    String stateText() const;
    String resolvedIpText() const { return _targetIp.toString(); }
    uint32_t stateSince() const { return _stateSince; }

private:
    static constexpr size_t CHUNK_SIZE = 1460;

    WiFiServer _server;
    WiFiUDP _udp;
    WiFiClient _client;
    File _file;

    State _state = State::Idle;
    String _message = "Aguardando comando.";
    String _host;
    String _password;
    String _filePath;
    String _fileMd5;
    IPAddress _targetIp;
    uint16_t _targetPort = 8266;
    uint32_t _timeoutMs = 10000;
    uint32_t _stateSince = 0;

    size_t _totalBytes = 0;
    size_t _bytesSent = 0;
    uint8_t _chunk[CHUNK_SIZE];
    size_t _chunkLength = 0;
    size_t _chunkOffset = 0;
    String _replyBuffer;

    void setState(State next, const String& message);
    void fail(const String& message);
    void succeed();
    void closeTransport();
    bool timedOut(uint32_t customTimeoutMs = 0) const;
    bool resolveTarget();
    bool sendUdpText(const String& text);
    bool readUdpReply(String& reply);
    void clearStaleTcpClients();
    void processTcpReply(bool finalStage);
    static String md5OfString(const String& text);
};
