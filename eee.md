Perubahan telah selesai diimplementasikan, diverifikasi oleh Verifikator Independen, dan diaudit final oleh Main Agent sesuai protokol AGENTS.md:
──────

### 1. Ringkasan Perubahan yang Diterapkan

#### A. Menghilangkan Delay Saat Memilih (Selection Latency Fix)

• Penyebab: Sebelumnya, driver multi_button.c menunggu window timeout SHORT_TICKS (300 ms) bahkan setelah klik kedua dilepas (menunggu kemungkinan triple-click).
• Perbaikan: 1. SHORT_TICKS dipersingkat dari 300 ms menjadi 180 ms pada multi_button.h:15. 2. Pada state BTN_STATE_REPEAT, begitu tombol kedua dilepaskan, event MBTN_DOUBLE_CLICK langsung ditembakkan seketika (0 ms lag) tanpa menunggu timeout lagi.

#### B. Skema Tombol Navigasi Menu Baru

• Dalam VIEW_MENU & VIEW_BOOK_LIST:
• Tombol BOOT (GPIO 0): Navigasi DOWN / Next (s_menu_index++).
• Tombol PWR (GPIO 18): Navigasi UP / Previous (s_menu_index--).
• Double Tap (BOOT atau PWR): SELECT / CONFIRM.
• Long Press BOOT: Kembali ke mode membaca (Cancel / Back).
• Footer UI: Diperbarui dengan teks panduan: BOOT:Dn PWR:Up | 2xTap:Select.

#### C. Indikator Visual Layar Saat Sleep / Power Off

• Fungsi reader_ui_render_sleeping() baru ditambahkan pada reader_ui.c:241-255.
• Sebelum ESP32-S3 masuk ke deep sleep (baik via Menu Sleep, Inactivity Timeout, BLE Sleep, maupun Long Press PWR), layar e-Paper menampilkan kartu status:
+-------------------------------+
| Mini-E-Reader |
| |
| SLEEPING... |
| |
| Press [PWR] to Wake |
+-------------------------------+
Karena e-Paper bersifat bistabil, gambar ini akan tetap tampil di layar tanpa mengonsumsi daya baterai sama sekali (0 μA) saat perangkat tertidur lelap.
──────

### 2. Diagram Arsitektur & Alur Kerja Firmware

Berikut adalah visualisasi menyeluruh tentang cara kerja Mini-E-Reader:

│ Diagram exceeds terminal width (717 > 368 cols)
│ Displayed as code block. Widen terminal to view inline.

```mermaid

    graph TD
        subgraph HW ["1. LAPISAN HARDWARE (Waveshare ESP32-S3 e-Paper 1.54 V2)"]
            MCU["ESP32-S3 (240MHz, 8MB Flash, 8MB PSRAM)"]
            EPD["Display e-Paper 1.54'' SSD1681 (SPI3, 200x200 1-bit)"]
            FLASH["Internal SPI Flash (5.8MB FATFS Wear-Levelling)"]
            SD["MicroSD Slot (1-line SDMMC, Opsional)"]
            BTNS["Tombol Fisik (BOOT: GPIO 0, PWR: GPIO 18)"]
            PWR_LATCH["Power Latch (GPIO 17: Hold 3.3V, GPIO 42: Audio Cut)"]
            RADIO["Radio BLE 5.0 (NimBLE GATT Server)"]
        end

        subgraph DRV ["2. HARDWARE DRIVERS & HAL"]
            PM["power_mgr<br/>- Board Latch<br/>- Battery ADC<br/>- Deep Sleep"]
            EPD_DRV["epaper_port<br/>- SSD1681 Init<br/>- Fast Partial (0xCF)<br/>- Full Refresh (0xC7)"]
            FS_DRV["sdcard / storage<br/>- Default: /spiflash<br/>- Secondary: /sdcard"]
            BTN_DRV["button / multi_button<br/>- 5ms Hardware Timer<br/>- Click / Double-Click / Long"]
            BLE_DRV["ble_sync<br/>- 512B MTU Streaming<br/>- Custom Opcodes"]
        end

        subgraph CORE ["3. READER CORE & STATE"]
            LIB["book_library<br/>- Scan .txt files<br/>- Atomic state.json<br/>- Dual Storage Target"]
            PAGER["text_pager<br/>- Byte-offset Indexing<br/>- Dynamic Font (12/16px)<br/>- Line Wrap Engine"]
            PAINT["GUI_Paint<br/>- 1-Bit Monochrome Framebuffer<br/>- 5,000 Bytes Canvas Buffer"]
        end

        subgraph APP ["4. APPLICATION STATE MACHINE (main.c)"]
            SM{"Reader State Machine"}
            V_READ["VIEW_READING<br/>- Baca Buku<br/>- BOOT: Next Page<br/>- PWR: Prev Page<br/>- Long BOOT: Open Menu"]
            V_MENU["VIEW_MENU<br/>- BOOT: Down<br/>- PWR: Up<br/>- 2xTap: Select"]
            V_LIB["VIEW_BOOK_LIST<br/>- BOOT: Down<br/>- PWR: Up<br/>- 2xTap: Open Book"]
            V_BLE["VIEW_BLE_SYNC<br/>- Web BLE Upload<br/>- Realtime Rx Counter"]
            V_SLEEP["DEEP SLEEP<br/>- Show Sleep Screen<br/>- RTC Wakeup via PWR<br/>- Power Consumption ~15µA"]
        end

        HW --> DRV
        DRV --> CORE
        CORE --> APP

        SM -->|Default / Resume| V_READ
        V_READ -->|Long BOOT| V_MENU
        V_MENU -->|Double Tap Option 0| V_READ
        V_MENU -->|Double Tap Option 1| V_LIB
        V_MENU -->|Double Tap Option 3| V_BLE
        V_MENU -->|Double Tap Option 4| V_SLEEP
        V_LIB -->|Double Tap Book| V_READ
        V_BLE -->|Long BOOT Exit| V_MENU
        V_READ -->|Inactivity Timeout| V_SLEEP
```

