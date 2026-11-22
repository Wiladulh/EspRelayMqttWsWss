#pragma once

#include <Arduino.h>

// GPIO allow-lists are deliberately conservative. Pins omitted here may be
// usable on a particular module, but are not approved for relay/button wiring.
inline bool isSafeOutputPin(int pin) {
#if defined(ESP8266)
    // NodeMCU: exclude boot straps, UART, flash pins and GPIO16.
    const uint8_t safe[] = {4, 5, 12, 13, 14};
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
    // ESP32-C3 SuperMini: exclude strap pins, USB/JTAG and flash pins.
    const uint8_t safe[] = {0, 1, 3, 4, 5, 6, 7, 10};
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
    // ESP32-S3 N16R8: exclude straps, USB, flash/PSRAM and UART/JTAG pins.
    const uint8_t safe[] = {4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
                            16, 17, 18, 21, 38, 47};
#elif defined(ESP32)
    // Classic ESP32 DevKit: exclude straps, flash, UART and input-only GPIOs.
    const uint8_t safe[] = {4, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33};
#else
    return false;
#endif
    for (uint8_t candidate : safe) {
        if (candidate == pin) return true;
    }
    return false;
}

inline bool isSafeInputPin(int pin) {
    // The same conservative allow-list is used for buttons to avoid selecting
    // pins reserved for boot, flash, USB, PSRAM, or board debug interfaces.
    return isSafeOutputPin(pin);
}

inline const char* boardPinProfile() {
#if defined(ESP8266)
    return "ESP8266 NodeMCU";
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
    return "ESP32-C3 SuperMini";
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
    return "ESP32-S3 N16R8";
#elif defined(ESP32)
    return "ESP32 DevKit";
#else
    return "Unknown board";
#endif
}

inline const char* boardSafeGpioList() {
#if defined(ESP8266)
    return "4, 5, 12, 13, 14";
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
    return "0, 1, 3, 4, 5, 6, 7, 10";
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
    return "4-18, 21, 38, 47";
#elif defined(ESP32)
    return "4, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33";
#else
    return "Tidak tersedia";
#endif
}
