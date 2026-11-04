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

Open the configuration menu:

```bash
idf.py menuconfig
```

Navigate to **Board & Hardware Configuration**:

- **Hardware Board Profile (`choice BOARD_PROFILE`)**: Select a target board profile or choose `Custom / Manual Configuration` to set GPIO allocations and display parameters manually.
- **Display Dimensions & Orientation**: Set dimensions (`BOARD_DISPLAY_WIDTH`, `BOARD_DISPLAY_HEIGHT`) and orientation (`BOARD_DISPLAY_ROTATION`).
- **Peripherals & Pins**: Configure E-Paper SPI pins, MicroSD SDMMC pins, navigation buttons (`BOARD_BTN_UP_PIN`, `BOARD_BTN_DOWN_PIN`), power latch, status LED (`BOARD_HAS_STATUS_LED`, `BOARD_LED_ACTIVE_LEVEL`, `BOARD_LED_RED_PIN`, `BOARD_LED_GREEN_PIN`, `BOARD_LED_BLUE_PIN`), or audio pins.
- Save changes to `sdkconfig`.

### B. Dynamic CI Multi-Board Matrix

CI dynamically parses `firmware/boards/boards.json` to generate the build matrix. For each cataloged board profile, CI sets `CONFIG_BOARD_PROFILE_<ID>=y`, compiles the project, and packages the release factory binary. Adding a board entry to `boards.json` automatically enables CI builds for that target without workflow changes.

### C. Environment Variables in Docker Compose

Build custom hardware configurations without modifying source files:

```bash
EXTRA_IDF_ARGS="-DCONFIG_BOARD_DISPLAY_WIDTH=250" BOARD_NAME="Custom-Board" docker compose run --rm firmware
```

---

## 3. Building & Packaging Binaries

The build generates a single factory binary per board (`bacain_<board_id>_factory.bin`) flashed directly at offset `0x0`.

### Option A: Using Docker Compose (Recommended)

Requires no local ESP-IDF installation:

```bash
cd firmware
docker compose run --rm firmware
```

The `firmware` service in `compose.yml` automatically executes incremental compilation and packaging without manual command overrides:

1. Runs the `espressif/idf:v5.3.1` container.
2. Compiles the project incrementally: `idf.py -B /project/build build ${EXTRA_IDF_ARGS}`.
3. Packages the release binaries: `python3 package_firmware.py /project/build -o "${FACTORY_BIN}" --board "${BOARD_NAME}"`.
4. Outputs the factory binary at `build/bacain_factory.bin` (or custom name configured via `FACTORY_BIN`) and manifest at `build/manifest.json`.

For a clean rebuild from scratch, run the `clean-build` service:

```bash
docker compose run --rm clean-build
```

The `clean-build` service deletes `/project/build`, reinitializes the target chip (`idf.py -B /project/build set-target esp32s3`), compiles the project, and packages the factory binary.

### Option B: Native ESP-IDF

Requires ESP-IDF v5.3+ installed locally:

```bash
cd firmware
idf.py set-target esp32s3
idf.py build
python3 package_firmware.py build -o bacain_<board_id>_factory.bin --board "Waveshare ESP32-S3-ePaper-1.54"
```

### Packaging Script Parameters (`package_firmware.py`)

The packaging script merges build artifacts into a single factory binary and creates the release manifest:

- `build_dir`: Path to build directory (default: `build`).
- `-o, --output`: Factory binary filename (default: `bacain_factory.bin`).
- `--board`: Board profile name stored in the manifest (default: `Waveshare ESP32-S3-ePaper-1.54`).
- `--chip`: esptool target chip (default: `esp32s3`).
- `--flash-size`: Flash memory size (default: `8MB`).
- `--name`: Firmware name stored in the manifest (default: `BacaIn Firmware`).

---

## 4. CI/CD Automation & Custom Builds via GitHub Actions

The workflow `.github/workflows/firmware_ci.yml` handles automated builds and on-demand custom builds.

### A. Automatic Matrix Builds

- **Push to `main` & Pull Request**: CI reads `firmware/boards/boards.json`, generates a dynamic matrix, and compiles firmware for every cataloged board profile.
- **Release Tags (`v*`)**: Builds and attaches factory binaries for all cataloged boards (such as `bacain_waveshare_154_factory.bin`), along with manifest metadata, to GitHub Releases.

### B. Custom Firmware Build via `workflow_dispatch` (Manual Trigger)

Build custom hardware profiles from the GitHub web interface without a local toolchain:

1. Navigate to the **Actions** tab in the repository.
2. Select the **BacaIn Firmware CI/CD** workflow.
3. Click **Run workflow**.
4. Fill in parameters:
   - **Board Profile Name**: Name of the target board profile (default: `Waveshare ESP32-S3-ePaper-1.54`).
   - **Extra IDF Build Args / Compiler Macros**: Compiler macro flags (`-D<macro>`) to adjust pins, resolution, or rotation.
   - **Output Factory Binary Name**: Filename for the merged factory binary (default: `bacain_factory.bin`).
5. Click **Run workflow**. Download the `bacain-firmware` artifact zip from the workflow summary after the run finishes.

