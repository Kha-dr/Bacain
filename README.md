# BacaIn

<div align="center">
  <img src="./web/icon.svg" alt="BacaIn" width="25%" />

Mini E-reader firmware for ESP32-S3 e-paper devices with PWA to manage it via Web Bluetooth.
[BacaIn PWA](https://bacain.khadr.dev) | [Web Flasher](https://bacain.khadr.dev/flasher.html).

</div>

> Developed on [Waveshare ESP32-S3-ePaper-1.54](https://github.com/waveshareteam/ESP32-S3-ePaper-1.54). The firmware features decoupled hardware configurations supporting multiple screen sizes, orientations, and GPIO pinouts. See [BOARDS.md](firmware/BOARDS.md) and [firmware/README.md](firmware/README.md).

> [!NOTE]
> The E-paper displays driver are only supports **SSD1681** monochrome screens. Other controller ICs (UC8151, color panels) need their own drivers. Pull requests for new displays are welcome. See [BOARDS.md](firmware/BOARDS.md).

## Features

- **E-Paper Display**: Crisp 1.54" monochrome e-paper display (200x200 px, 1-bit B/W) with battery indicator, book title, and reading percentage.
- **Supported Formats**: Reads `.txt`, `.epub`, and `.fb2` files.
- **Browser Preprocessing**: The PWA automatically cleanup the books.
- **Dual Storage**: Internal flash with optional MicroSD expansion.
- **Web Bluetooth Sync**: Transfer books directly from Progressive Web apps.
- (_Planned_)**Multi-Board Support**: Decoupled hardware configuration supporting multiple ESP32-S3 targets.

## Device Controls

Two-button navigation scheme uses **UP** (BOOT, GPIO 0) and **DOWN** (PWR, GPIO 18).

| Context / Screen | Action             | Button            | Function                             |
| :--------------- | :----------------- | :---------------- | :----------------------------------- |
| **Any (Sleep)**  | Click              | **UP** / **DOWN** | Wake device from deep sleep          |
| **Reading**      | Click              | **UP**            | Previous page                        |
|                  | Click              | **DOWN**          | Next page                            |
|                  | Double Click       | **UP**            | Add or remove bookmark               |
|                  | Double Click       | **DOWN**          | Open bookmarks list                  |
|                  | Long Press (>1.2s) | **UP**            | Open reader menu                     |
| **Menu**         | Click              | **UP**            | Previous item                        |
|                  | Click              | **DOWN**          | Next item                            |
|                  | Double Click       | **UP** / **DOWN** | Select item                          |
| **Book Library** | Click              | **UP**            | Previous book in list                |
|                  | Click              | **DOWN**          | Next book in list                    |
|                  | Double Click       | **UP** / **DOWN** | Open selected book or directory      |
|                  | Long Press (>1.2s) | **UP**            | Return to menu                       |
| **Bookmarks**    | Click              | **UP**            | Previous bookmark in list            |
|                  | Click              | **DOWN**          | Next bookmark in list                |
|                  | Double Click       | **UP** / **DOWN** | Jump to selected bookmark            |
|                  | Long Press (>1.2s) | **UP** / **DOWN** | Return to reading                    |
| **Sync Books**   | Long Press (>1.2s) | **UP**            | Exit sync mode and return to reading |

Note: In reading view, after 60 seconds of inactivity, reading progress is saved and the device enters deep sleep.

## Getting Started

### 1. Hardware & Storage

- **Hardware**: Developed on the [Waveshare ESP32-S3-ePaper-1.54](https://github.com/waveshareteam/ESP32-S3-ePaper-1.54). Pull requests are welcome for adding support for other ESP32-S3 boards.
- **Storage**: MicroSD is optional.
  - The reader defaults to internal flash (2MB firmware, ~5.7MB storage partition for ~20 typical books).
  - If using a MicroSD card, format it as FAT32 or exFAT and place books in the `books/` folder.
  - Syncing via the [BacaIn PWA](https://bacain.khadr.dev) is recommended for automatic text preprocessing.

### 2. Installing Firmware

- **Web Serial Flasher**: Open [https://bacain.khadr.dev/flasher.html](https://bacain.khadr.dev/flasher.html) in Chrome or Edge, connect the board via USB-C, select your board profile, and flash.
- **Building from Source**: See [firmware/README.md](firmware/README.md) for Docker Compose and native ESP-IDF build instructions.

### 3. Syncing Books via Web Bluetooth

1. On the reader, open the menu and select **Sync Books**.
2. Open [https://bacain.khadr.dev](https://bacain.khadr.dev) in Chrome, Edge, or Bluefy on iOS.
3. Click **Connect BacaIn** and select the device.
4. Drop `.txt`, `.epub`, or `.fb2` files to preprocess and upload.
5. Long press **UP** on the reader to exit sync mode and return to reading.

## Repository Structure

```text
.
├── firmware/                      # ESP-IDF firmware source and build system
│   ├── boards/                    # Board profiles and boards.json registry
│   │   └── waveshare_154/         # Waveshare ESP32-S3-ePaper-1.54 profile
│   ├── components/                # Modular ESP-IDF components
│   │   ├── ble_sync/              # NimBLE GATT service for wireless file transfer
│   │   ├── board_config/          # Hardware abstraction and Kconfig definitions
│   │   ├── button/                # Navigation button input handler
│   │   ├── epaper/                # SPI e-paper display driver and font rendering
│   │   ├── power_mgr/             # Power latch, battery ADC monitor, deep sleep
│   │   ├── reader_core/           # Text pager, UTF-8 parser, bookmarks, and library
│   │   └── sdcard/                # 1-line SDMMC host FatFS storage driver
│   ├── main/                      # Application entry point, reader UI, and state machine
│   ├── BOARDS.md                  # Supported board ledger and porting guide
│   ├── CMakeLists.txt             # ESP-IDF project build definition
│   ├── compose.yml                # Docker Compose build service
│   ├── package_firmware.py        # Factory binary packager and manifest generator
│   ├── partitions.csv             # 8MB partition table (2MB app, ~5.7MB storage)
│   └── README.md                  # Detailed firmware build and configuration guide
├── web/                           # PWA client and Web Serial flasher
│   ├── components/                # Web components (uploader, library, dashboard, flasher)
│   ├── processors/                # Browser text cleaners and parsers (EPUB, FB2, TXT)
│   ├── app.js                     # PWA controller and Web Bluetooth manager
│   ├── flasher.html               # Dedicated Web Serial flashing page
│   ├── index.html                 # PWA reader dashboard UI
│   ├── manifest.json              # Web app manifest for installation
│   └── sw.js                      # Offline service worker
├── .github/workflows/             # CI/CD multi-board build matrix and Pages deployment
├── LICENSE                        # CC BY-NC-SA 4.0 license
└── README.md                      # Project overview and quick start guide
```

## Acknowledgments

- Hardware reference: [Waveshare ESP32-S3-ePaper-1.54](https://github.com/waveshareteam/ESP32-S3-ePaper-1.54)
- Serial flashing: [esptool-js](https://github.com/espressif/esptool-js)
- Wireless transfer: [Web Bluetooth API](https://developer.mozilla.org/en-US/docs/Web/API/Web_Bluetooth_API)
