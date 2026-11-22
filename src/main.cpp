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

DeviceConfig cfg;
WebSocketsClient wsClient;

// Custom Client wrapper bridging WebSocketsClient -> PubSubClient
class WsMqttBridge : public Client {
public:
    WebSocketsClient* ws;
    uint8_t rxBuf[1024];
    size_t rxHead = 0, rxTail = 0;

    WsMqttBridge(WebSocketsClient* w) : ws(w) {}
    void pushData(uint8_t* payload, size_t len) {
        for (size_t i = 0; i < len; i++) {
            size_t next = (rxHead + 1) % sizeof(rxBuf);
            if (next != rxTail) {
                rxBuf[rxHead] = payload[i];
                rxHead = next;
            }
        }
    }
    int connect(IPAddress ip, uint16_t port) override { return 1; }
    int connect(const char* host, uint16_t port) override { return 1; }
    size_t write(uint8_t b) override { return write(&b, 1); }
    size_t write(const uint8_t* buf, size_t size) override {
        return ws->sendBIN(buf, size) ? size : 0;
    }
    int available() override {
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
unsigned long lastMqttReconnect = 0;
unsigned long lastBtnDebounce[MAX_CHANNELS] = {0};
bool lastBtnReading[MAX_CHANNELS];
int lastMinuteChecked = -1;

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
    cfg.mqttUseSsl = true;
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
    doc["mqttUseSsl"] = cfg.mqttUseSsl;
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

    File f = LittleFS.open(CONFIG_FILE, "w");
    if (!f) return false;
    serializeJson(doc, f);
    f.close();
    return true;
}

void loadConfig() {
    loadDefaultConfig();
    if (!LittleFS.exists(CONFIG_FILE)) {
        saveConfig();
        return;
    }
    File f = LittleFS.open(CONFIG_FILE, "r");
    if (!f) return;
    JsonDocument doc;
    if (deserializeJson(doc, f) == DeserializationError::Ok) {
        cfg.deviceId = doc["deviceId"] | cfg.deviceId;
        cfg.ssid1 = doc["ssid1"] | cfg.ssid1;
        cfg.pass1 = doc["pass1"] | cfg.pass1;
        cfg.ssid2 = doc["ssid2"] | cfg.ssid2;
        cfg.pass2 = doc["pass2"] | cfg.pass2;
        cfg.mqttHost = doc["mqttHost"] | cfg.mqttHost;
        cfg.mqttPort = doc["mqttPort"] | cfg.mqttPort;
        cfg.mqttPath = doc["mqttPath"] | cfg.mqttPath;
        cfg.mqttUser = doc["mqttUser"] | cfg.mqttUser;
        cfg.mqttPass = doc["mqttPass"] | cfg.mqttPass;
        cfg.mqttUseSsl = doc["mqttUseSsl"] | cfg.mqttUseSsl;
        cfg.timezoneOffset = doc["tz"] | cfg.timezoneOffset;

        JsonArray arr = doc["channels"].as<JsonArray>();
        int idx = 0;
        for (JsonObject o : arr) {
            if (idx >= MAX_CHANNELS) break;
            cfg.channels[idx].name = o["name"] | cfg.channels[idx].name;
            cfg.channels[idx].relayPin = o["rPin"] | -1;
            cfg.channels[idx].buttonPin = o["bPin"] | -1;
            cfg.channels[idx].activeLow = o["aLow"] | true;
            cfg.channels[idx].state = o["state"] | false;
            cfg.channels[idx].scheduleEnabled = o["sEn"] | false;
            cfg.channels[idx].scheduleHourOn = o["hOn"] | 18;
            cfg.channels[idx].scheduleMinOn = o["mOn"] | 0;
            cfg.channels[idx].scheduleHourOff = o["hOff"] | 6;
            cfg.channels[idx].scheduleMinOff = o["mOff"] | 0;
            idx++;
        }
    }
    f.close();
}

void publishChannelState(uint8_t idx) {
    if (!mqtt.connected()) return;
    String topic = "mhome/" + cfg.deviceId + "/ch/" + String(idx + 1) + "/state";
    mqtt.publish(topic.c_str(), cfg.channels[idx].state ? "ON" : "OFF", true);
}

void setChannelState(uint8_t idx, bool newState, bool persist = true) {
    if (idx >= MAX_CHANNELS) return;
    cfg.channels[idx].state = newState;
    int8_t pin = cfg.channels[idx].relayPin;
    if (pin >= 0) {
        bool level = cfg.channels[idx].activeLow ? !newState : newState;
        digitalWrite(pin, level ? HIGH : LOW);
    }
    publishChannelState(idx);
    if (persist) saveConfig();
}

void applyHardwarePins() {
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (cfg.channels[i].relayPin >= 0) {
            pinMode(cfg.channels[i].relayPin, OUTPUT);
            bool level = cfg.channels[i].activeLow ? !cfg.channels[i].state : cfg.channels[i].state;
            digitalWrite(cfg.channels[i].relayPin, level ? HIGH : LOW);
        }
        if (cfg.channels[i].buttonPin >= 0) {
            pinMode(cfg.channels[i].buttonPin, INPUT_PULLUP);
            lastBtnReading[i] = digitalRead(cfg.channels[i].buttonPin);
        }
    }
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
    if (type == WStype_BIN) {
        wsBridge.pushData(payload, length);
    }
}

