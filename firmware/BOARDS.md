# Supported Hardware & Board Profiles

The firmware uses a modular board configuration architecture to support multiple ESP32 hardware targets. Each board profile encapsulates pin mappings, display parameters, and power management settings, separating board-specific hardware definitions from core application logic.

> [!IMPORTANT]
> All board profiles, power sensing circuitry, and peripheral pin configurations must strictly comply with the engineering baseline in [firmware/RULES.md](./RULES.md). This establishes mandatory invariants: empirical piecewise OCV battery modeling, 16x ADC multisampling noise filtering, PMOS gate retention latches during deep sleep, sentinel `-1` peripheral availability, and zero silicon leakage into application code.

---

## 1. Supported Boards Ledger

| Board ID | Board Name | SoC / Chip | Flash / RAM | Display (Size & Controller) | Controls | Status |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `waveshare_154` | Waveshare ESP32-S3-ePaper-1.54 | ESP32-S3 (Xtensa dual-core) | 8MB Quad SPI Flash, 512KB SRAM | 1.54" E-Paper (SSD1681), 200x200 px, 1-bit B/W, 0 deg default rotation | UP / BOOT (GPIO 0), DOWN / PWR (GPIO 18) | Official / Supported (Default) |

> [!IMPORTANT]
> Flash storage size is configured dynamically. [package_firmware.py](./package_firmware.py) inspects the board flash storage setting in [boards.json](./boards/boards.json) to configure the combined binary partition layout.

---

## 2. Board Configuration Architecture

The hardware abstraction layer is organized into four components:

1. **Board Definition Header (`firmware/boards/<board_id>/board_def.h`)**:
   Defines board-specific pinouts, dimensions, signal polarities, Category B feature flags, and empirical battery discharge tables:
   - Display SPI bus pins (`BOARD_EPD_*`), dimensions (`BOARD_DISPLAY_*`), and active polarities (`BOARD_EPD_PWR_ACTIVE_LVL`, `BOARD_EPD_BUSY_ACTIVE_LVL`).
   - Navigation button GPIO allocations (`BOARD_BTN_UP_PIN`, `BOARD_BTN_DOWN_PIN`).
   - Feature flags (`BOARD_HAS_*`) enabling peripherals (SDMMC, audio, power latch, status LED, battery ADC).
   - Empirical piecewise battery discharge table (`BOARD_BATTERY_OCV_TABLE`) mapping millivolts to state-of-charge percentages.

2. **Board Hardware Hooks (`firmware/boards/<board_id>/board.c`)**:
   Optional C source file providing board-specific power and peripheral lifecycle hooks:
   - Weak default implementations are defined in `firmware/components/power_mgr/power_mgr.c`.
   - Any board can override lifecycle hooks: `board_power_early_init`, `board_power_init`, `board_power_is_charging`, `board_power_get_battery_mv`, `board_power_pre_sleep`, and `board_power_post_wake`.
   - `firmware/components/board_config/CMakeLists.txt` automatically detects `boards/<board_id>/board.c` when `BOARD_PROFILE` is set and links it into the compilation target. If absent, weak defaults satisfy linkage without modifications.

3. **Central Configuration Contract (`firmware/components/board_config/include/board_config.h`)**:
   Enforces compile-time validation of all board macros:
   - Implements strict `#error` guards on all mandatory Category A definitions and conditionally enabled Category B parameters (zero silent fallback).
   - Maps fallback aliases (for example, `BOARD_BTN_BOOT_PIN` aliased to `BOARD_BTN_UP_PIN`) and sets inactive features to sentinel `-1`.
   - Derives calculated layout constants such as `BOARD_DISPLAY_BUFFER_SIZE`.

4. **Catalog Metadata (`firmware/boards/boards.json`)**:
   Maintains a structured JSON catalog of supported boards:
   - Stores target SoC, flash memory size, display dimensions, controller IC type, configuration header path, and output binary filename.
   - Consumed by the Web Serial Flasher, `package_firmware.py`, and dynamic matrix generation in CI workflows (`.github/workflows/firmware_ci.yml`).

---

## 3. Display Controller Compatibility

E-paper displays require controller IC initialization routines, waveform lookup tables (LUT), and panel-specific physical timing. Connecting a panel with a different controller requires dedicated driver routines.

### Current Driver Support

The driver in `firmware/components/epaper/` targets:

