# Agent Rules & Workflow (`AGENTS.md`)

These rules are mandatory for all agents and subagents operating in this repository.

## 1. Code Standards (Ponytail & Zero-Mock)

1. **Zero Mock / Zero Fake Data**:
   - Never implement mocks, dummy returns, simulated progress, or fake metrics (`Math.random()`, fake timers).
   - If a feature or driver is not implemented, set `this.available = false` and throw `new Error('<feature> not implemented')`.
2. **Zero Silent Fallback & Universal Fail-Fast**:
   - Never catch errors silently, suppress error codes, or fall back without logging and propagating the error. Empty `catch {}` blocks and unhandled return values are forbidden across all languages and layers.
   - Forbid weak dummy symbols (`__attribute__((weak))`): never provide weak fallback stubs that return `NULL`, `0`, or no-op defaults. Missing implementations must fail the build immediately at link time.
   - Forbid silent feature degradation: any uninitialized peripheral, missing configuration, or failed driver contract must log a hard error or assert. Code must never quietly degrade fidelity or fall back to an unrequested operational mode.
   - Strict parameter integrity: all enum constants, domain selectors, register configurations, and pin numbers passed to system or SDK APIs must be verified against actual hardware specifications. Never pass speculative values or out-of-bounds sentinels.
3. **Ponytail (Minimal Complete Change)**:
   - Make the smallest complete change that solves the issue. Avoid speculative abstractions, unused code, and unnecessary layers.
   - Complexity is technical debt: the deleted line is the safest line.
4. **KISS (Keep It Stupid Simple)**:
   - Prefer the simplest workable solution in code, architecture, and documentation.
   - Resist speculative generalization: do not add unused CLI flags, speculative options, extra abstraction layers, or configuration toggles that have only one valid production state. Hardcode constants until multiple distinct consumers exist.
   - Avoid verbose explanations, unnecessary wrapper layers, and convoluted logic when a straightforward solution exists.
5. **User Ownership of Version Control & Atomic Commits**:
   - Never commit to git autonomously without explicit user instruction. Version control history belongs entirely to the human engineer.
   - Stage and commit changes atomically by concern (for example, separate commits for font pruning, power tuning, UI layouts, or protocol changes). Never combine distinct concerns into a single dirty working tree commit.

## 2. Companion Client Architecture & Protocols

For hardware design, electrical constraints, and embedded firmware rules, refer to [firmware/RULES.md](./firmware/RULES.md).

1. **Asymmetric Complexity Partitioning**:
   - Heavy computation belongs on the companion client: container extraction, schema validation, document parsing, string transformations, text layout wrapping, and rich filtering execute on the client where CPU, RAM, and display framerates are abundant.
   - The embedded device receives normalized payloads: only pre-processed raw planar streams, plain text, or fixed-width chunks go to the device. The device never parses complex archives or executes heavy regular expressions.
   - Offload complex configuration and file management from constrained device screens to the companion client.
2. **Transport Protocol Compactness**:
   - Strict binary framing: avoid text-based serialization (JSON, XML) over constrained wireless or serial links.
   - Use compact binary structs with explicit little-endian byte orders and single-byte command opcodes.
3. **Device NVS Ground Truth & Immediate Persistence**:
   - Hardware NVS is authoritative ground truth: on link establishment, query active hardware configuration (`GET_CONFIG` opcode 0x19) before rendering controls or applying local caches.
   - Immediate non-volatile persistence: every configuration mutation pushed by the client must commit immediately to device non-volatile storage, ensuring standalone continuity across resets and sleep cycles.
4. **High Signal-to-Noise UI Hygiene**:
   - Eliminate zero-metric clutter: suppress uninformative indicators for unstarted items (hide '0% read' on untouched items). Display metadata only until measurable progress occurs.
   - Delete dead and destructive actions: remove controls that sever active transport links abruptly without a managed lifecycle (for example, delete remote 'Sleep Now' triggers that sever transports). Expose policy controls (such as auto-sleep timeouts) rather than remote execution triggers.
   - Enforce hardware interlocks: dynamically disable controls when the corresponding hardware peripheral is missing (for example, grey out MicroSD selection when no card is inserted).

