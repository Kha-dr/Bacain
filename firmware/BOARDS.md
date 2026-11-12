# Supported Boards and Board Profiles

The firmware supports multiple ESP32 targets through board profiles. A profile holds pin mappings, display parameters and power behavior, separate from application code. All profiles follow the rules in [firmware/RULES.md](./RULES.md).

---

## 1. Supported Boards

| Board ID        | Board                          | SoC                                     | Memory                                             | Display                                         | Buttons                                    | Power                                             | Status              |
| :-------------- | :----------------------------- | :-------------------------------------- | :------------------------------------------------- | :---------------------------------------------- | :----------------------------------------- | :------------------------------------------------ | :------------------ |
| `waveshare_154` | Waveshare ESP32-S3-ePaper-1.54 | ESP32-S3, dual-core Xtensa LX7, 240 MHz | 8 MB quad SPI flash, 512 KB SRAM, 8 MB octal PSRAM | 1.54 in, SSD1681, 200x200 px, 1-bit, rotation 0 | ▲ UP: GPIO 0 (BOOT), ▼ DOWN: GPIO 18 (PWR) | Power latch GPIO 17; power-on from off: hold DOWN | Supported (default) |

**Build targets** for `waveshare_154`:

| Target                 | Image                             | Difference                                 |
| :--------------------- | :-------------------------------- | :----------------------------------------- |
| `waveshare_154`        | `bacain_waveshare_154.bin`        | Standard reader                            |
| `waveshare_154_mushaf` | `bacain_waveshare_154_mushaf.bin` | Same board, `CONFIG_BOARD_MUSHAF_ENABLE=y` |

Flash size is read from [boards.json](./boards/boards.json) by [package_firmware.py](./package_firmware.py) to build the combined image (offset `0x0`).

### Hardware Behavior: `waveshare_154`

- **Wake from deep sleep**: EXT1 on both buttons (GPIO 0 and GPIO 18).
- **Power-on from true off**: only the DOWN button (GPIO 18) is wired to the power latch circuit. The button must stay pressed until the firmware sets the latch in `power_mgr_early_init()`; a short tap can drop power again.
- **USB connected**: VBUS feeds the regulator directly, so releasing the latch does not cut power; Power Off falls back to deep sleep.
- **Sleep clock**: internal RC oscillator (no external 32 kHz crystal).
- **Pin assignments**: see [board_def.h](./boards/waveshare_154/board_def.h). Pins are not duplicated in this document.

---

## 2. Board Configuration Architecture

Four parts make up a board profile.

1. **Board definition (`firmware/boards/<board_id>/board_def.h`)**: pins, display geometry, signal polarities, feature flags (`BOARD_HAS_*`), the battery discharge table (`BOARD_BATTERY_OCV_TABLE`), and the light-sleep pin list (`BOARD_LIGHT_SLEEP_KEEP_PINS`).

2. **Board hooks (`firmware/boards/<board_id>/board.c`)**: required file. `board_light_sleep_pins_init()` has no default and must be implemented. The `board_power_*` hooks have weak defaults in [`power_mgr.c`](./components/power_mgr/power_mgr.c) (see section 4.2); a definition in `board.c` replaces the default. Every hook symbol is listed with `-u <symbol>` in [`components/board_config/CMakeLists.txt`](./components/board_config/CMakeLists.txt).

3. **Configuration contract ([`board_config.h`](./components/board_config/include/board_config.h))**:
   - `#error` guards on every mandatory (Category A) macro, and on every Category B macro whose feature flag is `1`.
   - When a feature flag is explicitly `0`, the matching pins resolve to the sentinel `-1`. This is the only derived default; a missing macro is always an `#error`.
   - No aliases that carry silicon labels into application code.

- Derived constants such as `BOARD_DISPLAY_BUFFER_SIZE` = `((BOARD_DISPLAY_WIDTH + 7) / 8) * BOARD_DISPLAY_HEIGHT` (rows padded to whole bytes).

