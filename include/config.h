#pragma once
#include <Arduino.h>

#define MAX_CHANNELS 10
#define CONFIG_FILE "/config.json"

struct ChannelMap {
    uint8_t id;
    String name;
    int8_t relayPin;
    int8_t buttonPin;
    bool activeLow;
    bool state;
    uint8_t scheduleHourOn;
    uint8_t scheduleMinOn;
    uint8_t scheduleHourOff;
    uint8_t scheduleMinOff;
    bool scheduleEnabled;
};

struct DeviceConfig {
    String deviceId;
    String ssid1;
    String pass1;
    String ssid2;
    String pass2;
    String mqttHost;
    uint16_t mqttPort;
    String mqttPath;
    String mqttUser;
    String mqttPass;
    bool mqttUseSsl;
    int8_t timezoneOffset;
    ChannelMap channels[MAX_CHANNELS];
};
