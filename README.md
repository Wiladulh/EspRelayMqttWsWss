# M-Home Relay Node Universal (v4)

Firmware modular untuk **ESP32 DevKit dan ESP32-C3 SuperMini** dengan dukungan:

- **10 channel relay** dengan pemetaan GPIO dinamis dan tombol fisik dengan debounce.
- **Dual-SSID Wi-Fi**: dua slot SSID dan dua password disimpan sebagai pasangan terpisah; koneksi mencoba jaringan utama dan cadangan secara bergiliran.
- **AP fallback**: aktif ketika STA belum tersambung, mati otomatis setelah Wi-Fi tersambung, dan aktif kembali saat STA terputus.
- **NTP scheduler** per channel.
- **MQTT over WebSockets (WS/WSS)**.
- **WebUI modular berbasis LittleFS** dengan halaman Dashboard dan Pengaturan terpisah.

## Target board aktif

Pengembangan dan CI saat ini difokuskan pada dua target:

- ESP32 DevKit
- ESP32-C3 SuperMini

ESP32-S3 N16R8 dan ESP8266 NodeMCU untuk sementara dinonaktifkan dari target aktif dan CI. Source code serta environment PlatformIO `esp32s3_n16r8` dan `esp8266_nodemcu` tetap dipertahankan sebagai legacy agar dukungan dapat diaktifkan kembali, tetapi keduanya tidak tercakup dalam verifikasi CI aktif.

## Struktur WebUI

Frontend tidak digabungkan dalam satu file. Sumber UI berada di folder data:

- data/index.html: kerangka halaman dan navigasi Dashboard/Pengaturan.
- data/assets/css/app.css: layout, warna, komponen, dan responsive styling.
- data/assets/js/api.js: komunikasi HTTP dengan API firmware.
- data/assets/js/dashboard.js: status perangkat dan toggle relay.
- data/assets/js/settings.js: pemuatan, validasi, dan penyimpanan pengaturan.
- data/assets/js/app.js: navigasi halaman dan notifikasi.

Dashboard hanya menampilkan status koneksi dan toggle relay. Seluruh konfigurasi berada di tab Pengaturan, dengan bagian Wi-Fi, MQTT/TLS, identitas perangkat, GPIO, dan jadwal.

## Setup awal

1. Build dan flash firmware sesuai board.
2. Build dan upload image LittleFS untuk board yang sama.
3. Saat Wi-Fi utama/cadangan belum tersambung, hubungkan ponsel ke AP MHOME-<deviceId>.
4. Password AP awal: 11111111. Buka alamat IP AP (umumnya 192.168.4.1) dari browser.
5. Atur dua SSID Wi-Fi pada bagian Pengaturan → Wi-Fi dan ganti password AP setelah setup.

**Catatan keamanan:** WebUI memang tanpa autentikasi sesuai keputusan desain. Semua perangkat yang dapat menjangkau jaringan perangkat dapat membuka dashboard dan mengendalikan relay. Password AP default 11111111 mudah ditebak; ganti sebelum perangkat dipasang di lokasi yang dapat dijangkau pihak lain.

## Konfigurasi MQTT WSS

Jika TLS/WSS diaktifkan, firmware menunggu sinkronisasi waktu NTP sebelum handshake.

- **ESP32 DevKit dan ESP32-C3:** jika kolom CA Certificate kosong, koneksi WSS menggunakan mode tanpa CA (tanpa verifikasi identitas sertifikat server). Jika CA PEM diisi, firmware menggunakan CA tersebut sebagai trust anchor untuk memverifikasi sertifikat server.
- Mode tanpa CA tetap mengenkripsi trafik, tetapi tidak mengautentikasi identitas broker dan rentan terhadap man-in-the-middle.

Mode tanpa CA dipilih untuk kompatibilitas koneksi awal. Jika koneksi ke broker gagal, periksa log handshake dan konektivitas terlebih dahulu sebelum mengubah konfigurasi TLS. Untuk penggunaan yang membutuhkan autentikasi server, isi CA PEM yang sesuai.

Password Wi-Fi, MQTT, dan AP tidak dikembalikan oleh GET /api/config. Kolom password yang dibiarkan kosong saat menyimpan akan mempertahankan password tersimpan; isi nilai baru untuk menggantinya. Untuk jaringan Wi-Fi terbuka, centang opsi hapus password pada slot terkait. Password Wi-Fi menerima 8–63 karakter atau 64 digit heksadesimal (PSK); panjang di luar itu ditolak. SSID kosong menonaktifkan slot jaringan tersebut.

## Profil GPIO

| Board | GPIO relay/tombol yang diizinkan |
|---|---|
| ESP32 DevKit | 4, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33 |
| ESP32-C3 SuperMini | 0, 1, 3, 4, 5, 6, 7, 10 |

Daftar GPIO sengaja konservatif untuk board profile yang ditentukan di platformio.ini. Periksa skematik board/clone sebelum menghubungkan relay, karena komponen tambahan pada varian board dapat menggunakan GPIO tertentu.

## Build & upload (PlatformIO)

Versi platform dan library dikunci ke versi yang berhasil dipakai pada CI terakhir agar build tidak berubah akibat rilis dependency baru. PlatformIO Core pada CI terakhir adalah 6.2.0.

Target aktif di `platformio.ini`:

- `esp32_devkit` (default)
- `esp32c3_supermini`

Environment legacy `esp32s3_n16r8` dan `esp8266_nodemcu` masih tersedia tetapi tidak dibangun oleh CI aktif.

Perintah:

~~~bash
pio run -e <environment>
pio run -e <environment> -t buildfs
pio run -e <environment> -t uploadfs
pio run -e <environment> -t upload
~~~

CI otomatis membangun firmware dan LittleFS untuk kedua target aktif. Workflow manual menyediakan pilihan untuk membangun semua target aktif atau satu board saja. Artifact ESP32 DevKit `esp32-devkit-flash-images` berisi firmware.bin, partitions.bin, dan littlefs.bin. Artifact ESP32-C3 berisi firmware dan filesystem.

Untuk ESP32 DevKit 4 MB, setelah memastikan board dan port serial benar, flash image secara terpisah:

~~~bash
python -m esptool --chip esp32 --port <PORT> write-flash 0x8000 partitions.bin
python -m esptool --chip esp32 --port <PORT> write-flash 0x3c0000 littlefs.bin
~~~

Firmware berada di offset aplikasi 0x100000; flash firmware.bin ke offset tersebut hanya bila memang diperlukan. Jangan gunakan perintah erase seluruh flash. Perubahan partition table harus diperlakukan sebagai perubahan layout flash; backup data penting sebelum penerapan. Partisi at_customize tetap pada offset dan ukuran semula.

Untuk environment legacy, gunakan image LittleFS yang cocok dengan environment dan partition layout board tersebut. Build CI memverifikasi kompilasi dan pembuatan image; pengujian fisik relay, tombol, Wi-Fi, dan koneksi broker tetap harus dilakukan pada perangkat sebenarnya.