4. **Catalog ([`boards.json`](./boards/boards.json))**: SoC, flash size, display parameters, controller, header path and output image name per board. Read by the Web Serial Flasher, `package_firmware.py` and the CI build matrix (`.github/workflows/firmware_ci.yml`).

---

## 3. Display Controller Compatibility

### 3.1 Current Driver

The driver in `firmware/components/epaper/` supports:

- Controller: Solomon Systech SSD1681.
- Resolution: `BOARD_DISPLAY_WIDTH` x `BOARD_DISPLAY_HEIGHT` (200x200 px on `waveshare_154`).
- Framebuffer: `BOARD_DISPLAY_BUFFER_SIZE` bytes per plane (`((W + 7) / 8) * H`, 5000 bytes at 200x200). `ui_kit` checks it against its own canvas size with `_Static_assert`.
- Wake-up differential refresh: the last frame is kept in RTC memory only if it is at most `UI_RTC_CANVAS_MAX` bytes (default 6144; 200x200 and 296x128 fit, 400x300 does not). Larger panels use a plain partial refresh for the first frame after wake.
- Busy polarity: `BOARD_EPD_BUSY_ACTIVE_LVL` (1 = high while busy).
- Power rail polarity: `BOARD_EPD_PWR_ACTIVE_LVL` (0 = active-low PMOS switch).
- Refresh timing and light-sleep behavior: see [RULES.md section 2.2 and 2.5](./RULES.md).

### 3.2 Adding a Controller

1. **Catalog**: controller model, resolution and default rotation in `boards.json` under `display`.
2. **Board definition**: display pins (`BOARD_EPD_DC_PIN`, `BOARD_EPD_CS_PIN`, `BOARD_EPD_SCK_PIN`, `BOARD_EPD_MOSI_PIN`, `BOARD_EPD_RST_PIN`, `BOARD_EPD_BUSY_PIN`, `BOARD_EPD_PWR_PIN`) and polarities in `board_def.h`.
3. **Driver**: initialization, LUTs and RAM write commands in `firmware/components/epaper/`. Multi-step SPI sequences hold the EPD `ESP_PM_NO_LIGHT_SLEEP` lock and release it before every BUSY wait (RULES.md section 2.5). Higher-level code is unchanged: `reader_core`, `text_engine` and `ui_kit` derive layout from `BOARD_DISPLAY_WIDTH` / `BOARD_DISPLAY_HEIGHT` and the current rotation.

### 3.3 Resolution-Dependent Behavior

| Item                         | Rule                               | 200x200      | 400x300      |
| :--------------------------- | :--------------------------------- | :----------- | :----------- |
| Text columns x lines (max)   | larger dimension / 7 px, / 14 px   | 28 x 14      | 57 x 28      |
| Body area                    | `UI_BODY_TOP` (22) to `H - 18`     | rows 22..181 | rows 22..281 |
| QR module size (sync screen) | largest integer that fits the body | 4 px         | 7 px         |
| Splash logo                  | 1x below 400 px, 2x from 400 px    | 1x           | 1x           |
| RTC frame copy               | canvas <= 6144 bytes               | kept         | not kept     |

---

## 4. Adding a Board

### 4.1 Board Definition (`board_def.h`)

Create `firmware/boards/<board_id>/board_def.h`.

#### Category A: Mandatory

Every macro below is defined directly. A missing macro is a compile-time `#error`.

