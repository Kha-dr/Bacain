# BacaIn Firmware (ESP32-S3)

Direktori ini berisi kode sumber firmware ESP-IDF untuk perangkat e-reader **BacaIn** berbasis modul **Waveshare ESP32-S3-ePaper-1.54G** (ESP32-S3-WROOM-1-N8R8, 8MB Quad SPI Flash, 8MB Octal PSRAM).

---

## 1. Build Firmware via Docker Compose (Recommended)

Membangun firmware menggunakan Docker Compose memastikan lingkungan build yang reproducible dan terisolasi tanpa perlu menginstal ESP-IDF toolchain atau Python dependencies secara manual di komputer host.

Pastikan Docker Desktop sudah terpasang dan service Docker telah berjalan.

### Cara 1: Menggunakan Docker Compose (Paling Mudah)

Masuk ke folder `firmware/` lalu jalankan:

```bash
cd firmware
docker compose up
```
*(Atau `docker compose run --rm firmware` jika ingin container otomatis dihapus setelah selesai)*

Docker Compose akan:
1. Menjalankan kontainer resmi `espressif/idf:v5.3.1`.
2. Melakukan targetting ke `esp32s3` dan mengompilasi firmware ke root **`.\build`** (`idf.py -B /project/build build`).
3. Menjalankan `package_firmware.py` untuk menggabungkan factory binary (**`build/bacain_factory.bin`**) dan membuat **`build/manifest.json`**.
4. Semua artefak kompilasi tersimpan rapi di satu tempat (**`.\build`**) yang diabaikan oleh `.gitignore`.

### Cara 2: Menggunakan Perintah One-Liner Docker Langsung

Dari root repositori:
```bash
docker run --rm -v "${PWD}:/project" -w /project/firmware espressif/idf:v5.3.1 bash -c "idf.py -B /project/build set-target esp32s3 && idf.py -B /project/build build && python3 package_firmware.py /project/build"
```

---

## 2. Alur Output Binary & Packaging

Seluruh hasil kompilasi dan pengemasan firmware tersimpan di root **`.\build`**:

1. **Hasil Kompilasi ESP-IDF (`build/`)**:
   - `build/bootloader/bootloader.bin` (Bootloader ESP32-S3, offset `0x0000`)
   - `build/partition_table/partition-table.bin` (Tabel partisi 8MB, offset `0x8000`)
   - `build/bacain.bin` (Aplikasi utama BacaIn, offset `0x20000`)

2. **Pengemasan Otomatis (`package_firmware.py`)**:
   - Menjalankan `esptool merge_bin` untuk menggabungkan ketiga berkas biner di atas menjadi satu file biner pabrik tunggal:
     **`build/bacain_factory.bin`** (offset awal `0x0000`, ukuran flash 8MB).
   - Menghasilkan berkas metadata **`build/manifest.json`** yang memuat checksum, offset partisi, dan timestamp kompilasi.

> [!NOTE]
> Seluruh isi folder `.\build` diabaikan oleh Git (`.gitignore`). Folder `firmware/` dan `web/` sepenuhnya bersih hanya berisi kode sumber tanpa tercampur artefak biner.

---

## 3. Flashing ke Hardware via Web Serial PWA

Anda dapat melakukan flashing firmware ke hardware ESP32-S3 langsung dari peramban web modern tanpa instalasi driver khusus atau CLI ESP-IDF.

1. **Hubungkan Hardware**:
   - Sambungkan port USB-C board ESP32-S3 ke komputer.
   - *(Jika board belum pernah di-flash atau gagal boot: tekan dan tahan tombol **BOOT**, hubungkan kabel USB, lalu lepaskan tombol BOOT untuk masuk ke mode ROM Download).*

2. **Jalankan Web Server Lokal**:
   Web Serial API memerlukan konteks aman (`http://localhost` atau `https://`).
   ```bash
   cd ../web
   npx serve .
   ```
   Buka alamat yang tampil di terminal (misal: `http://localhost:3000`) menggunakan Google Chrome atau Microsoft Edge.

3. **Lakukan Flash Melalui PWA**:
   - Buka tab **Firmware Flasher (Web Serial)** pada panel navigasi atas.
   - Klik tombol **Connect & Flash Firmware**.
   - Pilih port serial ESP32-S3 yang terdeteksi pada dialog pop-up browser.
   - Web flasher akan membaca berkas `web/firmware/manifest.json`, menulis binary ke flash chip ESP32-S3, dan melakukan restart perangkat otomatis saat proses selesai 100%.

---

## 4. Opsi Alternatif: Flash Manual via CLI

Jika ingin mem-flash secara manual menggunakan ESP-IDF atau esptool di terminal host:

```bash
# Opsi A: Flash langsung via idf.py (membutuhkan local ESP-IDF)
idf.py -p COM3 flash monitor

# Opsi B: Flash single binary factory menggunakan esptool
esptool.py --chip esp32s3 -p COM3 -b 460800 write_flash 0x0 web/firmware/bacain_factory.bin
```
