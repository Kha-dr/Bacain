# Firmware Engineering Rules (`firmware/RULES.md`)

Every board profile, power subsystem, display driver, and application state routine in this repository must adhere to these principles.

---

## 1. Hardware Engineering Decoupling

Hardware definitions and silicon specifics stay isolated from application code.

- **Centralized Declarations**: Pin numbers, display geometries, feature enable flags, and battery OCV tables belong in the target board definition, such as [`board_def.h`](./boards/waveshare_154/board_def.h). Board-specific C hooks (`board_*`, for example `board_power_pre_sleep()` and `board_light_sleep_pins_init()`) reside in `firmware/boards/<board_id>/board.c`. Every board hook is listed with `-u <symbol>` in `components/board_config/CMakeLists.txt`.
- **Compile-Time Zero Fallback Invariant**: [`board_config.h`](./components/board_config/include/board_config.h) enforces mandatory definitions with `#error` guards. It never supplies a default value for a board property.
- **Zero Silicon Leakage into Application Logic**: Application code never references silicon labels (`GPIO 0`, `GPIO 18`, `BOOT`, `PWR`) in identifiers or user-facing text. It uses semantic names: `BOARD_BTN_UP_PIN`, `BOARD_BTN_DOWN_PIN`, `BOARD_PWR_LATCH_PIN`. User-facing text names buttons by their current arrow (▲ / ▼), resolved at draw time from the button-invert setting. Known exception pending rename: `BTN_ID_BOOT` / `BTN_ID_PWR` in `button.h` and `WAKEUP_REASON_*_BUTTON` in `power_mgr.h`.
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

- **Resolution Independence**: No application code assumes a panel size. Layout is derived at run time from `ui_screen_w()` / `ui_screen_h()` (rotation-aware), `UI_BODY_TOP` and `ui_body_bottom()` ([`ui_layout.h`](./components/ui_kit/ui_layout.h), [`ui_widgets.c`](./components/ui_kit/ui_widgets.c)). Compile-time bounds (text columns and lines, wrap width, canvas size) are derived from `BOARD_DISPLAY_WIDTH` / `BOARD_DISPLAY_HEIGHT`, using the larger dimension where rotation can swap them.

---

## 2. Embedded Firmware Philosophy: Deterministic State and Resource Bounds

E-paper devices on constrained microcontrollers cannot afford heap fragmentation, runaway task synchronization, or unpredictable latency.

### 2.1 Deterministic Finite State Concurrency

The application is an enumerated finite state machine driven by discrete events.

- **Single Direction of State Mutation**: Views are enumerated in `reader_view_t` ([`views.h`](./main/views/views.h)), including the Mushaf views when `VIEWS_HAS_MUSHAF` is set. Each view module exports one `view_def_t` (`enter`, `wake`, `render`, `event`, `leave`); [`views.c`](./main/views/views.c) dispatches to it. An event handler returns the next view id and never renders another view; on a change of view the dispatcher calls `leave` (when the module changes), `enter` and `render`. `VIEW_REQUEST_POWER_OFF` is a request to `main.c`, never a stored view. State changes only in the [`main.c`](./main/main.c) event loop, in response to events from bounded FreeRTOS queues. Views never call sleep or power functions.
- **Decoupled Interrupts and Callbacks**: ISRs, timer callbacks and BLE callbacks (NimBLE host task) never render, write flash, sleep, or make blocking bus calls; they signal the main task or queue a command for the `ble_sync` worker task. The button GPIO ISR only disables its interrupt and notifies the button task; the 10 ms button tick runs only while a button is active. In [`button.c`](./components/button/button.c), button transitions post `button_event_t` records to the bounded queue `s_btn_queue` (depth 16) with non-blocking `xQueueSend()`. `esp_timer_start_*` is never called from an ISR.
- **Settings Changed off the Main Task**: The `ble_sync` worker task changes settings only through the [`app_settings`](./components/app_settings/app_settings.c) setters, which persist the value and set a pending flag. The main loop calls `app_settings_take_pending_apply()` before dispatching the next event and then `app_settings_apply()`, so display rotation, fonts and button mapping change only on the main task.
- **Module Links without Includes**: Modules that must not include each other are connected at boot through listeners in [`app_links.c`](./main/app_links.c) (for example `text_pager` reports opens and offset changes; `book_library` persists them).
- **Deterministic Chord Actions**: Multi-button events (such as holding UP and DOWN together for 2 seconds to reboot) are evaluated in the timer tick callback (`button_timer_cb`), suppressing single click and long press emissions to guarantee clean reboot sequencing.

### 2.2 Asymmetric Power Lifecycle: Transient Execution and Persistent Sleep

