#!/usr/bin/env python3
"""检查 C ABI 调用点的参数个数是否与 capi.h 声明一致。

为什么需要（真实事故，已发生 3 次）：
  给某个 nc_* 函数加参数后，只改了声明和部分调用点，
  剩下的调用点仍用旧参数个数。要等 CI 编译十几分钟才暴露：
    · nc_effect_count(ed, ti)               -> 少 1 个参数
    · nc_effect_get(... 3 个出参)            -> 少 1 个参数
  这些在链接期（尤其 JNI / design harness）才报错，
  而 Android Cross-Compile 不编译这些目标，所以「绿」也可能漏。

做法：
  1. 从 capi.h 解析所有 nc_xxx(...) 声明的形参个数
  2. 扫描 tests/ design/ mobile/native/ core/src 下的调用点
  3. 比对个数，不一致即报错（含文件:行号与两边的个数）

局限（有意为之）：
  · 只比个数，不比类型（类型错误编译器会立刻报）
  · 实参含无法静态切分的构造时按顶层逗号处理，不做过度推断
"""

import os
import re
import sys

DECL_RE = re.compile(
    r'\b(?:int32_t|int64_t|uint64_t|uint32_t|void|char\s*\*|double)\s+'
    r'(nc_[a-z0-9_]+)\s*\(')


def split_top_level(text):
    """按顶层逗号切分参数列表，跳过 () [] {} 与字符串内逗号。"""
    parts = []
    depth = 0
    cur = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c in '([{':
            depth += 1
            cur.append(c)
        elif c in ')]}':
            depth -= 1
            cur.append(c)
        elif c in '"\'':
            q = c
            cur.append(c)
            i += 1
            while i < n:
                if text[i] == '\\':
                    cur.append(text[i])
                    if i + 1 < n:
                        cur.append(text[i + 1])
                    i += 2
                    continue
                cur.append(text[i])
                if text[i] == q:
                    break
                i += 1
        elif c == ',' and depth == 0:
            parts.append(''.join(cur).strip())
            cur = []
        else:
            cur.append(c)
        i += 1
    tail = ''.join(cur).strip()
    if tail:
        parts.append(tail)
    return parts


def find_matching_paren(text, open_idx):
    """从 '(' 开始找配对的 ')'，跳过字符串与注释。返回下标或 -1。"""
    depth = 0
    i = open_idx
    n = len(text)
    while i < n:
        c = text[i]
        if c == '/' and i + 1 < n and text[i + 1] == '/':
            while i < n and text[i] != '\n':
                i += 1
            continue
        if c == '/' and i + 1 < n and text[i + 1] == '*':
            i += 2
            while i + 1 < n and not (text[i] == '*' and text[i + 1] == '/'):
                i += 1
            i += 2
            continue
        if c in '"\'':
            q = c
            i += 1
            while i < n:
                if text[i] == '\\':
                    i += 2
                    continue
                if text[i] == q:
                    break
                i += 1
            i += 1
            continue
        if c == '(':
            depth += 1
        elif c == ')':
            depth -= 1
            if depth == 0:
                return i
        i += 1
    return -1


def parse_declarations(header_path):
    """返回 {函数名: 形参个数}。"""
    with open(header_path, encoding='utf-8', errors='replace') as f:
        text = f.read()
    decls = {}
    for m in DECL_RE.finditer(text):
        name = m.group(1)
        open_idx = m.end() - 1
        close_idx = find_matching_paren(text, open_idx)
        if close_idx < 0:
            continue
        inner = text[open_idx + 1:close_idx].strip()
        if not inner or inner == 'void':
            decls[name] = 0
        else:
            decls[name] = len(split_top_level(inner))
    return decls


def count_call_args(text, m):
    """给出某个函数名匹配后的实参个数；无法判定时返回 None。"""
    open_idx = m.end() - 1
    close_idx = find_matching_paren(text, open_idx)
    if close_idx < 0:
        return None
    inner = text[open_idx + 1:close_idx].strip()
    if not inner or inner == 'void':
        return 0
    return len(split_top_level(inner))


