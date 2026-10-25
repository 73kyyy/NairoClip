from PIL import Image
from collections import deque
im = Image.open("/upload/d862c14e-30f0-430d-b464-141fbcc92777.jpg").convert("RGB")
S = 3
sm = im.resize((im.width//S, im.height//S), Image.BOX)
w, h = sm.size
px = sm.load()
def bg(c):
    r,g,b = c
    return r > 235 and g > 232 and b > 226
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
        if n > 3000:
            comps.append((n, x0*S, y0*S, x1*S, y1*S))
comps.sort(key=lambda t: (t[2]//200, t[1]))
print("N", len(comps))
for c in comps: print(c)