- Controller IC: Solomon Systech SSD1681
- Panel resolution: `BOARD_DISPLAY_WIDTH` x `BOARD_DISPLAY_HEIGHT` (200x200 pixels on official hardware)
- Framebuffer footprint: `BOARD_DISPLAY_BUFFER_SIZE` bytes per plane (`(BOARD_DISPLAY_WIDTH * BOARD_DISPLAY_HEIGHT) / 8`, 5000 bytes for 200x200 px)
- Bus and signal polarities:
  - Busy pin polarity: `BOARD_EPD_BUSY_ACTIVE_LVL` (1 = active high busy state)
  - Power rail polarity: `BOARD_EPD_PWR_ACTIVE_LVL` (0 = active low PMOS gate switch)

### Supporting Future Display Controllers

To integrate alternative e-paper controllers:

1. **Catalog Registration**: Specify controller model, resolution, and default rotation in `firmware/boards/boards.json` under the `display` object.
2. **Board Definition**: Map display SPI bus pins (`BOARD_EPD_DC_PIN`, `BOARD_EPD_CS_PIN`, `BOARD_EPD_SCK_PIN`, `BOARD_EPD_MOSI_PIN`, `BOARD_EPD_RST_PIN`, `BOARD_EPD_BUSY_PIN`, `BOARD_EPD_PWR_PIN`) and polarities (`BOARD_EPD_BUSY_ACTIVE_LVL`, `BOARD_EPD_PWR_ACTIVE_LVL`) in `firmware/boards/<board_id>/board_def.h`.
3. **Driver Routines**: Implement controller-specific initialization, LUT configuration, and memory write commands in `firmware/components/epaper/`. Higher-level components (`reader_core`, UI layout, text pagination) consume the abstract display interface without modification.

---

## 4. Community Contribution Guide

To contribute a new board profile, follow this 4-step checklist:

### Step 1: Create Board Definition Files

Review the hardware invariants in [firmware/RULES.md](./RULES.md). Create directory `firmware/boards/<board_id>/` and add `board_def.h`.

#### Mandatory Category A Definitions
All board profiles must define every Category A macro directly. Omitting any Category A macro triggers a compile-time `#error` in `board_config.h`:
- `BOARD_NAME`: String literal name of the board.
- `BOARD_DISPLAY_CONTROLLER`: Controller IC name string literal (e.g. `"SSD1681"`).
- `BOARD_DISPLAY_WIDTH`: Display horizontal resolution in pixels.
- `BOARD_DISPLAY_HEIGHT`: Display vertical resolution in pixels.
- `BOARD_DISPLAY_ROTATION`: Default display rotation (0: portrait, 1: 90 deg, 2: 180 deg, 3: 270 deg).
- `BOARD_EPD_DC_PIN`: E-Paper Data/Command selection GPIO pin.
- `BOARD_EPD_CS_PIN`: E-Paper SPI Chip Select GPIO pin.
- `BOARD_EPD_SCK_PIN`: E-Paper SPI Clock GPIO pin.
- `BOARD_EPD_MOSI_PIN`: E-Paper SPI MOSI GPIO pin.
- `BOARD_EPD_RST_PIN`: E-Paper Reset GPIO pin.
- `BOARD_EPD_RST_PULSE_US`: Controller minimum reset pulse width in microseconds (e.g. 20).
- `BOARD_EPD_RST_RECOVERY_US`: Controller post-reset recovery delay in microseconds (e.g. 200).
- `BOARD_EPD_BUSY_PIN`: E-Paper Busy status GPIO pin.
- `BOARD_EPD_PWR_PIN`: E-Paper power rail control GPIO pin.
- `BOARD_EPD_PWR_ACTIVE_LVL`: E-Paper power rail active level (0 = active low, 1 = active high).
- `BOARD_EPD_BUSY_ACTIVE_LVL`: E-Paper busy active level (0 = active low, 1 = active high).
- `BOARD_BTN_UP_PIN`: Primary UP / PREV navigation button GPIO pin.
- `BOARD_BTN_DOWN_PIN`: Primary DOWN / NEXT navigation button GPIO pin.

