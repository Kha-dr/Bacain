# Supported Hardware & Board Profiles

BacaIn firmware uses a modular board configuration architecture to support multiple ESP32-based hardware targets. Each board profile encapsulates pin mappings, display parameters, and power management settings, allowing developers to target different PCB layouts and e-paper displays without modifying core application logic.

---

## 1. Supported Boards Ledger

| Board ID        | Board Name                     | SoC / Chip                  | Flash / RAM                    | Display (Size & Controller)                                            | Controls                                 | Status                         |
| :-------------- | :----------------------------- | :-------------------------- | :----------------------------- | :--------------------------------------------------------------------- | :--------------------------------------- | :----------------------------- |
| `waveshare_154` | Waveshare ESP32-S3-ePaper-1.54 | ESP32-S3 (Xtensa dual-core) | 8MB Quad SPI Flash, 512KB SRAM | 1.54" E-Paper (SSD1681), 200x200 px, 1-bit B/W, 0 deg default rotation | UP / BOOT (GPIO 0), DOWN / PWR (GPIO 18) | Official / Supported (Default) |

---

## 2. Board Configuration Architecture

The hardware abstraction layer is organized into three components:

1. **Board Definition Header (`firmware/boards/<board_id>/board_def.h`)**:
   Defines board-specific pinouts and hardware constants:
   - Display SPI bus pins (DC, CS, SCK, MOSI, RST, BUSY, PWR).
   - Display dimensions and controller parameters (width, height, default rotation).
   - Navigation button GPIO allocations (UP/BOOT, DOWN/PWR).
   - Peripheral assignments (MicroSD SDMMC bus, I2S audio, power latch, status LED, battery ADC).

2. **Catalog Metadata (`firmware/boards/boards.json`)**:
   Maintains a structured JSON catalog of supported boards. This metadata is consumed by the Web Serial Flasher, packaging tools, and CI build scripts to identify board targets, chip architectures, memory sizes, and binary artifacts.

3. **Kconfig Profile Selection (`choice BOARD_PROFILE`)**:
   Managed under `firmware/components/board_config/Kconfig`. Developers can select a preconfigured profile via `idf.py menuconfig` or specify a custom pinout. Selecting a profile sets default pin and display values, while preserving the ability to override any parameter via compiler flags (`-D<flag>`).

---

## 3. Display Controller Compatibility (Why E-Paper Is Not Plug-and-Play)

Unlike SPI LCD or OLED panels where a generic framebuffer transfers over standard protocols, e-paper displays depend directly on controller IC architecture, voltage waveforms, and physical panel characteristics. Connecting a panel with a different controller requires dedicated driver routines.

### Controller IC Differences

E-paper displays use specialized controller ICs with distinct instruction sets and driving characteristics:
- **SSD1681 vs SSD1680**: Both are Solomon Systech controllers, but they differ in gate/source channel counts, timing specifications, and internal register mappings. Running SSD1681 initialization on an SSD1680 results in coordinate misalignments or corrupt frame boundaries.
- **UC8151D (IL0373)**: Common on various GoodDisplay panels. The UC8151D uses completely different SPI command bytes (such as `0x00` for panel setting, `0x04` for power on, `0x10`/`0x13` for image data start/stop) and inverted BUSY pin polarity (active-low on UC8151D versus active-high on SSD1681).
- **Color Panels (B/W/Red or 7-Color)**: Color e-paper displays require multi-phase driving cycles and separate RAM planes. They cannot run fast monochrome partial refresh cycles without specialized waveform sequencing.

### Look-Up Table (LUT) Waveforms and Partial Refresh

E-paper particles (microcapsules or microcups) move through viscous fluid under electrical fields. Driving these particles requires precise voltage pulse sequences (waveforms) stored in Look-Up Tables:
- **Full Refresh**: Cycles particles through multiple voltage inversion phases to eliminate ghosting and balance DC bias before writing the new frame.
- **Partial Refresh**: Rapidly updates only changed pixels (typically under 300 ms) without flashing the full display.
- In `firmware/components/epaper/epaper_port.c`, 159-byte custom LUT tables (`WF_Full_1IN54` and `WF_PARTIAL_1IN54_0`) are written directly to command register `0x32`, followed by gate voltage (`0x3F`), source drive voltage (`0x03`, `0x04`), and VCOM (`0x2C`) configuration. Panels with different physical glass or internal OTP controllers require their own tuned waveform tables.

