from PIL import Image
import sys
n = sys.argv[1]
tag = sys.argv[2]
x0 = int(sys.argv[3])
y0 = int(sys.argv[4])
cw = int(sys.argv[5])
ch = int(sys.argv[6])
cols = int(sys.argv[7])
rows = int(sys.argv[8])
pad = int(sys.argv[9])
im = Image.open("/upload/%s.png" % n).convert("RGB")
for r in range(rows):
    for c in range(cols):
        bx = x0 + c*cw - pad
        by = y0 + r*ch - pad
        box = (max(0,bx), max(0,by), bx+cw+2*pad, by+ch+2*pad)
        im.crop(box).save("tools/w/%s_%d_%d.png" % (tag, r, c))
print("OK", tag, cols*rows)
