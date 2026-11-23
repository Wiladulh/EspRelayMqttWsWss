#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <time.h>

#if defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <ESP8266WebServer.h>
  #include <WiFiClientSecureBearSSL.h>
  ESP8266WebServer server(80);
#elif defined(ESP32)
  #include <WiFi.h>
  #include <WebServer.h>
  #include <WiFiClientSecure.h>
  WebServer server(80);
#endif

#include <WebSocketsClient.h>
#include <PubSubClient.h>
#include "config.h"
#include "board_pins.h"

DeviceConfig cfg;
WebSocketsClient wsClient;

// Custom Client wrapper bridging WebSocketsClient -> PubSubClient
class WsMqttBridge : public Client {
public:
    WebSocketsClient* ws;
    uint8_t rxBuf[2048];
    size_t rxHead = 0, rxTail = 0;
    bool overflowed = false;

    WsMqttBridge(WebSocketsClient* w) : ws(w) {}
    void resetRxBuffer() {
        rxHead = 0;
        rxTail = 0;
        overflowed = false;
    }
    void pushData(uint8_t* payload, size_t len) {
        for (size_t i = 0; i < len; i++) {
            size_t next = (rxHead + 1) % sizeof(rxBuf);
            if (next == rxTail) { overflowed = true; return; }
            rxBuf[rxHead] = payload[i];
            rxHead = next;
        }
    }
    int connect(IPAddress ip, uint16_t port) override { return 1; }
    int connect(const char* host, uint16_t port) override { return 1; }
    size_t write(uint8_t b) override { return write(&b, 1); }
    size_t write(const uint8_t* buf, size_t size) override {
        return ws->sendBIN(buf, size) ? size : 0;
    }
    int available() override {
        ws->loop();
        if (overflowed) {
            overflowed = false;
            rxHead = rxTail = 0;
            ws->disconnect();
            return 0;
        }
        return (rxHead >= rxTail) ? (rxHead - rxTail) : (sizeof(rxBuf) - rxTail + rxHead);
    }
    int read() override {
        if (rxHead == rxTail) return -1;
        uint8_t b = rxBuf[rxTail];
        rxTail = (rxTail + 1) % sizeof(rxBuf);
        return b;
    }
    int read(uint8_t* buf, size_t size) override {
        size_t count = 0;
        while (count < size && available() > 0) {
            buf[count++] = read();
        }
        return count;
    }
    int peek() override { return (rxHead == rxTail) ? -1 : rxBuf[rxTail]; }
    void flush() override {}
    void stop() override { ws->disconnect(); }
    uint8_t connected() override { return ws->isConnected(); }
    operator bool() override { return ws->isConnected(); }
};

WsMqttBridge wsBridge(&wsClient);
PubSubClient mqtt(wsBridge);

unsigned long lastWifiCheck = 0;
unsigned long wifiAttemptStartedAt = 0;
unsigned long lastMqttReconnect = 0;
bool wifiInitialized = false;
bool wifiAttemptActive = false;
bool wifiAttemptSecondary = true;
bool wifiWasConnected = false;
bool wsConfigured = false;
bool tlsConfigWarningShown = false;
bool wsBinaryFragmentActive = false;
bool fallbackApActive = false;
bool storageReady = false;
int8_t appliedRelayPins[MAX_CHANNELS] = {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1};
int8_t appliedButtonPins[MAX_CHANNELS] = {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1};
bool appliedRelayActiveLow[MAX_CHANNELS] = {false,false,false,false,false,false,false,false,false,false};
unsigned long lastBtnDebounce[MAX_CHANNELS] = {0};
bool lastBtnReading[MAX_CHANNELS];
time_t lastCheckedMinute = 0;
void startFallbackAp();
void disconnectStation();

#if defined(ESP32)
void onWifiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_CONNECTED:
            Serial.println("Wi-Fi event: station associated; waiting for DHCP IP.");
            break;
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            Serial.print("Wi-Fi event: station received IP ");
            Serial.println(IPAddress(info.got_ip.ip_info.ip.addr));
            break;
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            Serial.print("Wi-Fi event: station disconnected, reason=");
            Serial.println(info.wifi_sta_disconnected.reason);
            break;
        case ARDUINO_EVENT_WIFI_STA_LOST_IP:
            Serial.println("Wi-Fi event: station lost its IP address.");
            break;
        case ARDUINO_EVENT_WIFI_AP_START:
            Serial.println("Wi-Fi event: fallback AP started.");
            break;
        case ARDUINO_EVENT_WIFI_AP_STOP:
            Serial.println("Wi-Fi event: fallback AP stopped.");
            break;
        default:
            break;
    }
}
#endif

String makeDefaultApPassword() {
    // Deliberately simple setup password; users should change it in WebUI.
    return "11111111";
}

void loadDefaultConfig() {
    cfg.deviceId = "mhome-node-01";
    cfg.ssid1 = "WiFi_Utama";
    cfg.pass1 = "password123";
    cfg.ssid2 = "WiFi_Cadangan";
    cfg.pass2 = "password123";
    cfg.mqttHost = "mqtt.giebux.my.id";
    cfg.mqttPort = 443;
    cfg.mqttPath = "/mqtt";
    cfg.mqttUser = "";
    cfg.mqttPass = "";
    cfg.mqttCaCert = "";
    cfg.mqttUseSsl = true;
    cfg.apPassword = makeDefaultApPassword();
    cfg.timezoneOffset = 7;

    for (int i = 0; i < MAX_CHANNELS; i++) {
        cfg.channels[i].id = i + 1;
        cfg.channels[i].name = "Relay " + String(i + 1);
        cfg.channels[i].relayPin = -1;
        cfg.channels[i].buttonPin = -1;
        cfg.channels[i].activeLow = true;
        cfg.channels[i].state = false;
        cfg.channels[i].scheduleEnabled = false;
        cfg.channels[i].scheduleHourOn = 18;
        cfg.channels[i].scheduleMinOn = 0;
        cfg.channels[i].scheduleHourOff = 6;
        cfg.channels[i].scheduleMinOff = 0;
    }
}