#### Example Compiler Flags for `Extra IDF Build Args`:

| Custom Requirement                                    | Compiler Macro Value                                                                                                               |
| :---------------------------------------------------- | :--------------------------------------------------------------------------------------------------------------------------------- |
| **Change Screen Resolution** (e.g. 250 x 122)         | `-DCONFIG_BOARD_DISPLAY_WIDTH=250 -DCONFIG_BOARD_DISPLAY_HEIGHT=122`                                                               |
| **Change Screen Orientation** (90 deg landscape)      | `-DCONFIG_BOARD_DISPLAY_ROTATION=1`                                                                                                |
| **Change Button Pins** (UP/BOOT=4, DOWN/PWR=5)        | `-DCONFIG_BOARD_BTN_UP_PIN=4 -DCONFIG_BOARD_BTN_DOWN_PIN=5`                                                                        |
| **Change E-Paper SPI Pins**                           | `-DCONFIG_BOARD_EPD_DC_PIN=14 -DCONFIG_BOARD_EPD_CS_PIN=15`                                                                        |
| **Configure Status LED** (Red=1, Green=2, Active-Low) | `-DCONFIG_BOARD_LED_RED_PIN=1 -DCONFIG_BOARD_LED_GREEN_PIN=2 -DCONFIG_BOARD_LED_ACTIVE_LEVEL=0`                                    |
| **Combine Multiple Settings**                         | `-DCONFIG_BOARD_DISPLAY_WIDTH=296 -DCONFIG_BOARD_DISPLAY_HEIGHT=128 -DCONFIG_BOARD_DISPLAY_ROTATION=1 -DCONFIG_BOARD_BTN_UP_PIN=4` |

---

## 5. Flashing the Device

### A. Web Serial Flasher (Browser)

Open the web flasher in a Chromium browser (Chrome or Edge):

1. Connect the ESP32-S3 board via USB-C.
2. Open `web/flasher.html` (or the **Firmware Flasher** tab in the web interface).
3. Select your board to download the latest release binary, or load a local `bacain_<board_id>_factory.bin`.
4. Click **Connect Serial Port**, select the ESP32-S3 port, and click **Flash Firmware**.

### B. Command Line via `esptool`

