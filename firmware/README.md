# BacaIn Firmware (ESP32-S3)

ESP-IDF firmware source code for the **BacaIn** e-reader. Hardware definitions are decoupled from application logic into board profiles under `firmware/boards/`.

---

## 1. Hardware Architecture & Supported Boards

BacaIn firmware uses a modular hardware abstraction layer supporting multiple ESP32-S3 boards. Pin mappings and hardware constants reside in `firmware/boards/<board_id>/board_def.h`, while `firmware/components/board_config/include/board_config.h` provides runtime abstraction for display parameters, buttons, and peripherals.

See [BOARDS.md](BOARDS.md) for supported boards, hardware profiles, and GPIO pinout tables.

---

## 2. Hardware Configuration

Hardware settings follow a 3-tier precedence order:

1. **Compiler Flags (`-D<macro>`)**: Highest priority, overrides Kconfig and board defaults.
2. **Kconfig (`BOARD_PROFILE_*` / `CONFIG_BOARD_*`)**: Menu-driven configuration via ESP-IDF.
3. **Board Profile Defaults**: Hardware constants declared in `firmware/boards/<board_id>/board_def.h`.

### A. Using `menuconfig` (Kconfig)

Open the configuration menu inside the `firmware/` directory:

```bash
# From repository root:
cd firmware
idf.py menuconfig

# Or run via Docker (interactive TTY):
docker compose -f firmware/compose.yml run --rm -it firmware idf.py menuconfig
```

Navigate to **Board & Hardware Configuration**:

- **Hardware Board Profile (`choice BOARD_PROFILE`)**: Select a target board profile or choose `Custom / Manual Configuration` to set GPIO allocations and display parameters manually.
- **Display Dimensions & Orientation**: Set dimensions (`BOARD_DISPLAY_WIDTH`, `BOARD_DISPLAY_HEIGHT`) and orientation (`BOARD_DISPLAY_ROTATION`).
- **Peripherals & Pins**: Configure E-Paper SPI pins, MicroSD SDMMC pins, navigation buttons (`BOARD_BTN_UP_PIN`, `BOARD_BTN_DOWN_PIN`), power latch, status LED (`BOARD_HAS_STATUS_LED`, `BOARD_LED_ACTIVE_LEVEL`, `BOARD_LED_RED_PIN`, `BOARD_LED_GREEN_PIN`, `BOARD_LED_BLUE_PIN`), or audio pins.
- Save changes to `sdkconfig`.

### B. Dynamic CI Multi-Board Matrix

CI dynamically parses `firmware/boards/boards.json` to generate the build matrix. For each cataloged board profile, CI sets `-DBOARD_PROFILE=<id>`, compiles the project, and packages the release factory binary. Adding a board entry to `boards.json` automatically enables CI builds for that target without workflow changes.

### C. Environment Variables in Docker Compose

Build any board cataloged in `firmware/boards/boards.json` by specifying `BOARD=<id>`:

```bash
# Build standard board (waveshare_154)
docker compose run --rm firmware

# Build Mushaf Edition
BOARD=waveshare_154_mushaf docker compose run --rm firmware
```

You can also override build arguments or factory binary filenames directly without modifying source files:

```bash
EXTRA_IDF_ARGS="-DBOARD_DISPLAY_WIDTH=250" FACTORY_BIN="bacain_custom.bin" docker compose run --rm firmware
```

### D. Adding or Customizing Board Definitions (`boards.json` & `board_def.h`)

