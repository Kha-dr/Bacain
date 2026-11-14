# Agent Rules & Workflow (`AGENTS.md`)

These rules are mandatory for all agents and subagents operating in this repository.

## 1. Code Standards (Ponytail & Zero-Mock)

All agents operating in this repository must load and enforce the `ponytail` skills (`ponytail` on coding tasks, `ponytail-review` on change reviews, `ponytail-audit` on repo audits) and `antislop` skills (`antislop`, `antislop-code`, `antislop-ui`, `antislop-copywriting`, `antislop-human`, `antislop-layoutmobile`).

1. **Zero Mock / Zero Fake Data**:
   - Never implement mocks, dummy returns, simulated progress, or fake metrics (`Math.random()`, fake timers).
   - If a feature or driver is not implemented, set `this.available = false` and throw `new Error('<feature> not implemented')`.
2. **Zero Silent Fallback & Universal Fail-Fast**:
   - Never catch errors silently, suppress error codes, or fall back without logging and propagating the error. Empty `catch {}` blocks and unhandled return values are forbidden across all languages and layers.
   - Forbid weak dummy symbols (`__attribute__((weak))`): never provide weak fallback stubs that return `NULL`, `0`, or no-op defaults. Missing implementations must fail the build immediately at link time.
   - Forbid silent feature degradation: any uninitialized peripheral, missing configuration, or failed driver contract must log a hard error or assert. Code must never quietly degrade fidelity or fall back to an unrequested operational mode.
   - Strict parameter integrity: all enum constants, domain selectors, register configurations, and pin numbers passed to system or SDK APIs must be verified against actual hardware specifications. Never pass speculative values or out-of-bounds sentinels.
3. **Ponytail & Surgical Changes (Minimal Complete Change)**:
   - Load and enforce the `ponytail` skill (`full` level by default) on every coding task; end every reply with 1 to 2 lines stating what was skipped or not checked and any risk the user must know.
   - Make the smallest complete change that solves the issue. Avoid speculative abstractions, unused code, and unnecessary layers.
   - Complexity is technical debt: the deleted line is the safest line.
   - Surgical scope: every changed line must trace directly to the user's request. Never reformat, refactor, or alter adjacent code, comments, or styles outside the task.
   - Clean up only your own mess: remove imports, variables, or helpers that your own edit made unused. Never delete pre-existing dead code unless explicitly instructed.
4. **KISS (Keep It Stupid Simple)**:
   - Prefer the simplest workable solution in code, architecture, and documentation.
   - Resist speculative generalization: do not add unused CLI flags, speculative options, extra abstraction layers, or configuration toggles that have only one valid production state. Hardcode constants until multiple distinct consumers exist.
   - Avoid verbose explanations, unnecessary wrapper layers, and convoluted logic when a straightforward solution exists.
5. **Think Before Coding (Zero Silent Assumptions)**:
   - Never guess silently on ambiguous requirements: state assumptions explicitly before editing, or use `ask_question` (`/grill-me` interview style: walk down the decision tree one question at a time, with a `(Recommended)` option first) when design choices depend on user intent.
   - Push back on overcomplication: if a requested approach is unnecessarily complex or conflicts with hardware/protocol constraints, state the simpler alternative before writing code.
   - Define checkable success criteria before editing and verify each criterion against actual build or test output before reporting completion.
6. **User Ownership of Version Control & Atomic Commits**:
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
   - Hardware NVS is authoritative ground truth: on link establishment, query active hardware configuration (`GET_CONFIG` opcode 0x19, 16-byte response payload including full refresh interval at offset 15) before rendering controls or applying local caches. Use opcode 0x1A (`SET_REFRESH_INTERVAL`) to configure the full refresh page interval (0 = never, 1 to 100 pages).
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

## 4. Delegation Protocol (Orchestrator and Subagents)

### 4.1 Roles

- **Orchestrator**: understands the problem, makes every design decision, writes task cards, runs builds, makes commits (on user instruction), performs the final audit, talks to the user.
- **Subagent**: executes exactly one task card. Never designs, never explores beyond the card, never builds, never commits.

