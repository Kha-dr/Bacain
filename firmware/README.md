# BacaIn Firmware (ESP32-S3)

Direktori ini berisi kode sumber firmware ESP-IDF untuk perangkat e-reader **BacaIn** berbasis modul **Waveshare ESP32-S3-ePaper-1.54** (ESP32-S3-WROOM-1-N8R8, 8MB Quad SPI Flash, 8MB Octal PSRAM).

---

## 1. Hardware Pinout & Fungsi

Semua definisi pin dan parameter layar dikelola terpusat di komponen `board_config` (`firmware/components/board_config/include/board_config.h`).

| Fungsi | Pin / GPIO | Keterangan |
| :--- | :--- | :--- |
| **E-Paper DC** | GPIO 10 | Data / Command control line |
| **E-Paper CS** | GPIO 11 | SPI Chip Select (Active LOW) |
| **E-Paper SCK** | GPIO 12 | SPI Clock |
| **E-Paper MOSI** | GPIO 13 | SPI Master Out Slave In |
| **E-Paper RST** | GPIO 9 | Display hardware reset (Active LOW) |
| **E-Paper BUSY** | GPIO 8 | Busy status indicator (Active HIGH saat refresh) |
| **E-Paper PWR** | GPIO 6 | E-Paper power switch gate (Active HIGH) |
| **MicroSD CLK** | GPIO 39 | 1-line SDMMC Clock |
| **MicroSD CMD** | GPIO 41 | 1-line SDMMC Command |
| **MicroSD D0** | GPIO 40 | 1-line SDMMC Data 0 |
| **Tombol DOWN / BOOT** | GPIO 0 | Input tombol bawah / BOOT (Active LOW, internal pull-up) |
| **Tombol UP / PWR** | GPIO 18 | Input tombol atas / Power key (Active LOW, internal pull-up) |
| **Power Hold** | GPIO 17 | Latch daya sistem (harus dijaga HIGH agar board tetap hidup) |
| **Status LED** | GPIO 3 | Indikator LED hijau |
| **Battery ADC** | ADC1_CH3 (GPIO 4) | Pemantauan tegangan baterai melalui voltage divider (rasio 2.0x) |
| **Audio I2S BCLK** | GPIO 47 | Bit Clock untuk audio |
| **Audio I2S LRCK** | GPIO 48 | Left/Right Word Select |
| **Audio I2S DOUT** | GPIO 45 | Serial Data Out |
| **Audio Power** | GPIO 42 | Audio subsystem power switch |
| **Audio PA** | GPIO 46 | Power Amplifier enable |

---

## 2. Cara Mengubah Konfigurasi Pin & Layar

Konfigurasi hardware mengikuti urutan prioritas 3 tingkat:
1. **Compiler Flags (`-D...`)**: Prioritas tertinggi, mengesampingkan Kconfig dan default.
2. **Kconfig (`CONFIG_BOARD_*`)**: Konfigurasi berbasis menu ESP-IDF.
3. **Default Standar**: Nilai bawaan Waveshare ESP32-S3-ePaper-1.54.

### A. Menggunakan `menuconfig` (Kconfig)
Buka menu konfigurasi melalui terminal:
```bash
idf.py menuconfig
```
Navigasi ke menu **Board & Hardware Configuration**:
- Atur dimensi display (`BOARD_DISPLAY_WIDTH`, `BOARD_DISPLAY_HEIGHT`, rotasi 0–3).
- Atur pin SPI E-Paper, pin MicroSD SDMMC, pin tombol, atau pin audio.
- Simpan perubahan ke `sdkconfig`.

### B. Menggunakan Variabel Lingkungan di Docker Compose
Anda dapat mengompilasi profil kustom tanpa mengubah berkas sumber:
```bash
EXTRA_IDF_ARGS="-DCONFIG_BOARD_DISPLAY_WIDTH=250" BOARD_NAME="Custom-Board" docker compose run --rm firmware
```

---

## 3. Cara Build & Menggabungkan Binary (Packaging)

### Opsi A: Menggunakan Docker Compose (Direkomendasikan)
Tidak memerlukan instalasi ESP-IDF lokal di host:
```bash
cd firmware
docker compose up
```
Docker Compose akan:
1. Menjalankan image `espressif/idf:v5.3.1`.
2. Mengompilasi proyek untuk target `esp32s3` ke folder `/project/build`.
3. Menjalankan `python3 package_firmware.py /project/build -o bacain_factory.bin --board "Waveshare ESP32-S3-ePaper-1.54"`.
4. Menghasilkan biner pabrik gabungan `build/bacain_factory.bin` dan berkas metadata `build/manifest.json`.

