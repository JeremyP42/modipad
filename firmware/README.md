# firmware/

Archive of built firmware / filesystem images, for history and versioning.

`extra_script.py` (wired via `extra_scripts = extra_script.py` in
`platformio.ini`) runs post-build hooks and copies the built images into a
sub-folder named after the version read from `src/config.h`
(`MODIPAD_FIRMWARE_VERSION`), as **timestamped history entries**:

- `pio run` (buildprog) -> `firmware.bin` -> `firmware_<version>_<stamp>.bin`
- `pio run -t buildfs` -> `littlefs.bin` -> `littlefs_<version>_<stamp>.bin`

Both land **side by side** in the version folder (no `firmware.bin` /
`littlefs.bin` duplicates are kept):

```
firmware/
└── 5.3.8/
    ├── firmware_5.3.8_20261009-234236.bin
    └── littlefs_5.3.8_20261009-234255.bin
```

Notes

- The version comes from `src/config.h` only. To release a new version, bump
  `MODIPAD_FIRMWARE_VERSION` there and mirror it in
  `datadevice/web/app.js` (`APP_VERSION`) and the `?v=` cache busters in
  `index.html` / `app.js`.
- The same version is included in the on-SD config backup names
  (`backup_<version>_<n>.json`).
- The internal filesystem image is built with `pio run -t buildfs` (`start.bat` -> **2**, or **3** for firmware + storage). `pio run -t uploadfs` also triggers it and archives a copy.
- Every build adds a new timestamped file, so all builds of a version are kept.
- This folder is only populated by a build; it is safe to delete the images if
  you don't need the history (the folder itself is kept by this README).
- Large `.bin` files need not be committed; keep them out of version control if
  you don't want the repository to grow.