──────

### 3. State Machine Interaksi Tombol (Matrix Kontrol)

    📊 Diagram (unsupported type)
    ──────────────────────────────

```mermaid
    stateDiagram-v2
        [*] --> BOOT_INIT: Power On / Wakeup

        state "BOOT_INIT" as B_INIT {
            [*] --> LatchPower: GPIO 17 HIGH
            LatchPower --> MountStorage: Default /spiflash
            MountStorage --> LoadState: Read state.json
            LoadState --> [*]
        }

        BOOT_INIT --> VIEW_READING: Book Available
        BOOT_INIT --> VIEW_BLE_SYNC: No Books Found

        state VIEW_READING {
            BOOT_Click: BOOT 1xTap -> Next Page (Partial Refresh ~300ms)
            PWR_Click: PWR 1xTap -> Prev Page (Partial Refresh ~300ms)
            BOOT_Long: BOOT Hold -> Open Menu
            PWR_Long: PWR Hold -> Sleep Instan
        }

        state VIEW_MENU {
            M_BOOT: BOOT 1xTap -> Navigasi Turun (Down)
            M_PWR: PWR 1xTap -> Navigasi Naik (Up)
            M_Dbl: 2xTap (BOOT/PWR) -> Select Option
            M_Long: BOOT Hold -> Back to Reading
        }

        state VIEW_BOOK_LIST {
            L_BOOT: BOOT 1xTap -> Pilih Judul Berikutnya (Down)
            L_PWR: PWR 1xTap -> Pilih Judul Sebelumnya (Up)
            L_Dbl: 2xTap (BOOT/PWR) -> Buka Buku Terpilih
            L_Long: BOOT Hold -> Kembali ke Menu
        }

        state SLEEP_SEQUENCE {
            ShowSleepingScreen: Render 'SLEEPING... Press PWR to Wake'
            SaveOffset: Atomic write state.json
            EnterDeepSleep: Disable CPU & Latch Audio/Power Off
        }

        VIEW_READING --> VIEW_MENU: BOOT Hold
        VIEW_MENU --> VIEW_READING: 2xTap Continue / BOOT Hold
        VIEW_MENU --> VIEW_BOOK_LIST: 2xTap Library
        VIEW_BOOK_LIST --> VIEW_READING: 2xTap Open
        VIEW_BOOK_LIST --> VIEW_MENU: BOOT Hold

        VIEW_READING --> SLEEP_SEQUENCE: PWR Hold / 60s Idle
        VIEW_MENU --> SLEEP_SEQUENCE: 2xTap Option 'Power Off / Sleep'
        SLEEP_SEQUENCE --> [*]
```

──────

### 4. Pipeline Refresh Layar e-Paper

