#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <FS.h>
#include <MD5Builder.h>
#include "OtaSender.h"

// ============================================================
// GravadorESP 1.2
// Rede + armazenamento + transmissor compatível com espota.py
//
// IMPORTANTE:
// O LED verde de RS-485 (GPIO13) NÃO é utilizado.
// O GPIO13 permanece como INPUT_PULLDOWN.
//
// Envia firmware para ESP8266/ESP32 que executem ArduinoOTA.handle().
// ============================================================

namespace Pins {
constexpr uint8_t LED_WIFI_RED = 32;
constexpr uint8_t LED_WIFI_GREEN = 33;
constexpr uint8_t LED_SERVER_RED = 25;
constexpr uint8_t LED_SERVER_GREEN = 26;
constexpr uint8_t LED_RS485_RED = 27;
constexpr uint8_t LED_RS485_GREEN_UNUSED = 13;
constexpr uint8_t BUTTON = 12;
}  // namespace Pins

struct BiColorLed {
    uint8_t red;
    uint8_t green;
};

constexpr BiColorLed LED_WIFI{Pins::LED_WIFI_RED, Pins::LED_WIFI_GREEN};
constexpr BiColorLed LED_SERVER{Pins::LED_SERVER_RED, Pins::LED_SERVER_GREEN};

enum class LedColor : uint8_t {
    Off,
    Red,
    Green,
    Both
};

enum class NetworkState : uint8_t {
    Connecting,
    Station,
    AccessPoint
};

constexpr char HOSTNAME[] = "gravadoresp";
constexpr char AP_SSID[] = "GravadorESP";
constexpr char AP_PASSWORD[] = "gravador123";
constexpr char FIRMWARE_PATH[] = "/firmware.bin";
constexpr char TEMP_FIRMWARE_PATH[] = "/firmware.tmp";

WebServer server(80);
Preferences prefs;
EspOtaSender otaSender;
File uploadFile;
MD5Builder uploadMd5;

NetworkState networkState = NetworkState::Connecting;

String savedSsid;
String savedWifiPassword;
String targetHost;
String targetOtaPassword;
uint16_t targetPort = 8266;
uint32_t targetTimeoutSeconds = 10;

bool littleFsReady = false;
bool mdnsReady = false;
bool uploadInProgress = false;
bool uploadSucceeded = false;
bool uploadHeaderValid = false;
bool restartPending = false;
bool firmwareAvailable = false;
size_t firmwareStoredSize = 0;

String uploadOriginalName;
String uploadError;
String firmwareMd5;
size_t uploadBytes = 0;
uint32_t restartAtMs = 0;
uint32_t lastLedUpdateMs = 0;
bool blinkState = false;

bool lastRawButton = HIGH;
bool stableButton = HIGH;
uint32_t lastButtonTransitionMs = 0;
uint32_t buttonPressedAtMs = 0;
uint32_t rsRedPulseUntilMs = 0;

