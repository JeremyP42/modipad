"""Local preview server for the ModiPAD web UI.

Serves datadevice/web (HTML/CSS/JS) and mocks the device REST API so the FULL
configurator can be used in a normal browser without the ESP32. Everything you
edit is written to the same files the firmware is built from:

    GET  /api/config            -> datadevice/config.json
    POST /api/config            -> datadevice/config.json
                                   (+ mirror to datasdcard/modipad/config/backup_preview.json)
    GET  /api/settings          -> datadevice/settings.json
    POST /api/settings          -> datadevice/settings.json
    GET  /api/images?dir=...    -> directory listing of datadevice/<dir>
    POST /api/upload?path=...   -> datadevice/<path>   ("sd:..." -> datasdcard/modipad)
    POST /api/delete?path=...   -> remove datadevice/<path> ("sd:..." -> SD)
    GET  /api/fonts             -> [10,12,14,16,18] Roboto sizes
    GET  /api/backup/list       -> names in datasdcard/modipad/config
    POST /api/backup/save       -> write a backup_<version>_<stamp>.json there
    POST /api/backup/import     -> copy a backup back to datadevice/config.json
    GET  /api/wifi/scan         -> {"running":false,"networks":[]}
    POST /api/wifi/apply        -> {"status":"ok"}
    POST /api/fs/sync           -> {"copied":0}
    POST /api/reload            -> {"status":"rebooting"}
    GET  /api/log               -> preview log text
    GET  /images/<path>         -> datadevice/<path>      (same mapping as the device)
    GET  /sdimages/<path>       -> datasdcard/modipad/<path>
    GET  /locales/<file>        -> datadevice/web/locales/<file>

Usage:  python web_preview_server.py [port]   (default 8765)
"""
import datetime
import http.server
import json
import os
import re
import socketserver
import sys
import urllib.parse

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
DATA = os.path.join(ROOT, "datadevice")
SD_ROOT = os.path.join(ROOT, "datasdcard")
SD = os.path.join(SD_ROOT, "modipad")
BACKUP_DIR = os.path.join(SD, "config")
WEB = os.path.join(DATA, "web")
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8765

# Same file the firmware reads. Written on every config POST and flashed by
# `pio run -t uploadfs` (platformio.ini: data_dir = datadevice).
CONFIG_FILE = os.path.join(DATA, "config.json")
SETTINGS_FILE = os.path.join(DATA, "settings.json")
PREVIEW_BACKUP = os.path.join(BACKUP_DIR, "backup_preview.json")


def firmware_version():
    """MODIPAD_FIRMWARE_VERSION from src/config.h (same naming as backups)."""
    try:
        with open(os.path.join(ROOT, "src", "config.h"), encoding="utf-8") as f:
            m = re.search(r'MODIPAD_FIRMWARE_VERSION\s+"([^"]+)"', f.read())
            if m:
                return m.group(1)
    except OSError:
        pass
    return "dev"


def map_path(rel):
    """Map an API path to disk: 'sd:...' -> datasdcard/modipad, else datadevice."""
    rel = rel.lstrip("/")
    if rel.startswith("sd:"):
        return os.path.join(SD, rel[3:].replace("/", os.sep))
    return os.path.join(DATA, rel.replace("/", os.sep))


def read_file(path):
    with open(path, "rb") as f:
        return f.read()


def write_file(path, body):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(body if isinstance(body, bytes) else body.encode("utf-8"))


def mirror_preview_backup():
    """Keep a copy of the saved config in the SD backup folder so the device
    can restore it from Settings after the card is flashed ([5])."""
    try:
        if os.path.isfile(CONFIG_FILE):
            write_file(PREVIEW_BACKUP, read_file(CONFIG_FILE))
    except OSError:
        pass


def list_backups():
    if not os.path.isdir(BACKUP_DIR):
        return []
    out = []
    for n in sorted(os.listdir(BACKUP_DIR)):
        if n.startswith(".") or not n.lower().endswith(".json"):
            continue
        out.append(n)
    return out


