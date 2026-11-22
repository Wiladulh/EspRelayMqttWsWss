# M-Home Relay Node Universal (v4)

Firmware modular untuk **ESP8266 NodeMCU, ESP32 DevKit, ESP32-S3 N16R8, dan ESP32-C3 SuperMini** dengan dukungan:

- **10 channel relay** dengan pemetaan GPIO dinamis dan tombol fisik dengan debounce.
- **Dual-SSID Wi-Fi**: percobaan koneksi ke SSID utama dan cadangan dilakukan bergiliran.
- **AP fallback**: aktif ketika STA belum tersambung, mati otomatis setelah Wi-Fi tersambung, dan aktif kembali saat STA terputus.
- **NTP scheduler** per channel.
- **MQTT over WebSockets (WS/WSS)**.
- **WebUI LittleFS** untuk mengubah konfigurasi tanpa flash ulang firmware.

## Setup awal

1. Flash firmware sesuai board.
2. Upload filesystem image agar `data/index.html` tersedia di LittleFS.
3. Saat Wi-Fi utama/cadangan belum tersambung, hubungkan ponsel ke AP `MHOME-<deviceId>`.
4. Password AP awal: `11111111`. Buka alamat IP AP (umumnya `192.168.4.1`) dari browser.
5. Ganti password AP dari WebUI setelah setup.

**Catatan keamanan:** WebUI memang tanpa autentikasi sesuai keputusan desain. Semua perangkat yang dapat menjangkau jaringan perangkat dapat membuka dashboard dan mengendalikan relay. Password AP default `11111111` mudah ditebak; ganti sebelum perangkat dipasang di lokasi yang dapat dijangkau pihak lain.

## Konfigurasi MQTT WSS

Jika TLS/WSS diaktifkan, firmware menunggu sinkronisasi waktu NTP sebelum handshake dan memverifikasi sertifikat server (tanpa fallback TLS insecure).

- **ESP32 DevKit:** bila kolom **MQTT TLS CA Certificate** kosong, firmware memakai CA bundle publik bawaan ESP32 Arduino core. Jika kolom diisi, CA PEM tersebut dipakai sebagai trust anchor.
- **ESP8266 NodeMCU:** isi kolom **MQTT TLS CA Certificate** dengan CA broker dalam format PEM; tanpa CA, koneksi WSS tidak dimulai.

Gunakan sertifikat CA tepercaya. Jangan memasukkan private key. Sertifikat CA bukan password dan akan dikirim kembali oleh endpoint konfigurasi agar dapat diedit melalui WebUI.

Build CI untuk ESP32 DevKit mengunggah satu artifact bernama `esp32-devkit-firmware` yang berisi `firmware.bin`. Image LittleFS tidak disertakan; flash firmware ini pada offset aplikasi tanpa menghapus seluruh flash agar konfigurasi LittleFS yang ada tetap terjaga.

Password Wi-Fi, MQTT, dan AP tidak dikembalikan oleh `GET /api/config`. Kolom password yang dibiarkan kosong saat menyimpan akan mempertahankan password tersimpan; isi nilai baru untuk menggantinya.

## Profil GPIO

| Board | GPIO relay/tombol yang diizinkan |
|---|---|
| ESP8266 NodeMCU | 4, 5, 12, 13, 14 |
| ESP32 DevKit | 4, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33 |
| ESP32-S3 N16R8 | 4–18, 21, 38, 47 |
| ESP32-C3 SuperMini | 0, 1, 3, 4, 5, 6, 7, 10 |

Daftar GPIO sengaja konservatif untuk board profile yang ditentukan di `platformio.ini`. Periksa skematik board/clone sebelum menghubungkan relay, karena komponen tambahan pada varian board dapat menggunakan GPIO tertentu.

## Build & upload (PlatformIO)

Pilih environment di `platformio.ini`:

- `esp8266_nodemcu`
- `esp32_devkit`
- `esp32s3_n16r8`
- `esp32c3_supermini`

Perintah:

```bash
pio run -e <environment>
pio run -e <environment> -t uploadfs
pio run -e <environment> -t upload
```

Upload filesystem image dan firmware ke board yang sama. Build CI memverifikasi kompilasi; pengujian fisik relay, tombol, Wi-Fi, dan koneksi broker tetap harus dilakukan pada perangkat sebenarnya.
