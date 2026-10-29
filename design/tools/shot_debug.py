#!/usr/bin/env python3
"""页面截图 + JS 控制台/错误采集（WebKitGTK + Xvfb）。

用法：xvfb-run -a -s "-screen 0 1600x1000x24" python3 design/tools/shot_debug.py <url> <out.png> [w] [h] [zoom] [wait_ms]
"""
import os, sys
os.environ.setdefault("WEBKIT_DISABLE_COMPOSITING_MODE", "1")
os.environ.setdefault("LIBGL_ALWAYS_SOFTWARE", "1")
import gi
gi.require_version("Gtk", "3.0")
gi.require_version("WebKit2", "4.1")
from gi.repository import Gtk, WebKit2, GLib


def main():
    url = sys.argv[1]
    dst = os.path.abspath(sys.argv[2])
    w = int(sys.argv[3]) if len(sys.argv) > 3 else 1440
    h = int(sys.argv[4]) if len(sys.argv) > 4 else 900
    zoom = float(sys.argv[5]) if len(sys.argv) > 5 else 1.0
    wait = int(sys.argv[6]) if len(sys.argv) > 6 else 3500
    js = sys.argv[7] if len(sys.argv) > 7 else ""

    win = Gtk.OffscreenWindow()
    win.set_default_size(int(w * zoom), int(h * zoom))
    view = WebKit2.WebView()
    view.set_size_request(int(w * zoom), int(h * zoom))
    view.set_zoom_level(zoom)
    win.add(view)
    win.show_all()

    logs = []

    def on_console(_v, msg, _line, _src):
        logs.append(f"[console:{msg.get_level().value_nick}] {msg.get_text()}")
        return False

    try:
        view.connect("console-message", on_console)
    except TypeError:
        # WebKitGTK 4.1 在 Python 绑定里不再暴露 console-message；
        # 改用注入脚本把错误写进 document.title，便于诊断。
        pass

    def on_js_done(_v, res, _d=None):
        try:
            value = view.run_javascript_finish(res)
            if value is not None:
                print(f"[js] {value.to_string() if hasattr(value,'to_string') else value}")
        except Exception as exc:  # noqa: BLE001
            print(f"[js] error {exc}")
        GLib.timeout_add(max(600, wait // 2), snap)
        return False

    def run_js_then_snap():
        if js:
            view.run_javascript(js, None, on_js_done)
        else:
            snap()
        return False

    state = {"done": False}

    def snap():
        if state["done"]:
            return False
        state["done"] = True

        def on_snapshot(v, res, _d=None):
            try:
                surface = v.get_snapshot_finish(res)
                surface.write_to_png(dst)
                print(f"[shot] {dst} {surface.get_width()}x{surface.get_height()}")
            except GLib.Error as exc:
                print(f"[shot] error {exc}")
            for line in logs:
                print(line)
            Gtk.main_quit()
        view.get_snapshot(WebKit2.SnapshotRegion.FULL_DOCUMENT,
                          WebKit2.SnapshotOptions.NONE, None, on_snapshot)
        return False

    view.connect("load-changed", lambda v, ev: GLib.timeout_add(wait, run_js_then_snap)
                 if ev == WebKit2.LoadEvent.FINISHED else None)
    GLib.timeout_add(wait + 25000, snap)
    view.load_uri(url)
    Gtk.main()
    return 0


if __name__ == "__main__":
    sys.exit(main())
