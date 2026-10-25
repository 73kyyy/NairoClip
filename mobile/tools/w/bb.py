from PIL import Image
import sys
f = sys.argv[1]
im = Image.open(f).convert("RGB")
px = im.load()
w, h = im.size
def bg(c):
    r,g,b = c
    return r > 232 and g > 230 and b > 222
x0, y0, x1, y1 = w, h, 0, 0
for y in range(h):
    for x in range(w):
        if not bg(px[x,y]):
            if x < x0: x0 = x
            if x > x1: x1 = x
            if y < y0: y0 = y
            if y > y1: y1 = y
print(x0, y0, x1, y1)
