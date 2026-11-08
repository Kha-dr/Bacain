# Al-Quran Reader

Optional offline Al-Quran reader for BacaIn e-paper devices.

Arabic script requires complex contextual glyph shaping and ligatures that are heavy for an MCU, so the companion web app pre-renders the Mushaf words into 1-bit bitmaps and packs them with wrapped text into a single binary file (`quran.bqm`) for direct streaming from flash or MicroSD.

## Features

- **4 Reading Layers**: Mushaf 1-bit word sprites, Latin transliteration, Translation, and Tafsir.
- **Quick Navigation**: Direct jump to Sunnah surahs (Al-Kahf, Yasin, Ar-Rahman, Al-Waqiah, Al-Mulk) and 30 Juz.
- **Bookmarks**: 8 persistent bookmark slots stored in NVS.
- **Zero Overhead**: Zero flash and RAM footprint when disabled in standard builds (`CONFIG_BOARD_ALQURAN_ENABLE=n`).

## Build & Flash

Build the factory binary with Docker Compose:

```bash
docker compose -f firmware/compose.yml run --rm alquran
```

Flash the generated factory binary (`firmware/build/bacain_waveshare_154_alquran_factory.bin`) at offset `0x0`:

```bash
# Windows
esptool.py -p COM3 -b 460800 --chip esp32s3 write_flash 0x0 build/bacain_waveshare_154_alquran_factory.bin

# Linux / macOS
esptool.py -p /dev/ttyACM0 -b 460800 --chip esp32s3 write_flash 0x0 build/bacain_waveshare_154_alquran_factory.bin
```

Binaries can also be flashed via the browser at [bacain.khadr.dev/flasher.html](https://bacain.khadr.dev/flasher.html).

## Controls

Two-button navigation using **UP** (BOOT, GPIO 0) and **DOWN** (PWR, GPIO 18).

| Context                     | Action       | Button            | Function                                         |
| :-------------------------- | :----------- | :---------------- | :----------------------------------------------- |
| **Reading**                 | Click        | **UP**            | Previous page or subpage                         |
|                             | Click        | **DOWN**          | Next page or subpage                             |
|                             | Double Click | **UP**            | Cycle layer (Mushaf, Latin, Translation, Tafsir) |
|                             | Double Click | **DOWN**          | Toggle bookmark                                  |
|                             | Hold (>1.2s) | **UP** / **DOWN** | Open Quran menu                                  |
| **Menu / Jump / Bookmarks** | Click        | **UP** / **DOWN** | Navigate items                                   |
|                             | Double Click | **UP** / **DOWN** | Select or jump                                   |
|                             | Hold (>1.2s) | **UP** / **DOWN** | Back / Exit                                      |

## Companion Web Builder (`quran.bqm`)

1. Connect device in [BacaIn PWA](https://bacain.khadr.dev).
2. Open Quran Builder, select script (Madinah Uthmani or IndoPak), layers, and Surah or Juz range.
3. Generate binary file and upload to internal flash (`/spiflash/books/quran.bqm`) or MicroSD (`/sdcard/books/quran.bqm`).

| Target Storage   | Usable Capacity           | Recommended Scope                                   |
| :--------------- | :------------------------ | :-------------------------------------------------- |
| **8 MB Flash**   | ~6.94 MB (`/spiflash`)    | Full 30 Juz (Mushaf + Latin + Translation, ~4.2 MB) |
| **4 MB Flash**   | ~2.94 MB (`/spiflash`)    | Juz 30 ('Amma), custom Surah ranges, or MicroSD     |
| **MicroSD Card** | Card capacity (`/sdcard`) | Full 30 Juz with complete Tafsir (~8.7 MB)          |

## Binary File Layout (`quran.bqm`)

Pre-rendered binary container (`BQM1`) designed for zero-heap streaming:

| Section             | Offset               | Purpose                                                                   |
| :------------------ | :------------------- | :------------------------------------------------------------------------ |
| **Header**          | `0x0000` (64 B)      | File signature `"BQM1"`, version, layer mask, dimensions, section offsets |
| **Surah Table**     | `0x0040` (2,736 B)   | 114 entries (24 B each): Surah number, Ayah count, first Ayah, Latin name |
| **Ayah Table**      | `0x0AF0` (149,664 B) | 6,236 entries (24 B each): Juz, subpage counts, layer offsets             |
| **Word Dictionary** | `word_dict_off`      | Sprite descriptors (8 B each): width, height, baseline, bitmap offset     |
| **Sprite Blob**     | `sprite_blob_off`    | Packed 1-bit monochrome glyph bitmaps (8 px/byte, MSB-first)              |
| **Mushaf Pages**    | `mushaf_pages_off`   | Subpage layouts: placed word coordinates `(word_id, x, y)`                |
| **Text Blob**       | `text_blob_off`      | Pre-wrapped UTF-8 lines for Latin, Translation, and Tafsir                |

## BLE Sync Protocol

Quran extensions for the BacaIn BLE GATT service (`0000fe60-0000-1000-8000-00805f9b34fb`):

| OpCode | Name              | Payload (Request / Response)                                                                    | Purpose                                                |
| :----- | :---------------- | :---------------------------------------------------------------------------------------------- | :----------------------------------------------------- |
| `0x19` | `GET_CONFIG`      | Req: `[0x19]`<br>Resp: standard config + `caps`                                                 | Bit 0 (`0x01`) in `caps` signals Quran feature support |
| `0x20` | `GET_QURAN_STATE` | Req: `[0x20]`<br>Resp: `[0x20, status, started, surah, ayah(2B), layer, count, bookmarks[0-7]]` | Query active cursor and 8 bookmarks                    |
| `0x21` | `SET_QURAN_STATE` | Req: `[0x21, sub, data]`<br>Resp: `[0x21, status]`                                              | Mutate cursor (`sub=1`) or bookmarks (`sub=2`) in NVS  |

## Acknowledgments

- Mushaf fonts: [King Fahd Glorious Quran Printing Complex (KFGQPC)](https://qurancomplex.gov.sa/) (Uthmanic Hafs) and [Quran.com / QUL](https://quran.com/) (IndoPak Nastaleeq)
- Quran text, translations, and tafsir datasets: [fawazahmed0/quran-api](https://github.com/fawazahmed0/quran-api)
- Indonesian translation and Tafsir Ringkas: [Lajnah Pentashihan Mushaf Al-Qur'an (LPMQ) Kemenag RI](https://quran.kemenag.go.id/)
- English translations and tafsir: Saheeh International, Abdullah Yusuf Ali, and Tafsir Ibn Kathir
