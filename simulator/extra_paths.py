# Simulator build help.
#
# The simulator compiles the REAL production UI sources directly - there is no
# copy step any more (the old simulator/src/project/ folder is gone). The
# production files are pulled in with env.BuildSources(); simulator/src keeps
# only the host entry point, the shims and cJSON.
#
# Common for both environments:
#  - absolute forward-slash include paths (Windows backslashes in ${PROJECT_DIR}
#    get stripped by the build system)
#  - simulator/lv_conf.h first on the include path so LVGL uses the PC config
#  - -Wl,--wrap=fopen / --wrap=opendir redirects /littlefs -> ./datadevice and
#    /sdcard -> ./datasdcard
#
# SDL2 flags are added ONLY for the "native" environment (the "native_win32"
# environment uses the pure Win32/GDI backend and must not link SDL2).
Import("env")

proj = env.subst("$PROJECT_DIR").replace("\\", "/")
env_name = env.subst("$PIOENV")

common_inc = [
    "-I" + proj,                       # simulator/lv_conf.h (PC LVGL config)
    "-I" + proj + "/src/compat",
    "-I" + proj + "/src/cjson",
    "-I" + proj + "/../src",           # production headers
    "-I" + proj + "/../lib/lvgl",
]

env.Append(CPPDEFINES=["HOST_BUILD", "LV_CONF_INCLUDE_SIMPLE", "LV_LVGL_H_INCLUDE_SIMPLE"])

env.Append(CCFLAGS=common_inc)
env.Append(CXXFLAGS=common_inc + ["-std=gnu++14"])

# System libraries needed by both backends.
env.Append(LIBS=["user32", "gdi32"])

# --- Production UI sources, compiled directly from ../src -------------------
# Only the modules that are NOT stubbed in simulator/src/host_stubs.cpp.
# ui_loader is plain C (its C99 designated initializers are rejected by the
# host MinGW GCC 5.1 in C++), hence the .c extension in ../src.
prod_src = proj + "/../src"
prod_files = [
    "ui_loader.c",
    "ui_renderer.cpp",
    "ui_assets.cpp",
    "status_bar.cpp",
    "settings_page.cpp",
    "splash_screen.cpp",
    "gesture_handler.cpp",
    "button_style.c",
    "font_manager.c",
    "system_info.c",
    "i18n.c",
]
env.BuildSources(
    "$BUILD_DIR/prod_src",
    prod_src,
    ["+<" + f + ">" for f in prod_files],
)

# Fonts referenced by font_manager.c (otherwise "undefined reference to roboto_14/18").
env.BuildSources(
    "$BUILD_DIR/prod_fonts",
    prod_src + "/fonts",
    ["+<roboto_14.c>", "+<roboto_18.c>"],
)

if env_name == "native":
    sdl_inc = ["-I" + proj + "/../tools/sdl2/include"]
    env.Append(CCFLAGS=sdl_inc)
    env.Append(CXXFLAGS=sdl_inc)
    env.Append(LIBPATH=[proj + "/../tools/sdl2/lib"])
    env.Append(LIBS=["mingw32", "SDL2"])



env.Append(LINKFLAGS=["-Wl,--wrap=fopen", "-Wl,--wrap=opendir"])

# SCons does not track simulator/lv_conf.h as a dependency of the LVGL library
# (it is pulled in via LV_CONF_INCLUDE_SIMPLE), so editing it would silently not
# rebuild LVGL and the simulator would keep using stale settings (e.g. FS paths,
# gradient cache). Drop the compiled LVGL objects whenever lv_conf.h changes so
# the next `pio run` recompiles them automatically - no manual clean needed.
import os as _os
import glob as _glob
import shutil as _shutil

_lv_conf = _os.path.join(proj, "lv_conf.h")
_build_dir = env.subst("$BUILD_DIR")
_stamp = _os.path.join(_build_dir, ".lv_conf_mtime") if _build_dir else None

if _stamp and _os.path.isdir(_build_dir):
    try:
        _mtime = str(_os.path.getmtime(_lv_conf))
    except OSError:
        _mtime = "0"
    _prev = None
    try:
        with open(_stamp, "r", encoding="utf-8") as _f:
            _prev = _f.read().strip()
    except OSError:
        _prev = None
    if _prev != _mtime:
        for _d in _glob.glob(_os.path.join(_build_dir, "lib*", "lvgl")):
            _shutil.rmtree(_d, ignore_errors=True)
        try:
            with open(_stamp, "w", encoding="utf-8") as _f:
                _f.write(_mtime)
        except OSError:
            pass