### Opsi B: Build Manual via Native ESP-IDF
Jika ESP-IDF v5.2+ sudah terpasang di host:
```bash
cd firmware
idf.py set-target esp32s3
idf.py build
python3 package_firmware.py build -o bacain_factory.bin --board "Waveshare ESP32-S3-ePaper-1.54"
```

### Parameter Skrip Pengemasan (`package_firmware.py`)
Skrip pengemasan menerima argumen berikut:
- `build_dir`: Path direktori build (default: `build`).
- `-o, --output`: Nama file binary hasil penggabungan (default: `bacain_factory.bin`).
- `--board`: Nama profil board di dalam manifest (default: `Waveshare ESP32-S3-ePaper-1.54`).
- `--chip`: Target chip esptool (default: `esp32s3`).
- `--flash-size`: Ukuran memori flash (default: `8MB`).
- `--name`: Nama firmware di dalam manifest (default: `BacaIn Firmware`).

### Alur Penggabungan Binary
Skrip menggunakan `esptool merge_bin` untuk menyatukan partisi ke dalam satu file biner tunggal:
- `0x0000`: `bootloader.bin`
- `0x8000`: `partition-table.bin`
- `0x20000`: `bacain.bin`

---

## 4. Alur Otomasi CI/CD & Build Kustom via GitHub Actions

Workflow `.github/workflows/firmware_ci.yml` mendukung build otomatis dan build kustom via manual trigger:

### A. Triggers Otomatis
- **Push ke `main` & Pull Request**: Memvalidasi build firmware dengan konfigurasi standar.
- **Tag Rilis (`v*`)**: Mengompilasi dan mengunggah seluruh artefak biner (`bacain_factory.bin`, `manifest.json`, dll.) ke GitHub Releases publik.

### B. Build Firmware Kustom via `workflow_dispatch` (Manual Trigger)
Anda dapat memicu kompilasi profil hardware kustom langsung dari web GitHub tanpa menginstal toolchain:

1. Buka tab **Actions** di repositori GitHub.
2. Pilih workflow **BacaIn Firmware CI/CD**.
3. Klik menu drop-down **Run workflow**.
4. Isi parameter yang diinginkan:
   - **Board Profile Name**: Nama profil board (default: `Waveshare ESP32-S3-ePaper-1.54`).
   - **Extra IDF Build Args / Compiler Macros**: Opsi compiler flag `-D...` untuk memodifikasi pin, resolusi, atau rotasi.
   - **Output Factory Binary Name**: Nama file biner gabungan (default: `bacain_factory.bin`).
5. Klik **Run workflow**. Setelah selesai, unduh artefak zip `bacain-firmware` dari halaman workflow run.

#### Contoh Flag untuk Form `Extra IDF Build Args`:

| Kebutuhan Kustom | Nilai Compiler Macro |
| :--- | :--- |
| **Ubah Ukuran Layar** (misal 250 $\times$ 122) | `-DCONFIG_BOARD_DISPLAY_WIDTH=250 -DCONFIG_BOARD_DISPLAY_HEIGHT=122` |
| **Ubah Orientasi Layar** (90 derajat landscape) | `-DCONFIG_BOARD_DISPLAY_ROTATION=1` |
| **Ubah Pin Tombol** (misal BOOT=4, PWR=5) | `-DCONFIG_BOARD_BTN_DOWN_PIN=4 -DCONFIG_BOARD_BTN_UP_PIN=5` |
| **Ubah Pin SPI E-Paper** | `-DCONFIG_BOARD_EPD_DC_PIN=14 -DCONFIG_BOARD_EPD_CS_PIN=15` |
| **Kombinasi Beberapa Pengaturan** | `-DCONFIG_BOARD_DISPLAY_WIDTH=296 -DCONFIG_BOARD_DISPLAY_HEIGHT=128 -DCONFIG_BOARD_DISPLAY_ROTATION=1 -DCONFIG_BOARD_BTN_DOWN_PIN=4` |

---

## 5. Flashing ke Perangkat

### A. Melalui Web Serial PWA
Buka PWA di browser Chromium (Chrome/Edge):
1. Sambungkan perangkat ke komputer via USB-C.
2. Buka tab **Firmware Flasher**.
3. Klik **Unduh Firmware Terbaru dari GitHub Release** atau pilih berkas `bacain_factory.bin` lokal.
4. Klik **Connect Serial Port**, pilih port ESP32-S3, lalu klik **Flash Firmware**.

### B. Melalui Serial Terminal (ESP-IDF)
```bash
idf.py -p COM3 flash monitor          # Windows (sesuaikan port)
idf.py -p /dev/ttyUSB0 flash monitor  # Linux / macOS
```