def mask_comments(text):
    """把注释替换为等长空白，保留换行与字符串字面量。

    为什么需要（真实事故，2026-11 已发生）：
      scan_calls 用 re.finditer 扫**原始文本**，而文档注释里经常出现
      `nc_xxx()` 这样的引用（如 capi.cpp 的超燃预设说明块）。不剥注释
      就会把它当成「0 个参数的调用点」误报，CI 在这一步失败，
      但真实代码完全正确 —— 排查成本极高，因为报错指向注释行。

    只遮注释、不遮字符串：
      · 字符串里可能含逗号/括号，遮掉会破坏实参切分；
      · 字符串里的 `nc_xxx(` 极罕见，且 find_matching_paren 本身会跳过字符串。
    换行原样保留，保证报错行号仍指向真实行。
    """
    out = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        # 行注释：遮到行尾，保留换行符本身
        if c == '/' and i + 1 < n and text[i + 1] == '/':
            while i < n and text[i] != '\n':
                out.append(' ')
                i += 1
            continue
        # 块注释：逐字符遮成空白，换行保留
        if c == '/' and i + 1 < n and text[i + 1] == '*':
            while i < n and not (text[i] == '*' and i + 1 < n and text[i + 1] == '/'):
                out.append('\n' if text[i] == '\n' else ' ')
                i += 1
            # 吃掉结尾的 */（两个字符 → 两个空白）
            if i < n:
                out.append(' ')
                i += 1
            if i < n:
                out.append(' ')
                i += 1
            continue
        # 字符串/字符字面量：原样保留（含转义序列）
        if c in '"\'':
            q = c
            out.append(c)
            i += 1
            while i < n:
                if text[i] == '\\' and i + 1 < n:
                    out.append(text[i])
                    out.append(text[i + 1])
                    i += 2
                    continue
                out.append(text[i])
                if text[i] == q:
                    i += 1
                    break
                if text[i] == '\n':
                    # 未闭合字面量（异常代码）：不要吞掉后续内容
                    i += 1
                    break
                i += 1
            continue
        out.append(c)
        i += 1
    return ''.join(out)


def scan_calls(path, known):
    with open(path, encoding='utf-8', errors='replace') as f:
        text = f.read()
    # 关键：在「注释已替换为等长空白」的副本上匹配调用点。
    # 副本长度与原文一致，因此行号可直接用原文换算。
    masked = mask_comments(text)
    errs = []
    for name, expected in known.items():
        for m in re.finditer(r'\b' + re.escape(name) + r'\s*\(', masked):
            actual = count_call_args(masked, m)
            if actual is None:
                continue
            if actual != expected:
                line = text.count('\n', 0, m.start()) + 1
                errs.append((line, name, actual, expected))
    return errs


def main(root):
    header = os.path.join(root, 'core', 'include', 'nairoclip', 'core', 'capi.h')
    if not os.path.isfile(header):
        return [f'缺少 {header}'], 0
    known = parse_declarations(header)

    scan_roots = [
        os.path.join(root, 'tests'),
        os.path.join(root, 'design'),
        os.path.join(root, 'mobile', 'native'),
        os.path.join(root, 'core', 'src'),
    ]
    files = []
    for base in scan_roots:
        if not os.path.isdir(base):
            continue
        for dirpath, _d, fnames in os.walk(base):
            for fn in fnames:
                # .inc 是被 include 的代码片段（如 api_dispatch.inc），同样可能含 C ABI 调用；
                # .h 里的 inline/模板函数也会调用，必须一并扫描。
                if fn.endswith(('.cpp', '.cc', '.c', '.h', '.hpp', '.inc')):
                    files.append(os.path.join(dirpath, fn))

    errs = []
    for p in files:
        rel = os.path.relpath(p, root)
        for line, name, actual, expected in scan_calls(p, known):
            errs.append(
                f'{rel}:{line}: {name} 传了 {actual} 个参数，声明是 {expected} 个')
    return errs, len(files)


if __name__ == '__main__':
    root = sys.argv[1] if len(sys.argv) > 1 else '.'
    errs, count = main(root)
    if errs:
        print('C ABI 调用点检查失败：')
        for e in errs:
            print('  ✗', e)
        sys.exit(1)
    print(f'C ABI 调用点检查通过（扫描 {count} 个源文件 + capi.h 声明）')
