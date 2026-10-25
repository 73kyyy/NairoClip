import sys
sys.path.insert(0, "tools")
import ng_filter as F
from PIL import Image, ImageDraw, ImageFont
import glob
CW, CH = 92, 138
FP = "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"
def fo(s):
    try: return ImageFont.truetype(FP, s)
    except Exception: return ImageFont.load_default()
def tight(im):
    px = im.load(); w, h = im.size
    def bg(c): return c[0] > 232 and c[1] > 230 and c[2] > 222
    x0, y0, x1, y1 = w, h, 0, 0
    for y in range(h):
        for x in range(w):
            if not bg(px[x, y]):
                if x < x0: x0 = x
                if x > x1: x1 = x
                if y < y0: y0 = y
                if y > y1: y1 = y
    return im.crop((x0, y0, x1+1, y1+1))
files = sorted(glob.glob("tools/w/A_*.png"))[:13]
cells = []
for i, f in enumerate(files):
    im = tight(Image.open(f).convert("RGB"))
    r = min((CW*3)*0.98 / im.width, (CH*3)*0.98 / im.height)
    im = im.resize((int(im.width*r), int(im.height*r)), Image.LANCZOS)
    bg = Image.new("RGB", (CW*3, CH*3), (247,243,236))
    bg.paste(im, ((CW*3-im.width)//2, (CH*3-im.height)//2))
    st = F.FILTER_ORDER[i][0]
    cells.append((F.FILTER_ORDER[i][1], F.filter_image(bg, st).resize((CW, CH), Image.LANCZOS)))
W = 16 + 13*(CW+5)
H = 40 + CH + 12
sh = Image.new("RGB", (W, H), (22, 23, 25))
d = ImageDraw.Draw(sh)
d.text((14, 8), "13 filters x 13 faces (cell 92x138)", fill=(238,238,240), font=fo(15))
for i, (nm, c) in enumerate(cells):
    x = 14 + i*(CW+5); y = 34
    sh.paste(c, (x, y))
    d.text((x+2, y+CH+2), nm, fill=(152,154,157), font=fo(10))
sh.save("tools/w/f13d.png")
print("OK", sh.size)
