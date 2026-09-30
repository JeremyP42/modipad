# Architecture

**Русский:** [architecture_rus.md](architecture_rus.md)

This document covers the ModiPAD system architecture: runtime tasks and core placement, the memory model, the storage model, libraries, the project layout, diagnostics, the critical invariants, the PC simulator, anti-patterns and the working configuration. For the interface see [Interface](interface.md); for the front page see [README](../README.md).

> Read the "Critical invariants" section before changing any driver, the flush path, the memory placement or the task/core layout.

## Table of contents
1. [Runtime architecture](#1-runtime-architecture)
2. [Memory model](#2-memory-model)
3. [Storage model](#3-storage-model)
4. [Libraries & dependencies](#4-libraries--dependencies)
5. [Project layout](#5-project-layout)
6. [Diagnostics & tuning](#6-diagnostics--tuning)
7. [Critical invariants (do not break)](#7-critical-invariants-do-not-break)
8. [Simulator (PC, SDL2)](#8-simulator-pc-sdl2)
9. [Anti-patterns (don't repeat)](#9-anti-patterns-dont-repeat)
10. [Working configuration & best practices](#10-working-configuration--best-practices)
11. [Board pinout & wiring](#11-board-pinout--wiring)
12. [Build scripts & tooling](#12-build-scripts--tooling)
13. [Troubleshooting](#13-troubleshooting)

---

## 1. Runtime architecture

### 1.1 Boot sequence (`src/main.cpp`, `app_main`)

1. `sys_log_init()` – tee the ESP log into a RAM ring buffer (served over HTTP).
2. `nvs_flash_init()`.
3. **Panel bring-up on core 1** (`display_init_task`) – see 1.3. Creates the SPI2 bus, panel IO, the AXS15231B panel and the LVGL port; starts the LVGL task (pinned to core 1).
4. `init_touch()` – creates the touch input device.
5. `show_splash_screen()` / `update_progress()` – early splash (built-in font).
6. `ui_loader_init()` – mount LittleFS, load `settings.json` + `config.json` (writes defaults on first boot).
7. `boot_guard_check()` – boot-loop safety (forces Wi-Fi AP after 3 fast reboots).
8. `init_fonts()`, `i18n_init()`, `system_info_init()`, splash assets.
9. `init_keyboard(ble_enabled)` – BLE HID + the `kb_out` output task.
10. `macro_player_init()`, `obs_client_init()`.
11. Wi-Fi (`wifi_manager_*`) **only if** `settings.wifi_enabled`.
12. `sd_card_init()` – mount `/sdcard` (non-fatal if absent).
13. `create_ui()` / `create_settings_page()` / `init_gestures()` / `set_brightness()` under the LVGL lock.
14. Status bar overlay.
15. Close splash; start `app_loop` (unpinned) and `heartbeat` (core 0).

### 1.2 Tasks, cores and priorities

Measured with `uxTaskGetNumberOfTasks()` ≈ 14–17 tasks total (the rest are IDF internals: `main`, `IDLE0/1`, `esp_timer`, `Tmr Svc`, `ipc0/1`, and the Bluedroid/`ipc` stack tasks).

| Task | Created in | Core | Prio | Stack | Purpose |
|------|-----------|------|------|-------|---------|
| `LVGL task` | `lv_port.c:118` | **1** (cfg `task_affinity=1`) | 4 | 8192 | `lv_timer_handler` loop; owns the LVGL mutex; runs flush + touch read |
| `display_init` | `main.cpp` | **1** | 5 | 8192 | one-shot panel bring-up (binds SPI2 ISR to core 1), then deletes itself |
| `Tear task` | `esp_bsp.c` | unpinned (-1) | 4 | 2048 | re-arms the TE sync semaphore for tear-free frames |
| `heartbeat` | `main.cpp` | **0** | 1 | 3072 | 5 s diagnostics + stall watchdog (see 6) |
| `app_loop` | `main.cpp` | unpinned | 3 | 4096 | keyboard poll, sleep timeout, OTA rollback confirm |
| `macro_player` | `macro_player.c` | unpinned | 4 | 4096 | executes queued macro steps |
| `obs_client` | `obs_client.c` | **0** | 4 | 6144 | OBS WebSocket worker (lwIP sockets) |
| `kb_out` | `keyboard_manager.cpp` | unpinned | 4 | 4096 | serialised HID output (hotkeys/text) |
| `wifi_req` | `wifi_manager.c` | unpinned | 4 | 4096 | heavy Wi-Fi ops off the LVGL task |
| `wifi_cmd` | `web_server.cpp` | unpinned | 4 | 4096 | Wi-Fi ops requested from the web UI |
| `httpd` | `web_server.cpp` | **0** (`core_id=0`) | default | 6144 | `esp_http_server` |
| `Bluedroid`/`BT` | IDF | **0** | high | IDF | BLE controller + host |
| `restart` / `ota_sd` / `radio_reboot` | web/settings | unpinned | 5 | 2048–6144 | deferred restart / OTA-from-SD / radio-switch reboot |

**Why these assignments**

- **Core 1 = UI + panel.** A full-refresh frame is expensive; keeping it away from the radio/network stack avoids starvation and the interrupt watchdog.
- **Core 0 = radio/network.** Bluedroid, the Wi-Fi task and `httpd` are all pinned/created on core 0 (`CONFIG_BT_*_PINNED_TO_CORE_0=y`, `CONFIG_ESP_WIFI_TASK_PINNED_TO_CORE_0=y`, `http_cfg.core_id = 0`).
- The `heartbeat` runs on core 0 at priority 1 so it can never starve the UI and still reports liveness if the UI task blocks.

> **Task stacks are internal RAM.** Do not grow them casually; internal RAM is the scarce resource on this board (see 2.3).

### 1.3 Interrupt / driver placement — **the most important rule**

`spi_bus_initialize()` binds the **SPI2 interrupt to the core that calls it**. The AXS15231B panel is on **SPI2**, the radio is on **core 0**. Therefore the panel **must be initialised from a core-1 task**:

```c
/* main.cpp */
xTaskCreatePinnedToCore(display_init_task, "display_init", 8192, NULL, 5, NULL, 1);
```

If the panel is initialised from `app_main` (core 0), the SPI2 ISR lands on core 0 next to Bluedroid. Under Bluetooth load the QSPI colour transfer can stall; IDF's `esp_lcd` SPI transport then waits forever (`spi_device_get_trans_result(..., portMAX_DELAY)`), the LVGL task blocks while holding the LVGL mutex and **the whole UI freezes** (screen dark, no touch). Moving the ISR to core 1 fixed a reproducible freeze; do not undo this.

Supporting facts (IDF 5.3.1):
- `CONFIG_SPI_MASTER_ISR_IN_IRAM=y` (already set) keeps the SPI ISR runnable during flash/cache operations.
- `CONFIG_BT_BLUEDROID_PINNED_TO_CORE_0=y`, `CONFIG_BT_CTRL_PINNED_TO_CORE_0=y`.

### 1.4 Rendering pipeline

- **Interface:** AXS15231B over **QSPI** (`use_qspi_interface = 1`), 40 MHz, RGB565, `LCD_RGB_ELEMENT_ORDER_RGB`, `quad_mode`.
- **Full refresh only.** The QSPI path sends only `CASET` (no `RASET`) and streams the frame sequentially as `0x2C`/`0x3C` ("write continue"). Arbitrary partial rectangles are **not addressable**, so the display driver runs with `full_refresh = 1`. Do not enable partial refresh unless the panel driver is first taught windowed addressing.
- **Rotation** is done in software. `LCD_ROTATION_DEG = 90`; the panel MADCTL can mirror (180°) but cannot swap axes, so `lvgl_port_flush_callback()` does a transpose+flip into the transport buffer before `esp_lcd_panel_draw_bitmap()`.
- **Frame buffer** (`buf1`): one full frame (320×480 px = 307 KB) in **PSRAM**, single-buffered (`lv_disp_draw_buf_init(..., buf1, NULL, size)`).
- **Transport buffers** (`trans_buf_1/2`): `trans_size = 320*20 = 6400 px` (12 800 B each) in **internal DMA RAM**. A frame is therefore sent as 24 tiles of 20 source lines; `max_transfer_sz = 320*20*2 = 12 800 B`.
- **TE sync:** `bsp_display_sync_cb` waits for the TE edge on GPIO38 (15 ms timeout) at the first tile of a frame; `Tear task` re-arms the semaphore.
- **Flush serialisation (do not remove):** before every `esp_lcd_panel_draw_bitmap()` the flush waits on `trans_done_sem`, which is given by the SPI colour-done callback. On timeout it **aborts the frame** instead of calling `draw_bitmap()`. Reason: `esp_lcd`'s SPI transport recycles in-flight transactions with `portMAX_DELAY`, so calling it while a transfer is pending can hang the LVGL task forever. A dropped frame is harmless; a hang is not.

### 1.5 Concurrency & locking rules

- All LVGL object access must hold the LVGL mutex: `bsp_display_lock(timeout_ms)` / `bsp_display_unlock()` (recursive, wraps `lvgl_port_lock`). The LVGL task locks internally.
- **Never** take the LVGL lock with `timeout_ms = 0` (= `portMAX_DELAY`) from a non-LVGL task. Use a bounded timeout and skip the update if it fails (`connection_changed` uses 100 ms, `toast_show` uses 50 ms). An unbounded cross-task lock is a deadlock waiting to happen.
- HID output is queued (`kb_out` task); `send_hotkey()` / `send_text()` return immediately so a blocking BLE call can never freeze the UI.
- Heavy Wi-Fi start/stop is posted to `wifi_req` / `wifi_cmd` tasks, never done inline in an LVGL event handler.
- Touch is polled **inside the LVGL task** (indev callback) but over a timeout-bounded I2C transport (see 1.6).

### 1.6 Touch input (I2C)

- AXS15231B touch @ addr `0x3B`, 400 kHz, I2C port 0, SDA GPIO4 / SCL GPIO8.
- The vendor driver is used, but its transport is redirected to a **raw `i2c_master_dev_handle_t` with a 50 ms timeout** (`esp_lcd_touch_axs15231b_set_i2c_device()`, `esp_lcd_axs15231b.c`). IDF's `esp_lcd_panel_io_i2c` transport issues every transaction with `-1` (wait forever); since touch is read from the LVGL task, one disturbed transfer would freeze the UI. The raw device makes a wedged bus return an error instead.
- After 3 consecutive read errors the flush calls `bsp_i2c_recover()` (`i2c_master_bus_reset()`), and the I2C bus uses the **new `i2c_master` driver** (not the legacy `driver/i2c`) for the same reason (bounded, interrupt-light).
- Note the IDF warning about missing I2C pull-ups; the board does not enable internal pull-ups (`enable_internal_pullup = false`).

### 1.7 Radio model

Only one radio runs at a time (`radio_mode_t` in `config.h`):

| Mode | Enum | What runs |
|------|------|-----------|
| BLE  | `RADIO_MODE_BLE` | BLE HID only, no network |
| AP   | `RADIO_MODE_AP`  | Wi-Fi AP + web configurator (`ModiPAD_Setup` / `12345678`) |
| STA  | `RADIO_MODE_STA` | Wi-Fi client, used to reach OBS Studio |

- BLE and Wi-Fi are **mutually exclusive** because they share scarce internal RAM; tearing one stack down at runtime corrupts memory. Switching is applied by **save + reboot** (`radio_reboot` task in `settings_page.cpp`).
- `settings.radio_mode` is the source of truth; `wifi_enabled` / `ble_enabled` are **derived** on load (`ui_loader.c`).
- The web server only starts in a Wi-Fi mode; `obs_client` only connects in STA mode (`wifi_manager_mode() == WIFI_APP_STA && wifi_manager_connected()`).

### 1.8 Status bar: FPS / CPU readout

The top bar can show `FPS n` and `CPU n%` as two labels 5 px apart, centred; toggle it in **Settings → General → "Show FPS / CPU"** (`settings.json` `show_stats`, **off by default**). Values update once per second and are normalised to the real elapsed window (`src/status_bar.cpp`, `src/lv_port.c`):

- **FPS** = frames rendered since the previous tick (`lvgl_port_frame_count()`; with `full_refresh` one completed flush callback equals one full frame).
- **CPU** = share of the LVGL task loop spent inside `lv_timer_handler()`: `100 * delta_busy_us / delta_total_us` (busy = handler time, total = handler + sleep).

These are **honest** numbers: on a static screen they are ~0–1 FPS and a few % CPU. The old values (~10 FPS / ~40 % CPU) came from LVGL's built-in perf monitor, which redrew its own label every **300 ms** — and on this `full_refresh` panel that is a **full-screen redraw 3×/s**, so it was largely measuring (and causing) its own load. That built-in monitor is disabled (`LV_USE_PERF_MONITOR 0`); the status-bar readout draws once per second and, when off, causes no periodic redraw at all.

### 1.9 Gestures & accidental clicks

Swipes are detected by LVGL itself (`LV_EVENT_GESTURE` delivered to the screen; the status bar, pages and buttons carry `LV_OBJ_FLAG_GESTURE_BUBBLE`, so a swipe is recognised even if it starts on the status bar or a button). LVGL *also* emits `CLICKED` to the button under the finger when a swipe starts on it. To prevent that, `gesture_handler.cpp` timestamps the last detected gesture and `gesture_swallow_click()` makes the button handler (`ui_renderer.cpp`) drop a click that arrives within `GESTURE_CLICK_GUARD_MS` (500 ms) of a gesture.

Tuning options if taps are still misread: widen `GESTURE_CLICK_GUARD_MS`, or raise LVGL's `gesture_limit` / `scroll_limit` (indev init) so a swipe needs more travel to be recognised.

---


## 2. Memory model

### 2.1 Physical memory

| Region | Size | Speed | Notes |
|--------|------|-------|-------|
| Internal SRAM (DRAM/IRAM) | 512 KB | 240 MHz | **scarce** – stacks, DMA buffers, radio stacks, ISRs |
| PSRAM | 8 MB **octal** | 80 MHz, ECC | large buffers only; **not DMA-capable for SPI** |
| Flash | 16 MB | QIO 80 MHz | code + LittleFS |
| microSD | card | 1-bit SDMMC | bulky media only |

`esp_psram` reports ~7680 KB usable because of ECC; total heap ≈ 7936 KB.

### 2.2 What is allocated where, and why

| Allocation | Region | Why |
|-----------|--------|-----|
| LVGL frame buffer (`buf1`) | **PSRAM** (`buff_spiram=true`) | a 307 KB full frame does not fit internal RAM |
| LVGL transport buffers (`trans_buf_1/2`) | **internal DMA** (`MALLOC_CAP_DMA`) | QSPI DMA cannot read PSRAM; kept small (2×12.8 KB) |
| Sys-log ring buffer | PSRAM (`MALLOC_CAP_SPIRAM`) | sacrificial, large-ish buffer |
| Asset path strings | PSRAM (`ui_assets.cpp`) | avoids fragmenting internal RAM |
| Task stacks | internal | FreeRTOS requirement |
| Wi-Fi + Bluedroid stacks | internal | IDF requirement; a main reason radios are exclusive |
| SPI/I2C drivers, ISRs | internal/IRAM | hardware requirement |

### 2.3 Internal RAM budget (observed on this board)

| Point | Internal free | Largest internal block |
|-------|--------------|------------------------|
| After LittleFS mount | ~119 KB | ~57 KB |
| After BLE HID up | ~54 KB | ~31 KB |
| Steady state (BLE, UI built) | ~41 KB (min ever) | ~31 KB |

Consequences:
- **Only ~31 KB of contiguous internal RAM is left once BLE is up.** Any new large internal/DMA allocation, bigger task stack or PSRAM-disabled buffer can break BLE/`httpd`/LVGL. Prefer PSRAM; keep DMA buffers tiny.
- This is also why the transport buffers are 12.8 KB (2 of them fit) and why the radio is single-mode.

---


## 3. Storage model

### 3.1 NVS

Tiny. Only the `boot` namespace holds the boot-loop counter (see `main.cpp` `boot_guard_check`). **Settings are not stored in NVS.**

### 3.2 LittleFS (`/littlefs`, LVGL drive `S:`)

Partition `storage` @ `0x820000`, size `0x7E0000` (~7.9 MB). Flashed from `datadevice/` with `pio run -t uploadfs`.

| File / dir | Contents |
|-----------|----------|
| `config.json` | UI schema v3 (settings header, `main_page`, `pages[]`) |
| `settings.json` | runtime settings (see [Interface](interface.md) 1) |
| `fonts/` | `roboto_{10,12,14,16,18}[_bold].bin` (ASCII + Cyrillic, bpp 4) |
| `web/` | `index.html`, `style.css`, `app.js`, `locales/{ru,en}.json` |
| `images/` | default PNG library (see [Interface](interface.md) 3) |

### 3.3 microSD (`/sdcard`, LVGL drive `D:` = `/sdcard/modipad`)

SDMMC 1-bit, mounted at `/sdcard`. Repo source folder: `datasdcard/modipad` (deploy with `scripts\sync_sd.bat`).

| Path | Contents |
|------|----------|
| `/sdcard/modipad/backgrounds` | page background PNGs |
| `/sdcard/modipad/images` | extra image library |
| `/sdcard/modipad/sounds` | button sounds |
| `/sdcard/modipad/config/backup_<version>_<n>.json` | config backups (`config_backup.c`; `<version>` = `MODIPAD_FIRMWARE_VERSION`, `<n>` = uptime seconds) |
| `/sdcard/update.bin` | firmware image for OTA-from-SD |

### 3.4 Asset resolution

`asset_resolve_image()` (in `ui_assets.cpp`) tries the **flash first (`S:`)**, then the **card (`D:`)**. A background can therefore live in either place without editing `config.json`. The emulators mirror this (see 8).

### 3.5 OTA & rollback

- Partition table is **dual-OTA**: `ota_0` @ `0x20000`, `ota_1` @ `0x420000`, plus `otadata`.
- After a successful 15 s of runtime, `app_loop` calls `esp_ota_mark_app_valid_cancel_rollback()`; the bootloader otherwise reverts a bad image (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`).
- Update sources: web (`POST /api/ota`, `POST /api/ota/sd`) and the SD `update.bin` (Settings → System). See `ota_update.c` / `web_server.cpp`.
- `boot_guard_check()` forces Wi-Fi AP mode after 3 fast reboots so a bad radio setting can always be reconfigured.

---


## 4. Libraries & dependencies

| Dependency | Source | Used for |
|-----------|--------|----------|
| **LVGL 8.4** | local `lib/lvgl` (+ `lib/lvgl/lv_conf.h`) | UI. *Not* the managed component |
| **ble_hid** | local `lib/ble_hid` | BLE HID over Bluedroid |
| **cJSON** | IDF component | `config.json` / `settings.json` |
| **joltwallet/littlefs** | managed (`src/idf_component.yml`) | LittleFS |
| **esp_lcd** | IDF | panel IO + generic panel/touch API |
| `esp_lcd_axs15231b.[ch]`, `esp_lcd_touch.[ch]` | vendor, local `src/` | AXS15231B panel + touch drivers |
| **lwIP** | IDF | OBS WebSocket client |
| **esp_http_server** | IDF | web configurator |
| IDF `esp_driver_sdmmc`, `sdmmc`, `fatfs`, `mbedtls`, `esp_wifi`, `esp_netif`, `nvs_flash` | IDF | storage / radio / TLS / NVS |

`src/CMakeLists.txt` registers all of `src/*` and sets `PRIV_REQUIRES esp_driver_sdmmc sdmmc fatfs mbedtls lwip esp_wifi esp_netif nvs_flash esp_http_server`.

> `lv_conf.h` lives in `lib/lvgl/lv_conf.h`. LVGL picks it up automatically because the library directory is on the include path; this avoids the broken `LV_CONF_PATH` macro (Windows backslashes get stripped by the build system).

### Build flags / sdkconfig defaults (`sdkconfig.defaults`)

| Setting | Why |
|---------|-----|
| `CONFIG_FREERTOS_HZ=1000` | 1 ms tick (timing/sleep) |
| `CONFIG_FATFS_LFN_HEAP=y`, `CONFIG_FATFS_MAX_LFN=255`, `CONFIG_FATFS_SECTOR_512=y` | long file names + 512-byte sectors on SD |
| `CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG=y` | logs over the native USB port |
| `CONFIG_ESP_INT_WDT_TIMEOUT_MS=1000` | a flash erase during display/touch can block ISRs; give headroom over the 300 ms default |
| `CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE=6144` | the default 2304 B overflowed on the Wi-Fi AP start path |
| `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` | OTA rollback |
| `CONFIG_SPIRAM_MODE_OCT`, 80 MHz, ECC | the 8 MB PSRAM part |
| `CONFIG_SPI_MASTER_ISR_IN_IRAM=y` | keep the SPI ISR runnable during flash ops |

`platformio.ini` also sets `-DLOG_LOCAL_LEVEL=ESP_LOG_VERBOSE` (verbose app logs) and `monitor_filters = esp32_exception_decoder`. Reduce the log level for release builds if the verbosity is distracting.

---


## 5. Project layout

```
ModiPAD_custom/
├── platformio.ini                # [env:modipad]; data_dir=datadevice, board_build.filesystem=littlefs
├── CMakeLists.txt                # top-level ESP-IDF project wrapper
├── sdkconfig.defaults            # project sdkconfig overrides (see 4)
├── sdkconfig.modipad            # generated/PlatformIO sdkconfig.<env>
├── extra_script.py               # post-build: copy firmware.bin -> firmware/<version>/
├── start.bat                     # single root entry point (full menu)
├── README.md / README_RUS.md     # these docs
├── boards/
│   └── 320x480.json              # PIO board: 16 MB flash, 8 MB PSRAM, partitions
├── partitions/
│   └── modipad_16MB.csv         # dual OTA 4M+4M + storage 7.9M LittleFS
├── lib/
│   ├── lvgl/                     # LVGL 8.4 + lv_conf.h (vendored, locally patched)
│   └── ble_hid/                  # BLE HID implementation (Bluedroid)
├── src/                          # firmware sources (module list below)
├── datadevice/                   # LittleFS image contents -> `pio run -t uploadfs`
├── datasdcard/                   # media copied to the microSD card
├── simulator/                    # separate PC (SDL2/Win32) PlatformIO project
├── scripts/                      # build/flash/asset action scripts (*.bat / *.ps1)
├── tools/                        # PNG tools, fonts, SDL2, generators
├── firmware/                     # archive of built images (firmware/<version>/)
└── managed_components/           # joltwallet__littlefs (auto-fetched - do not edit)
```

### 5.1 `datadevice/` - the LittleFS image (`/littlefs`, LVGL drive `S:`)

Flashed with `[4] Upload storage only` (`pio run -t uploadfs`). This is what the device actually reads at boot and where the on-device web server serves from.

| Path | Contents |
|------|----------|
| `config.json` | UI schema v3: `main_page` + `pages[]` + `styles` + `network`/`obs` (see [Interface](interface.md) 1) |
| `settings.json` | runtime settings (`AppSettings`: language, brightness, sleep, radio mode) |
| `web/` | the whole web configurator: `index.html`, `app.js`, `style.css`, `locales/{en,ru}.json` |
| `images/pages/` | page background PNGs (480x320, filename <= 15 chars) |
| `images/buttons/<WxH>/` | pre-sized button background tiles (`100x100/`, `70x70/`, ...) |
| `images/icons/pages/` | page/app icons (64 px) |
| `images/icons/buttons/` | button glyphs (transparent, white) |
| `images/icons/system/` | status-bar icons (BT/WiFi on/off, home, info, ...) |
| `fonts/` | LVGL `.bin` fonts (`roboto_<N>.bin`, ASCII + Cyrillic) |

### 5.2 `datasdcard/` - the microSD files (`/sdcard`, LVGL drive `D:` = `/sdcard/modipad`)

Copied to the card root by `[5] Upload SD card` / `scripts/sync_sd.bat`.

| Path | Contents |
|------|----------|
| `modipad/images/` | extra image library (same layout as `datadevice/images/`) |
| `modipad/backgrounds/` | optional page backgrounds (compressed in place) |
| `modipad/sounds/` | button sounds |
| `modipad/fonts/` | extra Roboto sizes |
| `modipad/config/` | config backups `backup_<version>_<n>.json` (also the web-preview mirror) |
| `update.bin` (card root) | firmware image for OTA-from-SD |

### 5.3 `simulator/` - the PC build (separate PlatformIO project)

Compiles the **production** `../src` UI directly (no copies) against LVGL 8.4.

| File | Role |
|------|------|
| `platformio.ini` | envs `native` (SDL2 window) and `native_win32` (pure Win32/GDI) |
| `extra_paths.py` | include paths, `HOST_BUILD` defines, `--wrap=fopen/opendir`, builds `../src` |
| `lv_conf.h` | PC LVGL config (mirrors the device's gradient cache/dither settings) |
| `src/main_host.cpp` / `src/main_host_win32.cpp` | window backends; mouse = touch, drag = swipe |
| `src/host_stubs.cpp` | hardware stubs + `/littlefs`->`../datadevice`, `/sdcard`->`../datasdcard` |
| `src/compat/`, `src/cjson/` | ESP-IDF shims and cJSON for the host |
| `run_simulator.bat` / `run_win32_simulator.bat` / `sync_project.bat` | launchers (set CWD to `simulator/`) |

### 5.4 `scripts/` - action scripts (called by `start.bat`; menu item in brackets)

| Script | Menu | Does |
|--------|------|------|
| `build.bat` | [1] | `pio run` (build only, no flash) |
| `flash_firmware.bat` | [2] | upload firmware only |
| `flash.bat` | [3] | firmware + LittleFS |
| `upload_files_only.bat` | [4] | `pio run -t uploadfs` (LittleFS only) |
| `sync_sd.bat` | [5] | copy `datasdcard/modipad` to the card |
| `flash_all.bat` | [6] | firmware + storage + SD |
| `monitor.bat` | [7] | serial monitor (115200) |
| `open_web.bat` | [8] | open http://192.168.4.1 |
| `clean.bat` | [9] | remove `.pio` |
| `run_web_preview.bat` | [12] | local web-UI preview (saves to `datadevice/config.json`) |
| `generate_images.bat` | [13] | generate + losslessly compress the PNG library |
| `optimize_images.bat` / `optimize_max.bat` | [14]/[15] | lossless / lossy PNG optimization |
| `generate_fonts.bat` | [16] | build Roboto `.bin` fonts via `lv_font_conv` |
| `compress_backgrounds.bat` | [17] | compress SD backgrounds in place |
| `find_sd.ps1` | - | resolve the SD drive letter (used by `sync_sd.bat`) |

### 5.5 `tools/` - helper engines (not flashed)

| Path | Contents |
|------|----------|
| `utils/web_preview_server.py` | host server that mimics the device REST API (menu [12]) |
| `utils/generate_assets.ps1` | placeholder/gradient/pattern/icon generator |
| `utils/generate_gradients.ps1`, `utils/compress_backgrounds.py` | gradient + background helpers |
| `fonts/` | Roboto TTFs (source for `generate_fonts.bat`) |
| `sdl2/` | 32-bit SDL2 dev files for the SDL2 simulator |
| `optipng/`, `oxipng/`, `pngquant/` | PNG compressors |
| `download_app_icons.py` | fetch app icons into the library |

Firmware modules (`src/`):

| Module | Role |
|--------|------|
| `main.cpp` | boot sequence + `app_loop` + heartbeat |
| `config.h` | pins, memory/UI constants, radio modes, diag stage |
| `display_init.[ch]pp` | panel bring-up + brightness (runs on core 1) |
| `touch_handler.[ch]pp` | touch input device wrapper |
| `esp_bsp.[ch]` | board support (panel/SPI/I2C/TE/touch), adapted from vendor |
| `lv_port.[ch]` | LVGL port: flush, indev, mutex, diagnostics |
| `esp_lcd_axs15231b.[ch]` | AXS15231B panel + touch driver (bounded I2C transport) |
| `esp_lcd_touch.[ch]` | generic touch framework |
| `ui_loader.[ch]` | LittleFS + cJSON (config/settings) |
| `ui_renderer.[ch]pp` | JSON v3 -> LVGL UI, lazy tab build |
| `ui_assets.[ch]pp` | image path resolve, fonts, colours, backgrounds |
| `status_bar.[ch]pp` | top bar (page name + BT/WiFi icons) |
| `settings_page.[ch]pp` | settings tab + sub-pages + timers |
| `gesture_handler.[ch]pp` | touch gestures |
| `splash_screen.[ch]pp` | boot splash |
| `toast.[ch]` | transient overlay |
| `keyboard_manager.[ch]pp` | HID dispatch (hotkey/text/media) + `kb_out` task |
| `ble_controller.[ch]` | BLE HID (Bluedroid) |
| `wifi_manager.[ch]` | Wi-Fi AP/STA + async request task |
| `web_server.[ch]pp` | `esp_http_server` + REST API |
| `obs_client.[ch]` | OBS WebSocket v5 client |
| `macro_player.[ch]` | queued macro executor |
| `sd_card.[ch]` | SDMMC mount |
| `font_manager.[ch]` | LVGL font loading (Roboto `.bin`) |
| `config_backup.[ch]` | config backup/restore on SD |
| `ota_update.[ch]` | OTA from web / SD |
| `i18n.[ch]` | UI strings (RU/EN) |
| `system_info.[ch]` | About page data (chip/heap/tasks/MACs) |
| `sys_log.[ch]` | RAM log ring buffer (served at `/api/log`) |

> Note: `ui_loader`, `i18n`, `system_info`, `macro_player`, `ble_controller`, `obs_client`, `sd_card`, `config_backup`, `ota_update`, `toast`, `sys_log` are `.c` so they also build in the PC simulator (its MinGW GCC 5.1 rejects C++ designated initializers). Keep them C-compatible if you touch them.

---


## 6. Diagnostics & tuning

### `MODIPAD_DIAG_STAGE` (`src/config.h`)

A build-time progressive-isolation switch used to prove out the panel/driver:

| Value | Configuration |
|-------|---------------|
| **0** | production (everything on, auto-sleep enabled) |
| 1 | UI only (display + touch + LVGL + heartbeat) |
| 2 | UI + macro player + OBS worker (radios off) |
| 3 | UI + keyboard/radio only (macro/OBS off) |
| 4 | everything (== 0) |

Auto-sleep is disabled for every stage except 0. Useful when a future hang needs isolating between "UI/driver" and "network tasks".

### Heartbeat (`src/main.cpp`, `heartbeat_task`)

A core-0 task prints every 5 s:

```
HEARTBEAT: alive heap=.. int_min=.. int_largest=.. psram=.. tasks=.. locked=.. stage=.. loops=.. flush=.. done=.. exit=.. skip=.. uptime=..s
```

- `int_largest` – largest contiguous internal block (headroom for radio/LVGL).
- `loops` – **liveness counter** of the LVGL task; it always advances unless the task is blocked.
- `stage` – last place the LVGL task entered (0 idle, 10–19 touch read, 20–29 flush path); `22` = waiting for DMA-done, `23` inside draw_bitmap.
- `flush` / `done` / `exit` / `skip` – flushes entered / colour-DMA completions / flushes completed / frames aborted on DMA timeout.

**Stall watchdog:** if `loops` does not advance for ~10 s the task is genuinely blocked and the device `esp_restart()`s (`LVGL task stalled ... - rebooting to recover`). This is a self-healing measure; a healthy device never triggers it.

### How to verify a change

1. `pio run` (build), `pio run -t upload`.
2. Open the monitor and watch `HEARTBEAT`: `loops` must keep rising, `skip` must stay 0, `int_largest` must not collapse, and there must be no reboot lines.
3. Stress it: connect a BLE host and scroll **Settings → About** and the page with text fields for a few minutes (heaviest redraw path).
4. If a real stall occurs you will see `stage=23` with a frozen `loops` and an automatic reboot — capture the log before the reboot.

---


## 7. Critical invariants (do not break)

1. **Initialise the panel from core 1** (`display_init_task`). Never call `init_display()` directly from `app_main`. This keeps the SPI2 ISR off the Bluedroid core; violating it reintroduces the hard freeze. (See 1.3.)
2. **Keep `full_refresh = 1`.** The QSPI driver only addresses full frames; do not enable partial refresh unless you first implement windowed CASET/RASET addressing in the panel driver.
3. **Keep the flush handshake.** Never call `esp_lcd_panel_draw_bitmap()` without waiting (with a timeout, and aborting the frame on timeout) for the previous colour transfer. `esp_lcd`'s SPI transport recycles in-flight transfers with `portMAX_DELAY`.
4. **Memory placement:** LVGL frame buffer in **PSRAM**; transport buffers in **internal DMA RAM** and small; PSRAM is not SPI-DMA-capable.
5. **Never take the LVGL lock unbounded** from a non-LVGL task (`bsp_display_lock(0)` = infinite). Use a bounded timeout; skip on failure.
6. **Keep BLE/Wi-Fi on core 0** and **`CONFIG_SPI_MASTER_ISR_IN_IRAM=y`**.
7. **Keep touch on the bounded I2C transport** (50 ms) with bus recovery.
8. **Radios stay single-mode;** switching is save+reboot.
9. **Watch internal RAM** (`int_largest` ≈ 31 KB after BLE). Prefer PSRAM for anything large; do not grow task stacks/DMA buffers without measuring.
10. **Do not casually change the QSPI clock/geometry.** 40 MHz + 20-line tiles are tuned; 20 MHz made frame tearing worse and did not fix stalls.
11. **Hotkey/text output goes through the `kb_out` queue**, not directly from an LVGL event handler.
12. **Keep `lv_conf.h` in `lib/lvgl/`** (do not switch to the managed LVGL component) unless you migrate the UI code to LVGL v9.

---


## 8. Simulator (PC, SDL2)

`simulator/` is a **separate** PlatformIO project that renders the **real UI modules** (`ui_renderer`, `splash_screen`, `status_bar`, `settings_page`, `gesture_handler`, `ui_loader`, `ui_assets`) in an **SDL2** (or Win32) window, using the project's local LVGL 8.4 and the local `tools/sdl2/` (no downloads at build time). The display/input backend is implemented directly in `main_host.cpp` instead of `lv_drivers`, which is not available offline.

**The UI sources are compiled in place from `../src`** (via `env.BuildSources()` in `extra_paths.py`) — there is **no `src/project/` copy** any more. Only the host entry point, the shims and cJSON live under `simulator/src/`, so the simulator always reflects the current firmware UI code. `splash_screen.cpp` includes `esp_bsp.h`; that header is compiled in place too, so it has a `#ifdef HOST_BUILD` branch exposing just the LVGL mutex helpers.

```bat
start.bat                           :: main menu: [10] SDL2, [11] Win32
simulator\run_simulator.bat         :: SDL2  (env native,      needs SDL2.dll)
simulator\run_win32_simulator.bat   :: Win32 (env native_win32, no DLL)
```

| env | entry point | window | deps |
|-----|-------------|--------|------|
| `native` | `src/main_host.cpp` | SDL2 | `SDL2.dll` next to the exe |
| `native_win32` | `src/main_host_win32.cpp` | Win32/GDI | none |

```
simulator/
├── platformio.ini          # [env:native]
├── lv_conf.h               # PC LVGL config (COLOR_16_SWAP 0, MEM_CUSTOM 0)
├── extra_paths.py          # include paths, defines, local SDL2, -Wl,--wrap=fopen
├── run_simulator.bat       # checks SDL2, syncs, builds, copies SDL2.dll, runs
├── sync_project.bat        # no-op now (data is used in place; kept for the launchers)
├── src/
│   ├── main_host.cpp            # SDL2 window + flush + mouse/touch + swipe
│   ├── main_host_win32.cpp      # Win32/GDI window backend (env native_win32)
│   ├── host_stubs.cpp           # "hardware" stubs + __wrap_fopen
│   ├── cjson/                   # cJSON (copied from the local IDF package)
│   └── compat/                  # fake esp_*/freertos/driver headers
```

`fopen` is redirected at link time with `-Wl,--wrap=fopen`: `__wrap_fopen` rewrites `/littlefs/...` to `../datadevice/...` and `/sdcard/...` to `../datasdcard/...`. The simulator runs with its CWD set to `simulator/`, so it reads and writes the **project's real `datadevice/` and `datasdcard/` folders - the same ones `uploadfs` / `sync_sd.bat` deploy**. A config saved in the emulator is therefore the config that gets built and flashed (no intermediate copy, no "edited in the emulator but not on the device" gap). Hardware calls (`set_brightness`, `send_hotkey`, `init_keyboard`, `init_web_server`, the LVGL mutex) are stubbed in `host_stubs.cpp`. Swipe navigation and button presses are driven by the SDL event loop through the public tabview helpers.

Notes:

- `ui_loader` is plain C in the firmware too (`src/ui_loader.c`), because its C99 designated initializers are rejected in C++ by the host MinGW GCC 5.1. Keep C-only modules C-compatible.
- SDL2 must be the **32-bit (i686)** dev package in `tools/sdl2/`.
- `SIM_AUTOEXIT_MS=<ms>` runs for a few seconds then exits (smoke test).

**Requirements:** PlatformIO's `native` platform does not bundle a compiler:

```bat
pio pkg install -g -t platformio/toolchain-gccmingw32
```

`run_simulator.bat` adds it to `PATH`. The main firmware build (`pio run`) is unaffected — this folder is a separate PlatformIO project not referenced by the ESP-IDF build.

---


## 9. Anti-patterns (don't repeat)

Every item below is a mistake that was **actually made and fixed** during this project. Read this before touching the driver, the flush path, logging, the task layout or `sdkconfig`. Each row points at the section with the working rule.

| Symptom | Root cause | Do instead |
|---------|-----------|------------|
| Hard UI freeze under Bluetooth (screen dark, BLE still connected) | SPI2 (QSPI panel) interrupt was bound to **core 0**, shared with Bluedroid; a QSPI transfer stalls and IDF's `esp_lcd` waits forever | Initialise the panel from a **core-1 task** (`display_init_task`) — see 1.3 |
| Random freeze with no touches | Touch read from the LVGL task via `esp_lcd_panel_io_i2c`, which uses timeout `-1` (wait forever) | Bounded raw `i2c_master` transport (50 ms) + bus recovery — see 1.6 |
| Permanent hang inside `esp_lcd_panel_draw_bitmap` | The flush called `draw_bitmap()` while a colour DMA transfer was still in flight (or pre-gave the done semaphore); IDF recycles in-flight transfers with `portMAX_DELAY` | Strict handshake: wait on `trans_done_sem` before **every** `draw_bitmap`, abort the frame on timeout — see 1.4 |
| Deadlock when a background task updates the UI | `bsp_display_lock(0)` = `portMAX_DELAY` from a non-LVGL task | Bounded lock (100 ms / 50 ms) and skip the update on timeout — see 1.5 |
| UI stutters / loses frames while typing over BLE | HID call executed straight from the LVGL event callback | Queue it to the `kb_out` task — see 1.5 |
| `warning: "LOG_LOCAL_LEVEL" redefined` in IDF components | Global `-DLOG_LOCAL_LEVEL=ESP_LOG_VERBOSE` clashes with per-file `#define LOG_LOCAL_LEVEL` | Do not pass the `-D`; set the level in `sdkconfig` / change it at runtime |
| Boot stalls after the radio comes up (screen dark, BLE + COM alive) | Global `CONFIG_LOG_DEFAULT_LEVEL_VERBOSE` floods the USB-Serial-JTAG console; writes block when no reader (monitor) is attached | Keep the default **INFO**; enable verbose per tag at runtime with `esp_log_level_set()` — see 4 |
| A `sdkconfig.defaults` edit has no effect | PlatformIO uses `sdkconfig.<env>` (`sdkconfig.modipad`) and ignores `sdkconfig.defaults` while that file exists | Edit `sdkconfig.modipad` (or delete it to re-derive from defaults) — see 4 |
| `mklittlefs`: `error adding file! ... Error for adding content from pages!` | A filename in `images/pages/` longer than 15 characters | Keep page filenames ≤ 15 chars (truncate to the last 15) and update references — see [Interface](interface.md) 3 |
| Page transitions animate (against the no-animation rule) | LVGL's tabview `cont_scroll_end_event_cb` snaps to the nearest tab with **`LV_ANIM_ON`** (hardcoded in `lv_tabview.c`), triggered by the `LV_EVENT_SCROLL_END` the widget itself emits | Cancel the scroll animation before switching (`lv_anim_del(content, NULL)`) and patch the tabview snap to `LV_ANIM_OFF` — see 1.9 |
| "New" UI behaviour appears right after a config change | Editing `sdkconfig*` / `lv_conf.h` forces a full rebuild, which can surface behaviour a **stale** library object had hidden | After such changes, re-verify UI behaviour; don't assume the running binary matches the source |
| Crash / memory corruption when switching radios at runtime | BLE and Wi-Fi share scarce internal RAM; tearing a stack down corrupts memory | Radios stay mutually exclusive; switch by **save + reboot** — see 1.7 |
| Garbage image / wrong colours / freeze on partial refresh | The AXS15231B QSPI path only addresses full frames (CASET only, sequential `0x2C`/`0x3C`) | Keep `full_refresh = 1`; never enable partial refresh without windowed addressing in the driver — see 1.4 |
| `malloc` failures / radio fails to start after adding a feature | Large buffer or stack taken from internal RAM (PSRAM is used for big buffers) | Prefer PSRAM for anything large; keep DMA buffers tiny; measure `int_largest` in the heartbeat — see 2.3 |
| Wi-Fi AP start crashes / event lists corrupt | Default 2304 B system-event task stack is too small | `CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE=6144` — see 4 |

---


## 10. Working configuration & best practices

The settings and patterns below are the ones that are known to work on this board. Treat them as the default and deviate only with a measurement.

**Cores & tasks**
- Core 1: the LVGL task **and** the panel bring-up (`display_init_task`).
- Core 0: Bluedroid, Wi-Fi, `httpd`, `obs_client`, and the low-priority `heartbeat` (priority 1).
- Panel (SPI2) ISR lives on core 1; `CONFIG_SPI_MASTER_ISR_IN_IRAM=y`.

**Memory**
- LVGL frame buffer in **PSRAM**; transport buffers small in **internal DMA** RAM; log ring and asset path strings in PSRAM.
- Internal RAM is the scarce resource (~31 KB largest block after BLE) — never grow stacks/DMA buffers without checking the heartbeat.

**Rendering**
- QSPI at 40 MHz, 20-line tiles, `full_refresh = 1`, 90° software rotation, TE-synced at the first tile, flush serialised with bounded waits and frame-abort on timeout.

**Touch**
- Raw `i2c_master` device (addr `0x3B`, 400 kHz) with a 50 ms timeout; bus recovery after 3 consecutive read errors.

**Concurrency**
- All LVGL access from the LVGL task or under a **bounded** `bsp_display_lock`; HID output through the `kb_out` queue; heavy Wi-Fi work on the `wifi_req`/`wifi_cmd` tasks.

**Radio & storage**
- One radio at a time; switch by save + reboot. Dual-OTA + rollback + boot-loop guard. `config.json` (pages/language/obs/network) and `settings.json` (`AppSettings`) are both re-loaded into RAM on a web POST.

**Logging**
- Default level **INFO**; raise per tag at runtime (`esp_log_level_set`). Logs are tee'd into a PSRAM ring served at `/api/log`. Never enable a global VERBOSE default (USB-Serial-JTAG console blocks without a reader).

**Diagnostics**
- `MODIPAD_DIAG_STAGE` for isolation; heartbeat prints `loops` (liveness), `flush/done/exit/skip` and `int_largest`; a frozen `loops` triggers a self-reboot.

**Animations**
- None. Page switches are instant; the LVGL tabview snap is patched to `LV_ANIM_OFF` and the running scroll animation is cancelled before switching.

**Assets & build config**
- Page filenames ≤ 15 chars; `asset_resolve_image()` checks flash `S:` then SD `D:`. `sdkconfig.modipad` is the effective build config.
- The simulator compiles the **production** sources directly (no copies), with a `HOST_BUILD` branch in `esp_bsp.h` and stubs in `host_stubs.cpp`. Its `lv_conf.h` mirrors the device's `LV_GRAD_CACHE_DEF_SIZE`/`LV_DITHER_GRADIENT` so device-only gradient bugs reproduce on the PC.
- Vendored LVGL patch: `lv_draw_sw_gradient.c` `compute_key()` hashes the gradient **contents**, not the descriptor pointer. The stock pointer key made every same-size gradient (e.g. all home/multimedia tiles) reuse the first gradient's colours once `LV_GRAD_CACHE_DEF_SIZE != 0`.

**Versioning**
- `MODIPAD_FIRMWARE_VERSION` in `src/config.h` is the single source of truth (About page, and SD backup names `backup_<version>_<n>.json`). Mirror it in `datadevice/web/app.js` (`APP_VERSION`) and the `?v=` cache busters.
- `extra_script.py` (post-build) archives every build into `firmware/<version>/` so older images are kept for history.

## 11. Board pinout & wiring

The whole board is the JC3248W535EN module; the only external connector is the microSD slot. The display, touch and backlight lines are fixed on the module and defined in `src/config.h`.

| Signal | GPIO | Signal | GPIO |
|--------|------|--------|------|
| CS     | 45   | DATA2  | 40   |
| PCLK   | 47   | DATA3  | 39   |
| DATA0  | 21   | DC     | 8 (unused for QSPI) |
| DATA1  | 48   | TE     | 38   |
|        |      | BL     | 1    |

- **Display (QSPI):** CS = GPIO45, PCLK = GPIO47, DATA0..3 = GPIO21/48/40/39, DC = GPIO8 (unused for QSPI), TE = GPIO38.
- **Touch (I2C):** SDA = GPIO4, SCL = GPIO8. No touch INT and no touch RST pins (both `-1`), and no external I2C pull-ups are configured; the bus runs at 400 kHz on the new `i2c_master` driver.
- **Backlight:** GPIO1 (PWM brightness).
- **microSD (SDMMC 1-bit):** D0 = GPIO13, CLK = GPIO12, CMD = GPIO11 (internal pull-ups).
- **USB:** the ESP32-S3 native USB-Serial-JTAG (uses the module's USB connector, no GPIO pins).

## 12. Build scripts & tooling

`start.bat` (project root) is the menu; its items call the scripts under `scripts/` and the emulator launchers under `simulator/`.

| # | Script | What it does |
|---|--------|--------------|
| 1 | `build.bat` | `pio run -t clean` then `pio run` - full rebuild, nothing is flashed. Output: `.pio/build/modipad/firmware.bin` |
| 2 | `flash_firmware.bat` | `pio run -t upload` - firmware only (does **not** touch LittleFS) |
| 3 | `flash.bat` | lists the available COM ports, then `pio run -t upload` + `pio run -t uploadfs` - firmware **and** internal LittleFS |
| 4 | `upload_files_only.bat` | `pio run -t uploadfs` - only `datadevice/` (config.json, images, web, fonts); no firmware reflash. Reboot the device to apply |
| 5 | `sync_sd.bat` | copies `datasdcard/modipad` to the SD card's `\modipad`. Finds the card by volume label `MODIPAD` (via `scripts/find_sd.ps1`); if not found it asks for a drive letter and offers to (re)label the card so later runs are automatic |
| 6 | `flash_all.bat` | everything: firmware + LittleFS + SD (menu 2 + 4 + 5) |
| 7 | `monitor.bat` | `pio device monitor --baud 115200` (Ctrl+C to exit) |
| 8 | `open_web.bat` | opens `http://192.168.4.1` in the browser. The device must be in Wi-Fi **AP** mode - connect to `ModiPAD_Setup` (password `12345678`) first |
| 9 | `clean.bat` | asks for confirmation, then `pio run -t clean` and deletes `.pio/` - full clean; the next build re-fetches dependencies |
| 10 | `simulator\run_simulator.bat` | SDL2 emulator: checks `tools/sdl2/bin/SDL2.dll`, syncs the data folders, builds env `native`, copies `SDL2.dll` next to the exe and runs. Needs the **32-bit (i686)** SDL2 dev package in `tools/sdl2/` and MinGW |
| 11 | `simulator\run_win32_simulator.bat` | Win32/GDI emulator: syncs data, builds env `native_win32` (no SDL2) and runs. Only MinGW needed |
| 12 | `run_web_preview.bat` | local web-UI preview without the device: serves `datadevice/` with `python tools/utils/web_preview_server.py 8765` and opens `http://127.0.0.1:8765/`. Edits are saved back to `datadevice/config.json`, exactly like the on-device server |
| 13 | `generate_images.bat` | runs `tools/utils/generate_assets.ps1` to (re)generate the PNG library, then optimizes every `datadevice/images/**.png` with the first available compressor (Oxipng -> ECT -> OptiPNG 64/32 -> system) and prints the bytes saved |
| 14 | `optimize_images.bat` | lossless re-optimization **in place** (Oxipng `-o6` / ECT `-9` / OptiPNG `-o7`); no visual change |
| 15 | `optimize_max.bat` | Oxipng (lossless) **plus Pngquant (lossy, 65-90, `--skip-if-larger`)** - smallest files, slightly lower quality. Use only when size matters |
| 16 | `generate_fonts.bat` | rebuilds the LVGL fonts with `lv_font_conv` from `tools/fonts/Roboto-{Regular,Bold}.ttf`: sizes 10/12/14/16/18 + bold, bpp 4, ranges `0x20-0x7F` + `0x400-0x4FF` -> `datadevice/fonts/roboto_*.bin`. First run needs `npm install -g lv_font_conv` |
| 17 | `compress_backgrounds.bat` | aggressive compression of the SD backgrounds in `datasdcard/modipad/backgrounds` via `tools/utils/compress_backgrounds.py` (PNG-8 by default; `--method both`, or `--method jpg --quality 35`) |

Extra scripts that are **not** in the menu:
- `scripts/build_and_flash.bat` - full cycle (clean -> build -> upload -> uploadfs) and then offers to start the serial monitor.
- `scripts/find_sd.ps1` - the SD auto-detect helper used by `sync_sd.bat`.
- `pio run -t buildfs` - builds the LittleFS image (`.pio/build/modipad/littlefs.bin`) without flashing it.

Notes:
- Menu items 2/3/4/5/6 need the board on a USB port; item 5 needs the microSD inserted.
- The `native` PlatformIO platform does not bundle a compiler: install MinGW once with `pio pkg install -g -t platformio/toolchain-gccmingw32` (the simulator launchers add it to `PATH`).
- The serial monitor defaults to 115200 baud with the `esp32_exception_decoder` filter.

## 13. Troubleshooting

### Screen freezes / goes dark and does not wake
- Check the `HEARTBEAT` line: `loops` frozen + `stage=23` means the LVGL task is blocked in the panel transfer. The heartbeat reboots automatically after ~10 s; capture the log.
- Verify the panel is still initialised from **core 1** (invariant 1) and that the flush handshake (invariant 3) was not removed.
- `skip` growing while `loops` advances = frames aborted on a DMA timeout (UI alive, only frames missing). Usually transient.

### Internal RAM pressure (`int_largest` small / radio fails to start)
- Something large was allocated in internal RAM. Move it to PSRAM, shrink DMA buffers/stacks, or free it. See 2.3.

### Wi-Fi or BLE will not start after a change
- Likely internal RAM exhaustion from a new allocation. Check `int_largest` in the heartbeat right before the radio starts.

### PSRAM slowly drains while navigating
- Pages are lazily built and kept in RAM (schema v3, `ui_renderer.cpp`). Each visited page costs PSRAM. This is by design; freeing hidden pages is a possible future optimisation.

### Verbose logs after a reboot
- `-DLOG_LOCAL_LEVEL=ESP_LOG_VERBOSE` plus the bootloader log level produce a large boot dump. Lower `LOG_LOCAL_LEVEL` (and optionally the bootloader log level) for release builds.

### Nothing on the panel / wrong colours
- Confirm `full_refresh = 1`, QSPI geometry (`use_qspi_interface`), RGB element order and rotation. Do not switch to partial refresh.

### Notes
- LittleFS is provided by the `joltwallet/littlefs` component (declared in `src/idf_component.yml`); it is fetched automatically by the build.
- LVGL loads images through its POSIX FS driver (`LV_USE_FS_POSIX`, letter `S`, base path `/littlefs`); `LV_USE_FS_FATFS` is disabled to avoid a drive-letter clash.
- The `storage` partition is created by `partitions/modipad_16MB.csv`.
- This project is an ESP-IDF port of the JC3248W535EN `DEMO_LVGL` (Arduino) reference; keep the driver files (`esp_lcd_axs15231b.[ch]`, `esp_lcd_touch.[ch]`) close to the vendor version and re-apply the two deliberate deviations (bounded touch I2C transport, 40 MHz QSPI) if you re-sync them.