def list_font_sizes():
    """Roboto sizes found in the device FS and the SD card (like the firmware)."""
    sizes = set()
    for d in (os.path.join(DATA, "fonts"), os.path.join(SD, "fonts")):
        if not os.path.isdir(d):
            continue
        for n in os.listdir(d):
            m = re.match(r"roboto_(\d+)\.bin$", n)
            if m and "_bold" not in n:
                v = int(m.group(1))
                if 8 <= v <= 48:
                    sizes.add(v)
    if not sizes:
        sizes = {10, 12, 14, 16, 18}
    return sorted(sizes)


MIME = {
    ".html": "text/html; charset=utf-8",
    ".css": "text/css; charset=utf-8",
    ".js": "application/javascript; charset=utf-8",
    ".json": "application/json; charset=utf-8",
    ".png": "image/png",
    ".jpg": "image/jpeg",
    ".jpeg": "image/jpeg",
    ".svg": "image/svg+xml",
    ".ico": "image/x-icon",
}


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def _send(self, code, body, ctype="application/octet-stream"):
        if isinstance(body, str):
            body = body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _send_json(self, obj, code=200):
        self._send(code, json.dumps(obj), MIME[".json"])

    def _send_file(self, path):
        if not os.path.isfile(path):
            self._send(404, "Not found", "text/plain")
            return
        ext = os.path.splitext(path)[1].lower()
        self._send(200, read_file(path), MIME.get(ext, "application/octet-stream"))

    def _read_body(self):
        n = int(self.headers.get("Content-Length", 0))
        return self.rfile.read(n) if n > 0 else b""

    # ------------------------------------------------------------------ GET
    def do_GET(self):
        u = urllib.parse.urlparse(self.path)
        p = u.path
        q = urllib.parse.parse_qs(u.query)

        if p == "/api/config":
            self._send(200, read_file(CONFIG_FILE) if os.path.isfile(CONFIG_FILE) else b"{}", MIME[".json"])
        elif p == "/api/settings":
            self._send(200, read_file(SETTINGS_FILE) if os.path.isfile(SETTINGS_FILE) else b"{}", MIME[".json"])
        elif p == "/api/system":
            # Mirror every field the device's /api/system returns so the web
            # "Device" tab renders the same rows as the firmware About page.
            now = datetime.datetime.now()
            sd_present = os.path.isdir(SD)
            self._send_json({
                "chip": "Host (preview)",
                "chip_revision": 0,
                "cores": os.cpu_count() or 1,
                "temperature_c": 0.0,
                "cpu_freq_mhz": 0,
                "flash_freq_mhz": 0,
                "sram_free": 0,
                "sram_total": 0,
                "psram_free": 0,
                "psram_total": 0,
                "heap_free": 0,
                "heap_total": 0,
                "fs_free": 0,
                "fs_total": 0,
                "uptime_seconds": 0,
                "idf_version": "n/a (preview)",
                "reset_reason": 1,  # power-on
                "build_date": now.strftime("%b %d %Y"),
                "build_time": now.strftime("%H:%M:%S"),
                "sd_present": sd_present,
                "sd_total": 0,
                "sd_free": 0,
                "sd_fs": "FAT" if sd_present else "-",
                "wifi_rssi": 0,
                "wifi_reconnects": 0,
                "wifi_mac": "00:00:00:00:00:00",
                "bt_mac": "00:00:00:00:00:00",
                "tasks": [],
            })
        elif p == "/api/images":
            d = q.get("dir", ["images"])[0]
            base = map_path(d)
            out = []
            if os.path.isdir(base):
                for n in sorted(os.listdir(base)):
                    out.append({"name": n, "dir": os.path.isdir(os.path.join(base, n))})
            self._send_json(out)
        elif p == "/api/fonts":
            self._send_json(list_font_sizes())
        elif p == "/api/backup/list":
            self._send_json(list_backups())
        elif p == "/api/ota/status":
            self._send_json({"sd_update": os.path.isfile(os.path.join(SD_ROOT, "update.bin"))})
        elif p == "/api/wifi/scan":
            self._send_json({"running": False, "networks": []})
        elif p == "/api/log":
            self._send(200, "ModiPAD web preview - no device log available.\n", "text/plain; charset=utf-8")
        elif p == "/" or p == "/index.html":
            self._send_file(os.path.join(WEB, "index.html"))
        elif p == "/app.js":
            self._send_file(os.path.join(WEB, "app.js"))
        elif p == "/style.css":
            self._send_file(os.path.join(WEB, "style.css"))
        elif p == "/favicon.ico":
            self._send(204, b"", "image/x-icon")
        elif p.startswith("/locales/"):
            self._send_file(os.path.join(WEB, p.lstrip("/")))
        elif p.startswith("/images/"):
            self._send_file(os.path.join(DATA, p.lstrip("/").replace("/", os.sep)))
        elif p.startswith("/sdimages/"):
            self._send_file(os.path.join(SD, p[len("/sdimages/"):].replace("/", os.sep)))
        else:
            self._send(404, "Not found", "text/plain")

    # ----------------------------------------------------------------- POST
    def do_POST(self):
        body = self._read_body()
        u = urllib.parse.urlparse(self.path)
        q = urllib.parse.parse_qs(u.query)

        if u.path == "/api/config":
            # Primary file (flashed by uploadfs) + SD backup mirror.
            write_file(CONFIG_FILE, body)
            mirror_preview_backup()
            self._send_json({"status": "ok"})
        elif u.path == "/api/settings":
            write_file(SETTINGS_FILE, body)
            self._send_json({"status": "ok"})
        elif u.path == "/api/upload":
            dest = map_path(q.get("path", [""])[0])
            write_file(dest, body)
            self._send_json({"status": "ok"})
        elif u.path == "/api/delete":
            try:
                os.remove(map_path(q.get("path", [""])[0]))
            except OSError:
                pass
            self._send_json({"status": "ok"})
        elif u.path == "/api/backup/save":
            name = "backup_%s_%s.json" % (
                firmware_version(),
                datetime.datetime.now().strftime("%Y%m%d-%H%M%S"),
            )
            try:
                write_file(os.path.join(BACKUP_DIR, name), read_file(CONFIG_FILE))
                self._send_json({"status": "ok", "name": name})
            except OSError:
                self._send_json({"status": "error", "name": ""})
        elif u.path == "/api/backup/import":
            try:
                req = json.loads(body.decode("utf-8") or "{}")
                name = os.path.basename(req.get("name", ""))
            except (ValueError, AttributeError):
                name = ""
            src = os.path.join(BACKUP_DIR, name) if name else ""
            if name and os.path.isfile(src):
                write_file(CONFIG_FILE, read_file(src))
                self._send_json({"status": "ok"})
            else:
                self._send_json({"status": "error"})
        elif u.path == "/api/fs/sync":
            # Device-only (copy SD assets into LittleFS); nothing to do on the host.
            self._send_json({"copied": 0})
        elif u.path == "/api/wifi/scan":
            self._send_json({"status": "ok"})
        elif u.path == "/api/wifi/apply":
            self._send_json({"status": "ok"})
        elif u.path in ("/api/reload", "/api/ota", "/api/ota/sd"):
            # No hardware to reboot/flash in the preview.
            self._send_json({"status": "rebooting"})
        else:
            self._send(404, "Not found", "text/plain")


class ReuseServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


if __name__ == "__main__":
    print("ModiPAD web UI preview: http://127.0.0.1:%d/" % PORT)
    print("Serving %s" % DATA)
    print("Config saved to %s" % CONFIG_FILE)
    print("SD mirror: %s" % PREVIEW_BACKUP)
    with ReuseServer(("127.0.0.1", PORT), Handler) as httpd:
        httpd.serve_forever()
