#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""为所有功能宫格生成"效果预览缩略图"。

背景
----
宫格（滤镜 / 特效 / 转场 / 蒙版）在 UI 上是一格一个正方形缩略图。
此前这些格子只有两种状态：
  · 真机上由桥的 presetFrame 现场渲染（能反映效果）
  · 网页/静态版退到 static/preset_sm、face_sm 等底图 —— 而这些底图
    是几年前的欧美面孔或通用风景，且 preset_sm 已被删除，
    结果宫格大面积空白或风格不符。
本脚本生成一套**统一风格、每格不重复**的宫格缩略图，作为离线兜底。

**本脚本不由开发者在本地执行**（本地是 aarch64 手机环境）。
它由 .github/workflows/gen-assets.yml 在 GitHub Actions（x64）上运行，
产物自动提交回仓库。本地只改代码、看 CI 结果。

id 对齐（硬约束）
-----------------
下面三张清单的 **id 必须与 assets/l3-spec.js 完全一致**，
因为 l3-panel 用 `${kind}_${item.id}` 去 map.json 里查图。
id 错位 = 点了「电影」却显示「黑白」的缩略图 —— 属于虚假实现。

设计约定（与 UI 一致）
----------------------
· 尺寸 160x160 正方形，与 .l3grid .cell .thumb 的 aspect-ratio 1/1 对齐
· 底图用 mobile/static/faces/ 下的黏土表情（卡通，不受真人肖像权限制）
· **每格用不同的黏土图**：按 (kind, id) 做稳定散列选图，
  同一效果每次构建得到同一张，不同效果必定不同
· 效果本身按该功能的真实语义做近似（滤镜偏色调、转场偏叠加位移、
  蒙版偏区域裁切），让人一眼能看出差异

