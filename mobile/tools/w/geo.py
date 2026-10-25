from PIL import Image, ImageDraw
import sys
n = sys.argv[1]
cols = int(sys.argv[2])
rows = int(sys.argv[3])
im = Image.open("/upload/%s.jpg" % n).convert("RGB")
W, H = im.size
px = im.load()
def sat(c):
    mx = max(c); mn = min(c)
    return (mx - mn) / (mx + 1e-6)
# 内容区域：按饱和度列/行投影的 5%-95% 分位
colv = []
for x in range(W):
    c = sum(1 for y in range(0, H, 3) if sat(px[x, y]) > 0.20)
    colv.append(c)
rowv = []
for y in range(H):
    c = sum(1 for x in range(0, W, 3) if sat(px[x, y]) > 0.20)
    rowv.append(c)
def bounds(v, frac=0.12):
    th = max(v) * frac
    i0 = next(i for i, t in enumerate(v) if t > th)
    i1 = len(v) - 1 - next(i for i, t in enumerate(reversed(v)) if t > th)
    return i0, i1
cx0, cx1 = bounds(colv)
cy0, cy1 = bounds(rowv)
print("CONTENT", cx0, cx1, cy0, cy1, "size", cx1-cx0, cy1-cy0)
print("CELL", (cx1-cx0)/cols, (cy1-cy0)/rows)