const char MAIN_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="pt-BR">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>GravadorESP</title>
<style>
:root{
  color-scheme:dark;
  --bg:#101418;
  --panel:#171d23;
  --panel2:#1d252d;
  --line:#2c3843;
  --text:#edf4f8;
  --muted:#94a6b3;
  --accent:#35c98f;
  --accent2:#239a70;
  --danger:#f06060;
  --warning:#eab85d;
  --shadow:0 18px 48px rgba(0,0,0,.28);
}
*{box-sizing:border-box}
body{
  margin:0;
  font-family:Inter,system-ui,-apple-system,Segoe UI,Roboto,sans-serif;
  background:
    radial-gradient(circle at 12% 5%,rgba(53,201,143,.12),transparent 28rem),
    var(--bg);
  color:var(--text);
}
main{max-width:1000px;margin:auto;padding:28px 18px 56px}
header{display:flex;gap:16px;align-items:center;margin-bottom:22px}
.logo{
  width:52px;height:52px;border-radius:16px;
  display:grid;place-items:center;
  background:linear-gradient(145deg,var(--accent),var(--accent2));
  color:#06150f;font-weight:900;font-size:21px;
  box-shadow:var(--shadow)
}
h1{font-size:25px;margin:0}
.subtitle{color:var(--muted);margin-top:4px}
.grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:16px}
.card{
  background:linear-gradient(180deg,var(--panel2),var(--panel));
  border:1px solid var(--line);
  border-radius:18px;padding:18px;box-shadow:var(--shadow)
}
.card.full{grid-column:1/-1}
h2{font-size:16px;margin:0 0 15px}
label{display:block;color:var(--muted);font-size:13px;margin:12px 0 6px}
input,select,button{
  width:100%;border-radius:11px;border:1px solid var(--line);
  background:#0f1419;color:var(--text);padding:11px 12px;font:inherit
}
input:focus,select:focus{outline:2px solid rgba(53,201,143,.35);border-color:var(--accent)}
button{
  cursor:pointer;border:none;font-weight:750;
  background:linear-gradient(135deg,var(--accent),var(--accent2));
  color:#071510;margin-top:13px
}
button.secondary{background:#26313b;color:var(--text);border:1px solid var(--line)}
button.danger{background:#48272a;color:#ffd9dc;border:1px solid #74363b}
button:disabled{opacity:.45;cursor:not-allowed}
.row{display:grid;grid-template-columns:1fr 1fr;gap:12px}
.stats{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px}
.stat{padding:12px;border-radius:12px;background:#11171c;border:1px solid var(--line)}
.stat span{display:block;color:var(--muted);font-size:12px}
.stat strong{display:block;margin-top:4px;font-size:14px;overflow-wrap:anywhere}
.badge{
  display:inline-flex;align-items:center;gap:7px;
  padding:7px 10px;border-radius:999px;
  background:#12251e;color:#91efca;border:1px solid #275e49;font-size:13px
}
.dot{width:8px;height:8px;border-radius:50%;background:currentColor}
.progress{height:12px;border-radius:999px;background:#0f1419;border:1px solid var(--line);overflow:hidden;margin-top:12px}
.progress>div{height:100%;width:0;background:linear-gradient(90deg,var(--accent2),var(--accent));transition:width .15s}
.message{min-height:22px;color:var(--muted);margin-top:10px;font-size:13px}
.message.error{color:#ff9898}.message.ok{color:#8ce5bf}
.note{color:var(--muted);font-size:13px;line-height:1.5}
code{color:#a4efd2}
footer{color:var(--muted);font-size:12px;margin-top:18px;text-align:center}
@media(max-width:720px){.grid{grid-template-columns:1fr}.card.full{grid-column:auto}.row,.stats{grid-template-columns:1fr}}
</style>
</head>
<body>
<main>
<header>
  <div class="logo">OTA</div>
  <div>
    <h1>GravadorESP</h1>
    <div class="subtitle">Rede, armazenamento e preparação do firmware remoto</div>
  </div>
</header>

<div class="grid">
  <section class="card full">
    <div style="display:flex;justify-content:space-between;gap:12px;align-items:center;flex-wrap:wrap">
      <h2 style="margin:0">Estado do equipamento</h2>
      <span class="badge"><span class="dot"></span><span id="mode">Carregando...</span></span>
    </div>
    <div class="stats" style="margin-top:14px">
      <div class="stat"><span>Rede</span><strong id="ssid">—</strong></div>
      <div class="stat"><span>Endereço</span><strong id="ip">—</strong></div>
      <div class="stat"><span>mDNS</span><strong id="mdns">—</strong></div>
      <div class="stat"><span>LittleFS livre</span><strong id="fs">—</strong></div>
    </div>
  </section>

  <section class="card">
    <h2>Configuração Wi-Fi</h2>
    <p class="note">No modo AP, acesse esta mesma página em <code>192.168.4.1</code>.</p>
    <label for="wifiSsid">SSID</label>
    <input id="wifiSsid" list="networks" autocomplete="off">
    <datalist id="networks"></datalist>
    <label for="wifiPass">Senha</label>
    <input id="wifiPass" type="password" autocomplete="new-password">
    <button class="secondary" onclick="scanWifi()">Procurar redes</button>
    <button onclick="saveWifi()">Salvar e reiniciar</button>
    <div id="wifiMsg" class="message"></div>
  </section>

  <section class="card">
    <h2>Destino OTA</h2>
    <label for="target">IP, hostname ou mDNS</label>
    <input id="target" placeholder="controle-urp.local">
    <div class="row">
      <div>
        <label for="port">Porta OTA</label>
        <input id="port" type="number" min="1" max="65535">
      </div>
      <div>
        <label for="timeout">Timeout (s)</label>
        <input id="timeout" type="number" min="2" max="300">
      </div>
    </div>
    <label for="otaPass">Senha OTA opcional</label>
    <input id="otaPass" type="password">
    <button onclick="saveTarget()">Salvar destino</button>
    <div id="targetMsg" class="message"></div>
  </section>

  <section class="card full">
    <h2>Firmware remoto</h2>
    <p class="note">Envie o arquivo principal <code>firmware.bin</code>. O gravador verifica tamanho, cabeçalho e MD5 antes de armazená-lo.</p>
    <input id="firmware" type="file" accept=".bin,application/octet-stream">
    <button onclick="uploadFirmware()">Enviar firmware ao GravadorESP</button>
    <div class="progress"><div id="bar"></div></div>
    <div id="uploadMsg" class="message"></div>
    <div class="stats" style="margin-top:12px">
      <div class="stat"><span>Arquivo armazenado</span><strong id="fwName">Nenhum</strong></div>
      <div class="stat"><span>Tamanho</span><strong id="fwSize">—</strong></div>
      <div class="stat"><span>MD5</span><strong id="fwMd5">—</strong></div>
      <div class="stat"><span>Destino resolvido</span><strong id="otaIp">—</strong></div>
    </div>
    <h2 style="margin-top:22px">Gravação OTA</h2>
    <div class="progress"><div id="otaBar"></div></div>
    <div id="otaMsg" class="message">Aguardando comando.</div>
    <div class="stats" style="margin-top:12px">
      <div class="stat"><span>Estado</span><strong id="otaState">Aguardando</strong></div>
      <div class="stat"><span>Progresso</span><strong id="otaProgress">0%</strong></div>
    </div>
    <div class="row">
      <button id="startOta" onclick="startOta()">Gravar ESP remoto</button>
      <button id="cancelOta" class="secondary" onclick="cancelOta()">Cancelar transmissão</button>
    </div>
    <button class="danger" onclick="deleteFirmware()">Excluir firmware armazenado</button>
  </section>
</div>

<footer>GravadorESP 1.2 · transmissor ArduinoOTA · GPIO13 desativado</footer>
</main>

<script>
const $=id=>document.getElementById(id);
const fmt=n=>{
  if(n===null||n===undefined)return "—";
  if(n<1024)return n+" B";
  if(n<1048576)return (n/1024).toFixed(1)+" KiB";
  return (n/1048576).toFixed(2)+" MiB";
};
function msg(id,text,type=""){
  const e=$(id); e.textContent=text; e.className="message "+type;
}
let targetDirty=false;
["target","port","timeout","otaPass"].forEach(id=>{
  $(id).addEventListener("input",()=>targetDirty=true);
});
async function refresh(){
  try{
    const r=await fetch("/api/status",{cache:"no-store"});
    const s=await r.json();
    $("mode").textContent=s.mode;
    $("ssid").textContent=s.ssid||"Sem rede externa";
    $("ip").textContent=s.ip;
    $("mdns").textContent=s.mdns||"Indisponível no modo AP";
    $("fs").textContent=fmt(s.fsFree)+" de "+fmt(s.fsTotal);
    if(!targetDirty){
      $("target").value=s.target.host||"";
      $("port").value=s.target.port||8266;
      $("timeout").value=s.target.timeout||10;
    }
    $("fwName").textContent=s.firmware.exists?s.firmware.name:"Nenhum";
    $("fwSize").textContent=s.firmware.exists?fmt(s.firmware.size):"—";
    $("fwMd5").textContent=s.firmware.md5||"—";
    $("otaState").textContent=s.ota.state;
    $("otaProgress").textContent=s.ota.progress+"%";
    $("otaBar").style.width=s.ota.progress+"%";
    $("otaIp").textContent=s.ota.resolvedIp||"—";
    msg("otaMsg",s.ota.message,s.ota.state==="Erro"?"error":(s.ota.state==="Concluído"?"ok":""));
    $("startOta").disabled=s.ota.busy||!s.firmware.exists;
    $("cancelOta").disabled=!s.ota.busy;
  }catch(e){}
}
async function scanWifi(){
  msg("wifiMsg","Procurando redes...");
  try{
    const r=await fetch("/api/scan");
    const a=await r.json();
    $("networks").innerHTML="";
    a.forEach(n=>{
      const o=document.createElement("option");
      o.value=n.ssid;
      o.label=n.ssid+" ("+n.rssi+" dBm)";
      $("networks").appendChild(o);
    });
    msg("wifiMsg",a.length+" rede(s) encontrada(s).","ok");
  }catch(e){msg("wifiMsg","Falha ao procurar redes.","error")}
}
async function saveWifi(){
  const p=new URLSearchParams();
  p.set("ssid",$("wifiSsid").value);
  p.set("password",$("wifiPass").value);
  msg("wifiMsg","Salvando...");
  const r=await fetch("/wifi/save",{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body:p});
  const j=await r.json();
  msg("wifiMsg",j.message,j.ok?"ok":"error");
}
async function saveTarget(){
  const p=new URLSearchParams();
  p.set("host",$("target").value);
  p.set("port",$("port").value);
  p.set("timeout",$("timeout").value);
  p.set("password",$("otaPass").value);
  const r=await fetch("/config/save",{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body:p});
  const j=await r.json();
  msg("targetMsg",j.message,j.ok?"ok":"error");
  if(j.ok)targetDirty=false;
}
async function startOta(){
  const r=await fetch("/ota/start",{method:"POST"});
  const j=await r.json();
  msg("otaMsg",j.message,j.ok?"ok":"error");
  refresh();
}
async function cancelOta(){
  const r=await fetch("/ota/cancel",{method:"POST"});
  const j=await r.json();
  msg("otaMsg",j.message,j.ok?"ok":"error");
  refresh();
}
function uploadFirmware(){
  const f=$("firmware").files[0];
  if(!f){msg("uploadMsg","Selecione um arquivo .bin.","error");return}
  const data=new FormData();
  data.append("firmware",f,f.name);
  const xhr=new XMLHttpRequest();
  xhr.open("POST","/upload");
  xhr.upload.onprogress=e=>{
    if(e.lengthComputable)$("bar").style.width=((e.loaded/e.total)*100).toFixed(1)+"%";
  };
  xhr.onload=()=>{
    let j={ok:false,message:"Resposta inválida."};
    try{j=JSON.parse(xhr.responseText)}catch(e){}
    msg("uploadMsg",j.message,j.ok?"ok":"error");
    if(!j.ok)$("bar").style.width="0";
    refresh();
  };
  xhr.onerror=()=>msg("uploadMsg","Falha de comunicação durante o envio.","error");
  msg("uploadMsg","Enviando "+f.name+"...");
  xhr.send(data);
}
async function deleteFirmware(){
  const r=await fetch("/firmware/delete",{method:"POST"});
  const j=await r.json();
  msg("uploadMsg",j.message,j.ok?"ok":"error");
  $("bar").style.width="0";
  refresh();
}
refresh();
setInterval(refresh,3000);
</script>
</body>
</html>
)HTML";

void setLed(const BiColorLed& led, LedColor color) {
    digitalWrite(led.red,
                 (color == LedColor::Red || color == LedColor::Both) ? HIGH : LOW);
    digitalWrite(led.green,
                 (color == LedColor::Green || color == LedColor::Both) ? HIGH : LOW);
}

String jsonEscape(const String& input) {
    String output;
    output.reserve(input.length() + 8);

    for (size_t i = 0; i < input.length(); ++i) {
        const char c = input[i];
        switch (c) {
            case '\\': output += F("\\\\"); break;
            case '"': output += F("\\\""); break;
            case '\n': output += F("\\n"); break;
            case '\r': output += F("\\r"); break;
            case '\t': output += F("\\t"); break;
            default:
                if (static_cast<uint8_t>(c) >= 0x20) {
                    output += c;
                }
                break;
        }
    }

    return output;
}

String boolJson(bool value) {
    return value ? F("true") : F("false");
}

void sendJson(int statusCode, bool ok, const String& message) {
    String response = F("{\"ok\":");
    response += boolJson(ok);
    response += F(",\"message\":\"");
    response += jsonEscape(message);
    response += F("\"}");
    server.send(statusCode, F("application/json"), response);
}

void initializePins() {
    pinMode(Pins::LED_WIFI_RED, OUTPUT);
    pinMode(Pins::LED_WIFI_GREEN, OUTPUT);
    pinMode(Pins::LED_SERVER_RED, OUTPUT);
    pinMode(Pins::LED_SERVER_GREEN, OUTPUT);
    pinMode(Pins::LED_RS485_RED, OUTPUT);

    // Canal defeituoso deliberadamente não utilizado.
    pinMode(Pins::LED_RS485_GREEN_UNUSED, INPUT_PULLDOWN);

    pinMode(Pins::BUTTON, INPUT);

    setLed(LED_WIFI, LedColor::Off);
    setLed(LED_SERVER, LedColor::Off);
    digitalWrite(Pins::LED_RS485_RED, LOW);
}

void loadPreferences() {
    prefs.begin("gravador", false);

    savedSsid = prefs.getString("wifi_ssid", "");
    savedWifiPassword = prefs.getString("wifi_pass", "");

    targetHost = prefs.isKey("ota_host") ? prefs.getString("ota_host", "") : "";
    targetOtaPassword = prefs.isKey("ota_pass") ? prefs.getString("ota_pass", "") : "";
    targetPort = prefs.isKey("ota_port") ? prefs.getUShort("ota_port", 8266) : 8266;
    targetTimeoutSeconds = prefs.isKey("ota_timeout") ? prefs.getUInt("ota_timeout", 10) : 10;

    firmwareMd5 = prefs.isKey("fw_md5") ? prefs.getString("fw_md5", "") : "";
    uploadOriginalName = prefs.isKey("fw_name") ? prefs.getString("fw_name", "") : "";
}

bool connectStation(uint32_t timeoutMs) {
    if (savedSsid.isEmpty()) {
        return false;
    }

    networkState = NetworkState::Connecting;
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(HOSTNAME);
    WiFi.begin(savedSsid.c_str(), savedWifiPassword.c_str());

    Serial.printf("Conectando ao Wi-Fi \"%s\"", savedSsid.c_str());

    const uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
        delay(250);
        Serial.print('.');
    }
    Serial.println();

    if (WiFi.status() != WL_CONNECTED) {
        WiFi.disconnect(false, false);
        return false;
    }

    networkState = NetworkState::Station;
    Serial.printf("Wi-Fi conectado. IP: %s\n", WiFi.localIP().toString().c_str());
    return true;
}

void startAccessPoint() {
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(AP_SSID, AP_PASSWORD);

    networkState = NetworkState::AccessPoint;

    Serial.printf("Modo AP ativo: %s\n", AP_SSID);
    Serial.printf("Senha do AP: %s\n", AP_PASSWORD);
    Serial.printf("Pagina: http://%s\n", WiFi.softAPIP().toString().c_str());
}

void startMdns() {
    if (networkState != NetworkState::Station) {
        mdnsReady = false;
        return;
    }

    mdnsReady = MDNS.begin(HOSTNAME);
    if (mdnsReady) {
        MDNS.addService("http", "tcp", 80);
        Serial.printf("mDNS ativo: http://%s.local\n", HOSTNAME);
    } else {
        Serial.println("Falha ao iniciar mDNS.");
    }
}

bool mountLittleFs() {
    // O último argumento seleciona explicitamente a partição "littlefs".
    if (!LittleFS.begin(true, "/littlefs", 10, "littlefs")) {
        Serial.println("Falha ao montar LittleFS.");
        return false;
    }

    Serial.printf("LittleFS: %u bytes livres de %u.\n",
                  static_cast<unsigned int>(LittleFS.totalBytes() - LittleFS.usedBytes()),
                  static_cast<unsigned int>(LittleFS.totalBytes()));
    return true;
}

void refreshFirmwareMetadata() {
    firmwareAvailable = false;
    firmwareStoredSize = 0;

    if (!littleFsReady) {
        return;
    }

    File root = LittleFS.open("/");
    if (!root) {
        return;
    }

    File entry = root.openNextFile();
    while (entry) {
        String name = entry.name();
        if (name == FIRMWARE_PATH || name == "firmware.bin") {
            firmwareAvailable = true;
            firmwareStoredSize = entry.size();
            entry.close();
            break;
        }
        entry.close();
        entry = root.openNextFile();
    }
    root.close();
}

bool startStoredFirmwareOta() {
    if (!firmwareAvailable) {
        Serial.println("[OTA-TX] Nenhum firmware armazenado.");
        return false;
    }

    return otaSender.start(targetHost,
                           targetPort,
                           targetOtaPassword,
                           FIRMWARE_PATH,
                           firmwareMd5,
                           targetTimeoutSeconds);
}

String currentModeText() {
    switch (networkState) {
        case NetworkState::Station:
            return F("Conectado ao Wi-Fi");
        case NetworkState::AccessPoint:
            return F("Modo AP de manutenção");
        case NetworkState::Connecting:
        default:
            return F("Conectando");
    }
}

String currentIpText() {
    if (networkState == NetworkState::Station) {
        return WiFi.localIP().toString();
    }
    return WiFi.softAPIP().toString();
}

void handleRoot() {
    server.send_P(200, "text/html; charset=utf-8", MAIN_HTML);
}

void handleStatus() {
    const size_t fsTotal = littleFsReady ? LittleFS.totalBytes() : 0;
    const size_t fsFree =
        littleFsReady ? LittleFS.totalBytes() - LittleFS.usedBytes() : 0;

    String response;
    response.reserve(1200);

    response += F("{\"mode\":\"");
    response += jsonEscape(currentModeText());
    response += F("\",\"ssid\":\"");
    response += jsonEscape(networkState == NetworkState::Station ? WiFi.SSID() : String(AP_SSID));
    response += F("\",\"ip\":\"");
    response += jsonEscape(currentIpText());
    response += F("\",\"mdns\":\"");
    response += mdnsReady ? String(HOSTNAME) + F(".local") : String();
    response += F("\",\"fsTotal\":");
    response += String(fsTotal);
    response += F(",\"fsFree\":");
    response += String(fsFree);

    response += F(",\"target\":{\"host\":\"");
    response += jsonEscape(targetHost);
    response += F("\",\"port\":");
    response += String(targetPort);
    response += F(",\"timeout\":");
    response += String(targetTimeoutSeconds);
    response += F("}");

    response += F(",\"firmware\":{\"exists\":");
    response += boolJson(firmwareAvailable);
    response += F(",\"name\":\"");
    response += jsonEscape(uploadOriginalName.isEmpty() ? String("firmware.bin") : uploadOriginalName);
    response += F("\",\"size\":");
    response += String(firmwareStoredSize);
    response += F(",\"md5\":\"");
    response += jsonEscape(firmwareMd5);
    response += F("\"}");

    response += F(",\"ota\":{\"state\":\"");
    response += jsonEscape(otaSender.stateText());
    response += F("\",\"message\":\"");
    response += jsonEscape(otaSender.message());
    response += F("\",\"resolvedIp\":\"");
    response += jsonEscape(otaSender.resolvedIpText());
    response += F("\",\"busy\":");
    response += boolJson(otaSender.busy());
    response += F(",\"progress\":");
    response += String(otaSender.progress());
    response += F(",\"sent\":");
    response += String(otaSender.bytesSent());
    response += F(",\"total\":");
    response += String(otaSender.totalBytes());
    response += F("}}");

    server.send(200, F("application/json"), response);
}

void handleWifiScan() {
    const int count = WiFi.scanNetworks(false, true);

    String response = F("[");
    bool first = true;

    for (int i = 0; i < count; ++i) {
        if (!first) {
            response += ',';
        }
        first = false;

        response += F("{\"ssid\":\"");
        response += jsonEscape(WiFi.SSID(i));
        response += F("\",\"rssi\":");
        response += String(WiFi.RSSI(i));
        response += '}';
    }

    response += ']';
    WiFi.scanDelete();

    server.send(200, F("application/json"), response);
}

void handleWifiSave() {
    if (!server.hasArg("ssid")) {
        sendJson(400, false, F("SSID não recebido."));
        return;
    }

    const String ssid = server.arg("ssid");
    const String password = server.arg("password");

    if (ssid.isEmpty() || ssid.length() > 32) {
        sendJson(400, false, F("Informe um SSID válido."));
        return;
    }

    prefs.putString("wifi_ssid", ssid);
    prefs.putString("wifi_pass", password);

    sendJson(200, true, F("Credenciais salvas. O GravadorESP será reiniciado."));
    restartPending = true;
    restartAtMs = millis() + 1500;
}

void handleConfigSave() {
    const String host = server.arg("host");
    const long port = server.arg("port").toInt();
    const long timeout = server.arg("timeout").toInt();
    const String password = server.arg("password");

    if (host.isEmpty()) {
        sendJson(400, false, F("Informe o IP ou hostname do destino."));
        return;
    }

    if (port < 1 || port > 65535) {
        sendJson(400, false, F("Porta inválida."));
        return;
    }

    if (timeout < 2 || timeout > 300) {
        sendJson(400, false, F("Timeout deve ficar entre 2 e 300 segundos."));
        return;
    }

    targetHost = host;
    targetPort = static_cast<uint16_t>(port);
    targetTimeoutSeconds = static_cast<uint32_t>(timeout);
    targetOtaPassword = password;

    prefs.putString("ota_host", targetHost);
    prefs.putUShort("ota_port", targetPort);
    prefs.putUInt("ota_timeout", targetTimeoutSeconds);
    prefs.putString("ota_pass", targetOtaPassword);

    sendJson(200, true, F("Destino OTA salvo."));
}

void cleanupFailedUpload(const String& error) {
    if (uploadFile) {
        uploadFile.close();
    }

    if (littleFsReady && LittleFS.exists(TEMP_FIRMWARE_PATH)) {
        LittleFS.remove(TEMP_FIRMWARE_PATH);
    }

    uploadInProgress = false;
    uploadSucceeded = false;
    uploadError = error;

    Serial.printf("Falha no upload: %s\n", error.c_str());
}

void handleUploadData() {
    HTTPUpload& upload = server.upload();

    switch (upload.status) {
        case UPLOAD_FILE_START: {
            uploadInProgress = true;
            uploadSucceeded = false;
            uploadHeaderValid = false;
            uploadBytes = 0;
            uploadOriginalName = upload.filename;
            uploadError = "";
            firmwareMd5 = "";

            if (!littleFsReady) {
                cleanupFailedUpload(F("LittleFS indisponível."));
                return;
            }

            if (LittleFS.exists(TEMP_FIRMWARE_PATH)) {
                LittleFS.remove(TEMP_FIRMWARE_PATH);
            }

            uploadFile = LittleFS.open(TEMP_FIRMWARE_PATH, FILE_WRITE);
            if (!uploadFile) {
                cleanupFailedUpload(F("Não foi possível criar o arquivo temporário."));
                return;
            }

            uploadMd5.begin();
            Serial.printf("Recebendo firmware: %s\n", upload.filename.c_str());
            break;
        }

        case UPLOAD_FILE_WRITE: {
            if (!uploadInProgress || !uploadFile) {
                return;
            }

            if (uploadBytes == 0 && upload.currentSize > 0) {
                uploadHeaderValid = upload.buf[0] == 0xE9;
            }

            const size_t freeBytes =
                LittleFS.totalBytes() - LittleFS.usedBytes();

            if (upload.currentSize > freeBytes) {
                cleanupFailedUpload(F("Firmware maior que o espaço livre."));
                return;
            }

            const size_t written =
                uploadFile.write(upload.buf, upload.currentSize);

            if (written != upload.currentSize) {
                cleanupFailedUpload(F("Falha ao escrever no LittleFS."));
                return;
            }

            uploadMd5.add(upload.buf, upload.currentSize);
            uploadBytes += upload.currentSize;
            break;
        }

        case UPLOAD_FILE_END: {
            if (!uploadInProgress || !uploadFile) {
                return;
            }

            uploadFile.close();
            uploadInProgress = false;

            if (uploadBytes < 1024) {
                cleanupFailedUpload(F("Arquivo pequeno demais para ser um firmware."));
                return;
            }

            if (!uploadHeaderValid) {
                cleanupFailedUpload(F("Cabeçalho inválido: o arquivo não começa com 0xE9."));
                return;
            }

            uploadMd5.calculate();
            firmwareMd5 = uploadMd5.toString();

            if (LittleFS.exists(FIRMWARE_PATH)) {
                LittleFS.remove(FIRMWARE_PATH);
            }

            if (!LittleFS.rename(TEMP_FIRMWARE_PATH, FIRMWARE_PATH)) {
                cleanupFailedUpload(F("Não foi possível finalizar o arquivo."));
                return;
            }

            prefs.putString("fw_md5", firmwareMd5);
            prefs.putString("fw_name", uploadOriginalName);
            firmwareAvailable = true;
            firmwareStoredSize = uploadBytes;

            uploadSucceeded = true;
            Serial.printf("Firmware armazenado: %u bytes, MD5 %s\n",
                          static_cast<unsigned int>(uploadBytes),
                          firmwareMd5.c_str());
            break;
        }

        case UPLOAD_FILE_ABORTED:
            cleanupFailedUpload(F("Upload cancelado pelo cliente."));
            break;

        default:
            break;
    }
}

void handleUploadResult() {
    if (uploadSucceeded) {
        sendJson(200, true, F("Firmware recebido, validado e armazenado."));
    } else {
        sendJson(400, false,
                 uploadError.isEmpty() ? F("Falha desconhecida no upload.") : uploadError);
    }
}

void handleDeleteFirmware() {
    if (!littleFsReady) {
        sendJson(500, false, F("LittleFS indisponível."));
        return;
    }

    bool ok = true;

    if (LittleFS.exists(FIRMWARE_PATH)) {
        ok = LittleFS.remove(FIRMWARE_PATH);
    }

    if (LittleFS.exists(TEMP_FIRMWARE_PATH)) {
        LittleFS.remove(TEMP_FIRMWARE_PATH);
    }

    if (!ok) {
        sendJson(500, false, F("Não foi possível excluir o firmware."));
        return;
    }

    firmwareMd5 = "";
    uploadOriginalName = "";
    firmwareAvailable = false;
    firmwareStoredSize = 0;
    prefs.remove("fw_md5");
    prefs.remove("fw_name");

    sendJson(200, true, F("Firmware excluído."));
}

void handleOtaStart() {
    if (otaSender.busy()) {
        sendJson(409, false, F("Já existe uma transmissão OTA em andamento."));
        return;
    }

    if (!startStoredFirmwareOta()) {
        sendJson(400, false, otaSender.message());
        return;
    }

    sendJson(202, true, F("Transmissão OTA iniciada."));
}

void handleOtaCancel() {
    if (!otaSender.busy()) {
        sendJson(400, false, F("Não há transmissão em andamento."));
        return;
    }

    otaSender.cancel();
    sendJson(200, true, F("Transmissão cancelada."));
}

void handleNotFound() {
    sendJson(404, false, F("Rota não encontrada."));
}

void startWebServer() {
    server.on("/", HTTP_GET, handleRoot);
    server.on("/api/status", HTTP_GET, handleStatus);
    server.on("/api/scan", HTTP_GET, handleWifiScan);
    server.on("/wifi/save", HTTP_POST, handleWifiSave);
    server.on("/config/save", HTTP_POST, handleConfigSave);
    server.on("/upload", HTTP_POST, handleUploadResult, handleUploadData);
    server.on("/firmware/delete", HTTP_POST, handleDeleteFirmware);
    server.on("/ota/start", HTTP_POST, handleOtaStart);
    server.on("/ota/cancel", HTTP_POST, handleOtaCancel);
    server.onNotFound(handleNotFound);

    server.begin();
    Serial.println("Servidor web iniciado na porta 80.");
}

void updateLeds() {
    const uint32_t now = millis();

    if (now - lastLedUpdateMs >= 350) {
        lastLedUpdateMs = now;
        blinkState = !blinkState;
    }

    switch (networkState) {
        case NetworkState::Connecting:
            setLed(LED_WIFI, blinkState ? LedColor::Red : LedColor::Off);
            break;

        case NetworkState::Station:
            setLed(LED_WIFI, LedColor::Green);
            break;

        case NetworkState::AccessPoint:
            setLed(LED_WIFI, LedColor::Red);
            break;
    }

    if (!littleFsReady || !uploadError.isEmpty()) {
        setLed(LED_SERVER, LedColor::Red);
    } else if (uploadInProgress) {
        setLed(LED_SERVER, blinkState ? LedColor::Green : LedColor::Off);
    } else {
        setLed(LED_SERVER, LedColor::Green);
    }

    bool otaLed = false;
    if (otaSender.busy()) {
        const bool fast = otaSender.state() == EspOtaSender::State::SendingChunk ||
                          otaSender.state() == EspOtaSender::State::WaitingChunkAck;
        otaLed = ((now / (fast ? 120UL : 350UL)) % 2UL) != 0;
    } else if (otaSender.state() == EspOtaSender::State::Success &&
               now - otaSender.stateSince() < 5000UL) {
        otaLed = true;
    } else if (otaSender.state() == EspOtaSender::State::Error) {
        otaLed = ((now / 500UL) % 2UL) != 0;
    } else {
        otaLed = static_cast<int32_t>(rsRedPulseUntilMs - now) > 0;
    }

    digitalWrite(Pins::LED_RS485_RED, otaLed ? HIGH : LOW);
}

void handleButton() {
    const bool raw = digitalRead(Pins::BUTTON);
    const uint32_t now = millis();

    if (raw != lastRawButton) {
        lastRawButton = raw;
        lastButtonTransitionMs = now;
    }

    if (now - lastButtonTransitionMs < 35 || raw == stableButton) {
        return;
    }

    stableButton = raw;

    if (stableButton == LOW) {
        buttonPressedAtMs = now;
        rsRedPulseUntilMs = now + 150;
        Serial.println("[BOTAO] Pressionado.");
    } else {
        const uint32_t duration = now - buttonPressedAtMs;
        rsRedPulseUntilMs = now + 150;
        Serial.printf("[BOTAO] Solto após %lu ms.\n",
                      static_cast<unsigned long>(duration));

        if (duration >= 3000 && otaSender.busy()) {
            otaSender.cancel();
            Serial.println("[BOTAO] Transmissão OTA cancelada.");
        } else if (!otaSender.busy()) {
            if (startStoredFirmwareOta()) {
                Serial.println("[BOTAO] Transmissão OTA iniciada.");
            } else {
                Serial.printf("[BOTAO] Não foi possível iniciar: %s\n",
                              otaSender.message().c_str());
            }
        }
    }
}

void printStartupSummary() {
    Serial.println();
    Serial.println("============================================================");
    Serial.println(" GravadorESP 1.2 - Transmissor ArduinoOTA");
    Serial.println("============================================================");
    Serial.printf("Flash: %.2f MiB\n", ESP.getFlashChipSize() / 1048576.0);
    Serial.printf("GPIO13: desativado (LED verde RS-485 não utilizado)\n");

    if (networkState == NetworkState::Station) {
        Serial.printf("Pagina: http://%s\n", WiFi.localIP().toString().c_str());
        if (mdnsReady) {
            Serial.printf("Pagina: http://%s.local\n", HOSTNAME);
        }
    } else {
        Serial.printf("AP: %s\n", AP_SSID);
        Serial.printf("Senha: %s\n", AP_PASSWORD);
        Serial.printf("Pagina: http://%s\n", WiFi.softAPIP().toString().c_str());
    }

    if (littleFsReady) {
        Serial.printf("LittleFS livre: %u bytes\n",
                      static_cast<unsigned int>(
                          LittleFS.totalBytes() - LittleFS.usedBytes()));
    }
    Serial.println("============================================================");
}

void setup() {
    Serial.begin(115200);
    delay(900);

    initializePins();
    loadPreferences();
    littleFsReady = mountLittleFs();
    refreshFirmwareMetadata();

    const bool connected = connectStation(15000);
    if (!connected) {
        startAccessPoint();
    }

    startMdns();
    otaSender.beginServer();
    startWebServer();
    printStartupSummary();

    lastRawButton = digitalRead(Pins::BUTTON);
    stableButton = lastRawButton;
}

void loop() {
    server.handleClient();
    otaSender.loop();
    handleButton();
    updateLeds();

    if (restartPending &&
        static_cast<int32_t>(millis() - restartAtMs) >= 0) {
        delay(100);
        ESP.restart();
    }

    delay(2);
}