输出
----
mobile/static/grid/<kind>_<id>.jpg
mobile/static/grid/map.json   —— key 形如 "filter_1" → "grid/filter_1.jpg"
"""
import hashlib
import json
import math
import os
import random
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageEnhance, ImageOps

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC_DIR = os.path.join(ROOT, 'static', 'faces')
OUT_DIR = os.path.join(ROOT, 'static', 'grid')
SIZE = 160

# ---------------------------------------------------------------- 效果清单
# 与 assets/l3-spec.js 一一对应（FILTERS / EFFECTS / TRANSITIONS / MASKS）。
# 改这里之前先改 l3-spec.js，并且两边 id 必须一致。

# FilterStyle 1..13（core/filters.h）
FILTERS = [
    (1, 'movie'), (2, 'retro'), (3, 'fresh'), (4, 'black_white'),
    (5, 'cyber'), (6, 'japanese'), (7, 'film'), (8, 'warm_sun'),
    (9, 'cool_tone'), (10, 'hong_kong'), (11, 'high_gray'), (12, 'cream'),
    (13, 'negative'),
]

# EffectType 13..35（core/effects.h）
EFFECTS = [
    (13, 'glitch'), (14, 'split_screen'), (15, 'mirror'), (16, 'radial_blur'),
    (17, 'scanlines'), (18, 'rgb_split'), (29, 'flash'), (30, 'mosaic'),
    (19, 'snow'), (20, 'rain'), (21, 'light_leak'), (22, 'old_film'),
    (23, 'starburst'),
    (24, 'zoom_punch'), (25, 'whip_pan'), (26, 'beat_flicker'),
    (27, 'beat_rhythm'), (28, 'motion_blur'),
    (31, 'jitter'), (32, 'zoom_in'), (33, 'edge_detect'), (34, 'slim'),
    (35, 'glow_outline'),
]

# TransitionType 0..33（core/transition.cpp）
TRANSITIONS = [
    (0, 'cross_dissolve'), (1, 'fade_black'), (2, 'fade_in_black'),
    (3, 'flash_white'), (4, 'wipe_left'), (5, 'wipe_right'),
    (6, 'slide_up'), (7, 'zoom_in'), (8, 'hard_cut'), (9, 'color_dissolve'),
    (10, 'blur_dissolve'), (11, 'pull_back'), (12, 'rotate'),
    (13, 'whip_pan'), (14, 'zoom_punch'), (15, 'ripple'), (16, 'liquify'),
    (17, 'fisheye'), (18, 'glitch_warp'), (19, 'rgb_split'),
    (20, 'signal_noise'), (21, 'digital_noise'), (22, 'light_sweep'),
    (23, 'lens_flare'), (24, 'exposure_flash'), (25, 'circle_wipe'),
    (26, 'blinds'), (27, 'mosaic_split'), (28, 'pixelate'),
    (29, 'particles'), (30, 'smoke'), (31, 'bokeh'),
    (32, 'film_flicker'), (33, 'film_scratch'),
]

# MaskShape 1..6（core/mask.h）
MASKS = [
    (1, 'linear'), (2, 'mirror'), (3, 'circle'),
    (4, 'rect'), (5, 'star'), (6, 'heart'),
]


# ---------------------------------------------------------------- 工具函数
def _clay_files():
    if not os.path.isdir(SRC_DIR):
        raise SystemExit(f'找不到黏土底图目录：{SRC_DIR}')
    files = sorted(f for f in os.listdir(SRC_DIR) if f.endswith('.png'))
    if not files:
        raise SystemExit(f'黏土底图目录为空：{SRC_DIR}')
    return files


CLAY_FILES = None


def load_clay(index):
    """按序号取一张黏土图；序号对文件数取模，保证一定命中。"""
    global CLAY_FILES
    if CLAY_FILES is None:
        CLAY_FILES = _clay_files()
    f = CLAY_FILES[index % len(CLAY_FILES)]
    im = Image.open(os.path.join(SRC_DIR, f)).convert('RGB')
    # 居中裁成正方形再缩放，避免拉伸变形
    w, h = im.size
    side = min(w, h)
    im = im.crop(((w - side) // 2, (h - side) // 2,
                  (w - side) // 2 + side, (h - side) // 2 + side))
    return im.resize((SIZE, SIZE), Image.LANCZOS)


def stable_index(kind, eid, total):
    """同一 (kind,id) 永远得到同一张底图；不同 id 尽量分散。"""
    h = hashlib.md5(f'{kind}:{eid}'.encode()).hexdigest()
    return int(h[:8], 16) % total


def tint(im, rgb, strength=1.0, mix=0.55):
    """把灰度映射到目标色调，再与原图混合（滤镜类用）。"""
    gray = im.convert('L')
    w, h = im.size
    px_g = gray.load()
    out = Image.new('RGB', (w, h))
    px_o = out.load()
    r, g, b = rgb
    for y in range(h):
        for x in range(w):
            v = px_g[x, y] / 255.0
            px_o[x, y] = (
                min(255, int(v * r * strength)),
                min(255, int(v * g * strength)),
                min(255, int(v * b * strength)),
            )
    return Image.blend(im, out, mix)


def vgrad(im, top, bottom, alpha=0.45):
    """上下渐变叠加（转场/光效类用）。"""
    w, h = im.size
    layer = Image.new('RGB', (w, h))
    d = ImageDraw.Draw(layer)
    for y in range(h):
        t = y / max(1, h - 1)
        d.line([(0, y), (w, y)], fill=(
            int(top[0] * (1 - t) + bottom[0] * t),
            int(top[1] * (1 - t) + bottom[1] * t),
            int(top[2] * (1 - t) + bottom[2] * t),
        ))
    return Image.blend(im, layer, alpha)


def hgrad(im, left, right, alpha=0.45):
    """左右渐变叠加（光晕扫过/甩镜类用）。"""
    w, h = im.size
    layer = Image.new('RGB', (w, h))
    d = ImageDraw.Draw(layer)
    for x in range(w):
        t = x / max(1, w - 1)
        d.line([(x, 0), (x, h)], fill=(
            int(left[0] * (1 - t) + right[0] * t),
            int(left[1] * (1 - t) + right[1] * t),
            int(left[2] * (1 - t) + right[2] * t),
        ))
    return Image.blend(im, layer, alpha)


def noise_overlay(im, eid, amount=28, step=1):
    """确定性噪点叠加（同一 id 每次一样）。"""
    w, h = im.size
    rnd = random.Random(eid * 2654435761 + 17)
    px = im.load()
    for y in range(0, h, step):
        for x in range(0, w, step):
            n = rnd.randint(-amount, amount)
            r, g, b = px[x, y]
            px[x, y] = (max(0, min(255, r + n)),
                        max(0, min(255, g + n)),
                        max(0, min(255, b + n)))
    return im


# ---------------------------------------------------------------- 滤镜
FILTER_PARAMS = {
    'movie':       dict(tint=(255, 214, 170), contrast=1.12, sat=0.85),
    'retro':       dict(tint=(230, 200, 150), contrast=0.9, sat=0.7),
    'fresh':       dict(tint=(180, 255, 210), sat=1.15),
    'black_white': dict(mode='gray'),
    'cyber':       dict(tint=(120, 220, 255), sat=1.3, contrast=1.15),
    'japanese':    dict(tint=(255, 225, 225), bright=1.1, sat=0.9),
    'film':        dict(tint=(255, 226, 190), contrast=1.06, sat=0.9),
    'warm_sun':    dict(tint=(255, 190, 120)),
    'cool_tone':   dict(tint=(150, 190, 255)),
    'hong_kong':   dict(tint=(255, 170, 140), contrast=1.2, sat=1.1),
    'high_gray':   dict(sat=0.25, contrast=1.25),
    'cream':       dict(tint=(255, 240, 215), bright=1.08),
    'negative':    dict(mode='invert'),
}


def make_filter(im, eid, slug):
    """滤镜：整体色调。参考真实滤镜语义，差异要肉眼可辨。"""
    p = FILTER_PARAMS.get(slug)
    if p is None:
        # 清单里有、字典里没有 —— 直接报错让 CI 抓到，
        # 不静默返回原图（那是「看着有格子、实际没效果」的假功能）。
        raise KeyError(f'FILTER_PARAMS 缺少 slug={slug!r} 的参数定义')
    out = im
    if p.get('mode') == 'gray':
        return ImageEnhance.Contrast(out.convert('L').convert('RGB')).enhance(1.05)
    if p.get('mode') == 'invert':
        return ImageOps.invert(out)
    if p.get('tint'):
        out = tint(out, p['tint'])
    if p.get('bright'):
        out = ImageEnhance.Brightness(out).enhance(p['bright'])
    if p.get('contrast'):
        out = ImageEnhance.Contrast(out).enhance(p['contrast'])
    if p.get('sat'):
        out = ImageEnhance.Color(out).enhance(p['sat'])
    return out


# ---------------------------------------------------------------- 特效
def make_effect(im, eid, slug):
    """特效：结构/质感变化（EffectType 语义近似）。"""
    w, h = im.size

    if slug == 'glitch':
        out = im.copy()
        for y in range(0, h, 7):
            shift = (y * 13) % 11 - 5
            band = im.crop((0, y, w, min(h, y + 7)))
            out.paste(band, (shift, y))
        return tint(out, (150, 220, 255), 1.15, 0.35)

    if slug == 'split_screen':
        out = Image.new('RGB', (w, h))
        q = im.resize((w // 2, h // 2), Image.LANCZOS)
        out.paste(q, (0, 0))
        out.paste(q.transpose(Image.FLIP_LEFT_RIGHT), (w // 2, 0))
        out.paste(q.transpose(Image.FLIP_TOP_BOTTOM), (0, h // 2))
        out.paste(q.transpose(Image.ROTATE_180), (w // 2, h // 2))
        return out

    if slug == 'mirror':
        half = im.crop((0, 0, w // 2, h)).transpose(Image.FLIP_LEFT_RIGHT)
        out = im.copy()
        out.paste(half, (w // 2, 0))
        return out

    if slug == 'radial_blur':
        acc = None
        for scale in (1.0, 1.06, 1.12, 1.18):
            zw, zh = int(w * scale), int(h * scale)
            z = im.resize((zw, zh), Image.LANCZOS).crop(
                ((zw - w) // 2, (zh - h) // 2, (zw - w) // 2 + w, (zh - h) // 2 + h))
            acc = z if acc is None else Image.blend(acc, z, 0.5)
        return acc

    if slug == 'scanlines':
        out = im.copy()
        d = ImageDraw.Draw(out)
        for y in range(0, h, 4):
            d.line([(0, y), (w, y)], fill=(0, 0, 0), width=1)
        return ImageEnhance.Brightness(out).enhance(0.92)

    if slug == 'rgb_split':
        # 红色右移、蓝色左移，绿色不动
        r, g, b = im.split()
        r2 = Image.new('L', (w, h), 0)
        r2.paste(r, (4, 0))
        b2 = Image.new('L', (w, h), 0)
        b2.paste(b, (-4, 0))
        return Image.merge('RGB', (r2, g, b2))

    if slug == 'flash':
        return ImageEnhance.Brightness(im).enhance(1.6)

    if slug == 'mosaic':
        small = im.resize((w // 12, h // 12), Image.LANCZOS)
        return small.resize((w, h), Image.NEAREST)

    if slug == 'snow':
        out = im.copy()
        d = ImageDraw.Draw(out)
        rnd = random.Random(eid * 7 + 3)
        for _ in range(110):
            x = rnd.randrange(w); y = rnd.randrange(h)
            r = rnd.choice([1, 1, 2, 3])
            d.ellipse([x, y, x + r, y + r], fill=(255, 255, 255))
        return ImageEnhance.Brightness(out).enhance(1.05)

    if slug == 'rain':
        out = im.copy()
        d = ImageDraw.Draw(out)
        rnd = random.Random(eid * 11 + 5)
        for _ in range(70):
            x = rnd.randrange(w); y = rnd.randrange(h)
            d.line([(x, y), (x - 2, y + 9)], fill=(220, 235, 255), width=1)
        return ImageEnhance.Brightness(out).enhance(0.9)

    if slug == 'light_leak':
        out = vgrad(im, (255, 200, 130), (255, 245, 215), 0.55)
        return ImageEnhance.Brightness(out).enhance(1.12)

    if slug == 'old_film':
        out = tint(im, (230, 200, 150), 1.0, 0.5)
        out = noise_overlay(out, eid, 24, 1)
        return ImageEnhance.Contrast(out).enhance(0.88)

    if slug == 'starburst':
        out = im.copy()
        d = ImageDraw.Draw(out)
        cx, cy = w * 0.5, h * 0.34
        for ang in range(0, 360, 45):
            a = math.radians(ang)
            d.line([(cx, cy), (cx + math.cos(a) * w * 0.45,
                               cy + math.sin(a) * h * 0.45)],
                   fill=(255, 248, 220), width=2)
        return ImageEnhance.Brightness(out).enhance(1.08)

    if slug == 'zoom_punch':
        inner = im.crop((int(w * .18), int(h * .18), int(w * .82), int(h * .82)))
        inner = inner.resize((w, h), Image.LANCZOS)
        out = im.copy()
        out.paste(inner, (0, 0))
        return ImageEnhance.Contrast(out).enhance(1.2)

    if slug == 'whip_pan':
        out = im.copy()
        for dx in (-10, -5, 5, 10):
            layer = Image.new('RGB', (w, h), (0, 0, 0))
            layer.paste(im, (dx, 0))
            out = Image.blend(out, layer, 0.18)
        return hgrad(out, (255, 255, 255), (0, 0, 0), 0.12)

    if slug == 'beat_flicker':
        return ImageEnhance.Brightness(im).enhance(1.35)

    if slug == 'beat_rhythm':
        out = im.copy()
        d = ImageDraw.Draw(out)
        for x in range(0, w, 16):
            d.line([(x, 0), (x, h)], fill=(255, 255, 255), width=1)
        return ImageEnhance.Contrast(out).enhance(1.15)

    if slug == 'motion_blur':
        out = im.copy()
        for dx in (-8, -4, 4, 8):
            layer = Image.new('RGB', (w, h), (0, 0, 0))
            layer.paste(im, (dx, 0))
            out = Image.blend(out, layer, 0.22)
        return out

    if slug == 'jitter':
        out = im.copy()
        d = ImageDraw.Draw(out)
        rnd = random.Random(eid * 31 + 9)
        for _ in range(26):
            x = rnd.randrange(w); y = rnd.randrange(h)
            bw, bh = rnd.randint(4, 12), rnd.randint(2, 6)
            band = im.crop((x, y, min(w, x + bw), min(h, y + bh)))
            out.paste(band, (x + rnd.randint(-5, 5), y + rnd.randint(-4, 4)))
        return out

    if slug == 'zoom_in':
        inner = im.crop((int(w * .12), int(h * .12), int(w * .88), int(h * .88)))
        return inner.resize((w, h), Image.LANCZOS)

    if slug == 'edge_detect':
        edges = im.convert('L').filter(ImageFilter.FIND_EDGES)
        return Image.merge('RGB', [edges, edges, edges])

    if slug == 'slim':
        narrowed = im.resize((int(w * 0.82), h), Image.LANCZOS)
        canvas = Image.new('RGB', (w, h), (24, 26, 26))
        canvas.paste(narrowed, ((w - narrowed.size[0]) // 2, 0))
        return canvas

    if slug == 'glow_outline':
        edges = im.convert('L').filter(ImageFilter.FIND_EDGES)
        edges = ImageEnhance.Contrast(edges).enhance(2.2)
        glow = edges.filter(ImageFilter.GaussianBlur(3))
        base = ImageEnhance.Brightness(im).enhance(0.55)
        halo = Image.merge('RGB', (
            glow, glow.point(lambda v: min(255, int(v * 1.1))),
            glow.point(lambda v: min(255, int(v * 0.6)))))
        return Image.blend(base, halo, 0.6)

    # 未知 slug 必须炸，不静默返回「没处理的原图」——
    # 静默兜底会让漏写的分支看起来像生成成功（假功能）。
    raise KeyError(f'make_effect 未实现 slug={slug!r}')


# ---------------------------------------------------------------- 转场
def _pair(im, eid):
    """取转场用的 A/B 两帧。

    **必须返回尺寸相同的两张不同图**：
      · `Image.blend` 要求尺寸一致；各分支的 `crop` 边界也按 `a.size` 算。
      · A 与 B 若恰好选中同一张黏土图，「交叉溶解」就变成
        「原图与原图混合」—— 肉眼看不出任何转场效果，等于假格子。

    主流程传入的 A = `load_clay(stable_index('transition', eid, n))`，
    所以这里用同一表达式还原 A 的索引，再给 B 换一个不同的索引
    （`stable_index` 是纯散列，两个不同 salt 完全可能撞到同一个槽）。
    """
    total = len(_clay_files())
    a_idx = stable_index('transition', eid, total)
    b_idx = stable_index('transition_b', eid, total)
    if total > 1 and b_idx == a_idx:
        b_idx = (b_idx + 1) % total
    return im, load_clay(b_idx)


def make_transition(im, eid, slug):
    """转场：需要两段画面 → A（本题底图）与 B（另一张黏土图）的半程混合。"""
    a, b = _pair(im, eid)
    w, h = a.size
    p = 0.5  # 转场进行到一半

    if slug == 'cross_dissolve':
        return Image.blend(a, b, p)

    if slug == 'fade_black':
        mid = Image.blend(a, b, p)
        return Image.blend(mid, Image.new('RGB', (w, h), (0, 0, 0)), 0.5)

    if slug == 'fade_in_black':
        mid = Image.blend(a, b, p)
        return Image.blend(Image.new('RGB', (w, h), (0, 0, 0)), mid, 0.45)

    if slug == 'flash_white':
        mid = Image.blend(a, b, p)
        return Image.blend(mid, Image.new('RGB', (w, h), (255, 255, 255)), 0.55)

    if slug == 'wipe_left':
        # 左向擦除：B 从左侧推入（左半 B，右半仍 A）
        out = a.copy()
        out.paste(b.crop((0, 0, w // 2, h)), (0, 0))
        return out

    if slug == 'wipe_right':
        # 右向擦除：B 从右侧推入（右半 B，左半仍 A）
        out = a.copy()
        out.paste(b.crop((w // 2, 0, w, h)), (w // 2, 0))
        return out

    if slug == 'slide_up':
        out = a.copy()
        out.paste(b.crop((0, 0, w, h // 2)), (0, h // 2))
        return out

    if slug == 'hard_cut':
        # 硬切：没有混合，只有一刀两断。用 40% 分界 + 白线，
        # 与 wipe_right（50% 分界、无分界线）明确区分；
        # 否则两格肉眼几乎一样，等于少一格。
        cut = int(w * 0.4)
        out = a.copy()
        out.paste(b.crop((cut, 0, w, h)), (cut, 0))
        ImageDraw.Draw(out).line([(cut, 0), (cut, h)], fill=(255, 255, 255), width=2)
        return ImageEnhance.Contrast(out).enhance(1.05)

    if slug == 'color_dissolve':
        return Image.blend(a, tint(b, (255, 210, 150), 1.0, 0.45), 0.6)

    if slug == 'blur_dissolve':
        return Image.blend(a.filter(ImageFilter.GaussianBlur(3)),
                           b.filter(ImageFilter.GaussianBlur(3)), 0.55)

    if slug == 'zoom_in':
        # 「缩放」转场：B 画面从中心放大压过来
        zw, zh = int(w * 1.35), int(h * 1.35)
        z = b.resize((zw, zh), Image.LANCZOS).crop(
            ((zw - w) // 2, (zh - h) // 2, (zw - w) // 2 + w, (zh - h) // 2 + h))
        return Image.blend(a, z, 0.55)

    if slug == 'pull_back':
        small = b.resize((int(w * 0.62), int(h * 0.62)), Image.LANCZOS)
        out = a.copy()
        out.paste(small, ((w - small.size[0]) // 2, (h - small.size[1]) // 2))
        return out

    if slug == 'rotate':
        return Image.blend(a, b.rotate(14, resample=Image.BILINEAR), 0.6)

    if slug == 'whip_pan':
        mid = Image.blend(a, b, 0.5)
        out = mid
        for dx in (-12, -6, 6, 12):
            layer = Image.new('RGB', (w, h), (0, 0, 0))
            layer.paste(mid, (dx, 0))
            out = Image.blend(out, layer, 0.2)
        return out

    if slug == 'zoom_punch':
        zw, zh = int(w * 1.4), int(h * 1.4)
        z = b.resize((zw, zh), Image.LANCZOS).crop(
            ((zw - w) // 2, (zh - h) // 2, (zw - w) // 2 + w, (zh - h) // 2 + h))
        return Image.blend(a, ImageEnhance.Contrast(z).enhance(1.25), 0.55)

    if slug == 'ripple':
        mid = Image.blend(a, b, 0.5)
        out = Image.new('RGB', (w, h), (0, 0, 0))
        for y in range(h):
            dx = int(7 * math.sin(y / 6.0))
            out.paste(mid.crop((0, y, w, y + 1)), (dx, y))
        return out

    if slug == 'liquify':
        mid = Image.blend(a, b, 0.5)
        out = Image.new('RGB', (w, h), (0, 0, 0))
        for y in range(h):
            dx = int(9 * math.sin(y / 13.0 + 1.2))
            dy = int(3 * math.sin(y / 9.0))
            out.paste(mid.crop((0, y, w, y + 1)), (dx, max(0, min(h - 1, y + dy))))
        return out

    if slug == 'fisheye':
        mid = Image.blend(a, b, 0.5)
        zw, zh = int(w * 0.78), int(h * 0.78)
        out = mid.filter(ImageFilter.GaussianBlur(2.4))
        center = mid.resize((zw, zh), Image.LANCZOS)
        out.paste(center, ((w - zw) // 2, (h - zh) // 2))
        return out

    if slug == 'glitch_warp':
        out = Image.blend(a, b, 0.5)
        for y in range(0, h, 5):
            shift = (y * 17) % 13 - 6
            band = out.crop((0, y, w, min(h, y + 5)))
            out.paste(band, (shift, y))
        return tint(out, (160, 220, 255), 1.1, 0.3)

    if slug == 'rgb_split':
        mid = Image.blend(a, b, 0.5)
        r, g, bl = mid.split()
        r2 = Image.new('L', (w, h), 0); r2.paste(r, (5, 0))
        b2 = Image.new('L', (w, h), 0); b2.paste(bl, (-5, 0))
        return Image.merge('RGB', (r2, g, b2))

    if slug == 'signal_noise':
        return noise_overlay(Image.blend(a, b, 0.5), eid, 46, 1)

    if slug == 'digital_noise':
        mid = Image.blend(a, b, 0.5)
        tiny = mid.resize((w // 8, h // 8), Image.LANCZOS)
        blocky = tiny.resize((w, h), Image.NEAREST)
        return Image.blend(mid, blocky, 0.55)

    if slug == 'light_sweep':
        mid = Image.blend(a, b, 0.45)
        return hgrad(mid, (255, 245, 205), (20, 20, 26), 0.6)

    if slug == 'lens_flare':
        mid = Image.blend(a, b, 0.45)
        d = ImageDraw.Draw(mid)
        cx, cy = w * 0.7, h * 0.3
        for r, col in ((26, (255, 240, 200)), (16, (255, 255, 235)), (8, (255, 255, 255))):
            d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=col)
        return ImageEnhance.Brightness(mid).enhance(1.1)

    if slug == 'exposure_flash':
        # 曝光闪烁：整体提亮 + 暖偏。与 flash_white 的区别是
        # flash_white 是「白场压过来」，这里是「画面本身过曝」，
        # 两格必须能一眼分开。
        bright = ImageEnhance.Brightness(Image.blend(a, b, 0.35)).enhance(1.7)
        return tint(bright, (255, 240, 200), 1.02, 0.25)

    if slug == 'circle_wipe':
        mask = Image.new('L', (w, h), 0)
        ImageDraw.Draw(mask).ellipse([w * .18, h * .18, w * .82, h * .82], fill=255)
        mask = mask.filter(ImageFilter.GaussianBlur(2))
        return Image.composite(b, a, mask)

    if slug == 'blinds':
        mask = Image.new('L', (w, h), 0)
        d = ImageDraw.Draw(mask)
        for y in range(0, h, 10):
            d.rectangle([0, y, w, y + 5], fill=255)
        return Image.composite(b, a, mask)

    if slug == 'mosaic_split':
        mid = Image.blend(a, b, 0.5)
        small = mid.resize((w // 14, h // 14), Image.LANCZOS).resize((w, h), Image.NEAREST)
        out = mid.copy()
        out.paste(small.crop((0, 0, w, h // 2)), (0, 0))
        return out

    if slug == 'pixelate':
        mid = Image.blend(a, b, 0.5)
        small = mid.resize((w // 16, h // 16), Image.LANCZOS)
        return small.resize((w, h), Image.NEAREST)

    if slug == 'particles':
        out = Image.blend(a, b, 0.6)
        d = ImageDraw.Draw(out)
        rnd = random.Random(eid * 11 + 5)
        for _ in range(80):
            x = rnd.randrange(w); y = rnd.randrange(h); r = rnd.choice([1, 2, 3])
            d.ellipse([x, y, x + r, y + r], fill=(255, 240, 200))
        return out

    if slug == 'smoke':
        mid = Image.blend(a, b, 0.55).filter(ImageFilter.GaussianBlur(4))
        return ImageEnhance.Brightness(mid).enhance(1.1)

    if slug == 'bokeh':
        out = Image.blend(a, b, 0.5)
        d = ImageDraw.Draw(out)
        rnd = random.Random(eid * 13 + 7)
        for _ in range(40):
            x = rnd.randrange(w); y = rnd.randrange(h); r = rnd.choice([4, 6, 9])
            d.ellipse([x, y, x + r, y + r], fill=(255, 228, 180))
        return ImageEnhance.Brightness(out).enhance(1.06)

    if slug == 'film_flicker':
        mid = Image.blend(a, b, 0.5)
        return noise_overlay(ImageEnhance.Brightness(mid).enhance(1.18), eid, 22, 1)

    if slug == 'film_scratch':
        mid = Image.blend(a, b, 0.5)
        d = ImageDraw.Draw(mid)
        rnd = random.Random(eid * 17 + 3)
        for _ in range(18):
            x = rnd.randrange(w)
            d.line([(x, 0), (x + rnd.randint(-4, 4), h)], fill=(240, 240, 235), width=1)
        return ImageEnhance.Contrast(mid).enhance(0.92)

    # 未知 slug 必须炸：静默 fallback 会把「漏写的转场」变成
    # 「一张普通交叉溶解」，看起来生成成功，实际是错的效果图。
    raise KeyError(f'make_transition 未实现 slug={slug!r}')


# ---------------------------------------------------------------- 蒙版
def make_mask(im, eid, slug):
    """蒙版：形状外压暗、形状内保留（一眼能看出裁切形状）。"""
    w, h = im.size
    mask = Image.new('L', (w, h), 0)
    d = ImageDraw.Draw(mask)
    if slug == 'linear':
        d.polygon([(0, h), (w, 0), (w, h)], fill=255)
    elif slug == 'mirror':
        mask.paste(255, (w // 2, 0, w, h))
        mask = mask.filter(ImageFilter.GaussianBlur(3))
    elif slug == 'circle':
        d.ellipse([w * .12, h * .12, w * .88, h * .88], fill=255)
    elif slug == 'rect':
        d.rectangle([w * .12, h * .16, w * .88, h * .84], fill=255)
    elif slug == 'star':
        pts = []
        cx, cy, R, r = w / 2, h / 2, w * .42, w * .18
        for i in range(10):
            ang = -math.pi / 2 + i * math.pi / 5
            rad = R if i % 2 == 0 else r
            pts.append((cx + rad * math.cos(ang), cy + rad * math.sin(ang)))
        d.polygon(pts, fill=255)
    elif slug == 'heart':
        pts = []
        for i in range(120):
            t = i / 120 * 2 * math.pi
            x = 16 * math.sin(t) ** 3
            y = -(13 * math.cos(t) - 5 * math.cos(2 * t) -
                  2 * math.cos(3 * t) - math.cos(4 * t))
            pts.append((w / 2 + x * w / 40, h / 2 + y * h / 40))
        d.polygon(pts, fill=255)
    else:
        # 未知 slug 必须炸：mask 全 0 时整张图会被压到 30% 亮度，
        # 看起来像「一个很暗的蒙版」，实际是漏实现。
        raise KeyError(f'make_mask 未实现 slug={slug!r}')

    dark = ImageEnhance.Brightness(im).enhance(0.3)
    return Image.composite(im, dark, mask)


# ---------------------------------------------------------------- 主流程
PLAN = [
    ('filter', FILTERS, make_filter),
    ('effect', EFFECTS, make_effect),
    ('transition', TRANSITIONS, make_transition),
    ('mask', MASKS, make_mask),
]


def main():
    if not os.path.isdir(SRC_DIR):
        raise SystemExit(f'缺少黏土底图目录：{SRC_DIR}')
    os.makedirs(OUT_DIR, exist_ok=True)

    mapping = {}
    counts = {'filter': 0, 'effect': 0, 'transition': 0, 'mask': 0}
    failures = []

    for kind, items, fn in PLAN:
        for eid, slug in items:
            idx = stable_index(kind, eid, len(_clay_files()))
            base = load_clay(idx)
            try:
                img = fn(base, eid, slug)
            except Exception as e:  # 单个失败不影响整批，但最后要判定
                print(f'  [fail] {kind}_{eid} ({slug}) 生成失败：{e}', file=sys.stderr)
                failures.append(f'{kind}_{eid}')
                continue
            img = img.convert('RGB').resize((SIZE, SIZE), Image.LANCZOS)
            name = f'{kind}_{eid}.jpg'
            img.save(os.path.join(OUT_DIR, name), 'JPEG', quality=88, optimize=True)
            mapping[f'{kind}_{eid}'] = f'grid/{name}'
            counts[kind] += 1

    with open(os.path.join(OUT_DIR, 'map.json'), 'w', encoding='utf-8') as f:
        json.dump(mapping, f, ensure_ascii=False, indent=1)

    total = sum(counts.values())
    expect = sum(len(x[1]) for x in PLAN)
    print(f'生成 {total} 张宫格缩略图 → {OUT_DIR}')
    print(f'  filter {counts["filter"]} / effect {counts["effect"]} / '
          f'transition {counts["transition"]} / mask {counts["mask"]}')
    print(f'映射表 {len(mapping)} 条 → map.json')

    if failures:
        print(f'❌ {len(failures)} 格生成失败：{", ".join(failures)}')
        return 1
    if total != expect:
        print(f'❌ 数量不符：期望 {expect}，实际 {total}')
        return 1

    # 自检：每格图像必须互不相同（否则「看起来没区别」，等于没做）
    # 路径直接由 OUT_DIR + 文件名拼，不用 basename 去猜 ——
    # map.json 的值是 `grid/<name>.jpg`（相对 static/ 的路径），
    # 用 basename 会在目录结构变化时静默读到错误文件，让「去重自检」变成假绿。
    seen, dup = {}, []
    for key in mapping:
        with open(os.path.join(OUT_DIR, f'{key}.jpg'), 'rb') as fp:
            h = hashlib.md5(fp.read()).hexdigest()
        if h in seen:
            dup.append((key, seen[h]))
        seen[h] = key
    if dup:
        print(f'❌ {len(dup)} 组重复：')
        for k, other in dup:
            print(f'   {k} == {other}')
        return 1
    print(f'✅ {total} 格图像互不相同')
    return 0


if __name__ == '__main__':
    sys.exit(main())
