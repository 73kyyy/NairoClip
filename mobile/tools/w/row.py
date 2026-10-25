from PIL import Image
import sys
n = sys.argv[1]
im = Image.open("/upload/%s.jpg" % n).convert("RGB")
W, H = im.size
px = im.load()
def isbg(c):
    r, g, b = c
    return r > 232 and g > 228 and b > 220 and abs(r-g) < 18 and abs(g-b) < 20
rows = []
for y in range(H):
    c = 0
    for x in range(0, W, 2):
        if not isbg(px[x, y]): c += 1
    rows.append(c)
th = 40
runs = []
on = False
for y, v in enumerate(rows):
    if v > th and not on: s = y; on = True
    elif v <= th and on:
        runs.append((s, y-1)); on = False
if on: runs.append((s, H-1))
print("ROWS", len(runs))
for r in runs: print("  ", r, r[1]-r[0]+1)
