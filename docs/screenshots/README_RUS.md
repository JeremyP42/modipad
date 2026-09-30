# Скриншоты

**English:** [README.md](README.md)

Все экраны проекта: интерфейс прошивки (снят с PC-эмулятора, но это реальные кадры устройства 480x320) и полные страницы веб-конфигуратора.

- [Прошивка (устройство)](#прошивка-устройство)
- [Веб-конфигуратор](#веб-конфигуратор)

> Кадры устройства получены из эмулятора (`[11] Emulator: Win32`), который компилирует те же исходники `src/`, что и прошивка, поэтому они повторяют экран устройства пиксель в пиксель. Веб-страницы сняты с локального preview-сервера (`[12] Web UI preview`).

---

## Прошивка (устройство)

### Главная страница
Лаунчер: сетка плиток с иконками приложений и верхний статус-бар (имя страницы, индикаторы Bluetooth/Wi-Fi и, при желании, FPS/CPU). Свайп влево/вправо переключает страницы, свайп вниз возвращает на главную, свайп вверх приглушает яркость.

![Главная страница](device/main.png)

### Пользовательские страницы

| Страница | Описание |
|----------|----------|
| [Windows](device/page-windows.png) | Горячие клавиши Windows (Win, Win+E, Win+D, Win+R, …) |
| [Firefox](device/page-firefox.png) | Горячие клавиши браузера (Ctrl+T, Ctrl+W, F5, …) |
| [Chrome](device/page-chrome.png) | Горячие клавиши Chrome (новая вкладка, инкогнито, …) |
| [VS Code](device/page-vscode.png) | Команды редактора (Ctrl+Shift+P, Ctrl+P, терминал, …) |
| [OpenCode](device/page-opencode.png) | Команды OpenCode (новая сессия, отправка, отмена, …) |
| [Multimedia](device/page-multimedia.png) | Управление воспроизведением (Prev/Play/Next, громкость, mute) |
| [OBS Studio](device/page-obsstudio.png) | Управление записью OBS (Start/Stop/Record, проверка статуса) |

<p>
  <img src="device/page-windows.png" width="240" alt="Windows">
  <img src="device/page-firefox.png" width="240" alt="Firefox">
  <img src="device/page-chrome.png" width="240" alt="Chrome">
</p>
<p>
  <img src="device/page-vscode.png" width="240" alt="VS Code">
  <img src="device/page-opencode.png" width="240" alt="OpenCode">
  <img src="device/page-multimedia.png" width="240" alt="Multimedia">
</p>
<p>
  <img src="device/page-obsstudio.png" width="240" alt="OBS Studio">
</p>

### Настройки

| Экран | Описание |
|-------|----------|
| [Меню настроек](device/settings-menu.png) | Плитки разделов: Режим, OBS, Общие, Конфигурация, Система, О системе |
| [Режим](device/settings-mode.png) | Выбор радио: BLE-клавиатура / Wi-Fi точка доступа / Wi-Fi клиент |
| [OBS](device/settings-obs.png) | Адрес, порт и пароль OBS WebSocket |
| [Общие](device/settings-general.png) | Яркость, язык, звук кнопок, тайм-аут сна, шрифт подписей, стиль кнопок |
| [Конфигурация](device/settings-configuration.png) | Имя устройства Bluetooth, параметры Wi-Fi AP и Wi-Fi клиента |
| [Система](device/settings-system.png) | Резервное копирование/восстановление конфига на SD, обновление прошивки |
| [О системе](device/settings-about.png) | Оборудование, версии (LVGL/IDF), системная информация, стеки задач |

<p>
  <img src="device/settings-menu.png" width="240" alt="Меню настроек">
  <img src="device/settings-mode.png" width="240" alt="Режим">
  <img src="device/settings-obs.png" width="240" alt="OBS">
</p>
<p>
  <img src="device/settings-general.png" width="240" alt="Общие">
  <img src="device/settings-configuration.png" width="240" alt="Конфигурация">
  <img src="device/settings-system.png" width="240" alt="Система">
</p>
<p>
  <img src="device/settings-about.png" width="240" alt="О системе">
</p>

---

## Веб-конфигуратор

Открывается из браузера (адрес устройства `http://192.168.4.1` в режиме точки доступа или локальный preview-сервер). Ниже — все разделы верхнего меню.

### Основные
Яркость, звук кнопок, тайм-аут сна, режим устройства, Bluetooth (имя устройства для Windows редактируемое), Wi-Fi точка доступа, Wi-Fi клиент (OBS), OBS и резервные копии конфига.

![Основные](web/system.png)

### Страницы
Список, создание и удаление страниц, выбор главной, предпросмотр.

![Страницы](web/pages.png)

### Кнопки
Редактор кнопки: действие (hotkey/text/macro/page/multimedia/OBS), иконка, фон, подпись, сетка страницы и интерактивный предпросмотр.

![Кнопки](web/buttons.png)

### Стили
Пресеты оформления кнопок (скругление, рамка, тень, шрифт подписей) с живым превью.

![Стили](web/styles.png)

### Библиотека
Каталог изображений устройства и SD-карты: загрузка, удаление, категории.

![Библиотека](web/library.png)

### Предпросмотр
Устройство-подобный предпросмотр страниц (статус-бар, фоны, иконки, подписи).

![Предпросмотр](web/preview.png)

### Устройство
Оборудование, версии (прошивка/сборка/LVGL/IDF), полная системная информация (температура, память, ФС, SD, MAC, стеки задач), обновление прошивки и диагностический лог — те же параметры, что на экране «О системе» прошивки.

![Устройство](web/device.png)
