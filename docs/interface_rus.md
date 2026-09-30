# Интерфейс

**English:** [interface.md](interface.md)

Этот документ описывает интерфейс ModiPAD: интерфейс устройства и жесты, конфигурацию `config.json` / `settings.json`, веб-конфигуратор и библиотеку ресурсов (изображения/шрифты). Скриншоты всех экранов — в [docs/screenshots](../screenshots/README_RUS.md). Архитектура описана в [Архитектуре](architecture_rus.md); главная страница — [README](../README_RUS.md).

## Оглавление
1. [Конфигурация](#1-конфигурация)
2. [Веб-интерфейс](#2-веб-интерфейс)
3. [Ресурсы (Assets)](#3-ресурсы-assets)

---

## 1. Конфигурация

### `config.json` (схема v3)
Верхний уровень: `settings` (язык/яркость/таймаут сна), опциональная `main_page` и массив `pages[]`. `main_page` (при наличии) отображается как вкладка 0; ссылки на страницы и панель статуса учитывают это смещение.

Кнопки могут использовать:
* `type`: `settings` (открывает вкладку настроек) или `page_link` (с `target_page` = `id` страницы); `target_page` может быть `__HOME__` для главной страницы.
* `icon`: имя изображения из `images/icons/pages` (отображается над подписью).
* `caption`: простая строка (метка кнопки) как альтернатива `text.lines`.
* `action.type`: `hotkey`, `text`, `macro` (горячие клавиши, разделенные `;`), `multimedia`, `obs` или `page`.

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
* `background.type`: `image`, `solid` (с `color`) или `transparent`.
* `background.scale_mode`: `cover`, `contain`, `center` (LVGL 8.4 поддерживает только равномерное масштабирование; `stretch` / `tile` откатываются к `cover` / `center`).
* Синтаксис `hotkey`: модификаторы `CTRL`, `SHIFT`, `ALT`, `GUI`, соединенные через `+`, например: `CTRL+SHIFT+S`, `GUI+L`, `ALT+F4`.
* Страницы строятся лениво (при первом просмотре) и сохраняются в RAM после этого (`ui_renderer.cpp`); это стоит памяти PSRAM для каждой посещенной страницы. См. раздел [README](../README_RUS.md) 6.

### `settings.json`
Содержит `brightness`, `sound_enabled`, `radio_mode` (источник истины), вычисляемые `wifi_enabled` / `ble_enabled`, `sleep_timeout` (секунды, 0 = никогда), `show_stats` (вывод FPS/CPU в верхнем баре), `splash_bg`, `settings_bg`, `menu_bg`, плюс конечную точку OBS (`obs.host/port/password`) и сетевые учетные данные.

---


## 2. Веб-интерфейс

Подключитесь к точке доступа Wi-Fi `ModiPAD_Setup` (пароль `12345678`) и откройте `http://192.168.4.1/`.
* **System** – режим радио, яркость, звук, таймаут сна, фоны.
* **Pages** – добавление/переименование/удаление страниц.
* **Buttons** – настройки сетки для каждой страницы с интерактивным предпросмотром, полный редактор кнопок.
* **Library** – просмотр/загрузка/удаление изображений по категориям.
* **Preview** – точный рендеринг каждой страницы, как на устройстве.
* **Log / Backup / OTA** – просмотр логов в реальном времени, резервные копии конфигурации на SD, загрузка прошивки.

**REST API:** `GET/POST /api/config`, `GET/POST /api/settings`, `GET /api/system`, `POST /api/reload`, `GET /api/log`, `POST /api/ota`, `POST /api/ota/sd`, `GET /api/ota/status`, `GET /api/images?dir=...`, `POST /api/upload?path=...`, `POST /api/delete?path=...`, `POST /api/backup/{save,import}`, `GET /api/backup/list`, `GET /api/fonts`, статические `GET /images/*` и `GET /locales/*`.

### Контракт JSON веб ⇄ прошивка

Два файла, два «владельца»:

| Файл | Кто пишет | Кто читает/использует |
|------|-----------|------------------------|
| `config.json` | веб `POST /api/config` **и** устройство (смена языка, настройка Wi-Fi) | `ui_loader`/`ui_renderer` (`pages`, `main_page`), `i18n` (`settings.language`), `obs_client` (`obs`), `wifi_manager` (`network`) |
| `settings.json` | веб `POST /api/settings` **и** устройство (кнопки яркости/звука/сна) | `ui_loader` `AppSettings` (яркость, звук, `radio_mode`, сон, шрифты подписей, фоны) |

- Веб редактирует **файлы целиком** (`state.config` / `state.settings` — это распарсенные файлы) и отправляет их обратно, поэтому неизвестные поля сохраняются.
- **`POST /api/config` и `POST /api/settings` сразу перечитывают загруженный JSON в RAM** (`load_config()` / `load_settings()`), иначе последующий вызов на устройстве `save_config()` / `save_settings()` записал бы устаревшую копию из RAM и отбросил загрузку. Яркость также применяется на лету.
- Большинство изменений вступает в силу **после перезагрузки**: в вебе есть кнопка **Перезагрузить** (`POST /api/reload`); страницы/язык/сеть/радио применяются при загрузке. `radio_mode` — единственный источник истины; `wifi_enabled`/`ble_enabled` выводятся из него при загрузке.

### Вкладка «Устройство»

Веб-вкладка **Устройство** повторяет страницу прошивки **О системе**: блоки *Оборудование*, *Версии* и *Системная информация* показывают те же строки в том же порядке — текст об оборудовании, затем `Версия / Сборка / LVGL / ESP-IDF`, затем температура, чип, `CPU + Flash`, время работы, SRAM/PSRAM/Heap/`FS Устройства`, SD (свободно/всего + файловая система карты), WiFi RSSI+reconnects, WiFi/BT MAC, Сборка, Сброс, IDF и стеки задач (`имя: свободно/всего B`). `/api/system` отдаёт все поля, включая `stack_total` для задач.

### Языки

Язык UI выбирается из `localStorage` (с откатом к языку браузера: `ru` для русского, иначе `en`) и может быть переключен в заголовке. Переводы представляют собой простые JSON-файлы в `datadevice/web/locales/`; скопируйте один из них, чтобы добавить другой язык, затем добавьте кнопку в `index.html` для его отображения.

---


## 3. Ресурсы (Assets)

Библиотека `datadevice/images` поставляется с программно сгенерированными PNG:
* 24 подобранных вручную градиента (20 популярных + 4 устаревших имени) плюс 70 сгенерированных градиентов (`gradient_auto_*`), кэшированных для каждого размера кнопки – каждая из папок `datadevice/images/buttons/100x100` и `.../70x70` содержит ровно 100 тайлов (188 градиентов + 8 сплошных цветов + 4 паттерна).
* 8 сплошных цветов, 4 паттерна (дерево, карбон, металл, бетон).
* 7 фонов страниц, 3 системных фона, 10 иконок.

Перегенерируйте их с помощью:
```bash
pwsh -File tools/utils/generate_assets.ps1        # или scripts\generate_images.bat
pwsh -File tools/utils/generate_gradients.ps1     # 70 дополнительных градиентов на размер
```

PNG является форматом без потерь, поэтому сжатие влияет только на размер файла. GDI+ игнорирует параметр `Encoder.Compression` для PNG, поэтому реальное уменьшение выполняется внешним инструментом. Скрипт `scripts\generate_images.bat` выполняет генерацию, а затем оптимизацию без потерь за один проход; `scripts\optimize_images.bat` оптимизирует на месте; оба выбирают первый доступный компрессор:
1. `tools\oxipng\oxipng.exe` (предпочтительно: без потерь, многопоточный, быстрый)
2. `tools\ect\ect.exe`
3. `tools\optipng\optipng64.exe` / `optipng32.exe`
4. Системный `oxipng` / `optipng` в `PATH`

Если ни один не найден, этап оптимизации пропускается. Измерения на этой библиотеке (272 файла):
| Этап | Инструмент | Размер |
| :--- | :--- | :--- |
| Сырая генерация | GDI+ | ~993 КБ |
| Без потерь | Oxipng | ~643 КБ (экономия 35 %) |
| С потерями | + Pngquant (`--skip-if-larger`) | ~363 КБ (общая экономия 63 %) |

Скрипт `scripts\optimize_max.bat` дополнительно запускает Pngquant (с потерями) после Oxipng и является опциональным; он использует `--skip-if-larger`, потому что на этих уже крошечных PNG Pngquant в противном случае увеличил бы размер некоторых файлов. Поставляемые изображения сохраняются без потерь (Oxipng). См. `tools/README.md`.

### Имена файлов страниц (не более 15 символов)

Имена файлов в `datadevice/images/pages/` и `datasdcard/modipad/images/pages/` должны быть **не длиннее 15 символов вместе с `.png`**. `mklittlefs` не может упаковать папку `pages`, если имя длиннее (`error adding file! ... Error for adding content from pages!`), поэтому такие имена обрезаются до **последних 15 символов** (например, `0900f1bf…fef27f_p82r.png` → `fef27f_p82r.png`, `page_bg_dark.png` → `age_bg_dark.png`). При переименовании обязательно обновите все ссылки (`config.json`, а также значения по умолчанию в коде и веб-интерфейсе). Генератор ресурсов (`tools/utils/generate_assets.ps1`) и значения по умолчанию в `ui_loader.c` уже используют короткие (≤ 15) имена.

### Медиа на SD-карте (`datasdcard/`)
Внутренний LittleFS (прошиваемый из `datadevice/`, макс. ~8 МБ) хранит только `config.json`, `settings.json`, шрифты, веб-UI и небольшой набор ресурсов по умолчанию. Объемные медиафайлы хранятся на карте microSD:
| Тема | Значение |
| :--- | :--- |
| Интерфейс | SDMMC 1-bit (D0=GPIO13, CLK=GPIO12, CMD=GPIO11), внутренние подтяжки |
| Точка монтирования | `/sdcard`, диск LVGL `D:` (`/sdcard/modipad`) |
| Структура карты | `\modipad\backgrounds`, `\modipad\images`, `\modipad\sounds` |
| Папка в репозитории | `datasdcard/modipad/...` |
| Развертывание | `scripts\sync_sd.bat` (меню [5]) копирует это на карту |

`asset_resolve_image()` сначала ищет во flash (`S:`), затем на карте (`D:`), поэтому фон может находиться в любом из этих мест без изменения `config.json`. Эмуляторы зеркально отражают это (см. раздел [Архитектура](architecture_rus.md) 8).

### Кириллические шрифты (Roboto)
Шрифты UI генерируются из `tools/fonts/Roboto-Regular.ttf` / `Roboto-Bold.ttf` в бинарные шрифты LVGL с помощью `scripts/generate_fonts.bat` (требует однократной установки `npm install -g lv_font_conv`). Он создает `datadevice/fonts/roboto_{10,12,14,16,18}.bin` и `..._bold.bin`, все покрывают ASCII + кириллицу (`0x20-0x7F`, `0x400-0x4FF`, bpp 4), что соответствует параметрам размера/жирности подписей на веб-вкладке System. `src/font_manager.[ch]` загружает их с помощью `lv_font_load("S:/fonts/...")` и откатывается к встроенным шрифтам Montserrat, если `.bin` отсутствует (также используется для `LV_SYMBOL_*`). UI использует `get_font(size)` / `get_font_bold(size)` вместо `&lv_font_montserrat_N`, поэтому кириллица корректно отображается как на устройстве, так и в симуляторе.

---
