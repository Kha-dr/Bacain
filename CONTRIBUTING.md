# Contributing to BacaIn

Thank you for contributing to BacaIn. By submitting a pull request to this repository, you agree to the following terms based on the directory you modify.

## 1. Board Profiles (`firmware/boards/`)

Before adding a new board, follow the hardware architecture, registration steps (`board_def.h`, `board.c`, `boards.json`, `board_config/Kconfig`, `board_config/CMakeLists.txt`), and bring-up checks in [firmware/BOARDS.md](firmware/BOARDS.md).

- New or updated board profiles under `firmware/boards/` (and their catalog entries in `firmware/boards/boards.json` and `firmware/BOARDS.md`) are licensed under **GPL-3.0** (`firmware/LICENSE`).
- You retain copyright ownership of your board profile code.
- Board profiles registered in `firmware/boards/boards.json` are built and published as open-source GPL-3.0 firmware binaries by `.github/workflows/firmware_ci.yml`.
- Any third-party display LUTs or vendor initialization constants included in a board profile must comply with GPL-3.0 and be documented in `THIRD_PARTY_NOTICES.md`.

## 2. Core Firmware, Web Companion, and Documentation

For contributions outside `firmware/boards/` (including `firmware/main/`, `firmware/components/` such as `board_config/Kconfig`, `board_config/CMakeLists.txt`, or `epaper/` drivers, `web/`, and documentation):

1. Your contribution is licensed to the public under the directory's applicable license:
   - `firmware/`: **GPL-3.0** (`firmware/LICENSE`)
   - `web/`: **AGPL-3.0** (`web/LICENSE`)
   - `assets/` and documentation (`*.md`): **CC BY-NC-SA 4.0** (`assets/LICENSE`)
2. **Dual-Licensing Grant (CLA):** Because `khadr.dev` offers commercial licenses alongside the open-source licenses above, by submitting a pull request outside `firmware/boards/`, you also grant `khadr.dev` a perpetual, worldwide, non-exclusive, royalty-free license to distribute and relicense your contribution under both the repository's public licenses and separate commercial licenses.
3. You confirm that your contribution is your original work (or that you have the right to grant these licenses) and that any third-party code is clearly identified.