### 4.2 Triage: Delegate or Do It Directly

| Situation                                                 | Action                                         |
| :-------------------------------------------------------- | :--------------------------------------------- |
| Change touches 1 to 2 files and the location is known     | Orchestrator does it directly                  |
| Single factual question answerable with grep              | Orchestrator does it directly                  |
| Independent edits in 3 or more files                      | Delegate, one card per file group, in parallel |
| Read-only survey that would fill the orchestrator context | Delegate as a LOCATE card returning a table    |
| Design choice, trade-off, user decision, build, commit    | Never delegate                                 |

### 4.3 Locate, Decide, Delegate

1. **Locate**: the orchestrator finds the exact files, functions and line ranges (grep) before writing a card.
2. **Decide**: the orchestrator settles names, signatures, ordering, error handling and edge cases.
3. **Delegate**: the card carries those decisions. A subagent never chooses between alternatives.

### 4.4 Task Size Limits

- One card = one concern.
- IMPLEMENT: at most 2 files to edit, 1 to 3 functions, expected diff under 100 lines. Larger work is split into sequential cards.
- LOCATE: read-only, answers a numbered list of questions, at most 6 files to read.
- Never combine LOCATE and IMPLEMENT in one card.
- Model: `flash` for all cards. Reasoning-heavy models are for the orchestrator only.

### 4.5 Task Card (mandatory format)

    TYPE: IMPLEMENT | LOCATE
    GOAL: <one sentence>
    FILE SCOPE (edit): <path>:<line range>, ...
    READ ONLY: <path>:<line range>, ...   (nothing else may be opened)
    NOT IN SCOPE: <files or subsystems that must not be opened or changed>
    CONTEXT: <pasted code excerpts and facts the subagent needs>
    CONTRACT: <exact signatures, behavior, invariants, error handling>
    DONE WHEN: <checkable conditions, e.g. "grep X returns 0 matches">
    RETURN: unified diff + at most 5 lines of notes
            | table: question, answer, file:line
    BUDGET: at most 6 file reads, at most 15 tool calls
    STOP: return "BLOCKED: <reason>" if a needed file is outside scope,
          the contract is ambiguous, a design choice is needed, the budget
          is exhausted, or an environment error occurs.
    (Subagent Charter, section 4.6, appended verbatim)

Paste the relevant code into CONTEXT instead of asking the subagent to find it.

### 4.6 Subagent Charter (appended to every card)

1. This card is the whole task. Do not read AGENTS.md or any file outside FILE SCOPE and READ ONLY.
2. The design is final. Do not re-derive, question or extend it. If it looks wrong, return `BLOCKED` with the reason.
3. Do not summarize the codebase, explain unchanged code or restate the task.
4. No builds, no git write commands, no web access, no permission changes.
5. On the first tool or environment error (permission denied, `EPERM`, missing pipe, unreadable `.gitconfig`), stop and return the exact command and error.
6. Return exactly the RETURN format, nothing else.

### 4.7 Handling Results

- Diff edits a file outside FILE SCOPE: discard the whole diff, tighten the card, re-dispatch once. Second failure: the orchestrator does the task directly.
- `BLOCKED`: the orchestrator resolves it (decides, widens scope explicitly, or asks the user) and re-dispatches an updated card.
- Budget exhausted: treat as a scoping error and split the card.

### 4.8 Builds

1. Only the orchestrator builds (section 7), one build at a time, and waits for the result.
2. Compiler errors go back to the subagent as `file:line: message` only, in a new card scoped to that file.
3. If a build has not finished after 10 minutes, report it instead of restarting it.
4. Build every affected target: `waveshare_154`, plus the Mushaf target when Mushaf or shared code changes.

### 4.9 Verification

1. The implementer never verifies its own change.
2. Verification is a separate LOCATE card that receives the diff, the CONTRACT and DONE WHEN, and returns pass or fail per condition with `file:line`.
3. Agents cannot press buttons, measure current or watch the panel. Any timing, current or visual result not taken from a user-supplied log is labeled `estimated` with its basis. Hardware tests are listed for the user to run.

