"""PlatformIO post-build hook: archive every built firmware image.

Copies `.pio/build/<env>/firmware.bin` into `firmware/<version>/` as a
timestamped history entry:
  - firmware_<version>_<YYYYmmdd-HHMMSS>.bin

The version is read from `src/config.h` (`MODIPAD_FIRMWARE_VERSION`) - the
single source of truth shared with the About page, the SD config backups and the
web UI (`APP_VERSION` in datadevice/web/app.js).

Wired via `extra_scripts = extra_script.py` in platformio.ini.
"""
Import("env")

import datetime
import os
import re
import shutil


def _firmware_version(project_dir):
    try:
        with open(os.path.join(project_dir, "src", "config.h"), encoding="utf-8") as f:
            m = re.search(r'MODIPAD_FIRMWARE_VERSION\s+"([^"]+)"', f.read())
            if m:
                return m.group(1)
    except Exception as exc:  # pragma: no cover - build helper
        print("firmware-archive: cannot read version: %s" % exc)
    return "dev"


def archive_firmware(source, target, env):
    project_dir = env.subst("$PROJECT_DIR")
    bin_path = env.subst("$BUILD_DIR/${PROGNAME}.bin")
    if not os.path.isfile(bin_path):
        return

    version = _firmware_version(project_dir)
    out_dir = os.path.join(project_dir, "firmware", version)
    os.makedirs(out_dir, exist_ok=True)

    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    hist = os.path.join(out_dir, "firmware_%s_%s.bin" % (version, stamp))

    shutil.copy2(bin_path, hist)
    print("firmware-archive: %s (%s)" % (os.path.relpath(hist, project_dir), version))


env.AddPostAction("buildprog", archive_firmware)
