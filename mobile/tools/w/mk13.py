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
files = sorted(glob.glob("tools/w/A_*.png"))[:13]
cells = []
for i, f in enumerate(files):
    im = Image.open(f).convert("RGB")
    w, h = im.size
    s = min(w, h)
    im = im.crop(((w-s)//2, (h-s)//2, (w-s)//2+s, (h-s)//2+s))
    im = im.resize((CW*3, CW*3), Image.LANCZOS)
    bg = Image.new("RGB", (CW*3, CH*3), (247,243,236))
    bg.paste(im, (0, (CH*3-CW*3)//2))
    st = F.FILTER_ORDER[i][0]
    cells.append((F.FILTER_ORDER[i][1], F.filter_image(bg, st).resize((CW, CH), Image.LANCZOS)))
W = 16 + 13*(CW+5)
H = 16 + 26 + CH + 8
sh = Image.new("RGB", (W, H), (22, 23, 25))
d = ImageDraw.Draw(sh)
d.text((14, 8), "13 滤镜 x 13 表情（真实格子 92x138）", fill=(238,238,240), font=fo(15))
for i, (nm, c) in enumerate(cells):
    x = 14 + i*(CW+5)
    y = 32
    sh.paste(c, (x, y))
    d.text((x+2, y+CH+2), nm, fill=(150,152,155), font=fo(10))
sh.save("tools/w/f13.png")
print("OK", sh.size)