1. **Create `firmware/boards/<board_id>/board_def.h` and `board.c`**
   with every Category A macro, the Category B flags and all board hooks listed in [BOARDS.md section 4](BOARDS.md#4-adding-a-board).
   Display size and rotation are the only values the UI needs; layout, text bounds and canvas size are derived from `BOARD_DISPLAY_WIDTH` and `BOARD_DISPLAY_HEIGHT`.

   ```c
   #define BOARD_NAME               "Custom ESP32-S3 ePaper 2.13"
   #define BOARD_DISPLAY_CONTROLLER "SSD1681"
   #define BOARD_DISPLAY_WIDTH      250
   #define BOARD_DISPLAY_HEIGHT     122
   #define BOARD_DISPLAY_ROTATION   1
   /* ... remaining Category A and B macros, see BOARDS.md ... */
   ```

2. **Register in `firmware/boards/boards.json`**:
   ```json
   {
     "id": "custom_213",
     "name": "Custom ESP32-S3 ePaper 2.13",
     "chip": "esp32s3",
     "flash_size": "8MB",
     "display": {
       "width": 250,
       "height": 122,
       "rotation": 1,
       "controller": "SSD1681"
     },
     "config_header": "boards/custom_213/board_def.h",
     "output_bin": "bacain_custom_213.bin",
     "status": "supported"
   }
   ```
3. **Include in `firmware/components/board_config/include/board_config.h`** (or pass `-DBOARD_DEF_HEADER="\"../../boards/custom_213/board_def.h\""` via `extra_idf_args`).

---

## 3. Building & Packaging Binaries

The build generates a single factory binary per board (`bacain_<board_id>.bin`) flashed directly at offset `0x0`.

### Option A: Using Docker Compose (Recommended)

Requires no local ESP-IDF installation:

```bash
cd firmware
docker compose run --rm firmware
```

Any board defined in `firmware/boards/boards.json` can be built directly by passing its `id` via `BOARD=<id>`:

```bash
# Build Mushaf Edition (inside firmware/)
BOARD=waveshare_154_mushaf docker compose run --rm firmware

# Or from repository root:
docker compose -f firmware/compose.yml run --rm -e BOARD=waveshare_154_mushaf firmware
```

The `firmware` service in `compose.yml` automatically executes incremental compilation and packaging:

1. Runs the `espressif/idf:v5.3.1` container.
2. Resolves target board configuration from `firmware/boards/boards.json` (`CHIP`, `EXTRA_IDF_ARGS`, and `FACTORY_BIN`).
3. Tracks `/project/build/.board_id`: automatically clears `CMakeCache.txt` when switching `BOARD` profiles so compiler definitions take effect, while preserving cached object files for fast incremental rebuilds of the same board.
4. Compiles the project incrementally: `idf.py -B /project/build ${EXTRA_IDF_ARGS} build`.
5. Packages the release binary: `python3 package_firmware.py /project/build -o "${FACTORY_BIN}" --chip "${CHIP}" --board "${BOARD}"`.
6. Outputs the factory binary at `build/${FACTORY_BIN}` (e.g. `build/bacain_waveshare_154.bin` or `build/bacain_waveshare_154_mushaf.bin`).

For a clean rebuild from scratch, run the `clean-build` service:

```bash
# Standard clean build
docker compose run --rm clean-build

# Clean build for specific board
BOARD=waveshare_154_mushaf docker compose run --rm clean-build
```

The `clean-build` service deletes `/project/build`, initializes the target chip (`idf.py -B /project/build ${EXTRA_IDF_ARGS} set-target ${CHIP}`), compiles the project, writes `/project/build/.board_id`, and packages the factory binary.

### Option B: Native ESP-IDF

Requires ESP-IDF v5.3+ installed locally:

```bash
cd firmware
idf.py set-target esp32s3
idf.py build
python3 package_firmware.py build -o bacain_<board_id>.bin
```

### Packaging Script Parameters (`package_firmware.py`)

The packaging script merges build artifacts (`bootloader.bin` at `0x0`, `partition-table.bin` at `0x8000`, `bacain.bin` at `0x10000`) into a single factory binary and resizes the `storage` partition entry (`0x110000..end`) + MD5 checksum to match the board's flash size:

- `build_dir`: Path to build directory (default: `build`).
- `-o, --output`: Factory binary filename (default: resolved from `boards/boards.json` or `bacain.bin`).
- `--chip`: esptool target chip (default: resolved from `boards/boards.json` or `esp32s3`).
- `--board`: Board ID or name from `boards/boards.json` (used to resolve `chip`, `flash_size`, and `output_bin`).
- `--flash-size`: Flash memory size (`4MB`, `8MB`, `16MB`, `32MB`; resolved from `boards/boards.json` if omitted).
- `--build`: Compile the project using `idf.py` before packaging.
- `--clean`: Clean build directory and reinitialize target chip before building.

---

## 4. CI/CD Automation & Custom Builds via GitHub Actions

The workflow `.github/workflows/firmware_ci.yml` handles automated builds and on-demand custom builds.

### A. Automatic Matrix Builds

- **Pull Request**: Compiles firmware for every board profile in `firmware/boards/boards.json` to verify compilation (skips binary packaging and artifact uploads).
- **Push to `main`**: Compiles and uploads `bacain-firmware-<board_id>` workflow artifacts for every board in `boards.json`.
- **Release Tags (`v*`)**: Compiles and publishes unified factory binaries (`bacain_<board_id>_<version>.bin`) for all boards in `boards.json` directly to GitHub Releases.

### B. Custom Firmware Build via `workflow_dispatch` (Manual Trigger)

Build custom hardware profiles from the GitHub web interface without modifying `firmware/boards/boards.json` or `board_def.h`:

1. Navigate to the **Actions** tab in the repository.
2. Select the **Firmware Builder** workflow.
3. Click **Run workflow**.
4. Fill in parameters (pre-filled with `waveshare_154` defaults):
   - **`board_id`**: Identifier used for the uploaded artifact name `bacain-firmware-<board_id>` (default: `waveshare_154`). Does not need to exist in `boards.json`.
   - **`flash_size`**: Target flash size (`4MB`, `8MB`, `16MB`, `32MB`; default: `8MB`). Automatically scales the `/spiflash` `storage` partition (`3008K` for `4MB`, `7104K` for `8MB`, `15296K` for `16MB`).
   - **`extra_idf_args`**: Compiler macro flags (`-DBOARD_<MACRO>=<val>` or `-DCONFIG_BOARD_<MACRO>=<val>`) to override pins, resolution, rotation, or header path (default: `-DBOARD_PROFILE=waveshare_154`).
   - **`output_bin`**: Filename for the merged factory binary (default: `bacain_waveshare_154.bin`).
5. Click **Run workflow**. Download the `bacain-firmware-<board_id>` artifact zip from the workflow summary after the run finishes.

#### Example Compiler Flags for `extra_idf_args`:

You can override individual macros on top of `waveshare_154/board_def.h` (`-DBOARD_*`) or bypass `waveshare_154/board_def.h` completely (`-DBOARD_PROFILE=custom -DBOARD_*`):

| Custom Requirement                                    | `extra_idf_args` Value                                                                                 |
| :---------------------------------------------------- | :----------------------------------------------------------------------------------------------------- |
| **Change Screen Resolution** (e.g. 250 x 122)         | `-DBOARD_DISPLAY_WIDTH=250 -DBOARD_DISPLAY_HEIGHT=122`                                                 |
| **Change Screen Orientation** (90 deg landscape)      | `-DBOARD_DISPLAY_ROTATION=1`                                                                           |
| **Change Button Pins** (UP/BOOT=4, DOWN/PWR=5)        | `-DBOARD_BTN_UP_PIN=4 -DBOARD_BTN_DOWN_PIN=5`                                                          |
| **Change E-Paper SPI Pins**                           | `-DBOARD_EPD_DC_PIN=14 -DBOARD_EPD_CS_PIN=15`                                                          |
| **Configure Status LED** (Red=1, Green=2, Active-Low) | `-DBOARD_LED_RED_PIN=1 -DBOARD_LED_GREEN_PIN=2 -DBOARD_LED_ACTIVE_LEVEL=0`                             |
| **Disable MicroSD / Power Latch**                     | `-DBOARD_SDMMC_CLK_PIN=-1 -DBOARD_PWR_LATCH_PIN=-1`                                                    |
| **Use a Custom `board_def.h` File**                   | `-DBOARD_DEF_HEADER="\"../../boards/custom_213/board_def.h\""`                                         |
| **Enable Mushaf Feature**                             | `-DBOARD_MUSHAF_ENABLE=1`                                                                              |
| **Combine Multiple Settings**                         | `-DBOARD_DISPLAY_WIDTH=296 -DBOARD_DISPLAY_HEIGHT=128 -DBOARD_DISPLAY_ROTATION=1 -DBOARD_BTN_UP_PIN=4` |

---

## 5. Flashing the Device

### A. Web Serial Flasher (Browser)

Open the web flasher in a Chromium browser (Chrome or Edge):

1. Connect the ESP32-S3 board via USB-C.
2. Open `web/flasher.html` (or the **Firmware Flasher** tab in the web interface).
3. Select your board to download the latest release binary, or load a local `bacain_<board_id>.bin`.
4. Click **Connect Serial Port**, select the ESP32-S3 port, and click **Flash Firmware**.

### B. Command Line via `esptool`

```bash
# Windows (replace COM3 with your port)
esptool.py -p COM3 -b 460800 --chip esp32s3 write_flash 0x0 build/bacain_<board_id>.bin

# Linux / macOS (replace /dev/ttyACM0 with your port)
esptool.py -p /dev/ttyACM0 -b 460800 --chip esp32s3 write_flash 0x0 build/bacain_<board_id>.bin
```

### C. Serial Terminal (ESP-IDF)

For development builds within the ESP-IDF toolchain:

```bash
idf.py -p COM3 flash monitor          # Windows (adjust COM port)
idf.py -p /dev/ttyUSB0 flash monitor  # Linux / macOS
```

### D. Docker Compose (Linux / WSL2)

To flash directly from the Docker container (requires host serial device access):

```bash
# Linux (or WSL2 with usbipd):
ESP_PORT=/dev/ttyACM0 docker compose run --rm flash
```

Windows note: Docker Desktop runs inside a WSL2 virtual machine that does not forward host COM ports by default without [usbipd-win](https://github.com/dorssel/usbipd-win). For Windows hosts, Option A (Web Serial Flasher) or Option B (esptool) is recommended to access COM ports directly without VM passthrough.

---

## 6. Navigation & Menu Subsystem

Navigation uses two physical buttons, ▲ (`BOARD_BTN_UP_PIN`) and ▼ (`BOARD_BTN_DOWN_PIN`). Button invert swaps them; on-screen text always shows the arrow currently assigned.

### Button Architecture

The `button` component debounces the GPIO levels and posts `button_event_t` records to a bounded FreeRTOS queue read by `main.c`. `main.c` passes each event to the active view through `views_dispatch_event()` ([views.c](main/views/views.c)).

- **Cold Boot Dismissal**: pressing ▲ or ▼ dismisses the splash screen.
- **Short Click** (`BTN_CLICK`):
  - Reading: previous page (▲) / next page (▼).
  - Menus and lists: move selection up (▲) / down (▼).
- **Double Click** (`BTN_DOUBLE_CLICK`):
  - Reading: toggle bookmark (▲) / open bookmark list (▼).
  - Menus and lists: select or open the item.
- **Long Press (>1.2s)** (`BTN_LONG_PRESS`):
  - Reading: open the Reader Menu.
  - Menus, lists, Nav Hints and Sync: back.
- **Simultaneous Hold (2.0s)**: ▲ + ▼ held together for 2.0 s reboots the device with a status LED strobe.

For the end-user button table, see [Controls in README.md](../README.md#controls).

### On-Device Menu

The Reader Menu ([view_menu.c](main/views/view_menu.c)) has 7 items, or 8 on Mushaf builds. The status LED is configured from the companion PWA.

1. **Continue Reading**: resume the active book at its saved byte offset.
2. **Book Library**: browse and open books on internal flash or MicroSD.
3. **Mushaf** (Mushaf builds only): open the Mushaf menu, see [Mushaf_Reader.md](Mushaf_Reader.md).
4. **Font Size: [12px/16px]**: switch the body font between Font12 (7x12 px, default) and Font16 (11x16 px). It applies to reading text, list rows, messages, Nav Hints and Mushaf text layers. Headers, footers, splash, power-off and the sync screen stay Font12. Saved in NVS (`font_size`) and cached in RTC memory (`app_settings`).
5. **Sync Books**: start the BLE GATT server for the companion PWA and show the pairing QR code.
6. **Nav Hints**: open the hints screen (paged with ▲ / ▼); a double click toggles footer hints on every screen.
7. **Refresh Screen**: redraw the menu with a full refresh to clear ghosting.
8. **Power Off**: show the power-off screen and release the power latch (with USB connected the device enters deep sleep instead).

### Screen Layout

All screens use the shared `ui_kit` helpers and adapt to `BOARD_DISPLAY_WIDTH` x `BOARD_DISPLAY_HEIGHT` and the rotation setting: a 19 px header, the body, and a 16 px footer. Long lists scroll; rows wrap to up to 2 or 3 lines depending on the screen. See [RULES.md section 2.6](RULES.md).

---

## 7. BLE GATT Protocol

BacaIn provides a custom BLE GATT service (`0000fe60-0000-1000-8000-00805f9b34fb`) for wireless book transfers, status telemetry, and bidirectional configuration synchronization with the companion PWA.

### Characteristics

| Characteristic | UUID                                   | Properties             | Purpose                                                               |
| :------------- | :------------------------------------- | :--------------------- | :-------------------------------------------------------------------- |
| **Control**    | `0000fe61-0000-1000-8000-00805f9b34fb` | Write, Notify          | Command dispatch and synchronous operation responses                  |
| **Data**       | `0000fe62-0000-1000-8000-00805f9b34fb` | Write Without Response | Binary chunk stream for book transfers (506B payload + 2B seq)        |
| **Status**     | `0000fe63-0000-1000-8000-00805f9b34fb` | Read, Notify           | Packed 12-byte binary telemetry (battery %, voltage, storage metrics) |

### Control Characteristic OpCodes

Commands are written to the Control Characteristic. Responses are returned as notifications on the same characteristic. Multi-byte values are little-endian. Payload layouts follow [`ble_sync.c`](./components/ble_sync/ble_sync.c); opcode values are defined in [`ble_sync.h`](./components/ble_sync/ble_sync.h).

| OpCode           | Name                   | Request                        | Response                                                                                                                                                           | Purpose                                                                                                     |
| :--------------- | :--------------------- | :----------------------------- | :----------------------------------------------------------------------------------------------------------------------------------------------------------------- | :---------------------------------------------------------------------------------------------------------- |
| `0x01`           | `LIST_BOOKS`           | `[0x01]`                       | Per book: `[0x01, status, is_last, size(4B), name\0, offset(4B), bm_count]`<br>Empty list: `[0x01, status, 1, 0, 0, 0, 0, 0]`                                      | List `.txt` books in the active storage, with saved reading offset and bookmark count                       |
| `0x02`           | `START_UPLOAD`         | `[0x02, size(4B), name]`       | `[0x02, status]`                                                                                                                                                   | Open the target file. Rejected if size + 16 KB exceeds free space                                           |
| `0x03`           | `FINISH_UPLOAD`        | `[0x03]`                       | `[0x03, status, bytes(4B)]`                                                                                                                                        | Flush and close the file; an incomplete upload is deleted. `mushaf.bqm` is validated after upload           |
| `0x04`           | `DELETE_BOOK`          | `[0x04, name]`                 | `[0x04, status]`                                                                                                                                                   | Delete a book and its saved state                                                                           |
| `0x05`           | `ABORT_UPLOAD`         | `[0x05]`                       | `[0x05, status]`                                                                                                                                                   | Abort the upload and delete the partial file                                                                |
| `0x06`           | `SET_STORAGE_TARGET`   | `[0x06, target]`               | `[0x06, status]`                                                                                                                                                   | Switch active storage (`0`: MicroSD, `1`: Flash)                                                            |
| `0x07`           | `GET_BOOKMARKS`        | `[0x07, rel_path]`             | `[0x07, status, offset(4B), total(4B), count, bookmarks[count] (4B each)]`                                                                                         | Saved reading offset, file size and bookmark byte offsets                                                   |
| `0x08`           | `DELETE_BOOKMARK`      | `[0x08, offset(4B), rel_path]` | `[0x08, status]`                                                                                                                                                   | Delete the bookmark at a byte offset                                                                        |
| `0x09`           | `ADD_BOOKMARK`         | `[0x09, offset(4B), rel_path]` | `[0x09, status]`                                                                                                                                                   | Add a bookmark at a byte offset                                                                             |
| `0x0A`           | `SET_BOOK_OFFSET`      | `[0x0A, offset(4B), rel_path]` | `[0x0A, status]`                                                                                                                                                   | Set the saved reading position                                                                              |
| `0x10`           | `SET_FONT`             | `[0x10, font_id]`              | `[0x10, status]`                                                                                                                                                   | Body font size (`0`: 16 px, `1`: 12 px). Saved and applied on the next screen refresh                       |
| `0x12`           | `FORCE_REFRESH`        | `[0x12]`                       | `[0x12, status]`                                                                                                                                                   | Redraw the current screen (full refresh in the reading view)                                                |
| `0x13`           | `SET_SLEEP_TIMEOUT`    | `[0x13, seconds(2B)]`          | `[0x13, status]`                                                                                                                                                   | Inactivity timeout before deep sleep in seconds (`0`: never). Saved as a device setting                     |
| `0x15`           | `SET_NAV_HINTS`        | `[0x15, enabled]`              | `[0x15, status]`                                                                                                                                                   | Footer navigation hints (`0`: off, `1`: on). Saved and applied on the next screen refresh                   |
| `0x16`           | `SET_ROTATION`         | `[0x16, rotation]`             | `[0x16, status]`                                                                                                                                                   | Display orientation (`0`: 0°, `1`: 90°, `2`: 180°, `3`: 270°). Saved and applied on the next screen refresh |
| `0x17`           | `SET_BUTTON_INVERT`    | `[0x17, invert]`               | `[0x17, status]`                                                                                                                                                   | Swap ▲ / ▼ (`0`: normal, `1`: inverted). Saved and applied on the next event                                |
| `0x18`           | `SET_STATUS_LED`       | `[0x18, enabled]`              | `[0x18, status]`                                                                                                                                                   | Status LED on or off. `NOT_SUPPORTED` if the board has no LED                                               |
| `0x19`           | `GET_CONFIG`           | `[0x19]`                       | 16 bytes: `[0x19, status, font_id, nav_hints, rotation, btn_invert, status_led, storage_target, sleep_timeout(2B), caps, width(2B), height(2B), refresh_interval]` | Current settings, capabilities and display size                                                             |
| see `ble_sync.h` | `SET_REFRESH_INTERVAL` | `[op, pages]` (`0` to `100`)   | `[op, status]`                                                                                                                                                     | Full refresh every N page turns (`0`: never)                                                                |
| `0x20`           | `GET_MUSHAF_STATE`     | `[0x20]`                       | See [Mushaf_Reader.md](./Mushaf_Reader.md)                                                                                                                         | Mushaf reading cursor and 8 bookmarks                                                                       |
| `0x21`           | `SET_MUSHAF_STATE`     | `[0x21, sub, data]`            | `[0x21, status]`                                                                                                                                                   | Set Mushaf cursor (`sub=1`) or bookmarks (`sub=2`) and save to NVS                                          |

Settings received over BLE are saved by the `ble_sync` worker task and applied by the main task before it handles the next event. In `GET_CONFIG`, `storage_target` is `1` for Flash and `0` for MicroSD, and `caps` contains `BLE_SYNC_CAP_REFRESH_INTERVAL` and, on Mushaf builds, `BLE_SYNC_CAP_MUSHAF`. An unknown opcode is rejected with ATT error `BLE_ATT_ERR_UNLIKELY`.

---

## 8. References & Links

- **Hardware Reference Repository & Schematics**: [Waveshare ESP32-S3-ePaper-1.54](https://github.com/waveshareteam/ESP32-S3-ePaper-1.54)
- **Official ESP-IDF v5.3 Programming Guide**: [Espressif Documentation](https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32s3/)
