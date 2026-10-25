#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""为滤镜宫格生成 13 张预览图。

要求（用户指定）：
  · 全部用**风景**图，不用人像；
  · 每个滤镜用**不同的**风景原图（不是同一张套 13 次）；
  · 每张图上套用该滤镜的真实参数（经 ng_filter，与引擎导出逐像素一致）。

输出：static/preset_sm/<slug>_s1.jpg（120x180，宫格单格尺寸）
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__))))
from ng_filter import filter_image

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(ROOT, 'static', 'preset_sm')
THUMB_W, THUMB_H = 120, 180

# 滤镜 slug → 专属风景底图（一一对应，不重复）
PLAN = [
    ('movie',       's_152371299961.jpg'),                        # 电影 · 阳光森林
    ('retro',       'land_desert.jpg'),                           # 复古 · 沙漠
    ('fresh',       's_149075096786.jpg'),                        # 清新 · 黄花花田
    ('black_white', 'photo-1519681393784-d120267933ba.jpg'),      # 黑白 · 星空雪山
    ('cyber',       'land_snow.jpg'),                             # 赛博 · 雪山
    ('japanese',    's_152238322565.jpg'),                        # 日系 · 樱花
    ('film',        'land_lake.jpg'),                             # 胶片 · 湖泊
    ('warm_sun',    'land_coast.jpg'),                            # 暖阳 · 海岸
    ('cool_tone',   'x_143308696635.jpg'),                        # 冷调 · 瀑布
    ('hong_kong',   'b.jpg'),                                     # 港风 · 树林小路
    ('high_gray',   'land_canyon.jpg'),                           # 高级灰 · 峡谷
    ('cream',       'n_147221410345.jpg'),                        # 奶油 · 绿丘
    ('negative',    'x_144775287521.jpg'),                        # 负片 · 木栈道
]


def cover(im, w, h):
    """等比裁切填满目标框（居中）。"""
    sw, sh = im.size
    if sw / sh > w / h:
        nw, nh = int(sh * w / h), sh
    else:
        nw, nh = sw, int(sw * h / w)
    return im.crop(((sw - nw) // 2, (sh - nh) // 2,
                    (sw + nw) // 2, (sh + nh) // 2)).resize((w, h), Image.LANCZOS)


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    made, missing = 0, []
    for slug, photo in PLAN:
        src_path = os.path.join(ROOT, 'static', 'photos', photo)
        if not os.path.exists(src_path):
            missing.append(photo)
            continue
        src = Image.open(src_path).convert('RGB')
        # 先放大裁切（保证滤镜在足够像素上运算，再缩到缩略尺寸）
        base = cover(src, THUMB_W * 3, THUMB_H * 3)
        out = filter_image(base, slug).resize((THUMB_W, THUMB_H), Image.LANCZOS)
        dst = os.path.join(OUT_DIR, f'{slug}_s1.jpg')
        out.save(dst, quality=92, optimize=True)
        made += 1
        print(f'  {slug:12} <- {photo:42} {os.path.getsize(dst)}B')
    print(f'\nmade {made} / {len(PLAN)}')
    if missing:
        print('MISSING:', missing)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