- `BOARD_NAME`: board name string.
- `BOARD_DISPLAY_CONTROLLER`: controller name string (for example `"SSD1681"`).
- `BOARD_DISPLAY_WIDTH`, `BOARD_DISPLAY_HEIGHT`: resolution in pixels.
- `BOARD_DISPLAY_ROTATION`: default rotation (0: portrait, 1: 90 deg, 2: 180 deg, 3: 270 deg).
- `BOARD_EPD_DC_PIN`, `BOARD_EPD_CS_PIN`, `BOARD_EPD_SCK_PIN`, `BOARD_EPD_MOSI_PIN`, `BOARD_EPD_RST_PIN`, `BOARD_EPD_BUSY_PIN`, `BOARD_EPD_PWR_PIN`: display GPIOs.
- `BOARD_EPD_RST_PULSE_US`: minimum reset pulse width (for example 20).
- `BOARD_EPD_RST_RECOVERY_US`: post-reset recovery delay (for example 200).
- `BOARD_EPD_PWR_ACTIVE_LVL`, `BOARD_EPD_BUSY_ACTIVE_LVL`: polarities (0 = active low, 1 = active high).
- `BOARD_BTN_UP_PIN`, `BOARD_BTN_DOWN_PIN`: navigation buttons. Both must be RTC-capable GPIOs (ESP32-S3: GPIO 0 to 21) so they can wake the device from deep sleep through EXT1.
- `BOARD_LIGHT_SLEEP_KEEP_PINS`: array initializer listing every output and wake input whose configuration must not change in light sleep: power latch, audio power, EPD power, EPD RST, DC and CS, and both buttons. Use `-1` for pins the board does not have. Example:
  ```c
  #define BOARD_LIGHT_SLEEP_KEEP_PINS { \
      BOARD_PWR_LATCH_PIN, BOARD_AUDIO_PWR_PIN, BOARD_EPD_PWR_PIN, \
      BOARD_EPD_RST_PIN, BOARD_EPD_DC_PIN, BOARD_EPD_CS_PIN, \
      BOARD_BTN_UP_PIN, BOARD_BTN_DOWN_PIN }
  ```

#### Category B: Feature Flags

Each optional peripheral has a flag (`0` = absent, `1` = present). With `1`, its macros below are mandatory. With `0`, its pins resolve to `-1` and the driver reports the feature unavailable.

- `BOARD_HAS_SDCARD`: `BOARD_SDMMC_CLK_PIN`, `BOARD_SDMMC_CMD_PIN`, `BOARD_SDMMC_D0_PIN`.
- `BOARD_HAS_PWR_LATCH`: `BOARD_PWR_LATCH_PIN`, `BOARD_PWR_LATCH_ACTIVE_LVL`. Document which button powers the board on from true off.
- `BOARD_HAS_BATTERY_ADC`:
  - `BOARD_BATTERY_ADC_UNIT`: ADC unit (1 or 2).
  - `BOARD_BATTERY_ADC_CHANNEL`: ADC channel.
  - `BOARD_BATTERY_ADC_ATTEN`: attenuation (`ADC_ATTEN_DB_12`).
  - `BOARD_VOLTAGE_DIVIDER_RATIO`: divider ratio (for example `2.0f`).
  - `BOARD_BATTERY_MIN_MV`: cutoff voltage (for example `3300`).
  - `BOARD_BATTERY_MAX_MV`: full-charge voltage (for example `4180`).
  - `BOARD_BATTERY_OCV_TABLE`: piecewise discharge table of `battery_ocv_point_t` pairs `{permille, pct}`.
- `BOARD_HAS_CHARGE_DETECT`: `BOARD_CHARGE_DETECT_PIN`, `BOARD_CHARGE_ACTIVE_LEVEL`.
- `BOARD_HAS_STATUS_LED`: `BOARD_LED_ACTIVE_LEVEL` and at least one of `BOARD_LED_PIN`, `BOARD_LED_GREEN_PIN`, `BOARD_LED_RED_PIN`, `BOARD_LED_BLUE_PIN`.
- `BOARD_HAS_AUDIO`: `BOARD_AUDIO_PWR_PIN`, `BOARD_AUDIO_PWR_ACTIVE_LVL`, `BOARD_AUDIO_PA_PIN`, `BOARD_I2S_BCLK_PIN`, `BOARD_I2S_LRCK_PIN`, `BOARD_I2S_DOUT_PIN`.

### 4.2 Board Hooks (`board.c`)

