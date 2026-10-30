#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""校验 `mobile/static/grid/` 产物完整性。

必须在生成之后跑。检查六件事：

1. `map.json` 存在且可解析
2. 每个映射指向的 jpg **真实存在**
3. jpg 数量 == 映射条数（多出来的孤儿文件说明生成逻辑有残留）
4. 映射键集合 == 四类清单的 `<kind>_<id>` 全集
   （少了 = 某类没生成；多了 = 生成了 UI 点不到的孤儿）
5. 每张图尺寸 160×160、RGB JPEG（尺寸错了宫格会拉伸变形）
6. 每张图 md5 互不相同（否则「看起来没区别」等于没做）

用法
----
    python3 tools/verify_grid_assets.py
"""
import hashlib
import json
import os
import re
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GRID_DIR = os.path.join(ROOT, 'mobile', 'static', 'grid')
STATIC_DIR = os.path.join(ROOT, 'mobile', 'static')
GEN_PY = os.path.join(ROOT, 'mobile', 'tools', 'gen_grid_previews.py')

SIZE = 160
KINDS = ('filter', 'effect', 'transition', 'mask')


def expected_keys():
    """从生成器清单推出应当存在的全部键：`<kind>_<id>`。"""
    with open(GEN_PY, encoding='utf-8') as f:
        src = f.read()
    want = set()
    for name, kind in (('FILTERS', 'filter'), ('EFFECTS', 'effect'),
                       ('TRANSITIONS', 'transition'), ('MASKS', 'mask')):
        m = re.search(rf'^{name} = \[(.*?)\n\]', src, re.S | re.M)
        if not m:
            raise SystemExit(f'❌ 生成器里找不到清单 {name}')
        ids = re.findall(r'\((\d+),', m.group(1))
        if not ids:
            raise SystemExit(f'❌ 清单 {name} 解析不到 id')
        want |= {f'{kind}_{i}' for i in ids}
    return want


def main():
    fail = []

    map_path = os.path.join(GRID_DIR, 'map.json')
    if not os.path.isfile(map_path):
        raise SystemExit(f'❌ 没有 {map_path}（生成器没跑？）')

    with open(map_path, encoding='utf-8') as f:
        mapping = json.load(f)
    print(f'map.json：{len(mapping)} 条')

    # 2) 每条映射的图都要存在
    missing = []
    for key, rel in mapping.items():
        if not os.path.isfile(os.path.join(STATIC_DIR, rel)):
            missing.append(f'{key} → {rel}')
    if missing:
        fail.append(f'{len(missing)} 条映射指向不存在的文件：{missing[:5]}')

    # 3) 数量一致
    jpgs = sorted(f for f in os.listdir(GRID_DIR) if f.endswith('.jpg'))
    if len(jpgs) != len(mapping):
        fail.append(f'jpg 数 {len(jpgs)} != 映射条数 {len(mapping)}')

    # 4) 键集合 == 清单全集
    want = expected_keys()
    got = set(mapping)
    if want - got:
        fail.append(f'缺 {len(want - got)} 个键：{sorted(want - got)[:10]}')
    if got - want:
        fail.append(f'多 {len(got - want)} 个键（UI 点不到）：{sorted(got - want)[:10]}')

    # 5) 尺寸与色彩模式
    bad = []
    for f in jpgs:
        try:
            with Image.open(os.path.join(GRID_DIR, f)) as im:
                if im.size != (SIZE, SIZE):
                    bad.append(f'{f} {im.size}')
                elif im.mode != 'RGB':
                    bad.append(f'{f} mode={im.mode}')
        except Exception as e:
            bad.append(f'{f} 打不开：{e}')
    if bad:
        fail.append(f'{len(bad)} 张图尺寸/模式不对：{bad[:5]}')
    else:
        print(f'✅ {len(jpgs)} 张图全部 {SIZE}×{SIZE} RGB')

    # 6) 图像互不相同
    seen = {}
    dup = []
    for f in jpgs:
        with open(os.path.join(GRID_DIR, f), 'rb') as fp:
            h = hashlib.md5(fp.read()).hexdigest()
        if h in seen:
            dup.append(f'{f} == {seen[h]}')
        else:
            seen[h] = f
    if dup:
        fail.append(f'{len(dup)} 组图像重复：{dup[:5]}')
    else:
        print('✅ 全部图像互不相同')

    # 每类数量（写进日志，便于人工核对）
    for kind in KINDS:
        n = sum(1 for k in mapping if k.startswith(kind + '_'))
        print(f'   {kind:11s} {n}')

    if fail:
        print()
        for msg in fail:
            print(f'❌ {msg}')
        return 1

    print(f'\n✅ 全部通过：{len(mapping)} 格，映射与产物一一对应')
    return 0


if __name__ == '__main__':
    sys.exit(main())
