from PIL import Image
from collections import deque
import sys
n = sys.argv[1]
tag = sys.argv[2]
im = Image.open("/upload/%s.png" % n).convert("RGB")
S = 3
sm = im.resize((im.width//S, im.height//S), Image.BOX)
w, h = sm.size
px = sm.load()
def sat(c):
    return (max(c) - min(c)) / (max(c) + 1e-6)
def isfg(c):
    return sat(c) > 0.13 or max(c) < 130
seen = [[False]*w for _ in range(h)]
comps = []
for y in range(h):
    for x in range(w):
        if seen[y][x] or not isfg(px[x,y]): continue
        q = deque([(x,y)]); seen[y][x] = True
        n_ = 0; x0=x1=x; y0=y1=y
        while q:
            cx, cy = q.popleft(); n_ += 1
            if cx<x0: x0=cx
            if cx>x1: x1=cx
            if cy<y0: y0=cy
            if cy>y1: y1=cy
            for dx,dy in ((1,0),(-1,0),(0,1),(0,-1),(1,1),(-1,-1),(1,-1),(-1,1)):
                nx, ny = cx+dx, cy+dy
                if 0<=nx<w and 0<=ny<h and not seen[ny][nx] and isfg(px[nx,ny]):
                    seen[ny][nx] = True; q.append((nx,ny))
        if n_ > 2000:
            comps.append((x0*S, y0*S, (x1+1)*S, (y1+1)*S, n_))
comps.sort(key=lambda t: (round(t[1]/260.0), t[0]))
print("N", len(comps))
for i, c in enumerate(comps): print(i, c)
