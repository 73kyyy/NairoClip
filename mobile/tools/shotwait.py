#!/usr/bin/env python3
"""Load page -> run JS -> wait N ms (for real rendered thumbs) -> screenshot.
Usage: shotwait.py <url> <out.png> [js] [waitMs] [W] [H]
"""
import os
import sys

os.environ.setdefault("WEBKIT_DISABLE_COMPOSITING_MODE", "1")
import gi  # noqa: E402

gi.require_version("Gtk", "3.0")
gi.require_version("WebKit2", "4.1")
from gi.repository import Gtk, WebKit2, GLib  # noqa: E402

url = sys.argv[1]
out = sys.argv[2]
js = sys.argv[3] if len(sys.argv) > 3 else ""
wait_ms = int(sys.argv[4]) if len(sys.argv) > 4 else 2500
W = int(sys.argv[5]) if len(sys.argv) > 5 else 412
H = int(sys.argv[6]) if len(sys.argv) > 6 else 915

win = Gtk.OffscreenWindow()
win.set_default_size(W, H)
view = WebKit2.WebView()
view.set_size_request(W, H)
win.add(view)
win.show_all()


def snap():
    def done(_v, res, _d=None):
        surface = view.get_snapshot_finish(res)
        surface.write_to_png(out)
        print("OK", out, surface.get_width(), surface.get_height())
        Gtk.main_quit()

    view.get_snapshot(WebKit2.SnapshotRegion.FULL_DOCUMENT,
                      WebKit2.SnapshotOptions.NONE, None, done)
    return False


def run():
    if js:
        view.run_javascript(js, None, None, None)
    GLib.timeout_add(wait_ms, snap)
    return False


def on_load(_v, event, _d=None):
    if event == WebKit2.LoadEvent.FINISHED:
        GLib.timeout_add(1400, run)


view.connect("load-changed", on_load)
view.load_uri(url)
GLib.timeout_add(45000, lambda: (print("TIMEOUT"), Gtk.main_quit())[1])
Gtk.main()