#### Category B Feature Flags and Conditional Definitions
Optional peripherals are guarded by binary feature flags (`0` = absent, `1` = enabled). When set to `1`, all corresponding peripheral pins and configuration macros become mandatory at compile time. When set to `0`, `board_config.h` assigns sentinel values (`-1`):
- `BOARD_HAS_SDCARD`: Set to `1` if MicroSD slot is present. Requires `BOARD_SDMMC_CLK_PIN`, `BOARD_SDMMC_CMD_PIN`, and `BOARD_SDMMC_D0_PIN`.
- `BOARD_HAS_PWR_LATCH`: Set to `1` if hardware power latch circuit is present. Requires `BOARD_PWR_LATCH_PIN` and `BOARD_PWR_LATCH_ACTIVE_LVL`.
- `BOARD_HAS_BATTERY_ADC`: Set to `1` if battery voltage divider is wired to ADC. Requires:
  - `BOARD_BATTERY_ADC_UNIT`: ADC unit number (1 or 2).
  - `BOARD_BATTERY_ADC_CHANNEL`: ADC channel index.
  - `BOARD_BATTERY_ADC_ATTEN`: ADC attenuation setting (`ADC_ATTEN_DB_12`).
  - `BOARD_VOLTAGE_DIVIDER_RATIO`: Voltage divider attenuation ratio (e.g. `2.0f`).
  - `BOARD_BATTERY_MIN_MV`: Battery cutoff threshold in millivolts (e.g. `3300`).
  - `BOARD_BATTERY_MAX_MV`: Fully charged battery threshold in millivolts (e.g. `4180`).
  - `BOARD_BATTERY_OCV_TABLE`: Piecewise OCV discharge table array of `battery_ocv_point_t` pairs `{permille, pct}`.
- `BOARD_HAS_CHARGE_DETECT`: Set to `1` if charging state pin is present. Requires `BOARD_CHARGE_DETECT_PIN` and `BOARD_CHARGE_ACTIVE_LEVEL`.
- `BOARD_HAS_STATUS_LED`: Set to `1` if discrete status LED is present. Requires `BOARD_LED_ACTIVE_LEVEL` and at least one LED pin (`BOARD_LED_PIN`, `BOARD_LED_GREEN_PIN`, `BOARD_LED_RED_PIN`, or `BOARD_LED_BLUE_PIN`).
- `BOARD_HAS_AUDIO`: Set to `1` if I2S audio amplifier is present. Requires `BOARD_AUDIO_PWR_PIN`, `BOARD_AUDIO_PWR_ACTIVE_LVL`, `BOARD_AUDIO_PA_PIN`, `BOARD_I2S_BCLK_PIN`, `BOARD_I2S_LRCK_PIN`, and `BOARD_I2S_DOUT_PIN`.

#### Optional Board Power Hooks (`board.c`)
If the target board requires custom power sequence handling or non-standard hardware management, add `firmware/boards/<board_id>/board.c`. The build system detects this file automatically and links it into `board_config`. Available hook signatures:
- `esp_err_t board_power_early_init(void)`: Early hardware initialization prior to peripheral driver registration.
- `esp_err_t board_power_init(void)`: Main power configuration and GPIO direction latching.
- `bool board_power_is_charging(void)`: Custom battery charge detection logic.
- `uint32_t board_power_get_battery_mv(void)`: Custom battery voltage sampling override.
- `void board_power_pre_sleep(void)`: Bus isolation and power rail shutdown executed immediately prior to entering deep sleep.
- `void board_power_post_wake(void)`: Peripheral rail re-enable executed immediately after waking from deep sleep.

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
    "width": 200,
    "height": 200,
    "rotation": 0,
    "controller": "SSD1681"
  },
  "config_header": "boards/<board_id>/board_def.h",
  "output_bin": "bacain_<board_id>.bin",
  "status": "community"
}
```

The CI workflow (`.github/workflows/firmware_ci.yml`) reads `firmware/boards/boards.json` to dynamically generate the build matrix for automated testing.

### Step 4: Update Documentation

Add the new board row to the Supported Boards Ledger table in `firmware/BOARDS.md`. Test the build locally with `idf.py -DBOARD_PROFILE=<board_id> build`, then submit a pull request.

---

## 5. Architectural References

- Engineering Rules & Hardware Standards: [firmware/RULES.md](./RULES.md)
- Central Board Configuration Header: [board_config.h](./components/board_config/include/board_config.h)
- Board Catalog Metadata: [boards.json](./boards/boards.json)
- Waveshare 1.54 Board Definition: [board_def.h](./boards/waveshare_154/board_def.h)
- Waveshare 1.54 Board Hooks: [board.c](./boards/waveshare_154/board.c)