### 4.2 Board Hooks (`board.c`)

Create `firmware/boards/<board_id>/board.c`. Implement `board_light_sleep_pins_init()` and any `board_power_*` hook whose default does not fit the board. A hook for an absent feature reports it explicitly (for example `board_power_is_charging()` returns `false`, `board_power_get_battery_mv()` returns 0); it never returns invented data.

| Hook                                        | Default (`power_mgr.c`, weak)                                                                                                                                                                                | `waveshare_154` |
| :------------------------------------------ | :----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | :-------------- |
| `esp_err_t board_power_early_init(void)`    | No action, `ESP_OK`                                                                                                                                                                                          | default         |
| `esp_err_t board_power_init(void)`          | No action, `ESP_OK`                                                                                                                                                                                          | default         |
| `bool board_power_is_charging(void)`        | `BOARD_CHARGE_DETECT_PIN` at `BOARD_CHARGE_ACTIVE_LEVEL`, else `BOARD_VBUS_SENSE_PIN` high, else `false`                                                                                                     | default         |
| `uint32_t board_power_get_battery_mv(void)` | `0`: `power_mgr` reads the battery ADC (`BOARD_HAS_BATTERY_ADC`)                                                                                                                                             | default         |
| `void board_power_pre_sleep(void)`          | No action                                                                                                                                                                                                    | `board.c`       |
| `void board_power_post_wake(void)`          | No action                                                                                                                                                                                                    | default         |
| `void board_light_sleep_pins_init(void)`    | none (required): calls `gpio_sleep_sel_dis()` on every valid entry of `BOARD_LIGHT_SLEEP_KEEP_PINS` and logs `light sleep: N pins kept`. Called by `main.c` after display init, before `esp_pm_configure()`. | `board.c`       |

Add each symbol with `-u <symbol>` in `components/board_config/CMakeLists.txt`.

### 4.3 Kconfig Registration

Add an entry under `choice BOARD_PROFILE` in `firmware/components/board_config/Kconfig`. Pins and hardware values stay in `board_def.h`; Kconfig only selects the profile.

```kconfig
config BOARD_PROFILE_<BOARD_ID>
    bool "<Board Name>"
    help
        Hardware profile for <Board Name>.
```

### 4.4 Catalog Entry

Add a record to `firmware/boards/boards.json`:

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

The CI workflow reads this file to generate the build matrix.

### 4.5 Build and Bring-Up Checks

1. Build in Docker (see [AGENTS.md section 7](../AGENTS.md)):
   ```bash
   docker-compose -f firmware/compose.yml run --rm -e BOARD=<board_id> firmware
   ```
2. On first boot, the log shows `light sleep: N pins kept` with N equal to the number of non `-1` entries in `BOARD_LIGHT_SLEEP_KEEP_PINS`.
3. On battery, with USB unplugged:
   - Idle until deep sleep: the board never powers off by itself during the wait.
   - Both buttons wake the board, and the waking press is handled.
   - Partial and full refreshes render cleanly after an idle wait.
   - With a power latch: Power Off, then power on with the documented button.
4. Display checks:
   - The build passes the `UI_CANVAS_BYTES == BOARD_DISPLAY_BUFFER_SIZE` assertion.
   - Splash, menu, library, nav hints and the sync screen are centered and inside the frame in every supported rotation.
   - Switching the font size in the menu changes list and reading text, not headers or footers.
   - Mushaf builds only: a `.bqm` built for this board's size loads; the log shows no `sprite px outside body rows` warning.
5. Add the board to the table in section 1 and submit a pull request.

---

## 5. References

- Engineering rules: [RULES.md](./RULES.md)
- Configuration contract: [board_config.h](./components/board_config/include/board_config.h)
- Board catalog: [boards.json](./boards/boards.json)
- Waveshare 1.54 board definition: [board_def.h](./boards/waveshare_154/board_def.h)
- Waveshare 1.54 board hooks: [board.c](./boards/waveshare_154/board.c)
