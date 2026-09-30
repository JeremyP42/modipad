# OptiPNG (local, optional)

`scripts\generate_images.bat` and `scripts\optimize_images.bat` will use a local
OptiPNG binary if you place it here:

```
optipng/
├── optipng64.exe   (preferred, 64-bit)
└── optipng32.exe   (fallback, 32-bit)
```

Search order:

1. `optipng\optipng64.exe`
2. `optipng\optipng32.exe`
3. system `optipng` on `PATH`

If none is found, image generation still works — the optimization step is simply
skipped (GDI+ cannot control PNG compression, so OptiPNG is what actually shrinks
the files).

Download (open source, zlib licence): <https://optipng.sourceforge.net/>

> The binaries are intentionally **not** committed to this repository.
