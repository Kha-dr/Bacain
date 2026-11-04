Every board profile, power subsystem, display driver, and application state routine in this repository must strictly adhere to these principles.

---

## 1. Hardware Engineering Decoupling

Hardware definitions and silicon specificities must remain isolated from application code.

- **Centralized Declarations**: Pin numbers, SPI bus timings, display controllers, and ADC channels belong exclusively in [`board_config.h`](./components/board_config/include/board_config.h) and target board headers such as [`board_def.h`](./boards/waveshare_154/board_def.h).
- **Zero Silicon Leakage into Application Logic**: Higher-level subsystems must never reference silicon strapping labels (`GPIO 0`, `GPIO 18`, `BOOT`, `PWR`). All application code consumes semantic macros: `BOARD_BTN_UP_PIN`, `BOARD_BTN_DOWN_PIN`, `BOARD_VBAT_PWR_HOLD_PIN`.
- **Explicit Sentinel `-1` Availability**: Unconnected, absent, or unrouted peripherals must use the explicit sentinel `-1`, never arbitrary numeric defaults such as `0` (which collides with `GPIO 0`, ESP32-S3 strapping pin). Subsystems check availability at runtime:
  ```c
  #ifndef BOARD_LED_RED_PIN
  #define BOARD_LED_RED_PIN -1
  #endif
  ```
  If a peripheral pin evaluates to `-1` or startup validation fails, the driver sets its availability flag to false and returns `ESP_ERR_NOT_SUPPORTED`. Downstream logic queries availability rather than assuming presence.

---

## 2. Embedded Firmware Philosophy: Deterministic State & Resource Bounds

Constrained microcontrollers operating e-paper displays have zero tolerance for heap fragmentation, runaway task synchronization, or unpredictable latency.

### 2.1 Deterministic Finite State Concurrency

The firmware application is modeled as an enumerated finite state machine driven exclusively by discrete events.

- **Single Direction of State Mutation**: Operational views are strictly enumerated in [`reader_ui.h`](./main/reader_ui.h): `VIEW_READING`, `VIEW_MENU`, `VIEW_BOOK_LIST`, `VIEW_BLE_SYNC`, `VIEW_BOOKMARKS`, `VIEW_NAV_HINTS`. State mutations occur exclusively within the primary task loop of [`main.c`](./main/main.c) in response to events popped from bounded FreeRTOS queues.
- **Decoupled Interrupts and Ingestion Queues**: Hardware interrupts, GPIO timer pollers, and BLE transport callbacks must never invoke UI rendering, flash writes, or blocking bus calls. In [`button.c`](./components/button/button.c), button state transitions post lightweight `button_event_t` records into the bounded queue `s_btn_queue` (depth 16) via non-blocking `xQueueSend()`. The presentation thread consumes events synchronously.
- **Deterministic Chord Actions**: Multi-button events (such as holding UP and DOWN simultaneously for 2 seconds to trigger reboot) are evaluated within the timer tick callback (`button_timer_cb`), suppressing single click and long press emissions to guarantee clean reboot sequencing.

### 2.2 Asymmetric Power Lifecycle: Transient Execution vs. Persistent Sleep

Active CPU execution is a transient event between long periods of unpowered sleep.

- **Transient Compute**: When an input event or wake signal occurs, the CPU wakes, executes minimal logic, updates hardware, and returns to deep sleep immediately. The device never remains in an active polling loop when idle.
- **State Preservation in Low-Power Retention RAM**: Essential operational state is preserved in RTC slow memory using `RTC_DATA_ATTR` attributes across deep sleep cycles:
  - `s_rtc_last_view`: Active application view.
  - `s_rtc_menu_index`, `s_rtc_book_index`: Selected cursor positions.
  - `s_rtc_settings_cached`: Cached display rotation, font size, and navigation hints.
- **Differential Screen Refresh from Retention RAM**: In [`reader_ui.c`](./main/reader_ui.c), the previous display buffer `s_rtc_canvas` (5000 bytes) is retained in RTC memory. Upon wake, `reader_ui_flush_canvas` compares `s_rtc_canvas` against the new canvas and calls `epaper_port_display_part_diff`. This avoids full screen flashes, eliminates re-parsing book files from disk, and keeps the wake-to-sleep cycle under 300 ms.

### 2.3 Zero-Allocation Runtime & Predictable Footprints

Heap fragmentation is a primary failure mode in embedded systems with long uptime requirements.

- **Eliminate Run-Time Heap Churn**: Continuous execution loops must never allocate or free dynamic memory (`malloc`, `free`, `strdup`, `cJSON_Parse`). All buffers must be statically sized at compile time.
- **Compile-Time Sized Buffers**: Display framebuffers (`DISPLAY_BUFFER_SIZE` = 5000 bytes) are allocated once at boot in `reader_ui_init` using internal DMA-capable SRAM (`MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA`). Text pager lines, line break tables, and file catalogue arrays use fixed compile-time boundaries.
- **Aggressive Binary Pruning**: Never compile dead assets, unreferenced fonts, or obsolete controller LUTs into the firmware image. Unused font tables and display drivers must be excluded at the Kconfig and CMake component level to conserve flash and execution SRAM.

### 2.4 Uncompromising Fault Transparency (Zero Silent Fallbacks)

Reliability requires fast, deterministic failure rather than hidden corruption.

- **Fail Fast, Fail Loudly**: Swallowing errors via empty catch blocks or ignoring return codes (`esp_err_t`) is strictly forbidden. If an SPI bus transaction, flash sector write, or ADC measurement fails, log the exact return code via `ESP_LOGE` and bubble the failure up the call stack.
- **Zero Dummy Data**: Never synthesize fake metrics, randomized battery readings, or simulated timers. If battery ADC conversion fails, report 0 mV and render an error indicator rather than manufacturing dummy data.
- **Hardware Failure Manifestation**: When hardware fails (such as an uninitialized display or missing internal flash partition), the firmware halts gracefully, asserts the status LED or display error banner, and waits for diagnostics rather than executing undefined instructions.

---

## 3. Architecture & Code References

For detailed implementations adhering to this philosophy, inspect the canonical files:

- Repository Rules and Standards: [AGENTS.md](../AGENTS.md)
- Supported Boards and Profiles: [BOARDS.md](./BOARDS.md)
- Central Board Support Package: [board_config.h](./components/board_config/include/board_config.h)
- Waveshare 1.54 Board Definition: [board_def.h](./boards/waveshare_154/board_def.h)
- Power Management and OCV Curves: [power_mgr.c](./components/power_mgr/power_mgr.c)
- Deterministic UI State Machine and Differential Refresh: [reader_ui.c](./main/reader_ui.c)
- Main Lifecycle and Concurrency: [main.c](./main/main.c)
- Button Event Queue: [button.c](./components/button/button.c)
- E-Paper Hardware Port and LUT Waveforms: [epaper_port.c](./components/epaper/epaper_port.c)