### 4.10 Parallelism

At most 3 subagents at once. FILE SCOPE sets must not overlap. READ ONLY sets may overlap.

### 4.11 Sandbox and Windows Tooling

1. Subagents never request elevated or unsandboxed execution.
2. Only the orchestrator runs host-level commands: Docker builds and user-requested git commits, under the user's own git identity.
3. Git in the sandbox is read-only (`status`, `diff`, `log`, `show`). If the global config is unreadable: `$env:GIT_CONFIG_GLOBAL="NUL"; git <subcommand>`.

### 4.12 Report Contract (orchestrator to user)

Every implementation report ends with:

- build command and result per target (errors, warning count)
- files changed with line ranges
- unverified assumptions, open risks, and hardware tests for the user

## 5. Communication, Style & Anti-Slop Standards

All agents and subagents must load and enforce the antislop skills (`antislop`, `antislop-code`, `antislop-ui`, `antislop-copywriting`, `antislop-human`, `antislop-layoutmobile`):

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

| Subsystem               | Authoritative Files                                                                             | Semantics & Boundaries                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   |
| :---------------------- | :---------------------------------------------------------------------------------------------- | :------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Board Definition**    | `firmware/boards/<board_id>/board_def.h`<br>`firmware/boards/<board_id>/board.c`                | Defines physical pins, active polarities, power rails, display LUTs. Zero board defines allowed in core components. Board lists pins whose configuration must not change in light sleep (outputs and wake inputs) via `BOARD_LIGHT_SLEEP_KEEP_PINS`; applied by `board_light_sleep_pins_init()`.                                                                                                                                                                                                         |
| **Power & Sleep**       | `firmware/components/power_mgr/power_mgr.c`                                                     | Deep sleep entry and wake. RTC IO domain must remain `ESP_PD_OPTION_ON` for wake pull-ups. GPIO holds deferred to sleep entry. Button release loops bounded. Menu Power Off releases the latch (true off; only the GPIO 18 button powers on in hardware). With USB connected the rail stays up and it falls back to deep sleep. `esp_pm_configure()` runs only after `board_light_sleep_pins_init()`; core components never call `gpio_sleep_sel_*`. A pin reconfigured after init must be re-protected. |
| **Display & LUTs**      | `firmware/components/epaper/epaper_port.c`<br>`firmware/components/board_config/CMakeLists.txt` | EPD SPI bus and refresh sequencing. Archive linking requires explicit symbol extraction (`-u board_epd_get_lut_partial -u board_power_pre_sleep`). EPD SPI transfers hold an `ESP_PM_NO_LIGHT_SLEEP` lock, released before the BUSY wait.                                                                                                                                                                                                                                                                |
| **Status LED**          | `firmware/components/status_led/status_led.c`                                                   | Wake event must execute on a one-shot background task (0 ms wake delay). Sleep event is synchronous (150 ms) for visibility only                                                                                                                                                                                                                                                                                                                                                                         |
| **UI Views**            | `firmware/main/views/`<br>`firmware/main/main.c`                                                | Views never call sleep or power functions; they return VIEW_REQUEST_POWER_OFF and main.c performs it.                                                                                                                                                                                                                                                                                                                                                                                                    |
| **Storage & Library**   | `firmware/components/reader_core/`<br>`firmware/components/sdcard/`                             | SD card has priority; falls back to internal SPIFFS / Flash storage.                                                                                                                                                                                                                                                                                                                                                                                                                                     |
| **BLE Synchronization** | `firmware/components/ble_sync/`                                                                 | Binary frame transport for file upload and config sync. Companion client executes heavy processing. Opcode 0x19 returns 16-byte GET_CONFIG; opcode 0x1A sets full refresh interval.                                                                                                                                                                                                                                                                                                                      |

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

3. **Configuration**:
   Configuration: sdkconfig.defaults is the single source of truth. The build regenerates each target's sdkconfig when the defaults are newer. Local developer settings go in the gitignored sdkconfig.local. Confirm any option change in build/<board>/config/sdkconfig.h.
