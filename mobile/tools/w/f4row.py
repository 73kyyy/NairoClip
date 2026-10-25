from PIL import Image
im = Image.open("/upload/d862c14e-30f0-430d-b464-141fbcc92777.jpg").convert("RGB")
W, H = im.size
px = im.load()
def sat(c):
    return (max(c) - min(c)) / (max(c) + 1e-6)
# 逐行找角色带
rows = []
for y in range(H):
    c = sum(1 for x in range(0, W, 3) if sat(px[x, y]) > 0.18)
    rows.append(c)
th = max(rows) * 0.15
runs = []; on = False
for y, v in enumerate(rows):
    if v > th and not on: s = y; on = True
    elif v <= th and on:
        if y - s > 60: runs.append((s, y-1))
        on = False
if on and H - s > 60: runs.append((s, H-1))
print("ROW BANDS", len(runs))
for r in runs: print("  ", r, r[1]-r[0]+1)
