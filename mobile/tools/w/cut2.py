from PIL import Image
import sys
n = sys.argv[1]
tag = sys.argv[2]
cols = int(sys.argv[3])
rows = int(sys.argv[4])
cx0 = int(sys.argv[5]); cx1 = int(sys.argv[6])
cy0 = int(sys.argv[7]); cy1 = int(sys.argv[8])
im = Image.open("/upload/%s.jpg" % n).convert("RGB")
cw = (cx1 - cx0) / cols
chh = (cy1 - cy0) / rows
for r in range(rows):
    for c in range(cols):
        box = (int(cx0 + c*cw), int(cy0 + r*chh), int(cx0 + (c+1)*cw), int(cy0 + (r+1)*chh))
        im.crop(box).save("tools/w/f%s_%d_%d.png" % (tag, r, c))
print("OK", tag, cols*rows)