bool isValidDeviceId(const String& value) {
    if (value.length() < 1 || value.length() > 26) return false;
    for (size_t i = 0; i < value.length(); i++) {
        const char c = value[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
    }
    return true;
}

bool isValidWifiPassword(const String& value) {
    if (value.length() == 0 || (value.length() >= 8 && value.length() <= 63)) return true;
    if (value.length() != 64) return false;
    for (size_t i = 0; i < value.length(); i++) {
        const char c = value[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
    }
    return true;
}

bool isJsonObjectFile(const String& path) {
    File file = LittleFS.open(path, "r");
    if (!file) return false;
    JsonDocument document;
    const bool valid = deserializeJson(document, file) == DeserializationError::Ok &&
                       document.is<JsonObject>();
    file.close();
    return valid;
}

bool recoverConfigFile() {
    const String backupPath = String(CONFIG_FILE) + ".bak";
    const bool hasConfig = LittleFS.exists(CONFIG_FILE);
    const bool hasBackup = LittleFS.exists(backupPath);

    if (hasConfig && isJsonObjectFile(CONFIG_FILE)) {
        if (hasBackup && !LittleFS.remove(backupPath)) {
            Serial.println("WARNING: Valid config is available, but stale backup cleanup failed.");
        }
        return true;
    }
    if (hasBackup && isJsonObjectFile(backupPath)) {
        if (hasConfig && !LittleFS.remove(CONFIG_FILE)) {
            Serial.println("ERROR: Could not remove invalid config; backup preserved.");
            return false;
        }
        if (!LittleFS.rename(backupPath, CONFIG_FILE)) {
            Serial.println("ERROR: Config backup restore failed; backup preserved.");
            return false;
        }
        return isJsonObjectFile(CONFIG_FILE);
    }
    if (hasConfig || hasBackup) {
        Serial.println("ERROR: No valid config or recoverable backup; preserving stored files.");
        return false;
    }
    return true;
}

bool writeConfigDocument(JsonDocument& doc) {
    const String tempPath = String(CONFIG_FILE) + ".tmp";
    const String backupPath = String(CONFIG_FILE) + ".bak";
    if (!recoverConfigFile()) return false;
    LittleFS.remove(tempPath);

    File tempFile = LittleFS.open(tempPath, "w");
    if (!tempFile) return false;
    const size_t expected = measureJson(doc);
    const size_t written = serializeJson(doc, tempFile);
    tempFile.flush();
    const size_t stored = tempFile.size();
    tempFile.close();
    if (written != expected || stored != expected || !isJsonObjectFile(tempPath)) {
        LittleFS.remove(tempPath);
        return false;
    }

    const bool hadCurrent = LittleFS.exists(CONFIG_FILE);
    if (hadCurrent && !LittleFS.rename(CONFIG_FILE, backupPath)) {
        LittleFS.remove(tempPath);
        return false;
    }
    if (!LittleFS.rename(tempPath, CONFIG_FILE) || !isJsonObjectFile(CONFIG_FILE)) {
        if (LittleFS.exists(CONFIG_FILE)) LittleFS.remove(CONFIG_FILE);
        if (hadCurrent && !LittleFS.rename(backupPath, CONFIG_FILE)) {
            Serial.println("CRITICAL: New config failed verification and backup restore failed.");
        }
        LittleFS.remove(tempPath);
        return false;
    }
    if (hadCurrent && LittleFS.exists(backupPath) && !LittleFS.remove(backupPath)) {
        Serial.println("WARNING: Config committed, but stale backup cleanup failed.");
    }
    return true;
}

bool saveConfig() {
    JsonDocument doc;
    doc["deviceId"] = cfg.deviceId;
    doc["ssid1"] = cfg.ssid1;
    doc["pass1"] = cfg.pass1;
    doc["ssid2"] = cfg.ssid2;
    doc["pass2"] = cfg.pass2;
    doc["mqttHost"] = cfg.mqttHost;
    doc["mqttPort"] = cfg.mqttPort;
    doc["mqttPath"] = cfg.mqttPath;
    doc["mqttUser"] = cfg.mqttUser;
    doc["mqttPass"] = cfg.mqttPass;
    doc["mqttCaCert"] = cfg.mqttCaCert;
    doc["mqttUseSsl"] = cfg.mqttUseSsl;
    doc["apPassword"] = cfg.apPassword;
    doc["tz"] = cfg.timezoneOffset;

    JsonArray arr = doc["channels"].to<JsonArray>();
    for (int i = 0; i < MAX_CHANNELS; i++) {
        JsonObject o = arr.add<JsonObject>();
        o["id"] = cfg.channels[i].id;
        o["name"] = cfg.channels[i].name;
        o["rPin"] = cfg.channels[i].relayPin;
        o["bPin"] = cfg.channels[i].buttonPin;
        o["aLow"] = cfg.channels[i].activeLow;
        o["state"] = cfg.channels[i].state;
        o["sEn"] = cfg.channels[i].scheduleEnabled;
        o["hOn"] = cfg.channels[i].scheduleHourOn;
        o["mOn"] = cfg.channels[i].scheduleMinOn;
        o["hOff"] = cfg.channels[i].scheduleHourOff;
        o["mOff"] = cfg.channels[i].scheduleMinOff;
    }

    return writeConfigDocument(doc);
}

void loadConfig() {
    loadDefaultConfig();
    if (!recoverConfigFile()) return;
    if (!LittleFS.exists(CONFIG_FILE)) {
        if (!saveConfig()) Serial.println("ERROR: Could not persist default configuration.");
        return;
    }

    JsonDocument doc;
    File file = LittleFS.open(CONFIG_FILE, "r");
    if (!file) {
        Serial.println("ERROR: Could not open configuration; defaults kept in RAM.");
        return;
    }
    const DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error || !doc.is<JsonObject>()) {
        Serial.println("ERROR: Configuration JSON is invalid; stored file preserved.");
        return;
    }

    bool corrected = false;
    String value;

    value = doc["deviceId"].is<const char*>() ? doc["deviceId"].as<String>() : "";
    if (isValidDeviceId(value)) cfg.deviceId = value;
    else corrected = true;

    if (doc["ssid1"].is<const char*>()) {
        value = doc["ssid1"].as<String>();
        if (value.length() <= 32) cfg.ssid1 = value;
        else corrected = true;
    } else corrected = true;
    if (doc["pass1"].is<const char*>()) {
        value = doc["pass1"].as<String>();
        if (isValidWifiPassword(value)) cfg.pass1 = value;
        else corrected = true;
    } else corrected = true;

    if (doc["ssid2"].is<const char*>()) {
        value = doc["ssid2"].as<String>();
        if (value.length() <= 32) cfg.ssid2 = value;
        else corrected = true;
    } else corrected = true;
    if (doc["pass2"].is<const char*>()) {
        value = doc["pass2"].as<String>();
        if (isValidWifiPassword(value)) cfg.pass2 = value;
        else corrected = true;
    } else corrected = true;

    if (doc["mqttHost"].is<const char*>()) {
        value = doc["mqttHost"].as<String>();
        if (value.length() <= 253) cfg.mqttHost = value;
        else corrected = true;
    } else corrected = true;
    if (doc["mqttPort"].is<int>() && doc["mqttPort"].as<int>() >= 1 &&
        doc["mqttPort"].as<int>() <= 65535) cfg.mqttPort = static_cast<uint16_t>(doc["mqttPort"].as<int>());
    else corrected = true;

    if (doc["mqttPath"].is<const char*>()) {
        value = doc["mqttPath"].as<String>();
        if (value.length() > 0 && value.length() <= 128 && value[0] == '/') cfg.mqttPath = value;
        else corrected = true;
    } else corrected = true;
    if (doc["mqttUser"].is<const char*>()) {
        value = doc["mqttUser"].as<String>();
        if (value.length() <= 128) cfg.mqttUser = value;
        else corrected = true;
    } else corrected = true;
    if (doc["mqttPass"].is<const char*>()) {
        value = doc["mqttPass"].as<String>();
        if (value.length() <= 128) cfg.mqttPass = value;
        else corrected = true;
    } else corrected = true;
    if (doc["mqttCaCert"].is<const char*>()) {
        value = doc["mqttCaCert"].as<String>();
        if (value.length() <= 6000 &&
            (value.length() == 0 || (value.indexOf("-----BEGIN CERTIFICATE-----") >= 0 &&
                                     value.indexOf("-----END CERTIFICATE-----") >= 0))) cfg.mqttCaCert = value;
        else corrected = true;
    } else corrected = true;

    if (doc["mqttUseSsl"].is<bool>()) cfg.mqttUseSsl = doc["mqttUseSsl"].as<bool>();
    else corrected = true;
    if (doc["apPassword"].is<const char*>()) {
        value = doc["apPassword"].as<String>();
        if (value.length() >= 8 && value.length() <= 63) cfg.apPassword = value;
        else corrected = true;
    } else corrected = true;
    if (doc["tz"].is<int>() && doc["tz"].as<int>() >= -12 && doc["tz"].as<int>() <= 14)
        cfg.timezoneOffset = static_cast<int8_t>(doc["tz"].as<int>());
    else corrected = true;

    bool usedPins[64] = {false};
    JsonArray channels = doc["channels"].as<JsonArray>();
    if (channels.size() != MAX_CHANNELS) corrected = true;
    for (int i = 0; i < MAX_CHANNELS; i++) {
        JsonVariant channelValue = channels[i];
        if (!channelValue.is<JsonObject>()) {
            corrected = true;
            continue;
        }
        JsonObject channel = channelValue.as<JsonObject>();
        value = channel["name"].is<const char*>() ? channel["name"].as<String>() : "";
        if (value.length() > 0 && value.length() <= 32) cfg.channels[i].name = value;
        else corrected = true;

        if (!channel["rPin"].is<int>()) corrected = true;
        int relayPin = channel["rPin"].is<int>() ? channel["rPin"].as<int>() : -1;
        if (relayPin < 0 || relayPin >= 64 || !isSafeOutputPin(relayPin) || usedPins[relayPin]) {
            if (relayPin != -1) corrected = true;
            relayPin = -1;
        } else usedPins[relayPin] = true;
        cfg.channels[i].relayPin = static_cast<int8_t>(relayPin);

        if (!channel["bPin"].is<int>()) corrected = true;
        int buttonPin = channel["bPin"].is<int>() ? channel["bPin"].as<int>() : -1;
        if (buttonPin < 0 || buttonPin >= 64 || !isSafeInputPin(buttonPin) || usedPins[buttonPin]) {
            if (buttonPin != -1) corrected = true;
            buttonPin = -1;
        } else usedPins[buttonPin] = true;
        cfg.channels[i].buttonPin = static_cast<int8_t>(buttonPin);

        if (channel["aLow"].is<bool>()) cfg.channels[i].activeLow = channel["aLow"].as<bool>();
        else corrected = true;
        if (channel["state"].is<bool>()) cfg.channels[i].state = channel["state"].as<bool>();
        else corrected = true;
        if (channel["sEn"].is<bool>()) cfg.channels[i].scheduleEnabled = channel["sEn"].as<bool>();
        else corrected = true;

        int hOn = channel["hOn"].is<int>() ? channel["hOn"].as<int>() : -1;
        int mOn = channel["mOn"].is<int>() ? channel["mOn"].as<int>() : -1;
        int hOff = channel["hOff"].is<int>() ? channel["hOff"].as<int>() : -1;
        int mOff = channel["mOff"].is<int>() ? channel["mOff"].as<int>() : -1;
        if (hOn >= 0 && hOn <= 23) cfg.channels[i].scheduleHourOn = static_cast<uint8_t>(hOn);
        else corrected = true;
        if (mOn >= 0 && mOn <= 59) cfg.channels[i].scheduleMinOn = static_cast<uint8_t>(mOn);
        else corrected = true;
        if (hOff >= 0 && hOff <= 23) cfg.channels[i].scheduleHourOff = static_cast<uint8_t>(hOff);
        else corrected = true;
        if (mOff >= 0 && mOff <= 59) cfg.channels[i].scheduleMinOff = static_cast<uint8_t>(mOff);
        else corrected = true;
    }

    const String backupPath = String(CONFIG_FILE) + ".bak";
    if (LittleFS.exists(backupPath)) LittleFS.remove(backupPath);
    if (corrected && storageReady) {
        Serial.println("Configuration contained invalid fields; safe defaults applied and saved.");
        if (!saveConfig()) Serial.println("ERROR: Could not save sanitized configuration.");
    }
}
void publishChannelState(uint8_t idx) {
    if (!mqtt.connected()) return;
    String topic = "mhome/" + cfg.deviceId + "/ch/" + String(idx + 1) + "/state";
    mqtt.publish(topic.c_str(), cfg.channels[idx].state ? "ON" : "OFF", true);
}

void setChannelState(uint8_t idx, bool newState, bool persist = true) {
    if (idx >= MAX_CHANNELS) return;
    bool changed = cfg.channels[idx].state != newState;
    cfg.channels[idx].state = newState;
    int8_t pin = cfg.channels[idx].relayPin;
    if (pin >= 0 && isSafeOutputPin(pin)) {
        bool level = cfg.channels[idx].activeLow ? !newState : newState;
        digitalWrite(pin, level ? HIGH : LOW);
    }
    publishChannelState(idx);
    if (persist && changed) saveConfig();
}

void applyHardwarePins() {
    bool usedPins[64] = {false};
    bool configCorrected = false;

    for (int i = 0; i < MAX_CHANNELS; i++) {
        int8_t relayPin = cfg.channels[i].relayPin;
        int8_t buttonPin = cfg.channels[i].buttonPin;

        if (relayPin < 0 || !isSafeOutputPin(relayPin) || usedPins[(uint8_t)relayPin]) {
            if (relayPin != -1) configCorrected = true;
            relayPin = -1;
            cfg.channels[i].relayPin = -1;
        } else {
            usedPins[(uint8_t)relayPin] = true;
        }
        if (buttonPin < 0 || !isSafeInputPin(buttonPin) || usedPins[(uint8_t)buttonPin]) {
            if (buttonPin != -1) configCorrected = true;
            buttonPin = -1;
            cfg.channels[i].buttonPin = -1;
        } else {
            usedPins[(uint8_t)buttonPin] = true;
        }

        if (appliedRelayPins[i] >= 0 && appliedRelayPins[i] != relayPin) {
            int oldPin = appliedRelayPins[i];
            digitalWrite(oldPin, appliedRelayActiveLow[i] ? HIGH : LOW);
            pinMode(oldPin, INPUT);
        }
        if (appliedButtonPins[i] >= 0 && appliedButtonPins[i] != buttonPin) {
            pinMode(appliedButtonPins[i], INPUT);
        }

        if (relayPin >= 0) {
            // Preload the output latch while the pin is still an input so enabling
            // OUTPUT does not briefly drive the relay's active level.
            bool level = cfg.channels[i].activeLow ? !cfg.channels[i].state : cfg.channels[i].state;
            digitalWrite(relayPin, level ? HIGH : LOW);
            pinMode(relayPin, OUTPUT);
        }
        if (buttonPin >= 0) {
            pinMode(buttonPin, INPUT_PULLUP);
            lastBtnReading[i] = digitalRead(buttonPin);
        }
        appliedRelayPins[i] = relayPin;
        appliedButtonPins[i] = buttonPin;
        appliedRelayActiveLow[i] = cfg.channels[i].activeLow;
    }
    if (configCorrected && storageReady) saveConfig();
}

void onMqttMessage(char* topic, byte* payload, unsigned int length) {
    String t = String(topic);
    String msg;
    for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
    msg.trim();

    for (int i = 0; i < MAX_CHANNELS; i++) {
        String cmdTopic = "mhome/" + cfg.deviceId + "/ch/" + String(i + 1) + "/set";
        if (t == cmdTopic) {
            if (msg.equalsIgnoreCase("ON") || msg == "1") setChannelState(i, true);
            else if (msg.equalsIgnoreCase("OFF") || msg == "0") setChannelState(i, false);
            else if (msg.equalsIgnoreCase("TOGGLE")) setChannelState(i, !cfg.channels[i].state);
        }
    }
}

void onWsEvent(WStype_t type, uint8_t* payload, size_t length) {
    if (type == WStype_CONNECTED) {
        wsBinaryFragmentActive = false;
        wsBridge.resetRxBuffer();
    } else if (type == WStype_BIN) {
        wsBridge.pushData(payload, length);
    } else if (type == WStype_FRAGMENT_BIN_START) {
        wsBinaryFragmentActive = true;
        wsBridge.pushData(payload, length);
    } else if (type == WStype_FRAGMENT && wsBinaryFragmentActive) {
        wsBridge.pushData(payload, length);
    } else if (type == WStype_FRAGMENT_FIN && wsBinaryFragmentActive) {
        wsBridge.pushData(payload, length);
        wsBinaryFragmentActive = false;
    } else if (type == WStype_DISCONNECTED) {
        wsBinaryFragmentActive = false;
        wsBridge.resetRxBuffer();
    }
}

void serveUiAsset(const char* path, const char* contentType) {
    File file = LittleFS.open(path, "r");
    if (!file) {
        server.send(404, "text/plain", "WebUI asset not found.");
        return;
    }
    server.sendHeader("Cache-Control", "no-cache");
    server.streamFile(file, contentType);
    file.close();
}

void setupWebUI() {
    server.on("/", HTTP_GET, []() {
        File file = LittleFS.open("/index.html", "r");
        if (file) {
            server.sendHeader("Cache-Control", "no-cache");
            server.streamFile(file, "text/html");
            file.close();
        } else {
            server.send(503, "text/plain", "WebUI filesystem is unavailable.");
        }
    });

    server.on("/assets/css/app.css", HTTP_GET, []() {
        serveUiAsset("/assets/css/app.css", "text/css");
    });
    server.on("/assets/js/api.js", HTTP_GET, []() {
        serveUiAsset("/assets/js/api.js", "application/javascript");
    });
    server.on("/assets/js/dashboard.js", HTTP_GET, []() {
        serveUiAsset("/assets/js/dashboard.js", "application/javascript");
    });
    server.on("/assets/js/settings.js", HTTP_GET, []() {
        serveUiAsset("/assets/js/settings.js", "application/javascript");
    });
    server.on("/assets/js/app.js", HTTP_GET, []() {
        serveUiAsset("/assets/js/app.js", "application/javascript");
    });

    server.on("/api/config", HTTP_GET, []() {
        JsonDocument doc;
        doc["deviceId"] = cfg.deviceId;
        doc["ssid1"] = cfg.ssid1;
        doc["ssid2"] = cfg.ssid2;
        doc["mqttHost"] = cfg.mqttHost;
        doc["mqttPort"] = cfg.mqttPort;
        doc["mqttPath"] = cfg.mqttPath;
        doc["mqttUser"] = cfg.mqttUser;
        doc["mqttCaCert"] = cfg.mqttCaCert;
        doc["mqttUseSsl"] = cfg.mqttUseSsl;
        doc["tz"] = cfg.timezoneOffset;
        doc["pass1Set"] = cfg.pass1.length() > 0;
        doc["pass2Set"] = cfg.pass2.length() > 0;
        doc["mqttPassSet"] = cfg.mqttPass.length() > 0;
        doc["apPasswordSet"] = cfg.apPassword.length() >= 8;
        doc["boardProfile"] = boardPinProfile();
        doc["safeGpioPins"] = boardSafeGpioList();

        JsonArray channels = doc["channels"].to<JsonArray>();
        for (int i = 0; i < MAX_CHANNELS; i++) {
            JsonObject channel = channels.add<JsonObject>();
            channel["id"] = cfg.channels[i].id;
            channel["name"] = cfg.channels[i].name;
            channel["rPin"] = cfg.channels[i].relayPin;
            channel["bPin"] = cfg.channels[i].buttonPin;
            channel["aLow"] = cfg.channels[i].activeLow;
            channel["state"] = cfg.channels[i].state;
            channel["sEn"] = cfg.channels[i].scheduleEnabled;
            channel["hOn"] = cfg.channels[i].scheduleHourOn;
            channel["mOn"] = cfg.channels[i].scheduleMinOn;
            channel["hOff"] = cfg.channels[i].scheduleHourOff;
            channel["mOff"] = cfg.channels[i].scheduleMinOff;
        }

        String response;
        serializeJson(doc, response);
        server.send(200, "application/json", response);
    });

    server.on("/api/status", HTTP_GET, []() {
        JsonDocument doc;
        const bool wifiConnected = WiFi.status() == WL_CONNECTED;
        doc["ok"] = true;
        doc["deviceId"] = cfg.deviceId;
        doc["wifiConnected"] = wifiConnected;
        doc["wifiSsid"] = wifiConnected ? WiFi.SSID() : "";
        doc["wifiIp"] = wifiConnected ? WiFi.localIP().toString() : "";
        doc["mqttConnected"] = mqtt.connected() && wsClient.isConnected();
        doc["apActive"] = fallbackApActive;
        doc["apIp"] = fallbackApActive ? WiFi.softAPIP().toString() : "";
        doc["boardProfile"] = boardPinProfile();

        JsonArray channels = doc["channels"].to<JsonArray>();
        for (int i = 0; i < MAX_CHANNELS; i++) {
            JsonObject channel = channels.add<JsonObject>();
            channel["id"] = cfg.channels[i].id;
            channel["name"] = cfg.channels[i].name;
            channel["state"] = cfg.channels[i].state;
        }

        String response;
        serializeJson(doc, response);
        server.send(200, "application/json", response);
    });

    server.on("/api/config", HTTP_POST, []() {
        if (!server.hasArg("plain")) {
            server.send(400, "application/json", "{\"ok\":false}");
            return;
        }
        String body = server.arg("plain");
        if (body.length() == 0 || body.length() > 16384) {
            server.send(413, "application/json", "{\"ok\":false,\"error\":\"invalid_config_size\"}");
            return;
        }
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, body);
        if (error || !doc.is<JsonObject>() || !doc["channels"].is<JsonArray>() ||
            doc["channels"].as<JsonArray>().size() != MAX_CHANNELS) {
            server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_json_or_channels\"}");
            return;
        }
        // Device ID is used in the AP SSID, MQTT client ID, and topic namespace.
        // Keep it ASCII-safe and leave room for the fixed "MHOME-" AP prefix.
        if (!doc["deviceId"].is<const char*>()) {
            server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_device_id\"}");
            return;
        }
        String newDeviceId = doc["deviceId"].as<String>();
        if (!isValidDeviceId(newDeviceId)) {
            server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_device_id\"}");
            return;
        }
        if (!doc["ssid1"].is<const char*>() || !doc["ssid2"].is<const char*>() ||
            !doc["mqttHost"].is<const char*>() || !doc["mqttPath"].is<const char*>() ||
            !doc["mqttUser"].is<const char*>() || !doc["mqttPort"].is<int>() ||
            !doc["tz"].is<int>() || !doc["mqttUseSsl"].is<bool>()) {
            server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_config_field_type\"}");
            return;
        }
        String newSsid1 = doc["ssid1"].as<String>();
        String newSsid2 = doc["ssid2"].as<String>();
        String newMqttHost = doc["mqttHost"].as<String>();
        String newMqttPath = doc["mqttPath"].as<String>();
        String newMqttUser = doc["mqttUser"].as<String>();
        if (newSsid1.length() > 32 || newSsid2.length() > 32) {
            server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_wifi_ssid_length\"}");
            return;
        }
        if (newMqttHost.length() > 253 || newMqttPath.length() == 0 ||
            newMqttPath.length() > 128 || newMqttPath[0] != '/' || newMqttUser.length() > 128) {
            server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_mqtt_endpoint\"}");
            return;
        }
        // Blank password inputs retain existing secrets unless the open-network flag is explicit.
        // An empty CA explicitly clears the trust anchor.
        if ((doc["clearPass1"].is<bool>() && doc["clearPass1"].as<bool>())) doc["pass1"] = "";
        else if (!doc["pass1"].is<const char*>() || String((const char*)doc["pass1"]).length() == 0) doc["pass1"] = cfg.pass1;
        if ((doc["clearPass2"].is<bool>() && doc["clearPass2"].as<bool>())) doc["pass2"] = "";
        else if (!doc["pass2"].is<const char*>() || String((const char*)doc["pass2"]).length() == 0) doc["pass2"] = cfg.pass2;
        if (!doc["mqttPass"].is<const char*>() || String((const char*)doc["mqttPass"]).length() == 0) doc["mqttPass"] = cfg.mqttPass;
        if (!doc["apPassword"].is<const char*>() || String((const char*)doc["apPassword"]).length() == 0) doc["apPassword"] = cfg.apPassword;
        if (!doc["mqttCaCert"].is<const char*>()) {
            server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_mqtt_ca_certificate\"}");
            return;
        }
        if (!isValidWifiPassword(doc["pass1"].as<String>()) ||
            !isValidWifiPassword(doc["pass2"].as<String>())) {
            server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_wifi_password\"}");
            return;
        }
        String mqttPassword = doc["mqttPass"].as<String>();
        if (mqttPassword.length() > 128) {
            server.send(400, "application/json", "{\"ok\":false,\"error\":\"mqtt_password_too_long\"}");
            return;
        }
        String caCert = doc["mqttCaCert"].as<String>();
        if (caCert.length() > 6000 || (caCert.length() > 0 &&
            (caCert.indexOf("-----BEGIN CERTIFICATE-----") < 0 ||
             caCert.indexOf("-----END CERTIFICATE-----") < 0))) {
            server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_mqtt_ca_certificate\"}");
            return;
        }
        String newApPassword = doc["apPassword"] | "";
        if (newApPassword.length() < 8 || newApPassword.length() > 63) {
            server.send(400, "application/json", "{\"ok\":false,\"error\":\"ap_password_length\"}");
            return;
        }
        int port = doc["mqttPort"] | 0;
        int tz = doc["tz"] | 99;
        if (port < 1 || port > 65535 || tz < -12 || tz > 14) {
            server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_mqtt_or_timezone\"}");
            return;
        }
        bool usedPins[64] = {false};
        for (JsonObject channel : doc["channels"].as<JsonArray>()) {
            if (!channel["rPin"].is<int>() || !channel["bPin"].is<int>() ||
                !channel["hOn"].is<int>() || !channel["mOn"].is<int>() ||
                !channel["hOff"].is<int>() || !channel["mOff"].is<int>() ||
                !channel["name"].is<const char*>() || String(channel["name"].as<const char*>()).length() > 32 ||
                !channel["aLow"].is<bool>() || !channel["sEn"].is<bool>() ||
                !channel["state"].is<bool>()) {
                server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_channel_value\"}");
                return;
            }
            int relayPin = channel["rPin"].as<int>();
            int buttonPin = channel["bPin"].as<int>();
            int hOn = channel["hOn"].as<int>();
            int mOn = channel["mOn"].as<int>();
            int hOff = channel["hOff"].as<int>();
            int mOff = channel["mOff"].as<int>();
            if ((relayPin != -1 && !isSafeOutputPin(relayPin)) ||
                (buttonPin != -1 && !isSafeInputPin(buttonPin)) ||
                hOn < 0 || hOn > 23 || hOff < 0 || hOff > 23 ||
                mOn < 0 || mOn > 59 || mOff < 0 || mOff > 59) {
                server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_channel_value\"}");
                return;
            }
            if (relayPin >= 0) {
                if (usedPins[relayPin]) {
                    server.send(400, "application/json", "{\"ok\":false,\"error\":\"duplicate_gpio\"}");
                    return;
                }
                usedPins[relayPin] = true;
            }
            if (buttonPin >= 0) {
                if (usedPins[buttonPin]) {
                    server.send(400, "application/json", "{\"ok\":false,\"error\":\"duplicate_gpio\"}");
                    return;
                }
                usedPins[buttonPin] = true;
            }
        }
        doc.remove("clearPass1");
        doc.remove("clearPass2");
        if (!writeConfigDocument(doc)) {
            server.send(500, "application/json", "{\"ok\":false,\"error\":\"storage_write_failed\"}");
            return;
        }
        const String oldSsid1 = cfg.ssid1;
        const String oldPass1 = cfg.pass1;
        const String oldSsid2 = cfg.ssid2;
        const String oldPass2 = cfg.pass2;
        const String oldDeviceId = cfg.deviceId;
        const String oldApPassword = cfg.apPassword;
        const int8_t oldTimezoneOffset = cfg.timezoneOffset;
        loadConfig();
        applyHardwarePins();
        if (oldTimezoneOffset != cfg.timezoneOffset) {
            configTime(cfg.timezoneOffset * 3600, 0, "pool.ntp.org", "time.google.com");
            lastCheckedMinute = 0;
            Serial.printf("Timezone updated: UTC%+d\n", cfg.timezoneOffset);
        }
        const bool restartAp = fallbackApActive &&
                               (oldDeviceId != cfg.deviceId || oldApPassword != cfg.apPassword);
        const bool restartWifi = oldSsid1 != cfg.ssid1 || oldPass1 != cfg.pass1 ||
                                 oldSsid2 != cfg.ssid2 || oldPass2 != cfg.pass2;
        Serial.printf("Config saved: restartWifi=%s, restartAp=%s\n",
                      restartWifi ? "yes" : "no", restartAp ? "yes" : "no");
        mqtt.disconnect();
        wsClient.disconnect();
        wsConfigured = false;
        tlsConfigWarningShown = false;
        server.send(200, "application/json", "{\"ok\":true}");
        if (restartWifi) {
            Serial.println("Wi-Fi credentials changed; keeping fallback AP active during STA reconnect.");
            disconnectStation();
            wifiAttemptActive = false;
            wifiAttemptSecondary = true;
            wifiWasConnected = false;
            lastWifiCheck = millis() - 3000;
        }
        if (restartAp) {
            Serial.println("AP identity/password changed; restarting fallback AP.");
            WiFi.softAPdisconnect(false);
            fallbackApActive = false;
            startFallbackAp();
        }
    });

    server.on("/api/toggle", HTTP_POST, []() {
        const String channelArg = server.arg("ch");
        bool numeric = channelArg.length() > 0 && channelArg.length() <= 2;
        for (size_t i = 0; i < channelArg.length(); i++) {
            if (channelArg[i] < '0' || channelArg[i] > '9') numeric = false;
        }
        const int ch = numeric ? channelArg.toInt() - 1 : -1;
        const String requestedState = server.arg("state");
        if (ch < 0 || ch >= MAX_CHANNELS ||
            (requestedState.length() > 0 && requestedState != "0" && requestedState != "1")) {
            server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_channel_or_state\"}");
            return;
        }
        const bool newState = requestedState.length() == 0
            ? !cfg.channels[ch].state
            : requestedState == "1";
        setChannelState(ch, newState);
        JsonDocument result;
        result["ok"] = true;
        result["state"] = cfg.channels[ch].state;
        String response;
        serializeJson(result, response);
        server.send(200, "application/json", response);
    });

    server.begin();
}

