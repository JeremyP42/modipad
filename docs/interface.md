# Interface

**Русский:** [interface_rus.md](interface_rus.md)

This document describes the ModiPAD interface: the on-device UI and gestures, the `config.json` / `settings.json` configuration, the web configurator and the asset (image/font) library. Screenshots of every screen are in [docs/screenshots](../screenshots/README.md). For the system architecture see [Architecture](architecture.md); for the front page see [README](../README.md).

## Table of contents
1. [Configuration](#1-configuration)
2. [Web interface](#2-web-interface)
3. [Assets](#3-assets)

---

## 1. Configuration

### `config.json` (schema v3)

Top level: `settings` (language/brightness/sleep_timeout), an optional `main_page`, and `pages[]`. `main_page` (if present) is rendered as **tab 0**; page links and the status bar account for that offset.

Buttons may use:

- `type`: `settings` (opens the settings tab) or `page_link` (with `target_page` = a page `id`); `target_page` may be `__HOME__` for the main page.
- `icon`: an image name from `images/icons/pages` (rendered above the caption).
- `caption`: a plain string (button label) as an alternative to `text.lines`.
- `action.type`: `hotkey`, `text`, `macro` (`;`-separated hotkeys), `multimedia`, `obs` or `page`.

```json
{
  "version": "2.0",
  "pages": [{
    "id": "photoshop", "name": "Photoshop", "icon": "icon_photoshop.png",
    "background": { "type": "image", "image": "age_bg_dark.png", "opacity": 100 },
    "grid": { "rows": 2, "cols": 4,
      "margins": { "top": 10, "bottom": 10, "left": 15, "right": 15 },
      "spacing": { "horizontal": 10, "vertical": 10 } },
    "buttons": [{
      "id": "btn_copy", "position": { "row": 0, "col": 0 },
      "shape": { "type": "rounded", "radius": 12 },
      "padding": { "top": 8, "bottom": 8, "left": 8, "right": 8 },
      "text": { "lines": ["Copy"], "align": "center", "vertical_align": "center",
                "font": { "size": 14, "color": "#FFFFFF" } },
      "caption": { "enabled": true, "text": "Copy layer", "offset": 5,
                   "font": { "size": 12, "color": "#CCCCCC" } },
      "background": { "type": "image", "image": "gradient_blue.png",
                      "scale_mode": "cover", "opacity": 100 },
      "border": { "width": 2, "color": "#4ECDC4" },
      "action": { "type": "hotkey", "keys": "CTRL+J" }
    }]
  }]
}
```

- `background.type`: `image`, `solid` (with `color`) or `transparent`.
- `background.scale_mode`: `cover`, `contain`, `center` (LVGL 8.4 supports uniform zoom only; `stretch`/`tile` fall back to `cover`/`center`).
- `hotkey` syntax: modifiers `CTRL`, `SHIFT`, `ALT`, `GUI` joined with `+`, e.g. `CTRL+SHIFT+S`, `GUI+L`, `ALT+F4`.

Pages are built **lazily** on first view and kept in RAM afterwards (`ui_renderer.cpp`); this costs PSRAM per visited page. See [README](../README.md) 6.

### `settings.json`

`brightness`, `sound_enabled`, `radio_mode` (source of truth), derived `wifi_enabled`/`ble_enabled`, `sleep_timeout` (seconds, 0 = never), `show_stats` (show FPS/CPU in the status bar), `splash_bg`, `settings_bg`, `menu_bg`, plus the OBS endpoint (`obs.host/port/password`) and the network credentials.

---


## 2. Web interface

Connect to the Wi-Fi AP `ModiPAD_Setup` (password `12345678`) and open `http://192.168.4.1/`.

- **System** – radio mode, brightness, sound, sleep timeout, backgrounds
- **Pages** – add/rename/delete pages
- **Buttons** – per-page grid settings with clickable preview, full button editor
- **Library** – browse/upload/delete images per category
- **Preview** – device-accurate rendering of each page
- **Log / Backup / OTA** – live log view, SD config backups, firmware upload

REST API: `GET/POST /api/config`, `GET/POST /api/settings`, `GET /api/system`, `POST /api/reload`, `GET /api/log`, `POST /api/ota`, `POST /api/ota/sd`, `GET /api/ota/status`, `GET /api/images?dir=...`, `POST /api/upload?path=...`, `POST /api/delete?path=...`, `POST /api/backup/{save,import}`, `GET /api/backup/list`, `GET /api/fonts`, static `GET /images/*` and `GET /locales/*`.

### Web ⇄ firmware JSON contract

Two files, two "owners":

| File | Written by | Read/used by |
|------|-----------|--------------|
| `config.json` | web `POST /api/config` **and** the device (language change, Wi-Fi configure) | `ui_loader`/`ui_renderer` (`pages`, `main_page`), `i18n` (`settings.language`), `obs_client` (`obs`), `wifi_manager` (`network`) |
| `settings.json` | web `POST /api/settings` **and** the device (brightness/sound/sleep buttons) | `ui_loader` `AppSettings` (brightness, sound, `radio_mode`, sleep, caption fonts, backgrounds) |

- The web edits **whole files** (`state.config` / `state.settings` are the parsed files) and posts them back, so unknown fields are preserved.
- **`POST /api/config` and `POST /api/settings` re-parse the uploaded JSON into RAM** immediately (`load_config()` / `load_settings()`), because otherwise a later device-side `save_config()` / `save_settings()` would write the stale RAM copy back and discard the upload. Brightness is also applied live.
- Most changes take effect **after a reboot**: the web has a **Reboot** button (`POST /api/reload`); pages/language/network/radio are (re)applied on boot. `radio_mode` is the single source of truth; `wifi_enabled`/`ble_enabled` are derived from it on load.

### Device tab

The web **Device** tab mirrors the firmware **About** page: the *Hardware*, *Versions* and *System Info* blocks show the same rows in the same order — hardware text, then `Version / Build / LVGL / ESP-IDF`, then temperature, chip, `CPU + Flash`, uptime, SRAM/PSRAM/Heap/`Device FS`, SD (free/total + SD filesystem), WiFi RSSI+reconnects, WiFi/BT MAC, Build, Reset, IDF and the task stacks (`name: free/total B`). `/api/system` provides every field, including the per-task `stack_total`.

### Languages

The UI language is chosen from `localStorage` (falling back to the browser language: `ru` for Russian, otherwise `en`) and can be switched in the header. Translations are plain JSON under `datadevice/web/locales/`; copy one to add another language, then add a button in `index.html` to expose it.

---


## 3. Assets

The `datadevice/images` library ships with procedurally generated PNGs:

- **24 hand-picked gradients** (20 popular + 4 legacy names) plus **70 generated gradients** (`gradient_auto_*`), cached per button size — each of `datadevice/images/buttons/100x100` and `.../70x70` holds exactly **100 tiles** (188 gradients + 8 solid colours + 4 patterns)
- **8 solid colours**, **4 patterns** (wood, carbon, metal, concrete)
- **7 page backgrounds**, **3 system backgrounds**, **10 icons**

Regenerate them with:

```powershell
pwsh -File tools/utils/generate_assets.ps1        # or scripts\generate_images.bat
pwsh -File tools/utils/generate_gradients.ps1     # 70 extra gradients per size
```

PNG is lossless, so compression only affects file size. GDI+ **ignores** the `Encoder.Compression` parameter for PNG, so the real shrinking is done by an external tool. `scripts\generate_images.bat` runs generation and then lossless optimization in one go; `scripts\optimize_images.bat` optimizes in place; both pick the first available compressor:

1. `tools\oxipng\oxipng.exe` (preferred: lossless, multi-threaded, fast)
2. `tools\ect\ect.exe`
3. `tools\optipng\optipng64.exe` / `optipng32.exe`
4. system `oxipng` / `optipng` on `PATH`

If none is found the optimization step is skipped. Measured on this library (272 files):

| Stage | Tool | Size |
|-------|------|------|
| Raw generation | GDI+ | ~993 KB |
| Lossless | Oxipng | **~643 KB** (35 % saved) |
| Lossy | \+ Pngquant (`--skip-if-larger`) | **~363 KB** (63 % total) |

`scripts\optimize_max.bat` additionally runs **Pngquant** (lossy) after Oxipng and is opt-in; it uses `--skip-if-larger` because on these already-tiny PNGs Pngquant would otherwise *increase* some files. The shipped images are kept **lossless** (Oxipng). See `tools/README.md`.

### Page image filenames (max 15 characters)

Filenames inside `datadevice/images/pages/` and `datasdcard/modipad/images/pages/` must be **at most 15 characters including `.png`**. `mklittlefs` fails to pack the `pages` folder when a name is longer (`error adding file! ... Error for adding content from pages!`), so such names are trimmed to their **last 15 characters** (e.g. `0900f1bf…fef27f_p82r.png` → `fef27f_p82r.png`, `page_bg_dark.png` → `age_bg_dark.png`). Whenever you rename one, update every reference (`config.json`, plus code/web defaults). The asset generator (`tools/utils/generate_assets.ps1`) and the `ui_loader.c` defaults already use short (≤ 15) names.

### SD card media (`datasdcard/`)

The internal LittleFS (flashed from `datadevice/`, max ~8 MB) keeps only `config.json`, `settings.json`, fonts, the web UI and a small default asset set. Bulky media lives on the **microSD card**:

| Topic | Value |
|-------|-------|
| Interface | SDMMC 1-bit (D0=GPIO13, CLK=GPIO12, CMD=GPIO11), internal pull-ups |
| Device mount | `/sdcard`, LVGL drive `D:` (`/sdcard/modipad`) |
| Card layout | `\modipad\backgrounds`, `\modipad\images`, `\modipad\sounds` |
| Repo folder | `datasdcard/modipad/...` |
| Deploy | `scripts\sync_sd.bat` (menu `[5]`) copies it to the card |

`asset_resolve_image()` looks in flash (`S:`) first, then on the card (`D:`), so a background can live in either place without changing `config.json`. The emulators mirror this (see [Architecture](architecture.md) 8).

### Cyrillic fonts (Roboto)

The UI fonts are generated from `tools/fonts/Roboto-Regular.ttf` / `Roboto-Bold.ttf` into LVGL binary fonts by `scripts/generate_fonts.bat` (requires `npm install -g lv_font_conv` once). It produces `datadevice/fonts/roboto_{10,12,14,16,18}.bin` and `..._bold.bin`, all covering ASCII + Cyrillic (`0x20-0x7F`, `0x400-0x4FF`, bpp 4), matching the caption size/weight options in the web System tab.

`src/font_manager.[ch]` loads them with `lv_font_load("S:/fonts/...")` and falls back to the built-in Montserrat fonts when a `.bin` is missing (also used for `LV_SYMBOL_*`). The UI uses `get_font(size)` / `get_font_bold(size)` instead of `&lv_font_montserrat_N`, so Cyrillic renders on the device and in the simulator.

---