void setupWebUI() {
    server.on("/", HTTP_GET, []() {
        File f = LittleFS.open("/index.html", "r");
        if (f) {
            server.streamFile(f, "text/html");
            f.close();
        } else {
            server.send(200, "text/plain", "Upload LittleFS data folder for WebUI.");
        }
    });

    server.on("/api/config", HTTP_GET, []() {
        File f = LittleFS.open(CONFIG_FILE, "r");
        if (f) {
            server.streamFile(f, "application/json");
            f.close();
        } else {
            server.send(404, "application/json", "{}");
        }
    });

    server.on("/api/config", HTTP_POST, []() {
        if (!server.hasArg("plain")) {
            server.send(400, "application/json", "{\"ok\":false}");
            return;
        }
        File f = LittleFS.open(CONFIG_FILE, "w");
        f.print(server.arg("plain"));
        f.close();
        loadConfig();
        applyHardwarePins();
        server.send(200, "application/json", "{\"ok\":true}");
    });

    server.on("/api/toggle", HTTP_POST, []() {
        int ch = server.arg("ch").toInt() - 1;
        if (ch >= 0 && ch < MAX_CHANNELS) {
            setChannelState(ch, !cfg.channels[ch].state);
            server.send(200, "application/json", "{\"ok\":true}");
        } else {
            server.send(400, "application/json", "{\"ok\":false}");
        }
    });

    server.begin();
}

void ensureWifi() {
    if (WiFi.status() == WL_CONNECTED) return;
    if (millis() - lastWifiCheck < 15000) return;
    lastWifiCheck = millis();

    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(("MHOME-" + cfg.deviceId).c_str(), "12345678");
    WiFi.begin(cfg.ssid1.c_str(), cfg.pass1.c_str());
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 8000) {
        delay(200);
        yield();
    }
    if (WiFi.status() != WL_CONNECTED && cfg.ssid2.length() > 0) {
        WiFi.begin(cfg.ssid2.c_str(), cfg.pass2.c_str());
    }
}

void ensureMqtt() {
    if (WiFi.status() != WL_CONNECTED) return;
    if (mqtt.connected()) {
        mqtt.loop();
        return;
    }
    if (millis() - lastMqttReconnect < 5000) return;
    lastMqttReconnect = millis();

    if (!wsClient.isConnected()) {
        if (cfg.mqttUseSsl) {
            wsClient.beginSSL(cfg.mqttHost.c_str(), cfg.mqttPort, cfg.mqttPath.c_str(), "", "mqtt");
        } else {
            wsClient.begin(cfg.mqttHost.c_str(), cfg.mqttPort, cfg.mqttPath.c_str(), "mqtt");
        }
        wsClient.onEvent(onWsEvent);
        wsClient.setReconnectInterval(5000);
    }

    if (wsClient.isConnected()) {
        bool ok = cfg.mqttUser.length() > 0
            ? mqtt.connect(cfg.deviceId.c_str(), cfg.mqttUser.c_str(), cfg.mqttPass.c_str())
            : mqtt.connect(cfg.deviceId.c_str());
        if (ok) {
            String sub = "mhome/" + cfg.deviceId + "/ch/+/set";
            mqtt.subscribe(sub.c_str());
            for (int i = 0; i < MAX_CHANNELS; i++) publishChannelState(i);
        }
    }
}

void checkButtons() {
    for (int i = 0; i < MAX_CHANNELS; i++) {
        int8_t pin = cfg.channels[i].buttonPin;
        if (pin < 0) continue;
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
    if (t->tm_min == lastMinuteChecked) return;
    lastMinuteChecked = t->tm_min;

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
    LittleFS.begin();
    loadConfig();
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