void startFallbackAp() {
    if (fallbackApActive) return;
    WiFi.mode(WIFI_AP_STA);
    String apName = "MHOME-" + cfg.deviceId;
    if (cfg.apPassword.length() < 8) cfg.apPassword = makeDefaultApPassword();
    fallbackApActive = WiFi.softAP(apName.c_str(), cfg.apPassword.c_str());
    if (fallbackApActive) {
        Serial.print("Fallback AP started: "); Serial.println(apName);
        Serial.print("Fallback AP IP: "); Serial.println(WiFi.softAPIP());
    } else {
        Serial.println("ERROR: Failed to start fallback AP.");
    }
}

void stopFallbackAp() {
    if (!fallbackApActive) return;
    Serial.println("Fallback AP stopped after station connection.");
    WiFi.softAPdisconnect(false);
    WiFi.mode(WIFI_STA);
    fallbackApActive = false;
}

void disconnectStation() {
#if defined(ESP32)
    WiFi.disconnect(false, false);
#else
    WiFi.disconnect(false);
#endif
}

void beginWifiAttempt(bool secondary) {
    const String& ssid = secondary ? cfg.ssid2 : cfg.ssid1;
    const String& pass = secondary ? cfg.pass2 : cfg.pass1;
    if (ssid.length() == 0) {
        wifiAttemptActive = false;
        return;
    }
    disconnectStation();
    Serial.print("Wi-Fi attempt (");
    Serial.print(secondary ? "secondary" : "primary");
    Serial.print("): SSID=");
    Serial.println(ssid);
    WiFi.begin(ssid.c_str(), pass.c_str());
    wifiAttemptSecondary = secondary;
    wifiAttemptStartedAt = millis();
    wifiAttemptActive = true;
}