## 3. Technical Documentation Standards

1. **Single Source of Truth (DRY Documentation)**:
   - Reference over duplication: link across documents rather than duplicating pinout tables, feature listings, register maps, or font tables. Maintain one canonical source file and cross-reference it via relative markdown links.
2. **Single Consolidated Deliverable**:
   - Document unified production deliverables: document unified factory binaries (offset 0x0) rather than fragmented multi-offset partition flashes (`0x1000`, `0x8000`, `0x10000`).
3. **Radical Parsimony & Calm Technical Copy**:
   - Zero marketing fluff: forbid promotional adjectives ('revolutionary', 'blazing fast', 'seamless', 'ultimate'). Present only verifiable facts, pin numbers, byte offsets, timing delays, and microamp currents.
   - Eliminate AI narrative commentary: remove decorative headers, introductory pleasantries, and inflated labels (such as 'Pro Tips' or 'Advanced:').
   - Calm punctuation: ban repeated exclamation marks (`!!`), ellipses (`...`), and em dashes (`—`). Use colons, commas, and parentheses for structural separation.

## 4. Multi-Subagent & AI Reasoning Protocol

To prevent context exhaustion, cognitive degradation, and hallucination:

1. **Context Protection & KISS Triage (Orchestrator Boundary)**:
   - Triage before delegation (KISS): If a bug, fix, or feature touches 1 to 2 files and has a localized solution, the primary orchestrator must implement and verify it directly. Do not spawn subagents for trivial edits that take seconds.
   - Delegate when:
     - exploring unfamiliar subsystems across multiple files, parallel multi-file refactoring, or running long-running build/test scripts.
     - If a solution is stupid simple, do it yourself directly. If a task can be broken down into multiple distinct parts, delegate them.
   - For exploratory tasks, the orchestrator formulates the specific objective and target files, then delegates exploration.
2. **Strict Local Ground Truth (Anti-Rabbit-Hole)**:
   - The repository is 100% self-contained. Never use web search or external API queries for board pinouts, hardware schematics, or drivers.
   - Hardware ground truth lives strictly in `firmware/boards/<board_id>/board_def.h` and `board.c`. If a pin, polarity, or rail is defined there, it is authoritative.
3. **Scoped Granularity**:
   - Delegate tasks to subagents scoped to 1 to 4 files only.
   - Keep file scopes between parallel subagents strictly non-overlapping.
   - Explicitly list target files (`FILE SCOPE`) and forbid edits outside that list.
4. **Contract Specifications**:
   - Provide exact target functions, line references, and contracts.
   - Use `Model: 'flash'` for concrete implementation and verification tasks.
5. **Independent Verification (Separation of Concerns)**:
   - Implementors must never verify their own changes: an agent that writes code possesses the same cognitive blind spots that generated the bugs. A task is not complete when the implementor claims it works.
   - Verification requires runtime execution without mocks: static syntax checks (`node -c`, lint passes) are insufficient. Verification requires actual runtime execution (headless browser DOM execution, clean container compilation, physical assertion passes).
   - Dedicated verifiers: every non-trivial change must be verified by an independent verifier subagent or automated test script operating without mocks.
6. **Main Agent Final Audit**:
   - The main agent conducts the final audit directly by inspecting files, reviewing `git diff`, and running test scripts before completing the task.

## 5. Communication, Style & Anti-Slop Standards

All agents and subagents must enforce antislop skills (`antislop`, `antislop-code`, `antislop-ui`, `antislop-copywriting`):

1. **Direct Technical Language & Dense Status Updates**:
   - Use concise technical terms: avoid marketing buzzwords, conversational filler, and self-evident status narratives.
   - Reference exact filenames, line numbers, function names, and measured numbers.
2. **Clean Identifiers & Copy**:
   - Use functional variable and constant names (`config`, `state`, `options`).
   - Write factual comments and documentation without promotional adjectives.
