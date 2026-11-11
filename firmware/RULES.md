# Firmware Engineering Rules (`firmware/RULES.md`)

Every board profile, power subsystem, display driver, and application state routine in this repository must adhere to these principles.

---

## 1. Hardware Engineering Decoupling

Hardware definitions and silicon specifics stay isolated from application code.

- **Centralized Declarations**: Pin numbers, display geometries, feature enable flags, and battery OCV tables belong in the target board definition, such as [`board_def.h`](./boards/waveshare_154/board_def.h). Board-specific C hooks (`board_*`, for example `board_power_pre_sleep()` and `board_light_sleep_pins_init()`) reside in `firmware/boards/<board_id>/board.c` (such as [`board.c`](./boards/waveshare_154/board.c)). Every board hook is listed with `-u <symbol>` in `components/board_config/CMakeLists.txt`.
- **Compile-Time Zero Fallback Invariant**: [`board_config.h`](./components/board_config/include/board_config.h) enforces mandatory definitions with `#error` guards. It never supplies a default value for a board property.
- **Zero Silicon Leakage into Application Logic**: Application code never references silicon labels (`GPIO 0`, `GPIO 18`, `BOOT`, `PWR`) in identifiers or user-facing text. It uses semantic names: `BOARD_BTN_UP_PIN`, `BOARD_BTN_DOWN_PIN`, `BOARD_PWR_LATCH_PIN`. User-facing text names buttons by their current arrow (▲ / ▼), resolved at draw time from the button-invert setting.
- **Explicit Sentinel `-1` Availability**: Every board declares every pin macro explicitly. Absent or unrouted peripherals use `-1`, never `0` (GPIO 0 is an ESP32-S3 strapping pin). Shared headers never supply defaults; `board_config.h` only checks:

  ```c
  /* board_def.h */
  #define BOARD_LED_RED_PIN -1   /* not populated */

  /* board_config.h */
  #ifndef BOARD_LED_RED_PIN
  #error "Board must define BOARD_LED_RED_PIN (-1 if absent)"
  #endif
  ```

  A driver whose pin is `-1`, or whose startup validation fails, sets its availability flag to false and returns `ESP_ERR_NOT_SUPPORTED`. Downstream logic queries availability rather than assuming presence.

---

## 2. Embedded Firmware Philosophy: Deterministic State and Resource Bounds

E-paper devices on constrained microcontrollers cannot afford heap fragmentation, runaway task synchronization, or unpredictable latency.

### 2.1 Deterministic Finite State Concurrency

The application is an enumerated finite state machine driven by discrete events.

- **Single Direction of State Mutation**: Views are enumerated in `reader_view_t` ([`views.h`](./main/views/views.h)), including the Mushaf views when `CONFIG_BOARD_MUSHAF_ENABLE` is set. `VIEW_REQUEST_POWER_OFF` is a request to `main.c`, never a stored view. State changes only in the [`main.c`](./main/main.c) event loop, in response to events from bounded FreeRTOS queues. Views never call sleep or power functions.
- **Decoupled Interrupts and Callbacks**: ISRs, timer callbacks and BLE callbacks (NimBLE host task) never render, write flash, sleep, or make blocking bus calls; they signal the main task. The button GPIO ISR only disables its interrupt and notifies the button task; the 10 ms button tick runs only while a button is active. In [`button.c`](./components/button/button.c), button transitions post `button_event_t` records to the bounded queue `s_btn_queue` (depth 16) with non-blocking `xQueueSend()`. `esp_timer_start_*` is never called from an ISR.
- **Deterministic Chord Actions**: Multi-button events (such as holding UP and DOWN together for 2 seconds to reboot) are evaluated in the timer tick callback (`button_timer_cb`), suppressing single click and long press emissions to guarantee clean reboot sequencing.

### 2.2 Asymmetric Power Lifecycle: Transient Execution and Persistent Sleep

Active CPU execution is a short event between long periods of sleep.

- **Transient Compute**: A wake runs the minimal path to the first rendered frame. The device then waits for input in automatic light sleep, and enters deep sleep after the inactivity timeout (user setting, default 5 s). Menu Power Off releases the power latch (true off; with USB connected it falls back to deep sleep).
- **State Preservation in Retention RAM**: State that must survive deep sleep lives in RTC slow memory (`RTC_DATA_ATTR`):
  - `s_rtc_last_view`: active view.
  - `s_rtc_menu_index`, `s_rtc_book_index`: cursor positions.
  - Cached settings: see `app_settings_load()` in [`reader_ui.c`](./main/reader_ui.c).