Active CPU execution is a short event between long periods of sleep.

- **Transient Compute**: A wake runs the minimal path to the first rendered frame. The device then waits for input in automatic light sleep, and enters deep sleep after the inactivity timeout (user setting, default 5 s). Menu Power Off releases the power latch (true off; with USB connected it falls back to deep sleep).
- **State Preservation in Retention RAM**: State that must survive deep sleep lives in RTC slow memory (`RTC_DATA_ATTR`):
  - `s_rtc_last_view` ([`main.c`](./main/main.c)): active view.
  - Cursor positions: `s_rtc_menu_index`, `s_rtc_book_index`, `s_rtc_bookmark_index`, the nav hints page, and the Mushaf menu, jump, bookmark indices and sub-view.
  - Cached settings: `s_settings` in [`app_settings.c`](./components/app_settings/app_settings.c), read from NVS once per cold boot (`app_settings_read()`).
  - Page-turn counter and last frame: [`ui_display.c`](./components/ui_kit/ui_display.c).
- **Differential Screen Refresh from Retention RAM**: The previous frame `s_rtc_canvas` (`UI_CANVAS_BYTES`, equal to `BOARD_DISPLAY_BUFFER_SIZE` = `((W + 7) / 8) * H`) is kept in RTC memory when it fits (`UI_CANVAS_BYTES <= UI_RTC_CANVAS_MAX`, default 6144 bytes). After wake, `ui_display_flush()` compares it with the new canvas and calls `epaper_port_display_part_diff`. Panels whose frame does not fit use a plain partial refresh for the first frame after wake. Measured panel time on `waveshare_154`: partial refresh about 570 ms (BUSY), full refresh 1.5 to 2.4 s.
- **Periodic Full Refresh**: A full refresh runs every N page turns (user setting `full_refresh_n`, default 10, 0 = never). Views request page-turn refreshes with `ui_end(UI_REFRESH_PAGE_TURN)` (reading and Mushaf page turns only). The counter lives in RTC memory; any full refresh resets it.

### 2.3 Zero-Allocation Runtime and Predictable Footprints

Heap fragmentation is a primary failure mode in embedded systems with long uptime.

- **No Run-Time Heap Churn**: Continuous execution loops never allocate or free dynamic memory (`malloc`, `free`, `strdup`, `cJSON_Parse`). All buffers are sized at compile time.
- **Compile-Time Sized Buffers**: The display canvas (`UI_CANVAS_BYTES`) is allocated once at boot in `ui_display_init()`, from internal DMA-capable SRAM (`MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA`), falling back to internal SRAM and then PSRAM. Text layout buffers (`TEXT_LAYOUT_MAX_COLS`, `TEXT_LAYOUT_MAX_LINES`, `TEXT_LAYOUT_READ_CHUNK` in [`text_layout.h`](./components/text_engine/text_layout.h)), wrap buffers (`UI_WRAP_COLS_MAX`) and the Mushaf text buffer (`MUSHAF_TEXT_BUF_MAX`) are static and derived from the board's larger display dimension.
- **Binary Pruning**: Never compile dead assets, unreferenced fonts, or obsolete controller LUTs into the image. Unused font tables and display drivers are excluded at the Kconfig and CMake component level to conserve flash and SRAM.

### 2.4 Fault Transparency (Zero Silent Fallbacks)

Reliability requires fast, visible failure rather than hidden corruption.

- **Compile-Time Fault Transparency**: A missing board profile, mandatory property, or pin for an enabled peripheral halts compilation with an explicit `#error`. Size contracts are checked with `_Static_assert` (for example `UI_CANVAS_BYTES == BOARD_DISPLAY_BUFFER_SIZE`).
- **Fail Fast, Fail Loudly**: Empty catch blocks and ignored return codes (`esp_err_t`) are forbidden. If an SPI transaction, flash write, or ADC measurement fails, log the exact return code with `ESP_LOGE` and propagate the failure.
- **Zero Dummy Data**: Never synthesize metrics, battery readings, or timers. If battery ADC conversion fails, report 0 mV and render an error indicator.
- **Hardware Failure Manifestation**: When hardware fails (such as an uninitialized display or missing internal flash partition), the firmware stops the affected function, shows the status LED or display error banner, and logs the cause instead of executing undefined behavior.
- **Data Mismatch Is Visible**: Data built for another layout is rejected or logged, never drawn silently: `mushaf.bqm` must match `BQM_VERSION` and the board's display size; sprite pixels outside the reader body are counted and logged once.
- **Failure on Battery**: A panic or abort resets the GPIOs and may release the power latch, so on battery a crash can power the device off. After the latch is set, `ESP_ERROR_CHECK` is used only where power-off is an acceptable outcome. Non-critical failures (for example `esp_pm_configure`) log `ESP_LOGE` and continue in a defined reduced mode.

