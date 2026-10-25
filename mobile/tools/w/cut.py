from PIL import Image
import sys
n = sys.argv[1]
cols = int(sys.argv[2])
rows = int(sys.argv[3])
cx0 = int(sys.argv[4])
cx1 = int(sys.argv[5])
cy0 = int(sys.argv[6])
cy1 = int(sys.argv[7])
im = Image.open("/upload/%s.jpg" % n).convert("RGB")
cw = (cx1 - cx0) / cols
chh = (cy1 - cy0) / rows
for r in range(rows):
    for c in range(cols):
        box = (int(cx0 + c*cw), int(cy0 + r*chh), int(cx0 + (c+1)*cw), int(cy0 + (r+1)*chh))
        im.crop(box).save("tools/w/g_%d_%d.png" % (r, c))
print("OK", cols, rows)
