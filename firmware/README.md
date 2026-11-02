# BacaIn Firmware

ESP-IDF (C/C++) firmware untuk perangkat **BacaIn** (ESP32-S3 + E-Paper 1.54" V2 + SPI Flash + Web Bluetooth).

---

## 🚀 Panduan Build (Docker-First)

Anda **tidak perlu menginstal ESP-IDF atau Python di komputer Anda**. Build dijalankan sepenuhnya di dalam kontainer Docker resmi `espressif/idf:v5.3.1`.

### Opsi 1: Menggunakan Skrip Runner (PowerShell di Windows)

Cukup masuk ke folder `firmware` lalu jalankan skrip helper:

```powershell
cd firmware
.\build.ps1
```

Skrip ini akan:
1. Memverifikasi Docker daemon aktif.
2. Menjalankan kontainer `espressif/idf:v5.3.1` untuk build ESP-IDF target `esp32s3`.
3. Menjalankan `package_firmware.py` untuk menggabungkan binary menjadi single factory file (`bacain_factory.bin`) dan membuat `manifest.json`.
4. Otomatis menyinkronkan binary hasil build ke folder [`../web/firmware/`](../web/firmware/).

---

### Opsi 2: One-Liner Docker Langsung (Terminal / Bash / PowerShell)

Dari root proyek:

```powershell
docker run --rm -v "${PWD}:/project" -w /project/firmware espressif/idf:v5.3.1 bash -c "idf.py set-target esp32s3 && idf.py build && python3 package_firmware.py"
```

---

### Opsi 3: Menggunakan `Dockerfile`

Jika Anda ingin membangun image container sendiri:

```powershell
cd firmware
docker build -t bacain-firmware .
```

---

## 📦 Alur Binary & Web Flasher

1. **Hasil Kompilasi**: Berada di folder lokal `firmware/build/`:
   - `bootloader/bootloader.bin` (offset `0x0`)
   - `partition_table/partition-table.bin` (offset `0x8000`)
   - `bacain.bin` (offset `0x20000`)
2. **Factory Merged Binary**:
   - `package_firmware.py` menggabungkan ketiga binary di atas menjadi satu file tunggal: `firmware/build/bacain_factory.bin`.
   - File ini bersama `manifest.json` otomatis disalin ke [`web/firmware/`](../web/firmware/).
3. **Flashing ke Board via Browser**:
   - Buka [`web/index.html`](../web/index.html) di browser Chrome / Edge.
   - Buka tab **Web Serial Flasher**.
   - Hubungkan perangkat ESP32-S3 via kabel USB ke PC.
   - Klik **Flash BacaIn** (Web Serial API akan memprogram flash secara langsung tanpa memerlukan driver atau tool tambahan).

---

## ⚡ Flashing via CLI (Opsional)

Jika Anda ingin flash langsung lewat terminal USB serial menggunakan Docker:

```powershell
# Flash file tunggal factory binary pada offset 0x0
esptool.py -p COM6 -b 460800 write_flash 0x0 build/bacain_factory.bin
```