3. **Calm Punctuation**:
   - Avoid ellipses (`...`), repeated exclamation points (`!!`), and em dashes (`—`). Use commas, colons, or parentheses.
4. **Code & Comment Hygiene (`antislop-code`)**:
   - Forbid generic AI commentary (such as `// Function to do X`, `// Helper function`, `// Set variable`).
   - Retain only comments detailing hardware timings, register specifications, FreeRTOS synchronization semantics, and memory boundary constraints.
5. **UI & Layout (`antislop-ui` / `antislop-human`)**:
   - Avoid artificial generic templates, arbitrary padding, and unbalanced typography.
   - Design with deliberate hierarchy, intentional negative space, clear visual alignment, and tactile feedback.
6. **Copy & Text (`antislop-copywriting`)**:
   - Never use promotional adjectives, vague filler, or hyperbolic descriptions.
   - Keep UI labels, headers, and documentation direct, purposeful, and human-crafted.

## 6. Architecture & Subsystem Directory

Any agent navigating this codebase must consult this directory before exploring files:

| Subsystem               | Authoritative Files                                                                             | Semantics & Boundaries                                                                                                                                       |
| :---------------------- | :---------------------------------------------------------------------------------------------- | :----------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Board Definition**    | `firmware/boards/<board_id>/board_def.h`<br>`firmware/boards/<board_id>/board.c`                | Defines physical pins, active polarities, power rails, display LUTs. Zero board defines allowed in core components. Board lists outputs that keep their level in light sleep (`BOARD_LIGHT_SLEEP_KEEP_PINS`); applied by `board_light_sleep_pins_init()`. |
| **Power & Sleep**       | `firmware/components/power_mgr/power_mgr.c`                                                     | Deep sleep entry and wake. RTC IO domain must remain `ESP_PD_OPTION_ON` for wake pull-ups. GPIO holds deferred to sleep entry. Button release loops bounded. Menu Power Off releases the latch (true off; only the GPIO 18 button powers on in hardware). With USB connected the rail stays up and it falls back to deep sleep. `esp_pm_configure()` runs only after `board_light_sleep_pins_init()`; core components never call `gpio_sleep_sel_*`. A pin reconfigured after init must be re-protected. |
| **Display & LUTs**      | `firmware/components/epaper/epaper_port.c`<br>`firmware/components/board_config/CMakeLists.txt` | EPD SPI bus and refresh sequencing. Archive linking requires explicit symbol extraction (`-u board_epd_get_lut_partial -u board_power_pre_sleep`).           |
| **Status LED**          | `firmware/components/status_led/status_led.c`                                                   | Wake event must execute on a one-shot background task (0 ms wake delay). Sleep event is synchronous (150 ms) for visibility only                             |
| **UI Views**            | `firmware/main/views/`<br>`firmware/main/main.c`                                                | Views never call sleep or power functions; they return VIEW_REQUEST_POWER_OFF and main.c performs it.                                                       |
| **Storage & Library**   | `firmware/components/reader_core/`<br>`firmware/components/sdcard/`                             | SD card has priority; falls back to internal SPIFFS / Flash storage.                                                                                         |
| **BLE Synchronization** | `firmware/components/ble_sync/`                                                                 | Binary frame transport for file upload and config sync. Companion client executes heavy processing.                                                          |

## 7. Build & Verification Cookbook

Always execute builds inside Docker to prevent toolchain drift:

1. **Fast Incremental Verification (Default)**:

   ```bash
   docker-compose -f firmware/compose.yml run --rm -e BOARD=waveshare_154 firmware
   ```

   _Recompiles only modified files in 5 to 10 seconds. Use for all iterative code verification._

2. **Cold Clean Build (Cache Wipe)**:
   ```bash
   docker-compose -f firmware/compose.yml run --rm -e BOARD=waveshare_154 clean-build
   ```
   _Wipes the `build/` directory and recompiles all 1,143 targets from scratch. Use only when validating configuration signature isolation or diagnosing stale cache corruption._