void ensureWifi() {
    if (!wifiInitialized) {
        WiFi.mode(WIFI_AP_STA);
        startFallbackAp();
        wifiInitialized = true;
        wifiAttemptActive = false;
        lastWifiCheck = millis() - 3000;
    }

    if (WiFi.status() == WL_CONNECTED) {
        if (!wifiWasConnected) {
            Serial.print("Wi-Fi connected: SSID=");
            Serial.print(WiFi.SSID());
            Serial.print(", IP=");
            Serial.println(WiFi.localIP());
        }
        wifiWasConnected = true;
        wifiAttemptActive = false;
        stopFallbackAp();
        return;
    }

    startFallbackAp();
    const unsigned long now = millis();
    if (wifiAttemptActive && now - wifiAttemptStartedAt < 20000) return;
    if (!wifiAttemptActive && now - lastWifiCheck < 3000) return;

    lastWifiCheck = now;
    bool nextSecondary = wifiWasConnected ? wifiAttemptSecondary : !wifiAttemptSecondary;
    wifiWasConnected = false;
    if (cfg.ssid1.length() == 0) nextSecondary = true;
    if (cfg.ssid2.length() == 0) nextSecondary = false;
    beginWifiAttempt(nextSecondary);
    if (!wifiAttemptActive) beginWifiAttempt(!nextSecondary);
}

