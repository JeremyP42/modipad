#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
compress_backgrounds.py — агрессивное сжатие фоновых изображений.

Методы:
  jpg   : JPG quality 30-40 + Progressive + optimize  (~5-9 KB для 480x320)
  png8  : индексированный PNG-8, 64/128 цветов + дизеринг Floyd-Steinberg
  both  : сделать оба варианта рядом для сравнения размеров

Использование:
  python compress_backgrounds.py -i datasdcard/modipad/backgrounds
  python compress_backgrounds.py -i datadevice/images -r --method png8 --colors 128
  python compress_backgrounds.py -i datasdcard/modipad/backgrounds --method both --dry-run
"""

import sys
import argparse
from pathlib import Path
from PIL import Image, ImageFilter

try:  # keep emoji output safe on non-UTF8 Windows consoles
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

IMAGE_EXTENSIONS = {'.png', '.jpg', '.jpeg'}


def compress_jpg(img: Image.Image, path: Path, quality: int) -> Path:
    """Экстремальный JPG: низкое качество + progressive + optimize."""
    if img.mode != 'RGB':
        img = img.convert('RGB')
    out = path.with_suffix('.jpg')
    img.save(out, 'JPEG', quality=quality, progressive=True, optimize=True)
    return out


def compress_png8(img: Image.Image, path: Path, colors: int) -> Path:
    """Индексированный PNG-8 с дизерингом Floyd-Steinberg."""
    has_alpha = img.mode in ('RGBA', 'LA') or (img.mode == 'P' and 'transparency' in img.info)
    if has_alpha:
        img = img.convert('RGBA')
        if img.getchannel('A').getextrema()[0] == 255:
            # Прозрачности нет — сбрасываем альфу, чтобы MEDIANCUT работал
            img = img.convert('RGB')
            method = Image.MEDIANCUT
        else:
            # Есть настоящая прозрачность — MEDIANCUT её не поддерживает
            method = Image.FASTOCTREE
    else:
        img = img.convert('RGB')
        method = Image.MEDIANCUT
    quantized = img.quantize(colors=colors, method=method,
                             dither=Image.FLOYDSTEINBERG)
    out = path.with_suffix('.png')
    quantized.save(out, 'PNG', optimize=True)
    return out


def process_file(src: Path, out_dir: Path, method: str, colors: int,
                 quality: int, dry_run: bool, recursive_root: Path,
                 max_width: int = 0, blur: float = 0.0):
    """Обработать один файл, вернуть список (путь, размер_до, размер_после)."""
    results = []
    orig_size = src.stat().st_size

    try:
        with Image.open(src) as img:
            img.load()

            # Пред-обработка (уменьшает вес фона; устройство всё равно растянет cover'ом)
            if max_width and img.width > max_width:
                nh = max(1, round(img.height * max_width / img.width))
                img = img.resize((max_width, nh), Image.LANCZOS)
            if blur and blur > 0:
                img = img.filter(ImageFilter.GaussianBlur(blur))

            # Относительный путь для сохранения структуры при recursive
            if recursive_root:
                rel = src.relative_to(recursive_root)
                dst_base = out_dir / rel.parent / src.stem
                dst_base.parent.mkdir(parents=True, exist_ok=True)
            else:
                dst_base = out_dir / src.stem

            if dry_run:
                # Только прикинуть: сжимаем во временный буфер не пишем
                print(f"  [dry] {src.name} ({orig_size/1024:.1f} KB)")
                return results

            if method in ('jpg', 'both'):
                out = compress_jpg(img, dst_base if method == 'jpg'
                                   else dst_base.with_name(dst_base.name + '_jpg'),
                                   quality)
                results.append((out, orig_size, out.stat().st_size))

            if method in ('png8', 'both'):
                out = compress_png8(img, dst_base if method == 'png8'
                                    else dst_base.with_name(dst_base.name + '_png8'),
                                    colors)
                results.append((out, orig_size, out.stat().st_size))

    except Exception as e:
        print(f"  ! Ошибка {src.name}: {e}", file=sys.stderr)

    return results


def main():
    ap = argparse.ArgumentParser(
        description='Агрессивное сжатие фоновых изображений')
    ap.add_argument('-i', '--input', required=True, help='входная директория')
    ap.add_argument('-o', '--output', default=None,
                    help='выходная директория (по умолчанию = входная, in-place)')
    ap.add_argument('-r', '--recursive', action='store_true',
                    help='обрабатывать подпапки рекурсивно')
    ap.add_argument('-m', '--method', choices=['jpg', 'png8', 'both'],
                    default='png8', help='метод сжатия (по умолчанию png8)')
    ap.add_argument('-c', '--colors', type=int, default=32,
                    help='цветов для PNG-8: 4..256 (по умолчанию 32)')
    ap.add_argument('-q', '--quality', type=int, default=35,
                    help='качество JPG 30-40 (по умолчанию 35)')
    ap.add_argument('--max-width', type=int, default=0,
                    help='уменьшить ширину до N px (0=выкл; напр. 240 для 480x320)')
    ap.add_argument('--blur', type=float, default=0.0,
                    help='пред-размытие фона, радиус в px (0=выкл)')
    ap.add_argument('--dry-run', action='store_true',
                    help='только показать файлы, не записывать')
    args = ap.parse_args()

    if args.colors < 2 or args.colors > 256:
        print("Ошибка: --colors должно быть 2..256", file=sys.stderr)
        sys.exit(1)

    input_dir = Path(args.input)
    if not input_dir.exists():
        print(f"Ошибка: директория не найдена: {input_dir}", file=sys.stderr)
        sys.exit(1)

    out_dir = Path(args.output) if args.output else input_dir
    out_dir.mkdir(parents=True, exist_ok=True)

    print(f"📁 Вход : {input_dir.absolute()}")
    print(f"📂 Выход: {out_dir.absolute()}")
    print(f"🔧 Метод: {args.method}"
          + (f" ({args.colors} цветов)" if args.method in ('png8', 'both') else "")
          + (f" (quality {args.quality})" if args.method in ('jpg', 'both') else ""))
    print(f"📐 max-width: {args.max_width or 'off'}   blur: {args.blur or 'off'}")
    print(f"🔁 Рекурсивно: {'да' if args.recursive else 'нет'}")
    print("-" * 60)

    # Сбор файлов
    if args.recursive:
        files = [f for f in input_dir.rglob('*')
                 if f.is_file() and f.suffix.lower() in IMAGE_EXTENSIONS]
        root = input_dir
    else:
        files = [f for f in input_dir.iterdir()
                 if f.is_file() and f.suffix.lower() in IMAGE_EXTENSIONS]
        root = None

    if not files:
        print("Файлы не найдены")
        return

    print(f"Найдено файлов: {len(files)}")
    print("-" * 60)

    total_before = 0
    total_after = 0
    processed = 0

    for src in sorted(files):
        results = process_file(src, out_dir, args.method, args.colors,
                               args.quality, args.dry_run, root,
                               args.max_width, args.blur)
        for out, before, after in results:
            total_before += before
            total_after += after
            processed += 1
            ratio = (1 - after / before) * 100 if before else 0
            print(f"  ✓ {out.name}: {before/1024:.1f} KB → {after/1024:.1f} KB "
                  f"(-{ratio:.0f}%)")

    print("-" * 60)
    if not args.dry_run and processed:
        ratio = (1 - total_after / total_before) * 100 if total_before else 0
        print(f"✅ Обработано: {processed}")
        print(f"💾 Общий размер: {total_before/1024:.1f} KB → "
              f"{total_after/1024:.1f} KB (-{ratio:.0f}%)")
    elif args.dry_run:
        print(f"🔍 Dry-run: найдено {len(files)} файлов (запись не выполнялась)")


if __name__ == '__main__':
    main()
