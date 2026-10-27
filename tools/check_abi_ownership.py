#!/usr/bin/env python3
"""B0-4：检查 capi.h 中每个 `char**` / `uint8_t**` 出参都有所有权标注。

约定（见 capi.h 顶部「内存所有权约定」）：
  每个带 `**` 出参的声明，必须在**签名或上方连续注释**中出现以下关键词之一：
    需 nc_free_string / 需 nc_free_buffer / 库持有 / 勿释放

退出码：0 = 全部标注；1 = 有遗漏（打印明细）。
"""
import re
import sys

HEADER = "core/include/nairoclip/core/capi.h"
KEYWORDS = ("nc_free_string", "nc_free_buffer", "库持有", "勿释放")


def collect_decls(lines):
    """把可能跨行的函数声明聚成块，返回 (起始行号, 签名全文, 上方注释)。"""
    decls = []
    i = 0
    while i < len(lines):
        line = lines[i]
        if re.match(r'^[A-Za-z_][\w \*]*\bnc_\w+\(', line):
            start = i
            block = [line]
            while ');' not in block[-1] and i + 1 < len(lines):
                i += 1
                block.append(lines[i])
            j = start - 1
            doc = []
            while j >= 0 and lines[j].strip().startswith('//'):
                doc.insert(0, lines[j])
                j -= 1
            decls.append((start + 1, '\n'.join(block), '\n'.join(doc)))
        i += 1
    return decls


def main():
    with open(HEADER, encoding='utf-8') as fh:
        lines = fh.read().split('\n')

    missing = []
    checked = 0
    for lineno, sig, doc in collect_decls(lines):
        if '**' not in sig:
            continue
        checked += 1
        haystack = sig + '\n' + doc
        if not any(k in haystack for k in KEYWORDS):
            first = sig.split('\n')[0].strip()
            missing.append((lineno, first))

    print(f"检查 {HEADER}: {checked} 个带 ** 出参的声明")
    if missing:
        print(f"缺少所有权标注: {len(missing)} 处")
        for lineno, sig in missing:
            print(f"  L{lineno}: {sig}")
        return 1
    print("全部已标注 ✅")
    return 0


if __name__ == '__main__':
    sys.exit(main())
