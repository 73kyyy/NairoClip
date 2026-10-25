from PIL import Image
import sys
n = sys.argv[1]
im = Image.open("/upload/%s.jpg" % n).convert("RGB")
W, H = im.size
px = im.load()
def sat(c):
    mx = max(c); mn = min(c)
    return (mx - mn) / (mx + 1e-6)
# 在 y=300 这条水平线上找连续彩色段
for yy in (300, 350, 400):
    runs = []; on = False
    for x in range(W):
        v = sat(px[x, yy]) > 0.20
        if v and not on: s = x; on = True
        elif not v and on:
            if x - s > 30: runs.append((s, x-1, x-s))
            on = False
    if on: runs.append((s, W-1, W-s))
    print(yy, runs)
