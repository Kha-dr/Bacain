# Brag Plan: BacaIn

## 1. Planning Rubric

1. **What is this app?**
   BacaIn is open-source ESP32-S3 e-paper mini e-reader firmware paired with a browser companion (Web Bluetooth book sync and Web Serial USB flasher).
2. **Most impressive claim:**
   Two-button e-paper reading with 20s auto-sleep, instant wake page turns, browser-side EPUB/FB2 conversion, and offline Mushaf support built straight from your browser.
3. **Visual hook:**
   Stark monochrome 200x200 e-paper clarity ("200 x 200 pixels. Two buttons. Zero notifications.") contrasted with Zinc 950 (`#09090b`) and `#3b82f6` companion controls.
4. **Actual UI and hardware visuals:**
   Real hardware photos (`assets/Hardware/1000288500_clean.jpg`, `1000288497_clean.jpg` Menu, `1000288498_clean.jpg` Reading View, `1000288499_clean.jpg` Sync QR), the live PWA palette and cards (`web/style.css`), `assets/pwa-dashboard.png`, `assets/web-flasher.png`, and `web/assets/icon.svg`.
5. **Duration:**
   20.0 seconds (600 frames at 30fps, 1920x1080 landscape).
6. **Tone:**
   - Preset: `polished`
   - Creative direction: Focused hardware-meets-web product launch: crisp high-contrast e-paper restraint and purposeful UI motion.
   - Interpretation: Confident holds (~0.3s+ per word settled), clean slide/scale entrances, zero flashy filler, soft staggered dips between scenes so layouts never overlap.
7. **Audio:**
   - Role: Warm, steady electronic bed + sparse crisp UI taps and hardware card impacts aligned to strong cues of `happy-beats-business-moves-vol-9-by-ende-dot-app.mp3` (114.84 BPM).
   - Strong cues used: `3.18s`, `6.34s`, `11.60s`, `16.34s` (fade out smoothly ending at `20.0s`).
   - SFX: `interface/click_002.ogg`, `casino/card-place-1.ogg`, `interface/switch_004.ogg`, `impact/impactSoft_medium_001.ogg` tucked subtly under the music bed.
   - Audio-reactive: Subtle hardware/card ambient presence (no waveform bars or strobing).
8. **Share copy draft:**
   Built BacaIn: open-source ESP32-S3 firmware and a browser PWA for a 200x200 pocket e-paper reader with zero distractions. Flash in Chrome, push EPUB/FB2 books or offline Mushaf builds over Web Bluetooth, and keep 20s auto-sleep battery life.
9. **User flow shown:**
   Click Sync Books on the e-paper menu -> scan/open BacaIn Web Companion -> drop an EPUB/FB2 -> clean planar wrap streams over Web Bluetooth to the paginated 200x200 display.

## 2. Visual Identity (from `web/style.css`)

- Background: `#09090b` (Zinc 950) and `#141417` (Card surface), `#27272a` borders
- Primary text: `#f4f4f5`, Secondary text: `#a1a1aa`, Muted: `#71717a`
- Accent: `#3b82f6` (`--accent-color`), Success: `#10b981`
- Fonts: System sans (`Segoe UI`, `system-ui`, sans-serif) and monospace (`Consolas`, `ui-monospace`, monospace)

## 3. Storyboard (20.0s Total, 1920x1080 @ 30fps)

1. **Scene 1: Hook (`0.00s - 3.18s`, 3.18s)**
   - Pill badge fades in: `ESP32-S3 · 1.54" E-PAPER · 20s AUTO-SLEEP`
   - Type/word-by-word punchline: `"200 x 200 pixels. Two buttons. Zero notifications."`
   - SFX: `interface/click_002.ogg` on badge/line lock (`0.35s`).
   - Staggered fade-out (`2.95s - 3.18s`) -> Scene 2.

2. **Scene 2: Hardware Reveal (`3.18s - 6.34s`, 3.16s)**
   - Headline: `"Meet BacaIn."` Subtitle: `"Pocket e-paper, open firmware."`
   - Real hardware showcase card (`1000288500_clean.jpg`) alongside live E-Paper view cards (`1000288497_clean.jpg` Reader Menu, `1000288498_clean.jpg` Reading View) showing 7-item navigation with BOOT (GPIO 0) and PWR (GPIO 18).
   - Settled window for poster frame `brag.jpg`: `4.80s`.
   - SFX: `casino/card-place-1.ogg` as hardware cards settle (`3.45s`).
   - Staggered exit (`6.10s - 6.34s`) -> Scene 3.

3. **Scene 3: Web Bluetooth Sync Flow (`6.34s - 11.60s`, 5.26s)**
   - Split live-workflow view using BacaIn PWA card styling (`#141417` + `#3b82f6`) + real `pwa-dashboard.png` and device Sync screen `1000288499_clean.jpg`.
   - Simulated flow steps animate sequentially:
     1) Select `.epub` / `.fb2` / `.txt` in browser (`6.80s`)
     2) Client wraps text cleanly and extracts ZIP/XML (`7.90s`)
     3) Streams compact binary frames over Web Bluetooth GATT (`9.20s`, progress bar `0% -> 100%`)
     4) Instant paginated reading view ready (`10.20s` hold to `11.35s`)
   - SFX: `interface/switch_004.ogg` (`6.85s`), `interface/click_002.ogg` (`9.20s`).
   - Staggered exit (`11.35s - 11.60s`) -> Scene 4.

4. **Scene 4: Offline Mushaf & Safe Web Flasher (`11.60s - 16.34s`, 4.74s)**
   - Two side-by-side capability cards anchored by `assets/web-flasher.png` and BacaIn's pre-flight flasher checks:
     - Left card: **Offline Mushaf Builder** (builds `.bqm` packages client-side from Uthmani / IndoPak data with translation options).
     - Right card: **Web Serial Flasher (`0x0`)** displaying the real verification readout (`ESP32-S3 · 8 MB Flash · Waveshare ESP32-S3-ePaper-1.54`) with Book & NVS preservation active.
   - SFX: `casino/card-place-1.ogg` (`11.90s`).
   - Staggered exit (`16.10s - 16.34s`) -> Scene 5.

5. **Scene 5: Outro (`16.34s - 20.00s`, 3.66s)**
   - Centered `web/assets/icon.svg` mark + `"BacaIn"` wordmark.
   - Line: `"Flash in Chrome. Sync over Bluetooth. Read anywhere."`
   - Links pill: `bacain.khadr.dev` · `github.com/Kha-dr/BacaIn` · `GPL-3.0 / AGPL-3.0`
   - SFX: `impact/impactSoft_medium_001.ogg` (`16.45s`) as logo lands; music fades smoothly to 0 by `20.00s`.
