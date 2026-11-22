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

Build CI untuk ESP32 DevKit mengunggah artifact `esp32-devkit-flash-images` yang berisi `firmware.bin`, `partitions.bin`, dan `littlefs.bin`. Partition table khusus DevKit mempertahankan partisi `at_customize` yang ada, menyediakan dua slot OTA 1.375 MB, dan menyediakan partisi LittleFS 256 KB berlabel `spiffs`.

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

Untuk ESP32 DevKit 4 MB, unduh artifact CI `esp32-devkit-flash-images`. Setelah memeriksa port serial dan memastikan board yang benar, flash image secara terpisah:
```bash
python -m esptool --chip esp32 --port <PORT> write-flash 0x8000 partitions.bin
python -m esptool --chip esp32 --port <PORT> write-flash 0x3c0000 littlefs.bin
```
Firmware berada di offset aplikasi `0x100000`; flash `firmware.bin` ke offset tersebut hanya bila memang diperlukan. Jangan gunakan perintah erase seluruh flash. Perubahan partition table harus diperlakukan sebagai perubahan layout flash; backup data penting sebelum penerapan. Partisi `at_customize` tetap pada offset dan ukuran semula.

Untuk environment lain, jalankan `pio run -e <environment> -t uploadfs` sesuai board dan partition layout-nya. Build CI memverifikasi kompilasi dan pembuatan image; pengujian fisik relay, tombol, Wi-Fi, dan koneksi broker tetap harus dilakukan pada perangkat sebenarnya.
