# Agent Rules & Workflow (`AGENTS.md`)

These rules are mandatory for all agents and subagents operating in this repository.

## 1. Code Standards (Ponytail & Zero-Mock)

1. **Zero Mock / Zero Fake Data**:
   - Never implement mocks, dummy returns, simulated progress, or fake metrics (`Math.random()`, fake timers).
   - If a feature or driver is not implemented, set `this.available = false` and throw `new Error('... not implemented')`.
2. **Zero Silent Fallback**:
   - Never catch errors silently or fall back without logging and throwing. Empty `catch {}` blocks are forbidden.
3. **Ponytail (Minimal Complete Change)**:
   - Make the smallest complete change that solves the issue. Avoid speculative abstractions, unused code, and unnecessary layers.
4. **KISS (Keep It Stupid Simple)**:
   - Prefer the simplest workable solution in code, architecture, and documentation.
   - Avoid verbose explanations, unnecessary wrapper layers, and convoluted logic when a straightforward solution exists.

## 2. Multi-Subagent Protocol

To prevent context exhaustion and hallucination:

1. **Scoped Granularity**:
   - Delegate tasks to subagents scoped to 1 to 4 files only.
   - Keep file scopes between parallel subagents strictly non-overlapping.
   - Explicitly list target files (`FILE SCOPE`) and forbid edits outside that list.
2. **Contract Specifications**:
   - Provide exact target functions, line references, and contracts.
   - Use `Model: 'flash'` for concrete implementation and verification tasks.
3. **Proactive Delegation (No Manual File Traversal)**:
   - Delegate any task that can be handled by a subagent, including exploratory codebase searches and reading file subsets to understand subsystems.
   - Do not iterate through files manually in the main context before delegating. Formulate the scope and objective, then delegate exploration directly.
4. **Independent Verification**:
   - Implementors must not verify their own changes.
   - Every change must be verified by an independent verifier subagent or automated test script without mocks.
5. **Main Agent Final Audit**:
   - The main agent conducts the final audit directly by inspecting files, reviewing `git diff`, and running test scripts before completing the task.

## 3. Communication & Style

1. **Direct Technical Language**:
   - Use concise technical terms. Avoid marketing buzzwords and filler phrases.
   - Reference exact filenames, line numbers, function names, and measured numbers.
2. **Clean Identifiers & Copy**:
   - Use functional variable and constant names (`config`, `state`, `options`).
   - Write factual comments and documentation without promotional adjectives.
3. **Calm Punctuation**:
   - Avoid ellipses (`...`), repeated exclamation points (`!!`), and em dashes (`—`). Use commas, colons, or parentheses.

## 4. Antislop Design & Hygiene Standards

All agents and subagents must enforce antislop skills (`antislop`, `antislop-code`, `antislop-ui`, `antislop-copywriting`):

1. **Code & Comment Hygiene (`antislop-code`)**:
   - Forbid generic AI commentary (e.g. `// Function to do X`, `// Helper function`, `// Set variable`).
   - Retain only comments detailing hardware timings, register specifications, FreeRTOS synchronization semantics, and memory boundary constraints.
2. **UI & Layout (`antislop-ui` / `antislop-human`)**:
   - Avoid artificial generic templates, arbitrary padding, and unbalanced typography.
   - Design with deliberate hierarchy, intentional negative space, clear visual alignment, and tactile feedback.
3. **Copy & Text (`antislop-copywriting`)**:
   - Never use promotional adjectives, vague filler, or hyperbolic descriptions.
   - Keep UI labels, headers, and documentation direct, purposeful, and human-crafted.

