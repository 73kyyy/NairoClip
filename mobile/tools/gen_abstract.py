#!/usr/bin/env python3
"""程序化生成三级面板宫格的抽象底图（纯 PIL，无 numpy、无外部素材）。

四个场景，都为 9:16 竖版、深色底、带连续色调与高光：
  orb    暗场里一个受光球体（明暗层次最丰富，做滤镜底图）
  prism  斜向发光色带交叠、中心过曝（几何结构清楚，做特效底图）
  torus  环面（边界清晰，做蒙版底图）
  dune   层叠的沙丘等值线（低饱和、明暗交替）

用法：
    python3 tools/gen_abstract.py out/orb.png  orb 368 654
    python3 tools/gen_abstract.py out/all.png  --all
"""

import math
import os
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter


# ---------------------------------------------------------------- 基础工具

def lerp3(a, b, t):
    return (a[0] + (b[0] - a[0]) * t,
            a[1] + (b[1] - a[1]) * t,
            a[2] + (b[2] - a[2]) * t)


def grad(size, stops, direction=(0.0, 1.0)):
    """多段线性渐变。stops = [(pos, (r,g,b)), ...]，pos 0..1 升序。

    在低分辨率上逐像素算，再交给调用方放大（比全尺寸快十几倍）。
    """
    w, h = size
    img = Image.new('RGB', (w, h))
    px = img.load()
    dx, dy = direction
    n = math.hypot(dx, dy) or 1.0
    dx, dy = dx / n, dy / n
    # 投影范围归一化
    lo = min(0.0, dx * w) + min(0.0, dy * h)
    hi = max(0.0, dx * w) + max(0.0, dy * h)
    span = (hi - lo) or 1.0
    for y in range(h):
        for x in range(w):
            t = ((x * dx + y * dy) - lo) / span
            t = 0.0 if t < 0 else (1.0 if t > 1 else t)
            for i in range(len(stops) - 1):
                p0, c0 = stops[i]
                p1, c1 = stops[i + 1]
                if p0 <= t <= p1 or i == len(stops) - 2:
                    k = 0.0 if p1 <= p0 else (t - p0) / (p1 - p0)
                    k = 0.0 if k < 0 else (1.0 if k > 1 else k)
                    px[x, y] = tuple(int(c0[j] + (c1[j] - c0[j]) * k) for j in range(3))
                    break
    return img


def up(img, w, h):
    return img.resize((w, h), Image.LANCZOS)


def add_layer(base, layer, opacity=1.0):
    """把 layer 以加法方式叠上去（模拟发光）。"""
    out = ImageChops.add(base, layer)
    if opacity < 1.0:
        out = Image.blend(base, out, opacity)
    return out


def glow(img, radius, gain=1.0):
    """提取亮部做模糊光晕，再加回原图。"""
    lum = img.convert('L')
    mask = lum.point(lambda v: 0 if v < 120 else int((v - 120) * 1.7))
    halo = Image.new('RGB', img.size, (0, 0, 0))
    halo.paste(img, (0, 0), mask.filter(ImageFilter.GaussianBlur(radius)))
    halo = halo.filter(ImageFilter.GaussianBlur(radius * 0.55))
    return add_layer(img, halo.point(lambda v: int(v * gain)))


