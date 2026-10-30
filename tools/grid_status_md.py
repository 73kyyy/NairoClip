#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""输出宫格缩略图现状的 Markdown，供 CI 写进 Job Summary。

为什么要单独一个脚本
--------------------
GitHub Actions 的 run: 里写 heredoc（python3 - <<'PY'）很容易踩坑：
YAML block scalar 会统一剥掉公共缩进，而 bash heredoc 要求终止符
PY 在行首且无前导空白。若这段 heredoc 还套在 { ... } >> file 分组里，
缩进一变就变成 "warning: here-document delimited by end-of-file" ——
命令照跑、结果照写，但「脚本」其实是把后续内容当正文，极难看出是错。

所以：逻辑放进本脚本，workflow 只做一行
  python3 tools/grid_status_md.py >> $GITHUB_STEP_SUMMARY

用法
----
  python3 tools/grid_status_md.py            打印 Markdown
  python3 tools/grid_status_md.py --check    同时做完整性检查（有问题退出 1）
"""
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GRID = os.path.join(ROOT, 'mobile', 'static', 'grid')
STATIC = os.path.join(ROOT, 'mobile', 'static')
KINDS = ('filter', 'effect', 'transition', 'mask')


def main(argv):
    do_check = '--check' in argv
    out = []

    map_path = os.path.join(GRID, 'map.json')
    if not os.path.isfile(map_path):
        out.append('## 宫格缩略图（仓库已提交产物）')
        out.append('')
        out.append('⚠️ `mobile/static/grid/map.json` 尚不存在。')
        out.append('')
        out.append('生成器已就绪，但还没有成功跑过 CI。'
                   '手动触发 **Gen Assets** 工作流即可产出。')
        print('\n'.join(out))
        return 0

    with open(map_path, encoding='utf-8') as f:
        mapping = json.load(f)
    jpgs = sorted(f for f in os.listdir(GRID) if f.endswith('.jpg'))

    out.append('## 宫格缩略图（仓库已提交产物）')
    out.append('')
    out.append('- `map.json`：**%d** 条' % len(mapping))
    out.append('- jpg 实际：**%d** 张' % len(jpgs))
    out.append('')
    out.append('| 类别 | 数量 |')
    out.append('|---|---|')
    for kind in KINDS:
        n = sum(1 for k in mapping if k.startswith(kind + '_'))
        out.append('| %s | %d |' % (kind, n))

    missing = [k for k, v in mapping.items()
               if not os.path.isfile(os.path.join(STATIC, v))]
    orphan = [f for f in jpgs if f[:-4] not in mapping]

    out.append('')
    problems = []
    if missing:
        problems.append('%d 条映射指向不存在的文件' % len(missing))
        out.append('❌ **%d 条映射指向不存在的文件**：`%s`'
                   % (len(missing), missing[:5]))
    if orphan:
        problems.append('%d 张图没有映射' % len(orphan))
        out.append('⚠️ **%d 张图没有映射（孤儿文件）**：`%s`'
                   % (len(orphan), orphan[:5]))
    if len(jpgs) != len(mapping):
        problems.append('jpg 数 %d != 映射条数 %d' % (len(jpgs), len(mapping)))
        out.append('❌ **数量不一致**：jpg %d vs 映射 %d'
                   % (len(jpgs), len(mapping)))
    if not problems:
        out.append('✅ 映射与文件一一对应')

    print('\n'.join(out))

    if do_check and problems:
        for p in problems:
            print('检查失败：%s' % p, file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
