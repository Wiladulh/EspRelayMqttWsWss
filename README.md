# M-Home Relay Node Universal (v4)

Firmware modular anti-crash untuk **ESP8266, ESP32, ESP32-S3, dan ESP32-C3** dengan dukungan:
- **10-Channel Dynamic GPIO Mapper** (Relay & Tombol Fisik *debounce*)
- **Dual-SSID Wi-Fi Fallback** + AP Fallback (`MHOME-<deviceId>`, pass `12345678`)
- **NTP UTC+7 Scheduler** otomatis per channel
- **MQTT over WebSockets Secure (WSS)** ke `wss://mqtt.giebux.my.id:443`
- **Embedded WebUI** berbasis LittleFS tanpa perlu *reflash* ulang saat mengubah pin

## Cara Build & Upload (PlatformIO)
1. Pilih environment target di `platformio.ini` (`esp8266_nodemcu`, `esp32_devkit`, `esp32s3_n16r8`, atau `esp32c3_supermini`).
2. Jalankan **Upload Filesystem Image** (`pio run -t uploadfs`) agar folder `data/index.html` terunggah ke LittleFS.
3. Jalankan **Upload** firmware (`pio run -t upload`).