┌──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┐ ┌─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┐
│ "Partial Refresh (~300ms, NO FLICKER)" │ │ "Full Refresh (~1.8s, Anti-Ghosting)" │
│ │ │ │
│ │ │ │
│ ┌───────────────────────────────┐ ┌────────────────────────────────┐ ┌───────────────────────┐ ┌────────────────────────────────────────────────────────────┐ │ ├──────────────────────────────────────────────────────────┐ │
│ │ │ │ │ │ │ │ │ │ │ │ │
│ │ Permintaan Render Layar │ │ COND ├──────────────────────┐ │ P4 ├─┐ │ F2 ├─┤ │ F4 ├────────────────────────────────┐ │
│ │ │ │ │ │ │ │ │ │ │ │ │ │ │ │
│ └───────────────┬───────────────┘ └────────────────┬───────────────┘ Halaman ke-10 / Force Refresh PWA / Clear └───────────────────────┘ │ └────────────────────────────────────────────────────────────┘ │ ├──────────────────────────────────────────────────────────┘ │ │
│ │ Baca Halaman 1-9 / Menu / BLE │ │ │ │ │ │
│ ▼ ▼ └─────────────────────────────────▼ └────────────────────────────────▼ ├─┼─────────────────────────────▼ ▼ │
│ ┌───────────────────────────────┐ ┌────────────────────────────────┐ ┌───────────────────────┐ ┌────────────────────────────────────────────────────────────┐ │ ├──────────────────────────────────────────────────────────┐ ┌────────────────────────────────────────────────────────┐ │
│ │ │ │ │ │ │ │ │ │ │ │ │ │ │
│ │ COND{Jenis Pembaruan?} │ ┌─┤ Fast Partial Refresh │ ┌─────────────────────┤ Periodic Full Refresh │ │ Hasil: Partikel berpindah langsung tanpa kedip hitam-putih │ ├─┤ Kirim Perintah 0x22 -> 0xC7 │ │ Kembalikan Driver ke Mode Partial (WF_PARTIAL_1IN54_0) │ │
│ │ │ │ │ │ │ │ │ │ │ │ │ │ │ │ │
│ └───────────────────────────────┘ │ └────────────────────────────────┘ │ └───────────────────────┘ └────────────────────────────────────────────────────────────┘ │ ├──────────────────────────────────────────────────────────┘ └────────────────────────────┬───────────────────────────┘ │
│ │ │ │ │ │ │
│ │ │ │ │ │ │
│ │ │ │ │ │ │
│ ┌─────────────────┘ ┌──────────────────────────────────────┘ ┌──────────────────────────────────────────────┬───────────────────────────────┤ │ │ │
│ │ │ │ │ │ │ │ │
│ ▼ ▼ ▼ ▼ │ │ │ │
│ ┌───────────────────────────────┐ ┌────────────────────────────────┐ ┌───────────────────────┐ ┌────────────────────────────────────────────────────────────┐ │ ├──────────────────────────────────────────────────────────┐ │ │
│ │ │ │ │ │ │ │ │ │ │ │ │ │
│ │ Set Kursor (0, 199) │ │ Reset LUT ke WF_Full_1IN54 ├──────────────────────┐ │ F4["Trigger 0x20 │ │ Tunggu BUSY ~1.760ms"] │ │ │ Hasil: Ghosting bersih sempurna, siap partial berikutnya │◄───────────────────────────────┘ │
│ │ │ │ │ │ │ │ │ │ │ │ │ │
│ └───────────────┬───────────────┘ └────────────────┬───────────────┘ │ └───────────────────────┘ └────────────────────────────────────────────────────────────┘ │ ├──────────────────────────────────────────────────────────┘ │
│ │ │ │ │ │ │
│ ▼ ▼ └─────────────────────────────────▼ │ │ │
│ ┌───────────────────────────────┐ ┌────────────────────────────────┐ ┌───────────────────────┐ │ │ │
│ │ │ │ │ │ │ │ │ │
│ │ Kirim 5.000 Bytes ke RAM 0x24 │ │ F2["Kirim Buffer ke 0x24 │ │ 0x26"] │ │ │ │
│ │ │ │ │ │ │ │ │ │
│ └───────────────┬───────────────┘ └────────────────────────────────┘ └───────────────────────┘ │ │ │
│ │ │ │ │
│ ▼ │ └─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┘
│ ┌───────────────────────────────┐ │
│ │ │ │
│ │ Kirim Perintah 0x22 -> 0xCF ├────────────────────┐ │
│ │ │ │ │
│ └───────────────┬───────────────┘ │ │
│ │ │ │
│ ▼ ▼ │
│ ┌───────────────────────────────┐ ┌────────────────────────────────┐ │
│ │ │ │ │ │
│ │ P4["Trigger 0x20 │ │ Tunggu BUSY ~300ms"] │ │
│ │ │ │ │ │
│ └───────────────────────────────┘ └────────────────────────────────┘ │
│ │
└──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┘
──────

### 5. Hasil Verifikasi Zero-Trust

• Subagent Verifikator: bb95e2d5-6572-44bf-a6bc-e0524714f7ba.
• Skrip Pengujian Baru: scratch/button_menu_verification.js (63/63 PASSED).
• Audit Regresi Keseluruhan: 191/191 Checks PASSED (0 Failed).
• Zero Mock: Tidak ada timer palsu atau data dummy.
──────

### 6. Flashing ke Board

Jalankan perintah berikut di terminal ESP-IDF Anda:

    cd reader_firmware
    idf.py -p COM6 flash monitor

Sekarang seleksi double-tap langsung responsif tanpa delay, navigasi menu mendukung Up & Down dengan kedua tombol, dan saat memilih Sleep, layar akan menampilkan kartu status tidur secara jelas.