def vignette(img, strength=0.55, blur=None):
    w, h = img.size
    vg = grad((max(2, w // 4), max(2, h // 4)),
              [(0.0, (255, 255, 255)), (0.55, (222, 222, 222)), (1.0, (110, 110, 110))])
    vg = up(vg.convert('L'), w, h)
    vg = vg.filter(ImageFilter.GaussianBlur(blur if blur else max(2, w * 0.05)))
    dark = Image.new('RGB', img.size, (0, 0, 0))
    return Image.composite(img, Image.blend(img, dark, strength), vg)


# ---------------------------------------------------------------- 场景 1：球

def make_orb(w, h):
    """暗场中一个受光球体：冷高光 → 暖反射 → 青色轮廓光。

    球体占画面约 55%，92px 格子里仍然一眼看出是"立体物体"。
    """
    bgstops = [(0.00, (9, 11, 15)), (0.42, (16, 20, 27)),
               (0.72, (21, 26, 34)), (1.00, (8, 9, 12))]
    img = up(grad((w // 5, h // 5), bgstops, (0.35, 1.0)), w, h)

    cx, cy = w * 0.50, h * 0.455
    r = min(w, h * 0.5625) * 0.435
    lw, lh = w, h

    # 球的低分辨率逐像素着色
    sw, sh = max(24, int(r * 1.2)), max(24, int(r * 1.2))
    sphere = Image.new('RGB', (sw, sh), (0, 0, 0))
    px = sphere.load()
    L = (-0.52, -0.66, 0.55)
    ln = math.sqrt(sum(v * v for v in L))
    L = tuple(v / ln for v in L)
    COOL = (86, 132, 176)
    WARM = (255, 190, 132)
    RIM = (96, 214, 226)
    SPEC = (255, 252, 244)
    for yy in range(sh):
        for xx in range(sw):
            nx = (xx + 0.5) / sw * 2 - 1
            ny = (yy + 0.5) / sh * 2 - 1
            d2 = nx * nx + ny * ny
            if d2 > 1.0:
                continue
            nz = math.sqrt(max(0.0, 1.0 - d2))
            ndl = max(0.0, nx * L[0] + ny * L[1] + nz * L[2])
            # 反射方向 z 分量（用来做视线高光）
            rdotv = max(0.0, nz * 1.0 + ny * 0.18)
            spec = rdotv ** 46
            rim = (1.0 - nz) ** 3.2
            # 上半冷、右下暖：按法线的 y 与 x 混合
            warm_mix = 0.5 - 0.5 * ny * 1.1 + 0.22 * nx
            warm_mix = 0.0 if warm_mix < 0 else (1.0 if warm_mix > 1 else warm_mix)
            base = lerp3(COOL, WARM, warm_mix)
            amb = 0.16
            c = [base[j] * (amb + 0.94 * ndl) for j in range(3)]
            c = [c[j] + RIM[j] * rim * 0.72 for j in range(3)]
            c = [c[j] + SPEC[j] * spec * 1.0 for j in range(3)]
            px[xx, yy] = tuple(min(255, int(v)) for v in c)

    sphere = up(sphere, int(r * 2), int(r * 2))
    # 圆形裁剪
    mask = Image.new('L', sphere.size, 0)
    ImageDraw.Draw(mask).ellipse([0, 0, sphere.width - 1, sphere.height - 1], fill=255)
    mask = mask.filter(ImageFilter.GaussianBlur(max(1, sphere.width * 0.004)))

    # 地面接触阴影
    sh_w = int(r * 1.55)
    sh_h = int(r * 0.30)
    shadow = Image.new('L', (sw * 2, sh_h * 2), 0)
    ImageDraw.Draw(shadow).ellipse([0, 0, sw * 2 - 1, sh_h * 2 - 1], fill=170)
    shadow = shadow.filter(ImageFilter.GaussianBlur(sh_h * 0.55))
    shadow = shadow.resize((sh_w, sh_h), Image.LANCZOS)
    dark = Image.new('RGB', (sh_w, sh_h), (0, 0, 0))
    img.paste(dark, (int(cx - sh_w / 2), int(cy + r * 0.96 - sh_h / 2)), shadow)

    # 球体背后的环境光（让体积"坐"进场景）
    halo = Image.new('L', img.size, 0)
    ImageDraw.Draw(halo).ellipse([cx - r * 1.5, cy - r * 1.5, cx + r * 1.5, cy + r * 1.5], fill=90)
    halo = halo.filter(ImageFilter.GaussianBlur(r * 0.62))
    tint = Image.new('RGB', img.size, (34, 74, 96))
    img = Image.composite(ImageChops.add(img, tint.point(lambda v: int(v * 0.85))), img, halo)

    img.paste(sphere, (int(cx - sphere.width / 2), int(cy - sphere.height / 2)), mask)

    img = glow(img, max(2, w * 0.055), 0.85)
    img = vignette(img, 0.52)
    return img


# ---------------------------------------------------------------- 场景 2：棱镜

def make_prism(w, h):
    """黑底 + 三条斜向发光色带交叠，交叠处过曝成白。

    几何结构清楚、对比极强：故障/分裂/镜像这类特效套上去立得住。
    """
    img = up(grad((w // 6, h // 6), [(0.0, (10, 11, 14)), (1.0, (5, 6, 8))],
                  (0.2, 1.0)), w, h)

    def band(angle_deg, offset, width, color, strength):
        lay = Image.new('RGB', (w, h), (0, 0, 0))
        d = ImageDraw.Draw(lay)
        a = math.radians(angle_deg)
        dx, dy = math.cos(a), math.sin(a)
        # 沿方向铺一条无限长矩形：用四边形近似
        px, py = -dy, dx
        ox, oy = w * 0.5 + px * offset * w, h * 0.5 + py * offset * h
        far = max(w, h) * 2.2
        pts = []
        for s in (1, -1):
            for t in (1, -1):
                pts.append((ox + px * width * w * 0.5 * s + dx * far * t,
                            oy + py * width * w * 0.5 * s + dy * far * t))
        d.polygon([pts[0], pts[1], pts[3], pts[2]], fill=color)
        lay = lay.filter(ImageFilter.GaussianBlur(width * w * 0.30))
        return lay.point(lambda v: int(v * strength))

    img = add_layer(img, band(-24, -0.16, 0.20, (34, 196, 212), 1.00))
    img = add_layer(img, band(-24, 0.02, 0.13, (236, 120, 74), 1.00))
    img = add_layer(img, band(-24, 0.17, 0.11, (206, 74, 158), 0.95))
    # 中心过曝
    core = Image.new('L', (w, h), 0)
    ImageDraw.Draw(core).ellipse([w * 0.34, h * 0.36, w * 0.70, h * 0.60], fill=255)
    core = core.filter(ImageFilter.GaussianBlur(w * 0.11))
    img = Image.composite(Image.new('RGB', (w, h), (250, 244, 234)), img, core)
    # 硬边细线（结构感）
    d = ImageDraw.Draw(img, 'RGBA')
    for i, (yy, al) in enumerate(((0.22, 70), (0.78, 50))):
        d.line([(0, h * yy), (w, h * yy - h * 0.05)], fill=(255, 255, 255, al),
               width=max(1, int(w * 0.004)))
    img = glow(img, max(2, w * 0.045), 0.9)
    img = vignette(img, 0.58)
    return img


# ---------------------------------------------------------------- 场景 3：环面

def make_torus(w, h):
    """环面（甜甜圈）：管壁有冷→暖渐变 + 白色高光。边界清晰，适合蒙版预览。"""
    img = up(grad((w // 6, h // 6), [(0.0, (13, 16, 21)), (0.55, (19, 24, 31)),
                                    (1.0, (10, 12, 16))], (0.4, 1.0)), w, h)
    cx, cy = w * 0.50, h * 0.46
    R = min(w, h * 0.5625) * 0.300     # 环心半径
    r = min(w, h * 0.5625) * 0.132     # 管半径

    sw = max(40, int((R + r) * 2))
    sh = sw
    tor = Image.new('RGB', (sw, sh), (0, 0, 0))
    px = tor.load()
    L = (-0.45, -0.70, 0.55)
    ln = math.sqrt(sum(v * v for v in L))
    L = tuple(v / ln for v in L)
    COOL = (78, 128, 208)
    WARM = (255, 168, 116)
    RIM = (120, 226, 236)
    SPEC = (255, 252, 246)
    c2 = sw * 0.5
    for yy in range(sh):
        for xx in range(sw):
            dx = (xx + 0.5) - c2
            dy = (yy + 0.5) - c2
            dist = math.hypot(dx, dy)
            if not (R - r < dist < R + r):
                continue
            # 管截面局部坐标
            radial = (dx / dist, dy / dist)
            u = (dist - R) / r                      # 横向 -1..1
            vv = math.sqrt(max(0.0, 1.0 - u * u))   # 朝观察者
            nx = radial[0] * u
            ny = radial[1] * u
            nz = vv
            ndl = max(0.0, nx * L[0] + ny * L[1] + nz * L[2])
            spec = max(0.0, nz) ** 30 * max(0.0, ndl) ** 3
            rim = (1.0 - max(0.0, nz)) ** 3.0
            warm_mix = 0.5 - 0.5 * ny + 0.25 * nx
            warm_mix = max(0.0, min(1.0, warm_mix))
            base = lerp3(COOL, WARM, warm_mix)
            c = [base[j] * (0.15 + 0.95 * ndl) for j in range(3)]
            c = [c[j] + RIM[j] * rim * 0.55 for j in range(3)]
            c = [c[j] + SPEC[j] * spec * 0.9 for j in range(3)]
            px[xx, yy] = tuple(min(255, int(v)) for v in c)

    tor = up(tor, int((R + r) * 2.1), int((R + r) * 2.1))
    mask = Image.new('L', tor.size, 0)
    ImageDraw.Draw(mask).ellipse([0, 0, tor.size[0] - 1, tor.size[1] - 1], fill=255)
    mask = mask.filter(ImageFilter.GaussianBlur(max(1, tor.size[0] * 0.003)))

    back = Image.new('L', img.size, 0)
    ImageDraw.Draw(back).ellipse([cx - (R + r) * 1.4, cy - (R + r) * 1.4,
                                  cx + (R + r) * 1.4, cy + (R + r) * 1.4], fill=70)
    back = back.filter(ImageFilter.GaussianBlur((R + r) * 0.9))
    img = Image.composite(ImageChops.add(img, Image.new('RGB', img.size, (20, 44, 58))),
                          img, back)

    img.paste(tor, (int(cx - tor.width / 2), int(cy - tor.height / 2)), mask)
    img = glow(img, max(2, w * 0.05), 0.8)
    img = vignette(img, 0.5)
    return img


# ---------------------------------------------------------------- 场景 4：沙丘

def make_dune(w, h):
    """层叠沙丘：从上到下 5 条弧形山脊，明暗交替，暖冷过渡。

    低饱和 + 明确的明暗分层，是测「暗部抬升 / 对比 / 去色」最直观的底图。
    """
    img = up(grad((w // 5, h // 5), [
        (0.00, (16, 22, 34)), (0.30, (34, 46, 62)), (0.52, (96, 92, 92)),
        (0.62, (196, 148, 104)), (0.80, (128, 96, 72)), (1.00, (38, 32, 30)),
    ], (0.25, 1.0)), w, h)

    d = ImageDraw.Draw(img, 'RGBA')
    layers = [
        (0.50, (42, 52, 68), 0.16, 1),
        (0.60, (60, 66, 82), 0.13, 1), 
        (0.71, (36, 42, 54), 0.12, 1),
        (0.82, (24, 28, 38), 0.11, 1),
        (0.92, (14, 17, 24), 0.09, 1),
    ]
    for k, (base_y, col, amp, _n) in enumerate(layers):
        pts = []
        for i in range(41):
            x = w * i / 40.0
            y = h * base_y - h * amp * (0.55 * math.sin(i * 0.34 + k * 1.9)
                                        + 0.30 * math.sin(i * 0.81 + k * 2.7))
            pts.append((x, y))
        pts += [(w, h), (0, h)]
        d.polygon(pts, fill=col + (255,))
        # 脊线亮边
        d.line(pts[:41], fill=(255, 240, 214, 90 if k < 2 else 46),
               width=max(1, int(w * 0.006)), joint='curve')

    # 一颗低垂的光点（给高光/过曝层）
    sx, sy, sr = w * 0.72, h * 0.30, w * 0.052
    for i in range(12, 0, -1):
        kk = i / 12.0
        rr = sr * (1 + kk * 4.2)
        d.ellipse([sx - rr, sy - rr, sx + rr, sy + rr],
                  fill=(255, 226, 180, int(16 * (1 - kk))))
    d.ellipse([sx - sr, sy - sr, sx + sr, sy + sr], fill=(255, 246, 224))

    img = glow(img, max(2, w * 0.05), 0.7)
    img = vignette(img, 0.5)
    return img


# ---------------------------------------------------------------- 入口

SCENES = {
    'orb': make_orb,
    'prism': make_prism,
    'torus': make_torus,
    'dune': make_dune,
}


def render(name, w=368, h=654):
    fn = SCENES[name]
    img = fn(w, h)
    return img.convert('RGB')


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    out = sys.argv[1]
    os.makedirs(os.path.dirname(out) or '.', exist_ok=True)
    if sys.argv[2] == '--all':
        sheet_w = 368
        ims = []
        for k in SCENES:
            ims.append((k, render(k, sheet_w, 654)))
        total = Image.new('RGB', (sheet_w * len(ims), 654), (12, 13, 15))
        for i, (k, im) in enumerate(ims):
            total.paste(im, (i * sheet_w, 0))
        total.save(out)
        print('ALL', total.size, '->', out)
    else:
        name = sys.argv[2]
        w = int(sys.argv[3]) if len(sys.argv) > 3 else 368
        h = int(sys.argv[4]) if len(sys.argv) > 4 else int(round(w * 16 / 9))
        im = render(name, w, h)
        im.save(out)
        print('SCENE', name, im.size, '->', out)
    return 0


if __name__ == '__main__':
    sys.exit(main())