void ensureMqtt() {
    if (WiFi.status() != WL_CONNECTED) {
        if (mqtt.connected()) mqtt.disconnect();
        if (wsConfigured) {
            wsClient.disconnect();
            wsConfigured = false;
        }
        return;
    }

    if (mqtt.connected() && wsClient.isConnected()) {
        mqtt.loop();
        return;
    }
    if (mqtt.connected() && !wsClient.isConnected()) mqtt.disconnect();

    if (!wsConfigured) {
        if (cfg.mqttHost.length() == 0 || cfg.mqttPath.length() == 0 || cfg.mqttPort == 0) return;
        if (cfg.mqttUseSsl) {
            if (time(nullptr) < 1700000000) return;
#if defined(ESP32)
            if (cfg.mqttCaCert.length() > 0) {
                // An explicitly configured CA takes precedence over the built-in public CA bundle.
                wsClient.beginSslWithCA(cfg.mqttHost.c_str(), cfg.mqttPort,
                                        cfg.mqttPath.c_str(), cfg.mqttCaCert.c_str(), "mqtt");
            } else {
                // The PlatformIO pre-build patch makes this use ESP32's embedded CA bundle,
                // rather than WebSocketsClient's insecure null-bundle fallback.
                wsClient.beginSslWithBundle(cfg.mqttHost.c_str(), cfg.mqttPort,
                                            cfg.mqttPath.c_str(), nullptr, 0, "mqtt");
            }
#else
            if (cfg.mqttCaCert.length() == 0) {
                if (!tlsConfigWarningShown) {
                    Serial.println("MQTT WSS disabled: configure the broker CA certificate in WebUI.");
                    tlsConfigWarningShown = true;
                }
                return;
            }
            wsClient.beginSslWithCA(cfg.mqttHost.c_str(), cfg.mqttPort,
                                    cfg.mqttPath.c_str(), cfg.mqttCaCert.c_str(), "mqtt");
#endif
        } else {
            wsClient.begin(cfg.mqttHost.c_str(), cfg.mqttPort, cfg.mqttPath.c_str(), "mqtt");
        }
        wsClient.onEvent(onWsEvent);
        wsClient.setReconnectInterval(5000);
        wsClient.enableHeartbeat(15000, 5000, 2);
        wsConfigured = true;
        lastMqttReconnect = millis();
        return;
    }

    if (!wsClient.isConnected() || millis() - lastMqttReconnect < 5000) return;
    lastMqttReconnect = millis();

    bool ok = cfg.mqttUser.length() > 0
        ? mqtt.connect(cfg.deviceId.c_str(), cfg.mqttUser.c_str(), cfg.mqttPass.c_str())
        : mqtt.connect(cfg.deviceId.c_str());
    if (ok) {
        String sub = "mhome/" + cfg.deviceId + "/ch/+/set";
        mqtt.subscribe(sub.c_str());
        for (int i = 0; i < MAX_CHANNELS; i++) publishChannelState(i);
    }
}

