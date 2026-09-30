# firmware/

Archive of built firmware images, for history and versioning.

`extra_script.py` (wired via `extra_scripts = extra_script.py` in
`platformio.ini`) runs as a post-build hook and copies every `firmware.bin`
produced by `pio run` into a sub-folder named after the version read from
`src/config.h` (`MODIPAD_FIRMWARE_VERSION`), as a **timestamped history entry**
(no `firmware.bin` duplicate is kept):

```
firmware/
└── 5.1.1/
    └── firmware_5.1.1_20260930-184118.bin
```

Notes

- The version comes from `src/config.h` only. To release a new version, bump
  `MODIPAD_FIRMWARE_VERSION` there and mirror it in
  `datadevice/web/app.js` (`APP_VERSION`) and the `?v=` cache busters in
  `index.html` / `app.js`.
- The same version is included in the on-SD config backup names
  (`backup_<version>_<n>.json`).
- Every build adds a new timestamped file, so all builds of a version are kept.
- This folder is only populated by a build; it is safe to delete the images if
  you don't need the history (the folder itself is kept by this README).
- Large `.bin` files need not be committed; keep them out of version control if
  you don't want the repository to grow.
