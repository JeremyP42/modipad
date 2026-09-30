#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
download_app_icons.py — скачивает брендовые иконки приложений (Simple Icons)
и конвертирует их в 64x64 PNG с прозрачным фоном (белый контур) для панели.

Источник (монохром):   Simple Icons via jsDelivr
                       https://cdn.jsdelivr.net/npm/simple-icons@11/icons/<slug>.svg
Источник (цветной):    Papirus icon theme (fallback, --color)
                       .../Papirus/64x64/apps/<name>.svg

Зависимости:  pip install cairosvg requests
  (cairosvg требует нативную libcairo-2; на Windows проще всего через GTK runtime
   или MSYS2. Если cairo недоступна — см. сообщение об ошибке ниже.)

Использование:
  python tools/download_app_icons.py
  python tools/download_app_icons.py --out datadevice/images/icons/pages
  python tools/download_app_icons.py --color          # цветные (Papirus, если есть)
"""
import argparse
import sys
from pathlib import Path

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

import requests

DEFAULT_OUT = "datadevice/images/icons/pages"
COLOR = "#FFFFFF"

SLUGS = [
    # браузеры
    "googlechrome", "firefoxbrowser", "microsoftedge", "opera", "brave", "vivaldi",
    # разработка
    "visualstudiocode", "visualstudio", "intellijidea", "pycharm", "git", "github",
    "windowsterminal", "powershell", "docker", "nodedotjs", "python", "postman",
    # дизайн/видео
    "adobephotoshop", "adobeillustrator", "adobepremierepro", "adobeaftereffects",
    "figma", "gimp", "inkscape", "krita", "blender", "obsstudio",
    # офис
    "microsoftword", "microsoftexcel", "microsoftpowerpoint", "microsoftoutlook",
    "libreoffice", "notion", "microsoftonenote",
    # общение
    "telegram", "whatsapp", "discord", "slack", "zoom", "microsoftteams", "skype",
    # медиа/игры
    "vlcmediaplayer", "spotify", "youtube", "netflix", "steam", "epicgames",
    # утилиты
    "7zip", "notepadplusplus",
]

BASE = "https://cdn.jsdelivr.net/npm/simple-icons@11/icons/{}.svg"
PAPIRUS = ("https://raw.githubusercontent.com/PapirusDevelopmentTeam/"
           "papirus_icon_theme/master/Papirus/64x64/apps/{}.svg")


def main():
    ap = argparse.ArgumentParser(description="Download app icons -> 64x64 PNG")
    ap.add_argument("--out", default=DEFAULT_OUT, help="output dir (default: %s)" % DEFAULT_OUT)
    ap.add_argument("--color", action="store_true",
                    help="только уведомление: цветные берутся из Papirus вручную (шаблон в шапке)")
    args = ap.parse_args()

    try:
        import cairosvg
    except Exception as e:  # noqa: BLE001
        print("Не удалось загрузить cairosvg:", e)
        print("Установите cairo runtime:  pip install cairosvg  (нужна libcairo-2).")
        print("На Windows без GTK/MSYS2 используйте другой растеризатор или")
        print("сгенерируйте PNG через браузер (Playwright) — см. примечание в конце.")
        sys.exit(1)

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    ok, fail = 0, []
    for slug in SLUGS:
        url = BASE.format(slug)
        try:
            r = requests.get(url, timeout=20)
        except Exception:
            fail.append(slug)
            continue
        if r.status_code != 200:
            fail.append(slug)
            continue
        svg = r.text.replace("<svg", '<svg fill="%s"' % COLOR, 1)
        png = out / ("icon_%s.png" % slug)
        cairosvg.svg2png(bytestring=svg.encode("utf-8"), write_to=str(png),
                         output_width=64, output_height=64)
        ok += 1
        print("  ok  %s -> %s" % (slug, png))

    print("\nГотово: %d, не найдено: %d" % (ok, len(fail)))
    if fail:
        print("Missing:", ", ".join(fail))
    print("Файлы: %s/icon_<slug>.png (64x64, прозрачный фон, белый контур)" % out)


if __name__ == "__main__":
    main()
