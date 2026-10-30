#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""校验宫格生成器的「清单 → 实现分支」覆盖率。

为什么需要
----------
`gen_grid_previews.py` 里每类效果是一个 slug 清单 + 一堆 `if slug == '...'`
分支。如果清单里加了新 slug 却忘了写对应分支，函数会**静默返回原图**，
产物是「一张没做任何处理的黏土脸」—— 文件非空、md5 不同、自检通过，
但那个格子等于**假功能**。

这类漏洞只有机器能查：图生成出来了，肉眼很难发现「第 17 格的图没效果」。

做法
----
解析每个 `make_*()` 函数体，收集
  · `slug == 'x'`
  · `slug in ('a', 'b', ...)`
  · 字典查表（`make_filter` 用 `FILTER_PARAMS[slug]`）的键
与对应清单的 slug 集合比对，缺一个就退出码 1。

用法
----
    python3 tools/check_grid_render_coverage.py
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GEN = os.path.join(ROOT, 'mobile', 'tools', 'gen_grid_previews.py')

# 清单名 → 实现函数名 → 可选字典名
TARGETS = [
    ('FILTERS', 'make_filter', 'FILTER_PARAMS'),
    ('EFFECTS', 'make_effect', None),
    ('TRANSITIONS', 'make_transition', None),
    ('MASKS', 'make_mask', None),
]


def block(src, header_regex, end_lines, what):
    """取从 header 之后到第一个 end_lines 中任一出现处的内容。

    结束标记用列表而非单个字符串：`make_mask()` 是文件里最后一个 make_*
    函数，后面紧跟的是 `\n# ---- 主流程` 而不是另一个 `\ndef `，
    只认 `\ndef ` 会误报「找不到结束标记」。
    """
    m = re.search(header_regex, src, re.M)
    if not m:
        raise SystemExit(f'❌ 找不到 {what}')
    start = m.end()
    ends = [src.find(e, start) for e in end_lines]
    ends = [e for e in ends if e >= 0]
    if not ends:
        raise SystemExit(f'❌ {what} 没有结束标记 {end_lines!r}')
    return src[start:min(ends)]


def slugs_of_list(src, name):
    body = block(src, rf'^{name} = \[', ['\n]'], f'清单 {name}')
    pairs = re.findall(r"\((\d+), '([a-z0-9_]+)'\)", body)
    if not pairs:
        raise SystemExit(f'❌ 清单 {name} 解析不到任何 (id, slug)')
    return {s for _, s in pairs}


def handled_slugs(body):
    out = set(re.findall(r"slug == '([a-z0-9_]+)'", body))
    for group in re.findall(r"slug in \(([^)]*)\)", body):
        out |= set(re.findall(r"'([a-z0-9_]+)'", group))
    return out


def main():
    with open(GEN, encoding='utf-8') as f:
        src = f.read()

    fail = 0
    for list_name, fn_name, dict_name in TARGETS:
        want = slugs_of_list(src, list_name)
        body = block(src, rf'^def {fn_name}\(', ['\ndef ', '\n# ----'], f'函数 {fn_name}()')

        got = handled_slugs(body)
        if dict_name:
            dbody = block(src, rf'^{dict_name} = \{{', ['\n}'], f'字典 {dict_name}')
            got |= set(re.findall(r"'([a-z0-9_]+)':", dbody))

        missing = sorted(want - got)
        if missing:
            print(f'❌ {fn_name}() 未处理 {len(missing)} 个 slug：{missing}')
            print('   这些格子会静默返回原图 = 假功能。补分支，或从清单里删掉。')
            fail = 1
        else:
            print(f'✅ {fn_name}() 覆盖 {len(want)} 个 slug')

        extra = sorted(got - want)
        if extra:
            # 多出来的分支是死代码：不致命，但要提示（清单删了分支还留着）
            print(f'   ⚠ 有 {len(extra)} 个分支不在清单里（死代码）：{extra}')

    return fail


if __name__ == '__main__':
    sys.exit(main())