void checkButtons() {
    for (int i = 0; i < MAX_CHANNELS; i++) {
        int8_t pin = cfg.channels[i].buttonPin;
        if (pin < 0 || !isSafeInputPin(pin)) continue;
        bool reading = digitalRead(pin);
        if (reading != lastBtnReading[i] && (millis() - lastBtnDebounce[i] > 60)) {
            lastBtnDebounce[i] = millis();
            lastBtnReading[i] = reading;
            if (reading == LOW) {
                setChannelState(i, !cfg.channels[i].state);
            }
        }
    }
}

void checkSchedules() {
    time_t now = time(nullptr);
    if (now < 1700000000) return;
    struct tm* t = localtime(&now);
    time_t currentMinute = now / 60;
    if (currentMinute == lastCheckedMinute) return;
    lastCheckedMinute = currentMinute;

    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (!cfg.channels[i].scheduleEnabled) continue;
        if (t->tm_hour == cfg.channels[i].scheduleHourOn && t->tm_min == cfg.channels[i].scheduleMinOn) {
            setChannelState(i, true);
        }
        if (t->tm_hour == cfg.channels[i].scheduleHourOff && t->tm_min == cfg.channels[i].scheduleMinOff) {
            setChannelState(i, false);
        }
    }
}

void setup() {
    Serial.begin(115200);
#if defined(ESP32)
    WiFi.onEvent(onWifiEvent);
#endif
    storageReady = LittleFS.begin();
    if (!storageReady) {
        Serial.println("ERROR: LittleFS mount failed; loading defaults.");
        loadDefaultConfig();
    } else {
        loadConfig();
    }
    applyHardwarePins();
    configTime(cfg.timezoneOffset * 3600, 0, "pool.ntp.org", "time.google.com");
    mqtt.setCallback(onMqttMessage);
    setupWebUI();
    ensureWifi();
}

void loop() {
    ensureWifi();
    wsClient.loop();
    ensureMqtt();
    server.handleClient();
    checkButtons();
    checkSchedules();
    yield();
}
