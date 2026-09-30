# Image compression tools (local)

These binaries are used by `scripts\generate_images.bat`,
`scripts\optimize_images.bat` (lossless) and `scripts\optimize_max.bat`
(Oxipng + Pngquant, lossy).

| Tool | Folder | Mode | Notes |
|------|--------|------|-------|
| **Oxipng** | `tools\oxipng\oxipng.exe` | lossless | Preferred; multi-threaded, fast, best lossless ratio |
| **ECT** | `tools\ect\ect.exe` | lossless | Optional alternative (not bundled) |
| **OptiPNG** | `tools\optipng\optipng64.exe` / `optipng32.exe` | lossless | Fallback |
| **Pngquant** | `tools\pngquant\pngquant.exe` | **lossy** | Used only by `optimize_max.bat`; drastically smaller |

## Detection order

`generate_images.bat` and `optimize_images.bat` (lossless) use the first
available of:

1. `tools\oxipng\oxipng.exe`
2. `tools\ect\ect.exe`
3. `tools\optipng\optipng64.exe`
4. `tools\optipng\optipng32.exe`
5. system `oxipng` / `optipng` on `PATH`

If none is found the optimization step is skipped (image generation still
works).

## Downloads

- Oxipng: <https://github.com/shssoichiro/oxipng/releases>
- Pngquant: <https://pngquant.org/>
- ECT: <https://github.com/fhanau/Efficient-Compression-Tool/releases>
- OptiPNG: <https://optipng.sourceforge.net/>

> The binaries are third-party tools and are **not** authored by this project.
> Check each project's licence before redistributing.

## Project helper scripts (`tools/utils/`)

| Script | Purpose |
|--------|---------|
| `generate_assets.ps1` | Regenerates the PNG library (gradients / solids / patterns / pages / system / icons) |
| `generate_gradients.ps1` | Adds the 164 generated button gradients per button size |
| `compress_backgrounds.py` | Aggressive page-background compression (JPG / indexed PNG-8) |
| `web_preview_server.py` | Local web-UI preview server (mocks the device REST API) |

Run them from the repo root, e.g. `pwsh -File tools/utils/generate_assets.ps1`,
or through the wrappers in `scripts\`.
