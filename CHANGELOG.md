# Changelog

## [0.5.0](https://github.com/khad1r/Bacain/compare/bacain-v0.4.0...bacain-v0.5.0) (2026-11-10)


### Features

* add central configuration for BacaIn and enhance onboarding experience ([b118869](https://github.com/khad1r/Bacain/commit/b118869a7b7bf28fb97f78b2b21d44a52f6f91e3))
* add GitHub Release fetching functionality and enhance firmware packaging options ([814a309](https://github.com/khad1r/Bacain/commit/814a3093565d1e6a2b3dc3c1e9e61af6707d4303))
* Add QR code generation and configuration options ([4ed1c52](https://github.com/khad1r/Bacain/commit/4ed1c520f235a0e0039b72c5f31544d50f564303))
* **ble:** add GATT OpCodes 0x18 (status LED) and 0x19 (two-way config query) ([91fb72d](https://github.com/khad1r/Bacain/commit/91fb72d5f08ec452032a494bd8d8ada9d9c1603a))
* **board_config:** decouple hardware definitions and enforce compile-time error guards ([7b45d89](https://github.com/khad1r/Bacain/commit/7b45d895d7e0e8a84f2b9f1cedff5d8d25092a57))
* **button:** invert button functionality and update button IDs ([fca0f5f](https://github.com/khad1r/Bacain/commit/fca0f5fecdad920b17562ccfc2d41be291e3acd2))
* Enhance button handling, improve book library caching, and optimize UI memory allocation ([518a109](https://github.com/khad1r/Bacain/commit/518a10954dd103f2ea932f5eae5022bbc5db32b0))
* **epaper:** isolate panel waveform LUTs to board.c and decouple controller timings ([4f29551](https://github.com/khad1r/Bacain/commit/4f29551db5aec11e757d427a3e5214ac85dd5e3a))
* **firmware:** add form-feed page break, UI glyphs, and bookmark screen navigation ([c5a7927](https://github.com/khad1r/Bacain/commit/c5a792752c710813007c7918614c27c374c64220))
* **firmware:** set 12px default font, splash-to-menu boot sequence, and clean 7-item menu ([bdfedce](https://github.com/khad1r/Bacain/commit/bdfedce585d1e32c16d9bf7253288ec5c12fb8dc))
* implement Mini-E-Reader firmware, Web Bluetooth PWA, and SD template ([1bfc5a2](https://github.com/khad1r/Bacain/commit/1bfc5a2162c3b931c1a00652dd82500d70e24cca))
* implement navigation hints toggle in UI and BLE sync ([5853dc9](https://github.com/khad1r/Bacain/commit/5853dc9c612c757d961333991bf84efbd43b3fbd))
* initial release of BacaIn firmware and Web Bluetooth PWA ([3b1a949](https://github.com/khad1r/Bacain/commit/3b1a9498aecb012de490b3f890ccf85efe349f4a))
* **main:** implement NVS settings for rotation and button inversion ([fca0f5f](https://github.com/khad1r/Bacain/commit/fca0f5fecdad920b17562ccfc2d41be291e3acd2))
* **power:** add 16x ADC multisampling, 1S Li-ion OCV curve, and status LED driver ([2e079ff](https://github.com/khad1r/Bacain/commit/2e079ff06d7458a9e8ad9c523e01c9f046953938))
* rebrand project and BLE advertisement to BacaIn ([4dd7b37](https://github.com/khad1r/Bacain/commit/4dd7b370b3064eb785146f127b7d4e5664184d90))
* Refactoring web.Add initial flasher HTML page for ESP32-S3 firmware flashing tool ([88dcc86](https://github.com/khad1r/Bacain/commit/88dcc860c5d61e69c46a631a221573552a858acc))
* **web:** add client-side epub/fb2 ingestion pipeline with chapter page breaks ([c490e4b](https://github.com/khad1r/Bacain/commit/c490e4b16b83636e254d7a2e72196a318b0796fa))
* **web:** add settings for screen orientation and button inversion ([fca0f5f](https://github.com/khad1r/Bacain/commit/fca0f5fecdad920b17562ccfc2d41be291e3acd2))


### Bug Fixes

* deep sleep & full refresh problem after decoupling; ([2665ac3](https://github.com/khad1r/Bacain/commit/2665ac352aa597ed04225d9a6b12ab51d9a8b718))
* **epaper:** adjust image rotation handling ([fca0f5f](https://github.com/khad1r/Bacain/commit/fca0f5fecdad920b17562ccfc2d41be291e3acd2))
* **esp-flasher:** enhance device selection for multi-board releases ([fca0f5f](https://github.com/khad1r/Bacain/commit/fca0f5fecdad920b17562ccfc2d41be291e3acd2))
* gate mushaf view compilation and invalidate stale CMake cache ([2665ac3](https://github.com/khad1r/Bacain/commit/2665ac352aa597ed04225d9a6b12ab51d9a8b718))
* **mvp:** harden real drivers, resolve concurrency/SPI bugs, add smoke verification ([d903a2f](https://github.com/khad1r/Bacain/commit/d903a2fcf38f4d4855e13f30a84b6d8f19c9074a))
* **power_mgr:** update button pin definitions ([fca0f5f](https://github.com/khad1r/Bacain/commit/fca0f5fecdad920b17562ccfc2d41be291e3acd2))
* **reader_ui:** manage display rotation in UI ([fca0f5f](https://github.com/khad1r/Bacain/commit/fca0f5fecdad920b17562ccfc2d41be291e3acd2))
* **status_led:** move LED wake logic to a separate task and simplify trigger function ([a97f4a9](https://github.com/khad1r/Bacain/commit/a97f4a9bebf7457440bb8d2d9557a768740bdd10))


### Performance Improvements

* **power,button:** add RTC battery voltage caching and immediate button wake dispatch ([fb2acf1](https://github.com/khad1r/Bacain/commit/fb2acf1290f3bd528a4ab5a8466b5804217007de))


### Refactoring

* **AGENTS.md:** enhance error handling and delegation guidelines ([e144a95](https://github.com/khad1r/Bacain/commit/e144a957fd9d3f21510b7c97703320c091868bc4))
* **epaper:** prune unused fonts Font20 and Font24 ([d7f1800](https://github.com/khad1r/Bacain/commit/d7f180084a8a664760e4efffbc1e383e0f4f052b))
* **firmware:** centralize BSP pinout into board_config and update peripherals ([c56196b](https://github.com/khad1r/Bacain/commit/c56196bd8c7ad6431ebee88fbc1303c051a758ba))
* **firmware:** remove hardcoded display dimensions and integrate board power hooks ([8f22dee](https://github.com/khad1r/Bacain/commit/8f22deee7e74941c8ab3607b3d5211b7db405677))
* reorganize project structure into firmware and web ([daf5ae1](https://github.com/khad1r/Bacain/commit/daf5ae19e0f619d8ac5e2d8d43c26f192702e4f7))
* **views,main:** decouple view wake handlers and suppress wake LED delay ([691d018](https://github.com/khad1r/Bacain/commit/691d0183efb06bdeae271afc39c2680caaae2693))
* **web:** implement modular web components and GATT telemetry binding ([869c627](https://github.com/khad1r/Bacain/commit/869c627598e5994a0e30f1b3524a640d82241f85))


### Documentation

* add firmware Docker build README, ignore web binary artifacts, and complete BacaIn rebranding ([35626f6](https://github.com/khad1r/Bacain/commit/35626f6452339dfc1db47807cfa0d33e68ea4a5c))
* **firmware:** add Docker-first build guide and flashing instructions ([50f4e72](https://github.com/khad1r/Bacain/commit/50f4e72b9b42f34de5a5e29324a84199f34112d5))
* **firmware:** document board architecture, compile-time error guards, and hooks ([ab51c61](https://github.com/khad1r/Bacain/commit/ab51c61d23ab9d5c1a9a2aa4895142e1acd6a57d))


### Miscellaneous

* add web/firmware/*.bin to .gitignore ([ed15e48](https://github.com/khad1r/Bacain/commit/ed15e4870aaa160fe316c26808bbd281b1da0f7b))
* cleanup ([44ab5ac](https://github.com/khad1r/Bacain/commit/44ab5ac75dbd5551e4c8353f97358fc2175e2ada))
* **firmware:** prune package_firmware.py to minimal ponytail interface ([d8581bf](https://github.com/khad1r/Bacain/commit/d8581bf21c6fb431379564a4a8e03bdbc6347c09))
* Refactor AGENTS.md and README.md for clarity and consistency; enhance BOARDS.md with compliance notes; introduce RULES.md for hardware engineering principles ([64bc5b7](https://github.com/khad1r/Bacain/commit/64bc5b7785548868d100d0ba46f69b53ec0b7c8f))
* Refactor firmware documentation and code structure for BacaIn e-reader ([11f880a](https://github.com/khad1r/Bacain/commit/11f880a91f3b45189c3a90f4510dc0e270262a51))
* replace release drafter with release please ([4ecf051](https://github.com/khad1r/Bacain/commit/4ecf0518952f5b3727258ac4a84e58354efcbc27))
* tidying the projects ([b83c2fe](https://github.com/khad1r/Bacain/commit/b83c2fe33de867850d0f8c5ff70233a1b05240e8))
* vscode extension ([2356d89](https://github.com/khad1r/Bacain/commit/2356d895236d7ee755decf01375cfdb9c0032718))
