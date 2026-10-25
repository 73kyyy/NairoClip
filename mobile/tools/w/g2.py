from PIL import Image
import sys
n = sys.argv[1]
tag = sys.argv[2]
xs = [int(v) for v in sys.argv[3].split(",")]
ys = [int(v) for v in sys.argv[4].split(",")]
cw = int(sys.argv[5])
ch = int(sys.argv[6])
pad = int(sys.argv[7])
im = Image.open("/upload/%s.png" % n).convert("RGB")
for r, yy in enumerate(ys):
    for c, xx in enumerate(xs):
        box = (max(0,xx-pad), max(0,yy-pad), xx+cw+pad, yy+ch+pad)
        im.crop(box).save("tools/w/%s_%d_%d.png" % (tag, r, c))
print("OK", tag)