### Register Maps and RAM Windows

The current driver implements SSD1681-specific register layouts:
- **Dual Frame RAM**: Command `0x24` writes the current black/white frame buffer. Command `0x26` writes the previous frame buffer used as a baseline for differential partial updates.
- **Window Addressing**: Command `0x44` configures horizontal byte boundaries (`0x00` to `0x18`, spanning 24 bytes or 200 pixels). Command `0x45` configures vertical scan lines (`0xC7` to `0x00`, spanning lines 199 down to 0).
- **Cursor Positioning**: Commands `0x4E` (X address counter) and `0x4F` (Y address counter) position the write pointer before streaming image bytes.
- **Update Activation**: Command `0x22` sets the display sequence control mode (`0xC7` for full refresh, `0xCF` for partial refresh), followed by command `0x20` (Master Activation) and busy-wait polling.

### Current Driver Support

The driver in `firmware/components/epaper/` is specifically tuned for:
- Controller IC: Solomon Systech SSD1681
- Panel resolution: 200x200 pixels (1.54" monochrome, 1-bit black/white)
- Framebuffer footprint: 5000 bytes per plane (`(200 * 200) / 8`)

### Supporting Future Display Controllers

BacaIn uses a decoupled architecture to accommodate future e-paper controllers:
1. **Catalog Registration**: Specify controller model, resolution, and orientation in `firmware/boards/boards.json` under the `display` object.
2. **Board Definition**: Map display SPI bus and control pins (`EPD_DC`, `EPD_CS`, `EPD_BUSY`, `EPD_RST`, `EPD_PWR`) in `firmware/boards/<board_id>/board_def.h`.
3. **Driver Routines**: Implement controller-specific initialization, LUT configuration, and RAM write routines in `firmware/components/epaper/`. Higher-level components (`reader_core`, UI, and text pagination) consume the abstract display interface and do not require modification.

---

## 4. Community Contribution Guide

To contribute a new board profile, follow this 4-step checklist and submit a pull request:

### Step 1: Create Board Definition Header

Create directory `firmware/boards/<board_id>/` and add `board_def.h`. Define all required macros:

- Display resolution: `BOARD_DISPLAY_WIDTH`, `BOARD_DISPLAY_HEIGHT`, `BOARD_DISPLAY_ROTATION`
- E-Paper SPI: `BOARD_EPD_DC_PIN`, `BOARD_EPD_CS_PIN`, `BOARD_EPD_SCK_PIN`, `BOARD_EPD_MOSI_PIN`, `BOARD_EPD_RST_PIN`, `BOARD_EPD_BUSY_PIN`, `BOARD_EPD_PWR_PIN`
- Navigation buttons: `BOARD_BTN_UP_PIN`, `BOARD_BTN_DOWN_PIN`
- Peripherals: SDMMC pins, power hold pin, status LED pin, and battery ADC channel

### Step 2: Register Profile in Kconfig

Add a config entry under `choice BOARD_PROFILE` in `firmware/components/board_config/Kconfig`:

```kconfig
config BOARD_PROFILE_<BOARD_ID>
    bool "<Board Name>"
    help
        Hardware profile for <Board Name>.
```

Configure conditional pin defaults when this profile is selected.

### Step 3: Add Board Metadata to Catalog

Add a JSON record for the new board in `firmware/boards/boards.json`:

```json
{
  "id": "<board_id>",
  "name": "<Board Name>",
  "chip": "esp32s3",
  "flash_size": "8MB",
  "display": {
    "size": "1.54\"",
    "controller": "SSD1681",
    "width": 200,
    "height": 200,
    "rotation": 0
  },
  "binary": "bacain_<board_id>_factory.bin",
  "status": "community"
}
```

The CI workflow (`.github/workflows/firmware_ci.yml`) automatically reads and generates the build matrix dynamically from `firmware/boards/boards.json` on every CI run.

### Step 4: Update Documentation & Submit Pull Request

Add the new board row to the Supported Boards Ledger table in `firmware/BOARDS.md`. Test the build locally, then submit a pull request with device details and hardware verification notes.
