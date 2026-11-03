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
- **Peripherals & Pins**: Configure E-Paper SPI pins, MicroSD SDMMC pins, navigation buttons (`BOARD_BTN_UP_PIN`, `BOARD_BTN_DOWN_PIN`), power latch, status LED, or audio pins.
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

Docker Compose executes the following steps:

1. Runs the `espressif/idf:v5.3.1` container.
2. Compiles the project for `esp32s3` into `/project/build`.
3. Runs `python3 package_firmware.py /project/build -o bacain_<board_id>_factory.bin --board "Waveshare ESP32-S3-ePaper-1.54"`.
4. Outputs the factory binary at `build/bacain_<board_id>_factory.bin` and manifest at `build/manifest.json`.

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

| Custom Requirement                                  | Compiler Macro Value                                                                                                                 |
| :-------------------------------------------------- | :----------------------------------------------------------------------------------------------------------------------------------- |
| **Change Screen Resolution** (e.g. 250 x 122)       | `-DCONFIG_BOARD_DISPLAY_WIDTH=250 -DCONFIG_BOARD_DISPLAY_HEIGHT=122`                                                                 |
| **Change Screen Orientation** (90 deg landscape)    | `-DCONFIG_BOARD_DISPLAY_ROTATION=1`                                                                                                  |
| **Change Button Pins** (UP/BOOT=4, DOWN/PWR=5)       | `-DCONFIG_BOARD_BTN_UP_PIN=4 -DCONFIG_BOARD_BTN_DOWN_PIN=5`                                                                          |
| **Change E-Paper SPI Pins**                          | `-DCONFIG_BOARD_EPD_DC_PIN=14 -DCONFIG_BOARD_EPD_CS_PIN=15`                                                                          |
| **Combine Multiple Settings**                        | `-DCONFIG_BOARD_DISPLAY_WIDTH=296 -DCONFIG_BOARD_DISPLAY_HEIGHT=128 -DCONFIG_BOARD_DISPLAY_ROTATION=1 -DCONFIG_BOARD_BTN_UP_PIN=4`   |

---

## 5. Flashing the Device

Flashing requires writing only the single factory binary (`bacain_<board_id>_factory.bin`) at offset `0x0`. Partition fragments do not need to be flashed separately.

### A. Web Serial Flasher (Browser)

Open the web flasher in a Chromium browser (Chrome or Edge):

1. Connect the ESP32-S3 board via USB-C.
2. Open `web/flasher.html` (or the **Firmware Flasher** tab in the web interface).
3. Select your board to download the latest release binary, or load a local `bacain_<board_id>_factory.bin` file (flashed at offset `0x0`).
4. Click **Connect Serial Port**, select the ESP32-S3 port, and click **Flash Firmware**.

### B. Command Line via `esptool`

Flash the single factory binary directly to offset `0x0`:

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

## 6. References & Links

- **Hardware Reference Repository & Schematics**: [Waveshare ESP32-S3-ePaper-1.54](https://github.com/waveshareteam/ESP32-S3-ePaper-1.54)
- **Windows USB Passthrough for WSL2**: [usbipd-win](https://github.com/dorssel/usbipd-win)
- **Official ESP-IDF v5.3 Programming Guide**: [Espressif Documentation](https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32s3/)
