#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""校验「宫格缩略图生成器」与「UI 规格表」的 id 清单是否一致。

为什么必须有这一步
------------------
生成器 `mobile/tools/gen_grid_previews.py` 把四类清单（滤镜/特效/转场/蒙版）
写死在 Python 里；UI 的同一批清单写在 `mobile/assets/l3-spec.js` 里。

两边一旦漂移，`map.json` 的键就会对不上 UI 的 `<kind>_<id>` 查询键 ——
表现是**宫格图张冠李戴**（点「电影」显示「黑白」的缩略图），
或者整片空白。这种错误在界面上完全看不出来，属于「看着有功能、实际是错的」。

所以：生成之前先用机器比对。不一致直接退出码 1，不产出任何产物。

解析策略
--------
**按行解析**，不用跨行正则。原因：l3-spec.js 里的清单会被人不断编辑，
缩进/换行一变，正则就悄悄匹配到别处（甚至匹配到注释里的 `{ id: N }`），
结果是「校验通过但校验的是错的东西」—— 比不校验更危险。

行式规则：
  · 起始行 = `  const <NAME> = [` 或 `  const <NAME> = {`（缩进固定 2 空格）
  · 结束行 = 单独的 `  ];` 或 `  };`
  · 区间内的 `{ id: <n> ... }` 全部计入

用法
----
    python3 tools/check_grid_spec_alignment.py

成功打印每组比对结果并返回 0；任一组不一致返回 1。
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SPEC_JS = os.path.join(ROOT, 'mobile', 'assets', 'l3-spec.js')
GEN_PY = os.path.join(ROOT, 'mobile', 'tools', 'gen_grid_previews.py')

ID_IN_JS = re.compile(r'\{\s*id:\s*(\d+)')
ID_IN_PY = re.compile(r'\(\s*(\d+)\s*,')
CONST_START = re.compile(r'^  const (\w+) = ([\[{])$')
BLOCK_END = {'[': '  ];', '{': '  };'}


def spec_id_sets(path):
    """从 l3-spec.js 抽出每个 `const NAME = [...]` / `{...}` 块的 id 集合。"""
    with open(path, encoding='utf-8') as f:
        lines = f.read().splitlines()

    out = {}
    i = 0
    while i < len(lines):
        m = CONST_START.match(lines[i])
        if not m:
            i += 1
            continue
        name, opener = m.group(1), m.group(2)
        end = BLOCK_END[opener]
        body = []
        j = i + 1
        while j < len(lines) and lines[j].rstrip() != end:
            body.append(lines[j])
            j += 1
        if j >= len(lines):
            raise SystemExit(
                f'❌ l3-spec.js: `const {name} = {opener}` 没有找到结束行 {end!r}')
        out[name] = [int(x) for x in ID_IN_JS.findall('\n'.join(body))]
        i = j + 1
    return out


def gen_id_sets(path):
    """从 gen_grid_previews.py 抽出每个模块级 `NAME = [...]` 块的 id 集合。"""
    with open(path, encoding='utf-8') as f:
        lines = f.read().splitlines()

    start = re.compile(r'^([A-Z][A-Z0-9_]*) = \[$')
    out = {}
    i = 0
    while i < len(lines):
        m = start.match(lines[i])
        if not m:
            i += 1
            continue
        name = m.group(1)
        body = []
        j = i + 1
        while j < len(lines) and lines[j].rstrip() != ']':
            body.append(lines[j])
            j += 1
        if j >= len(lines):
            raise SystemExit(f'❌ 生成器: `{name} = [` 没有找到结束行 `]`')
        out[name] = [int(x) for x in ID_IN_PY.findall('\n'.join(body))]
        i = j + 1
    return out


# UI 规格表 → 生成器常量。EFFECTS 在 JS 里是分组对象，四个数组的 id 合起来算。
PAIRS = [
    ('FILTERS', 'FILTERS'),
    ('EFFECTS', 'EFFECTS'),
    ('TRANSITIONS', 'TRANSITIONS'),
    ('MASKS', 'MASKS'),
]


def main():
    for p in (SPEC_JS, GEN_PY):
        if not os.path.isfile(p):
            raise SystemExit(f'❌ 找不到文件：{p}')

    spec = spec_id_sets(SPEC_JS)
    gen = gen_id_sets(GEN_PY)

    fail = 0
    for spec_name, gen_name in PAIRS:
        if spec_name not in spec:
            raise SystemExit(f'❌ l3-spec.js 里没有 `const {spec_name} = ...`')
        if gen_name not in gen:
            raise SystemExit(f'❌ 生成器里没有 `{gen_name} = [...]`')

        a, b = sorted(spec[spec_name]), sorted(gen[gen_name])
        if len(a) != len(set(a)):
            print(f'❌ l3-spec.js 的 {spec_name} 有重复 id')
            fail = 1
        if len(b) != len(set(b)):
            print(f'❌ 生成器的 {gen_name} 有重复 id')
            fail = 1
        if a != b:
            only_spec = [x for x in a if x not in set(b)]
            only_gen = [x for x in b if x not in set(a)]
            print(f'❌ {spec_name} 不一致')
            print(f'   l3-spec.js 独有：{only_spec or "（无）"}')
            print(f'   生成器独有  ：{only_gen or "（无）"}')
            print(f'   l3-spec.js : {a}')
            print(f'   生成器      : {b}')
            fail = 1
        else:
            print(f'✅ {spec_name} 一致（{len(a)} 项）')

    if fail:
        print('\n修法：让两边 id 集合完全相同。'
              '\n  · UI 侧要加新项 → 同步在 gen_grid_previews.py 的清单里加（同 id）'
              '\n  · 生成器侧要加 → 同步在 l3-spec.js 里加（否则用户点不到）')
        return 1

    total = sum(len(spec[n]) for n, _ in PAIRS)
    print(f'\n全部一致，宫格总数 {total}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
