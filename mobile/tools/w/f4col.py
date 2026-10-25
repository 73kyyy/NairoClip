from PIL import Image
im = Image.open("/upload/d862c14e-30f0-430d-b464-141fbcc92777.jpg").convert("RGB")
W, H = im.size
px = im.load()
def sat(c):
    return (max(c) - min(c)) / (max(c) + 1e-6)
for yy in (600, 800, 1000, 1200):
    runs = []; on = False
    for x in range(W):
        v = sat(px[x, yy]) > 0.16
        if v and not on: s = x; on = True
        elif not v and on:
            if x - s > 50: runs.append((s, x-1))
            on = False
    if on: runs.append((s, W-1))
    print(yy, runs)
