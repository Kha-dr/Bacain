# BacaIn

An ultra-low-power, pocket-sized e-reader firmware and web management suite for ESP32-S3 devices with e-paper displays. Features UTF-8 book pagination, low-power deep sleep, and wireless book syncing via a Progressive Web App (PWA).

The firmware includes a modular hardware configuration layer (`board_config`) with ready-to-use defaults for the **Waveshare ESP32-S3-ePaper-1.54**, while allowing custom screen dimensions, rotation, and GPIO pinouts through Kconfig or build flags.

---

## Key Features

- **E-Paper Display Support**: Clean text rendering with configurable display dimensions and orientation (default: 200x200), supporting multiple font sizes (`Font16`, `Font12`), title header, battery monitor, and reading progress percentage.
- **UTF-8 Pagination & Rolling Cache**: Multi-byte character boundary protection with a 16-deep rolling offset history for instant forward and backward page turns.
- **Power Management & Deep Sleep**: Draws $< 30\ \mu\text{A}$ in deep sleep. Supports power rail latching, display power gating, and instant button wakeup (`Ext1`).
- **Web Bluetooth Sync**: Connect from any browser supporting Web Bluetooth (Chrome, Edge, Bluefy) to manage and upload `.txt` books directly over BLE GATT without installing apps.
- **Dual Storage & Progress Persistence**: Supports MicroSD cards (FAT32 and exFAT) with automatic fallback to internal SPI flash storage. Reading progress and bookmarks are saved automatically.
- **Configurable Hardware**: Screen dimensions, SPI bus pins, buttons, power pins, and audio peripherals can be customized via ESP-IDF Kconfig or environment variables.

---

## Device Controls

Navigation uses a simple two-button interface (default: **BOOT / GPIO 0** and **PWR / GPIO 18**):

| Context | Action | Button | Function |
| :--- | :--- | :--- | :--- |
| **Any (Sleep)** | Click | **BOOT** or **PWR** | Wake device from deep sleep |
| **Reading** | Click | **BOOT** | Next page |
| | Click | **PWR** | Previous page |
| | Double Click | **BOOT** | Add or remove bookmark |
| | Long Press (>1.2s) | **BOOT** | Open reader menu |
| | Long Press (>1.2s) | **PWR** | Enter deep sleep immediately |
| **Menu** | Click | **BOOT** | Next item ($\downarrow$) |
| | Click | **PWR** | Select item ($\checkmark$) |
| | Long Press (>1.2s) | **BOOT** | Close menu and return to reading |
| **Book Library** | Click | **BOOT** | Next book in list |
| | Click | **PWR** | Open selected book |
| | Long Press (>1.2s) | **BOOT** | Return to menu |
| **Bookmarks** | Click | **BOOT** | Next bookmark in list |
| | Click | **PWR** | Jump to selected bookmark |
| | Long Press (>1.2s) | **BOOT** | Return to menu |
| **BLE Sync** | Long Press (>1.2s) | **BOOT** | Exit sync mode and return to reading |

_Note: In reading view, after 60 seconds of inactivity, reading progress is automatically saved to storage and the device enters deep sleep._

---

## Getting Started

### 1. Storage Preparation
- Insert a MicroSD card formatted with **FAT32** or **exFAT** (MBR partition scheme).
- If no card is present, the reader automatically falls back to internal SPI flash storage (`/spiflash`).
- You can copy UTF-8 `.txt` books directly into the `books/` directory on the card or upload them wirelessly via Web Bluetooth.

### 2. Installing Firmware
- **Web Serial Flasher**: Connect the board via USB-C, open `web/flasher.html` in Chrome or Edge, and click **Unduh Firmware Terbaru dari GitHub Release** to flash directly from the browser.
- **Compiling & Customizing**: For building from source (Docker Compose or ESP-IDF), adjusting pinouts, or setting custom display resolutions, refer to [firmware/README.md](firmware/README.md).

### 3. Syncing Books via Web Bluetooth
1. On the reader, open the menu (long press **BOOT**) and select **Sync Books (BLE)**.
2. Open `web/index.html` in Chrome, Edge, or Bluefy on iOS.
3. Click **Connect BacaIn** to pair over Bluetooth.
4. Drag and drop `.txt` files to upload them to the device.
5. Long press **BOOT** on the device to exit sync mode and return to reading.

---

## Repository Structure

```text
.
├── firmware/                      # ESP-IDF firmware source & build system
│   ├── components/
│   │   ├── board_config/          # Hardware configuration & Kconfig definitions
│   │   ├── epaper/                # Hardware SPI display driver & font rendering
│   │   ├── sdcard/                # 1-line SDMMC host FatFS storage driver
│   │   ├── button/                # Hardware button handler (BOOT & PWR)
│   │   ├── power_mgr/             # Power latch, battery ADC monitor, deep sleep
│   │   ├── reader_core/           # Text pager, UTF-8 parser, bookmarks
│   │   └── ble_sync/              # NimBLE GATT service for file transfer
│   ├── compose.yml                # Docker Compose build service
│   ├── package_firmware.py        # Factory binary packaging & manifest generator
│   └── README.md                  # Hardware pinouts, build instructions & CI/CD
├── web/                           # Web application (Web Bluetooth sync & Web Serial flasher)
│   ├── components/                # Modular web components (esp-flasher, book-library, dashboard)
│   ├── index.html                 # Main dashboard UI
│   ├── flasher.html               # Dedicated web flasher page
│   └── sw.js                      # Offline service worker
├── .github/                       # CI/CD workflows, release drafter & GitHub Pages deploy
├── LICENSE                        # CC BY-NC-SA 4.0 license
└── README.md                      # Project overview & quick start
```

---

## Acknowledgments

- Base hardware design, schematics, and reference implementations are based on the [Waveshare ESP32-S3-ePaper-1.54](https://github.com/waveshareteam/ESP32-S3-ePaper-1.54) repository and documentation.

---

## License

This project is open source under the terms of the [Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International (CC BY-NC-SA 4.0)](https://creativecommons.org/licenses/by-nc-sa/4.0/) license:

- **Attribution**: You must give appropriate credit and reference this original project.
- **Share-Alike**: If you modify or build upon this project, your contributions must be released under the same open-source terms.
- **Non-Commercial**: Commercial use is not permitted without explicit permission from the author/owner.
