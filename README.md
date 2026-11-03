# BacaIn

<div align="center">
  <img src="./web/icon.svg" alt="BacaIn" width="25%" />

Mini E-reader firmware for ESP32-S3 e-paper devices with PWA management via Web Bluetooth.

[BacaIn PWA](https://bacain.khadr.dev) | [Web Flasher](https://bacain.khadr.dev/flasher.html)

</div>

Developed on [Waveshare ESP32-S3-ePaper-1.54](https://github.com/waveshareteam/ESP32-S3-ePaper-1.54) (SSD1681 monochrome panel). See [BOARDS.md](firmware/BOARDS.md) for pinouts and other board profiles.

## Features

- **Display**: 1.54" monochrome e-paper (200x200 px, 1-bit B/W) with status header and footer hints.
- **Formats**: Reads `.txt`, `.epub`, and `.fb2` files.
- **Storage**: Internal flash (~5.7MB FATFS) with optional MicroSD card expansion.
- **Wireless**: Web Bluetooth file transfer and settings management via PWA.
- **Power**: 20s auto-sleep, 0.0 uA static display hold, instant page turn on wake.
- **Ghosting Mitigation**: Automatic full refresh every 10 pages, with manual refresh option.

## Controls

Two-button navigation using **UP** (BOOT, GPIO 0) and **DOWN** (PWR, GPIO 18).

| Context                        | Action               | Button            | Function                  |
| :----------------------------- | :------------------- | :---------------- | :------------------------ |
| **Any (Sleep)**                | Click                | **UP** / **DOWN** | Wake device and turn page |
| **Any**                        | Hold together (2.0s) | **UP** + **DOWN** | Force reboot              |
| **Reading**                    | Click                | **UP**            | Previous page             |
|                                | Click                | **DOWN**          | Next page                 |
|                                | Double Click         | **UP**            | Toggle bookmark           |
|                                | Double Click         | **DOWN**          | Open bookmarks list       |
|                                | Hold (>1.2s)         | **UP** / **DOWN** | Open reader menu          |
| **Menu / Library / Bookmarks** | Click                | **UP** / **DOWN** | Navigate items            |
|                                | Double Click         | **UP** / **DOWN** | Select or open            |
|                                | Hold (>1.2s)         | **UP** / **DOWN** | Back / Exit               |

## Reader Menu

Access the 7-item menu by holding (>1.2s) UP or DOWN while reading:

1. **Continue Reading**: Resume reading current book.
2. **Book Library**: Browse internal flash and MicroSD books.
3. **Font Size: [16px / 12px]**: Toggle text font size (saved in NVS).
4. **Sync Books**: Start Web Bluetooth GATT service and show QR code.
5. **Nav Hints**: Toggle footer navigation hints on or off.
6. **Refresh Screen**: Trigger manual full display refresh to clear ghosting.
7. **Power Off**: Display power-off screen and enter deep sleep.

## Power and Refresh

- **Sleep**: Automatically enters deep sleep after 20 seconds of inactivity. E-paper panel holds static image at 0.0 uA. Pressing UP or DOWN wakes instantly and turns the page.
- **Full Refresh**: Runs automatically every 10 pages (`APP_FULL_REFRESH_INTERVAL 10`). Page counter persists in RTC memory across deep sleep cycles. Manual refresh is available from the menu.

## Quick Start

### 1. Flash Firmware

Open [bacain.khadr.dev/flasher.html](https://bacain.khadr.dev/flasher.html) in Chrome or Edge, connect the device via USB-C, and flash. To build from source using ESP-IDF or Docker, see [firmware/README.md](firmware/README.md).

### 2. Sync Books

1. On the reader, hold UP or DOWN (>1.2s) to open the menu, then select **Sync Books**.
2. Scan the QR code or navigate to [bacain.khadr.dev](https://bacain.khadr.dev) in Chrome, Edge, or Bluefy (iOS).
3. Connect to the reader and upload `.txt`, `.epub`, or `.fb2` files.
4. Hold UP or DOWN (>1.2s) on the reader to exit sync mode.

## Repository Structure

```text
.
├── firmware/       # ESP-IDF firmware source, drivers, and board profiles
├── web/            # Companion PWA, text processors, and Web Serial flasher
├── .github/        # Build and deployment workflows
├── LICENSE         # CC BY-NC-SA 4.0 license
└── README.md
```

## Acknowledgments

- Hardware reference: [Waveshare ESP32-S3-ePaper-1.54](https://github.com/waveshareteam/ESP32-S3-ePaper-1.54)
- Serial flashing: [esptool-js](https://github.com/espressif/esptool-js)
- Wireless transfer: [Web Bluetooth API](https://developer.mozilla.org/en-US/docs/Web/API/Web_Bluetooth_API)