- **Differential Screen Refresh from Retention RAM**: The previous frame `s_rtc_canvas` (`BOARD_DISPLAY_BUFFER_SIZE`, defined as `BOARD_DISPLAY_WIDTH * BOARD_DISPLAY_HEIGHT / 8`) is kept in RTC memory. After wake, `reader_ui_flush_canvas` compares it with the new canvas and calls `epaper_port_display_part_diff`. Measured panel time: partial refresh about 570 ms (BUSY), full refresh 1.5 to 2.4 s.
- **Periodic Full Refresh**: A full refresh runs every N page turns (user setting `full_refresh_n`, default 10, 0 = never). The counter lives in RTC memory and counts reading and Mushaf page turns only; any full refresh resets it.

### 2.3 Zero-Allocation Runtime and Predictable Footprints

Heap fragmentation is a primary failure mode in embedded systems with long uptime.

- **No Run-Time Heap Churn**: Continuous execution loops never allocate or free dynamic memory (`malloc`, `free`, `strdup`, `cJSON_Parse`). All buffers are sized at compile time.
- **Compile-Time Sized Buffers**: Display framebuffers (`BOARD_DISPLAY_BUFFER_SIZE`) are allocated once at boot in `reader_ui_init` from internal DMA-capable SRAM (`MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA`). Text pager lines, line break tables, and file catalogue arrays use fixed compile-time bounds.
- **Binary Pruning**: Never compile dead assets, unreferenced fonts, or obsolete controller LUTs into the image. Unused font tables and display drivers are excluded at the Kconfig and CMake component level to conserve flash and SRAM.

### 2.4 Fault Transparency (Zero Silent Fallbacks)

Reliability requires fast, visible failure rather than hidden corruption.

- **Compile-Time Fault Transparency**: A missing board profile, mandatory property, or pin for an enabled peripheral halts compilation with an explicit `#error`.
- **Fail Fast, Fail Loudly**: Empty catch blocks and ignored return codes (`esp_err_t`) are forbidden. If an SPI transaction, flash write, or ADC measurement fails, log the exact return code with `ESP_LOGE` and propagate the failure.
- **Zero Dummy Data**: Never synthesize metrics, battery readings, or timers. If battery ADC conversion fails, report 0 mV and render an error indicator.
- **Hardware Failure Manifestation**: When hardware fails (such as an uninitialized display or missing internal flash partition), the firmware stops the affected function, shows the status LED or display error banner, and logs the cause instead of executing undefined behavior.
- **Failure on Battery**: A panic or abort resets the GPIOs and may release the power latch, so on battery a crash can power the device off. After the latch is set, `ESP_ERROR_CHECK` is used only where power-off is an acceptable outcome. Non-critical failures (for example `esp_pm_configure`) log `ESP_LOGE` and continue in a defined reduced mode.

### 2.5 Sleep and Wake Invariants

1. **Boot order**: power latch, NVS, then anything that reads settings (status LED, `app_settings_load`), then display init, then `board_light_sleep_pins_init()`, then `esp_pm_configure()`.
2. **Deep sleep zeroes ordinary statics**: counters, caches and "loaded" flags that must survive a wake are `RTC_DATA_ATTR`. Everything else is assumed reset on every wake.
3. **Lazy loaders**: a "loaded" flag is set only after a successful read. A read that fails because NVS is not initialized is retried.
4. **No writes on the wake path**: opening, restoring or navigating writes nothing to flash unless state changed. Dirty state is flushed once at sleep entry.
5. **Guard what was not initialized**: code that saves state at sleep entry checks that the subsystem is active this wake (for example `text_pager_is_book_open()`). Views open only what they need, when entered.
6. **Light sleep pins**: `BOARD_LIGHT_SLEEP_KEEP_PINS` lists every output and wake input whose configuration must not change in light sleep. No listed pin is reconfigured after `board_light_sleep_pins_init()`. Core components never call `gpio_sleep_sel_*`.
7. **Light sleep and SPI**: multi-step EPD sequences hold an `ESP_PM_NO_LIGHT_SLEEP` lock, released before every BUSY wait and on every return path.

---

## 3. Architecture and Code References

For implementations of these rules, inspect the canonical files:

- Repository rules and agent workflow: [AGENTS.md](../AGENTS.md)
- Supported boards and profiles: [BOARDS.md](./BOARDS.md)
- Central board support package: [board_config.h](./components/board_config/include/board_config.h)
- Waveshare 1.54 board definition: [board_def.h](./boards/waveshare_154/board_def.h)
- Board hardware hooks: [board.c](./boards/waveshare_154/board.c) (`firmware/boards/<board_id>/board.c`)
- Power management and OCV curves: [power_mgr.c](./components/power_mgr/power_mgr.c)
- UI state, settings cache and differential refresh: [reader_ui.c](./main/reader_ui.c)
- View enumeration and dispatch: [views.h](./main/views/views.h)
- Main lifecycle and concurrency: [main.c](./main/main.c)
- Button event queue and tick gating: [button.c](./components/button/button.c)
- E-paper hardware port, LUT waveforms and PM lock: [epaper_port.c](./components/epaper/epaper_port.c)
- BLE opcodes and payload layouts: [ble_sync.h](./components/ble_sync/ble_sync.h)