```bash
# Windows (replace COM3 with your port)
esptool.py -p COM3 -b 460800 --chip esp32s3 write_flash 0x0 build/bacain_<board_id>_factory.bin

# Linux / macOS (replace /dev/ttyACM0 with your port)
esptool.py -p /dev/ttyACM0 -b 460800 --chip esp32s3 write_flash 0x0 build/bacain_<board_id>_factory.bin
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

Navigation uses two physical buttons: UP (`BOARD_BTN_UP_PIN`, default GPIO 0 / BOOT) and DOWN (`BOARD_BTN_DOWN_PIN`, default GPIO 18 / PWR).

### Button Architecture

The `button` component samples GPIO levels with hardware debouncing and dispatches events via FreeRTOS queue to `main.c`:

- **Cold Boot Dismissal**: Pressing UP or DOWN dismisses the sleep screen.
- **Short Click** (`BTN_CLICK`):
  - Reading: Previous page (UP) / Next page (DOWN).
  - Menu & Library: Move cursor up (UP) / Move cursor down (DOWN).
- **Double Click** (`BTN_DOUBLE_CLICK`):
  - Reading: Toggle bookmark (UP) / Open bookmarks list (DOWN).
  - Menu & Library: Select or activate item.
- **Long Press (>1.2s)** (`BTN_HOLD`):
  - Reading: Open Reader Menu.
  - Menu, Library & Sync: Back / Exit.
- **Simultaneous Hold (2.0s)**: Detected when UP + DOWN are held together for 2.0s, triggering an immediate hardware reboot with status LED strobe.

For the end-user contextual button action table, see [Controls in README.md](../README.md#controls).

### On-Device Menu

The Reader Menu (`reader_ui_render_menu`) provides strictly 7 items managed in `main.c` (Status LED is configured via the companion PWA and removed from the on-device menu):

1. **Continue Reading**: Resume reading active book at saved byte offset.
2. **Book Library**: Browse and open books from internal FATFS or MicroSD storage.
3. **Font Size: [12px/16px]**: Toggle text pager font between Font12 (12px, 6x12, default) and Font16 (16px, 8x16). Persisted in NVS (`font_size`) and RTC memory (`s_rtc_font_size`). Default font is 12px (Font12 / `TEXT_PAGER_FONT_12`). Note: Font20 and Font24 were deleted from the firmware build to conserve flash and memory footprints (Font8 is internal to epaper).
4. **Sync Books**: Initialize BLE GATT server for companion PWA transfer and display pairing QR code.
5. **Nav Hints**: Toggle on-screen button navigation hints in reading view.
6. **Refresh Screen**: Trigger manual full display refresh (`epaper_refresh_full`) to clear ghosting.
7. **Power Off**: Display power-off screen and enter deep sleep (`enter_deep_sleep_with_state`).

---

## 8. BLE GATT Protocol

BacaIn provides a custom BLE GATT service (`0000fe60-0000-1000-8000-00805f9b34fb`) for wireless book transfers, status telemetry, and bidirectional configuration synchronization with the companion PWA.

### Characteristics

| Characteristic | UUID                                   | Properties             | Purpose                                                               |
| :------------- | :------------------------------------- | :--------------------- | :-------------------------------------------------------------------- |
| **Control**    | `0000fe61-0000-1000-8000-00805f9b34fb` | Write, Notify          | Command dispatch and synchronous operation responses                  |
| **Data**       | `0000fe62-0000-1000-8000-00805f9b34fb` | Write Without Response | Binary chunk stream for book transfers (506B payload + 2B seq)        |
| **Status**     | `0000fe63-0000-1000-8000-00805f9b34fb` | Read, Notify           | Packed 12-byte binary telemetry (battery %, voltage, storage metrics) |

### Control Characteristic OpCodes

Commands are written to the Control Characteristic. Responses and operation statuses are returned via GATT notifications on the same characteristic.

| OpCode | Name                             | Direction        | Request Payload                        | Response Payload                                                                         | Purpose                                                                       |
| :----- | :------------------------------- | :--------------- | :------------------------------------- | :--------------------------------------------------------------------------------------- | :---------------------------------------------------------------------------- |
| `0x01` | `BLE_SYNC_OP_LIST_BOOKS`         | Client -> Server | `[0x01]`                               | `[0x01, target, size(4B LE), name...]` / EOF `[0x01, 0xFF]`                              | List all files in storage                                                     |
| `0x02` | `BLE_SYNC_OP_START_UPLOAD`       | Client -> Server | `[0x02, target, size(4B LE), name...]` | `[0x02, status]`                                                                         | Prepare file reception                                                        |
| `0x03` | `BLE_SYNC_OP_FINISH_UPLOAD`      | Client -> Server | `[0x03]`                               | `[0x03, status]`                                                                         | Finalize file upload and close file                                           |
| `0x04` | `BLE_SYNC_OP_DELETE_BOOK`        | Client -> Server | `[0x04, target, name...]`              | `[0x04, status]`                                                                         | Delete book from target storage                                               |
| `0x05` | `BLE_SYNC_OP_ABORT_UPLOAD`       | Client -> Server | `[0x05]`                               | `[0x05, status]`                                                                         | Abort active upload and cleanup temp file                                     |
| `0x06` | `BLE_SYNC_OP_SET_STORAGE_TARGET` | Client -> Server | `[0x06, target]`                       | `[0x06, status]`                                                                         | Switch active storage (`0`: MicroSD, `1`: Flash)                              |
| `0x10` | `BLE_SYNC_OP_SET_FONT`           | Client -> Server | `[0x10, font_id]`                      | `[0x10, status]`                                                                         | Set font size (`0`: 16px, `1`: 12px)                                          |
| `0x12` | `BLE_SYNC_OP_FORCE_REFRESH`      | Client -> Server | `[0x12]`                               | `[0x12, status]`                                                                         | Trigger manual full screen refresh                                            |
| `0x13` | `BLE_SYNC_OP_SET_SLEEP_TIMEOUT`  | Client -> Server | `[0x13, timeout(2B LE)]`               | `[0x13, status]`                                                                         | Configure sleep inactivity timeout in seconds                                 |
| `0x14` | `BLE_SYNC_OP_TRIGGER_SLEEP`      | Client -> Server | `[0x14]`                               | `[0x14, status]`                                                                         | Immediately enter deep sleep                                                  |
| `0x15` | `BLE_SYNC_OP_SET_NAV_HINTS`      | Client -> Server | `[0x15, enabled]`                      | `[0x15, status]`                                                                         | Toggle reading footer navigation hints (`0`: off, `1`: on)                    |
| `0x16` | `BLE_SYNC_OP_SET_ROTATION`       | Client -> Server | `[0x16, rotation]`                     | `[0x16, status]`                                                                         | Set display orientation (`0`: 0°, `1`: 90°, `2`: 180°, `3`: 270°)             |
| `0x17` | `BLE_SYNC_OP_SET_BUTTON_INVERT`  | Client -> Server | `[0x17, invert]`                       | `[0x17, status]`                                                                         | Invert UP/DOWN button assignments (`0`: normal, `1`: inverted)                |
| `0x18` | `BLE_SYNC_OP_SET_STATUS_LED`     | Client -> Server | `[0x18, enabled]`                      | `[0x18, status]`                                                                         | Enable or disable status LED indications (`0`: off, `1`: on)                  |
| `0x19` | `BLE_SYNC_OP_GET_CONFIG`         | Client -> Server | `[0x19]`                               | `[0x19, status, font_size, nav_hints, rotation, btn_invert, status_led, storage_target]` | Query active on-device and NVS configuration to synchronize PWA UI on connect |

---

## 9. References & Links

- **Hardware Reference Repository & Schematics**: [Waveshare ESP32-S3-ePaper-1.54](https://github.com/waveshareteam/ESP32-S3-ePaper-1.54)
- **Official ESP-IDF v5.3 Programming Guide**: [Espressif Documentation](https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32s3/)
