#!/usr/bin/env python3
"""NairoClip 原型截图工具（WebKitGTK + Xvfb，无 GPU 环境可用）。

用法：
    xvfb-run -a -s "-screen 0 1400x2000x24" \
        python3 tools/shot.py <页面或URL> <输出.png> [宽] [高] [缩放] [等待ms]

示例（2× 高清 PNG）：
    python3 tools/shot.py pages/01-home.html screenshots/01-home.png 390 844 2 900

说明：
- 以 CSS 宽高 + 页面缩放实现高分屏输出（截图尺寸 = 宽×缩放）。
- 截图取 FULL_DOCUMENT，因此页面比视口高时不会截断。
- 需 WEBKIT_DISABLE_COMPOSITING_MODE=1（脚本内已默认设置）。
"""
import os
import sys

os.environ.setdefault("WEBKIT_DISABLE_COMPOSITING_MODE", "1")
os.environ.setdefault("LIBGL_ALWAYS_SOFTWARE", "1")

import gi  # noqa: E402

gi.require_version("Gtk", "3.0")
gi.require_version("WebKit2", "4.1")
from gi.repository import Gtk, WebKit2, GLib  # noqa: E402


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    src = sys.argv[1]
    dst = os.path.abspath(sys.argv[2])
    width = int(sys.argv[3]) if len(sys.argv) > 3 else 390
    height = int(sys.argv[4]) if len(sys.argv) > 4 else 844
    zoom = float(sys.argv[5]) if len(sys.argv) > 5 else 1.0
    wait_ms = int(sys.argv[6]) if len(sys.argv) > 6 else 900

    vw, vh = int(width * zoom), int(height * zoom)
    window = Gtk.OffscreenWindow()
    window.set_default_size(vw, vh)
    view = WebKit2.WebView()
    view.set_size_request(vw, vh)
    view.set_zoom_level(zoom)
    window.add(view)
    window.show_all()

    state = {"done": False}

    def take_snapshot():
        if state["done"]:
            return False
        state["done"] = True
        view.get_snapshot(WebKit2.SnapshotRegion.FULL_DOCUMENT,
                          WebKit2.SnapshotOptions.NONE, None, on_snapshot)
        return False

    def on_snapshot(v, result, _data=None):
        try:
            surface = v.get_snapshot_finish(result)
            surface.write_to_png(dst)
            print(f"[shot] {dst} {surface.get_width()}x{surface.get_height()}")
        except GLib.Error as exc:  # pragma: no cover
            print(f"[shot] error: {exc}")
            Gtk.main_quit()
            return
        Gtk.main_quit()

    def on_load_changed(v, event, _data=None):
        if event == WebKit2.LoadEvent.FINISHED:
            GLib.timeout_add(wait_ms, take_snapshot)

    view.connect("load-changed", on_load_changed)
    GLib.timeout_add(wait_ms + 20000, take_snapshot)  # 兜底
    if "://" in src:
        view.load_uri(src)
    else:
        view.load_uri("file://" + os.path.abspath(src))
    Gtk.main()
    return 0


if __name__ == "__main__":
    sys.exit(main())
