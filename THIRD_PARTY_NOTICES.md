# Third-Party Notices

This repository includes or references third-party software and external data. Each component remains subject to its respective license and copyright notice.

## Firmware Components (`firmware/`)

| Component / Files                                                                                                                                                                      | Author / Upstream                               | License                       | Notes                                                |
| :------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | :---------------------------------------------- | :---------------------------- | :--------------------------------------------------- |
| `firmware/components/epaper/GUI_Paint.c`<br>`firmware/components/epaper/GUI_Paint.h`                                                                                                   | Waveshare Electronics                           | MIT                           | Modified for BacaIn                                  |
| `firmware/components/epaper/Fonts/font8.c`<br>`firmware/components/epaper/Fonts/font12.c`<br>`firmware/components/epaper/Fonts/font16.c`<br>`firmware/components/epaper/Fonts/fonts.h` | STMicroelectronics (2014, MCD Application Team) | BSD-3-Clause                  | Retain copyright and disclaimer in file headers      |
| `firmware/components/epaper/epaper_port.c`<br>`firmware/components/epaper/epaper_port.h`<br>`firmware/components/epaper/DEV_Config.h`<br>`firmware/boards/waveshare_154/`              | Adapted from Waveshare `ESP32-S3-ePaper-1.54`   | Upstream Waveshare terms      | Display initialization, LUT constants, and port glue |
| `firmware/components/button/multi_button.c`<br>`firmware/components/button/multi_button.h`                                                                                             | Zibin Zheng (2016)                              | Upstream `MultiButton` notice | Retain header notice                                 |
| `espressif/cjson` (`firmware/dependencies.lock`)                                                                                                                                       | Dave Gamble / Espressif                         | MIT                           | Managed component pulled at build time               |
| ESP-IDF and NimBLE                                                                                                                                                                     | Espressif Systems / Apache Mynewt               | Apache-2.0                    | Linked at build time                                 |

## Web Companion (`web/`)

| Component / Dependency                         | Author / Upstream | License    | Notes                                                                   |
| :--------------------------------------------- | :---------------- | :--------- | :---------------------------------------------------------------------- |
| `esptool-js` (`web/components/esp-flasher.js`) | Espressif Systems | Apache-2.0 | Loaded dynamically from CDN at runtime; not vendored in this repository |

## Mushaf Builder & External Quranic Data

The Mushaf builder in `web/` fetches external Quranic fonts, texts, translations, and tafsir (such as KFGQPC, IndoPak, Saheeh International, Kemenag, Jalalayn, and alquran.cloud editions) directly in the user's browser to generate `.bqm` files locally.

- These datasets and fonts are **not** stored in or distributed by this repository.
- Any generated `.bqm` file remains subject to the terms of the respective rights holders of those fonts, texts, translations, and tafsir, independent of BacaIn's code licenses.
- Commercial hardware distributions of BacaIn must not preload `.bqm` files without separate permission from the respective rights holders.
