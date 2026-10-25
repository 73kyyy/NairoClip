from PIL import Image
import sys, glob
from collections import deque
f = sys.argv[1]
out = sys.argv[2]
im = Image.open(f).convert("RGB")
w, h = im.size
px = im.load()
def bg(c):
    r,g,b = c
    return r > 230 and g > 228 and b > 220
seen = [[False]*w for _ in range(h)]
comps = []
for y in range(h):
    for x in range(w):
        if seen[y][x] or bg(px[x,y]): continue
        q = deque([(x,y)]); seen[y][x] = True
        n = 0; x0=x1=x; y0=y1=y
        while q:
            cx, cy = q.popleft(); n += 1
            if cx<x0: x0=cx
            if cx>x1: x1=cx
            if cy<y0: y0=cy
            if cy>y1: y1=cy
            for dx,dy in ((1,0),(-1,0),(0,1),(0,-1)):
                nx, ny = cx+dx, cy+dy
                if 0<=nx<w and 0<=ny<h and not seen[ny][nx] and not bg(px[nx,ny]):
                    seen[ny][nx] = True; q.append((nx,ny))
        comps.append((n, x0, y0, x1, y1))
comps.sort(reverse=True)
print(len(comps), comps[:4])
