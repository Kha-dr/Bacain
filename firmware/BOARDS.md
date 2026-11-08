# Supported Hardware & Board Profiles

The firmware uses a modular board configuration architecture to support multiple ESP32-based hardware targets. Each board profile encapsulates pin mappings, display parameters, and power management settings, allowing developers to target different PCB layouts and e-paper displays without modifying core application logic.

> [!IMPORTANT]
> All board profiles, power sensing circuitry, and peripheral pin configurations must strictly comply with the engineering baseline in [firmware/RULESs.md](./RULESs.md). This establishes mandatory invariants: empirical piecewise OCV battery modeling, 16x ADC multisampling noise filtering, PMOS gate retention latches during deep sleep, sentinel `-1` peripheral availability, and zero silicon leakage into application code.

---

## 1. Supported Boards Ledger

| Board ID        | Board Name                     | SoC / Chip                  | Flash / RAM                    | Display (Size & Controller)                                            | Controls                                 | Status                         |
| :-------------- | :----------------------------- | :-------------------------- | :----------------------------- | :--------------------------------------------------------------------- | :--------------------------------------- | :----------------------------- |
| `waveshare_154` | Waveshare ESP32-S3-ePaper-1.54 | ESP32-S3 (Xtensa dual-core) | 8MB Quad SPI Flash, 512KB SRAM | 1.54" E-Paper (SSD1681), 200x200 px, 1-bit B/W, 0 deg default rotation | UP / BOOT (GPIO 0), DOWN / PWR (GPIO 18) | Official / Supported (Default) |

> [!Important]
> The code is made to acomodate for the flash storage to be as dynamic as posible. Handled by [package_firmware.py](./package_firmware.py) as it check the board flash storage value define at [boards.json](./boards/boards.json) to modify the [partition](./partitions.csv) for the combined binary

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

## 3. Display Controller Compatibility

Unlike SPI LCD or OLED panels where a generic framebuffer transfers over standard protocols, e-paper displays depend directly on controller IC architecture, voltage waveforms, and physical panel characteristics. Connecting a panel with a different controller requires dedicated driver routines.

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

Review the hardware and firmware principles in [firmware/RULES.md](./RULES.md). Create directory `firmware/boards/<board_id>/` and add `board_def.h`. Define all required macros:

- Display resolution: `BOARD_DISPLAY_WIDTH`, `BOARD_DISPLAY_HEIGHT`, `BOARD_DISPLAY_ROTATION`
- E-Paper SPI: `BOARD_EPD_DC_PIN`, `BOARD_EPD_CS_PIN`, `BOARD_EPD_SCK_PIN`, `BOARD_EPD_MOSI_PIN`, `BOARD_EPD_RST_PIN`, `BOARD_EPD_BUSY_PIN`, `BOARD_EPD_PWR_PIN`
- Navigation buttons: `BOARD_BTN_UP_PIN`, `BOARD_BTN_DOWN_PIN` (semantic mappings, avoiding raw silicon references)
- Peripherals: SDMMC pins, power hold pin, status LED pin, and battery ADC channel (unrouted or absent peripherals must use sentinel `-1`)

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
  "output_bin": "bacain_<board_id>.bin",
  "status": "community"
}
```

The CI workflow (`.github/workflows/firmware_ci.yml`) automatically reads and generates the build matrix dynamically from `firmware/boards/boards.json` on every CI run.

### Step 4: Update Documentation & Submit Pull Request

Add the new board row to the Supported Boards Ledger table in `firmware/BOARDS.md`. Test the build locally, then submit a pull request with device details and hardware verification notes.

---

## 5. Architectural References

- Engineering Rules & Hardware Standards: [firmware/RULES.md](./RULES.md)
- Central Board Configuration Header: [board_config.h](./components/board_config/include/board_config.h)
- Waveshare 1.54 Board Definition: [board_def.h](./boards/waveshare_154/board_def.h)