### 2.5 Sleep and Wake Invariants

1. **Boot order**: power latch, NVS, status LED, `text_pager_init()`, `app_links_init()`, `app_settings_load()` (reads settings and applies rotation, refresh interval, nav hints, body font, button mapping and pager font), storage, display init (`ui_display_init()`), `board_light_sleep_pins_init()`, then `esp_pm_configure()`.
2. **Deep sleep zeroes ordinary statics**: counters, caches and "loaded" flags that must survive a wake are `RTC_DATA_ATTR`. Everything else is assumed reset on every wake.
3. **Lazy loaders**: a "loaded" flag is set only after a successful read. A read that fails because NVS is not initialized is retried.
4. **No writes on the wake path**: opening, restoring or navigating writes nothing to flash unless state changed. Navigation state (reading offset, Mushaf cursor) is held as dirty state and flushed once at sleep entry; explicit user edits (bookmarks, settings) are written when made.
5. **Guard what was not initialized**: code that saves state at sleep entry checks that the subsystem is active this wake (for example `text_pager_is_book_open()`). Views open only what they need, when entered.
6. **Light sleep pins**: `BOARD_LIGHT_SLEEP_KEEP_PINS` lists every output and wake input whose configuration must not change in light sleep. No listed pin is reconfigured after `board_light_sleep_pins_init()`. Core components never call `gpio_sleep_sel_*`.
7. **Light sleep and SPI**: multi-step EPD sequences hold an `ESP_PM_NO_LIGHT_SLEEP` lock, released before every BUSY wait and on every return path.

### 2.6 UI Composition

- **Shared helpers only**: views draw through `ui_kit`: `ui_begin()` / `ui_end()` (prepare and flush), `ui_draw_header*()`, `ui_draw_footer*()`, `ui_draw_message()`, `ui_draw_list()` / `ui_draw_row()`, and `ui_pager_draw()` for long text (reader, nav hints, Mushaf text layers). Views do not call `Paint_*` for layout that a helper covers.
- **Fonts**: headers, footers, splash, power-off and the sync screen use Font12 (Font16 for splash titles). Body text (list rows, messages, long text) follows the font size setting through `ui_body_font()` and `ui_pager_body_style()` / `text_pager_get_style()`.
- **One geometry source**: the rectangle used to lay out text is the rectangle used to draw it (`ui_pager_layout_cfg()` and `ui_pager_body_rect()`).

---

## 3. Architecture and Code References

- Repository rules and agent workflow: [AGENTS.md](../AGENTS.md)
- Supported boards and profiles: [BOARDS.md](./BOARDS.md)
- Central board support package: [board_config.h](./components/board_config/include/board_config.h)
- Waveshare 1.54 board definition: [board_def.h](./boards/waveshare_154/board_def.h) and hooks: [board.c](./boards/waveshare_154/board.c)
- Power management and OCV curves: [power_mgr.c](./components/power_mgr/power_mgr.c)
- Settings storage and pending apply: [app_settings.c](./components/app_settings/app_settings.c); apply to subsystems: [app_settings_apply.c](./main/app_settings_apply.c)
- Display lifecycle, differential refresh, refresh policy: [ui_display.c](./components/ui_kit/ui_display.c)
- UI helpers: [ui_widgets.c](./components/ui_kit/ui_widgets.c), [ui_list.c](./components/ui_kit/ui_list.c), [ui_pager.c](./components/ui_kit/ui_pager.c), [ui_text.c](./components/ui_kit/ui_text.c), system screens [ui_screens.c](./main/ui_screens.c)
- Text sources and layout: [text_source.c](./components/text_engine/text_source.c), [text_layout.c](./components/text_engine/text_layout.c)
- Book pager and library: [text_pager.c](./components/reader_core/text_pager.c), [book_library.c](./components/reader_core/book_library.c); module links: [app_links.c](./main/app_links.c)
- View enumeration and dispatch: [views.h](./main/views/views.h), [views.c](./main/views/views.c)
- Main lifecycle and concurrency: [main.c](./main/main.c)
- Button event queue and tick gating: [button.c](./components/button/button.c)
- E-paper hardware port, LUT waveforms and PM lock: [epaper_port.c](./components/epaper/epaper_port.c)
- BLE opcodes and payload layouts: [ble_sync.h](./components/ble_sync/ble_sync.h)
- Mushaf data and rendering: [mushaf_reader.c](./components/mushaf_reader/mushaf_reader.c), [Mushaf_Reader.md](./Mushaf_Reader.md)
