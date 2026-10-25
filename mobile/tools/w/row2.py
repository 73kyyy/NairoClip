from PIL import Image
import sys
n = sys.argv[1]
im = Image.open("/upload/%s.jpg" % n).convert("RGB")
W, H = im.size
px = im.load()
def sat(c):
    r, g, b = c
    mx = max(c); mn = min(c)
    return (mx - mn) / (mx + 1e-6)
rows = []
for y in range(H):
    c = 0
    for x in range(0, W, 2):
        if sat(px[x, y]) > 0.22: c += 1
    rows.append(c)
th = 30
runs = []
on = False
for y, v in enumerate(rows):
    if v > th and not on: s = y; on = True
    elif v <= th and on:
        runs.append((s, y-1)); on = False
if on: runs.append((s, H-1))
print("ROWS", len(runs))
for r in runs: print("  ", r, r[1]-r[0]+1)
