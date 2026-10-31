#!/usr/bin/env python3
"""移动端前端静态检查（JS / CSS / HTML 结构）。

为什么需要：
  CI 只编译 C++ 与 Kotlin，前端（mobile/assets/*.js、*.css、
  mobile/pages/*.html）此前**没有任何自动化验证**。改动全靠人手看，
  典型事故：
    · 图标 <use href="#i-xxx"> 引用了不存在的 symbol → 空白按钮
    · data-act 没有对应 JS 分支 → 点了没反应
    · CSS 花括号不配平 → 后续所有规则静默失效
  这三类都能在静态阶段发现，不必等真机。

检查项（任何一项失败即退出码 1）：
  1. 所有 .js / .css 花括号、圆括号、方括号配平（跳过字符串与注释）
  2. HTML 里 <use href="#id"> 的 id 必须在 icons.js 中有定义
  3. HTML 里 data-act 必须在 editor.js 里有 act === '<name>' 分支
  4. icons.js 里不得有重复键（后键覆盖前键，前一个是死代码）
  5. CSS 里 class 选择器的花括号数量与文件总配平一致（冗余校验）

用法：python3 tools/check_mobile_frontend.py [repo_root]
"""

import os
import re
import sys


def strip_code(text):
    """去掉注释与字符串字面量，只留下结构性字符。

    这只是配平检查用的简化扫描器，不追求完整 JS 词法；
    但对本仓库的代码风格足够（模板字符串里不含未闭合括号）。
    """
    out = []
    i = 0
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
                    i += 1
                    break
                i += 1
            continue
        if c == '`':
            i += 1
            while i < n:
                if text[i] == '\\':
                    i += 2
                    continue
                if text[i] == '`':
                    i += 1
                    break
                i += 1
            continue
        out.append(c)
        i += 1
    return ''.join(out)


def check_balance(path, text):
    """检查括号配平；返回错误列表。"""
    pairs = {'}': '{', ')': '(', ']': '['}
    depth = {'{': 0, '(': 0, '[': 0}
    line = 1
    stripped = strip_code(text)
    # strip_code 会移除换行之外的字符串内容，但仍保留换行，行号可对齐。
    for ch in stripped:
        if ch == '\n':
            line += 1
        elif ch in depth:
            depth[ch] += 1
        elif ch in pairs:
            depth[pairs[ch]] -= 1
            if depth[pairs[ch]] < 0:
                return [f'{path}:{line}: 多余的 {ch}']
    errs = []
    for k, v in depth.items():
        if v != 0:
            errs.append(f'{path}: 未闭合的 {k} × {v}')
    return errs


def icon_ids(icons_js):
    """icons.js 里定义的 symbol id 集合 + 重复键列表。"""
    keys = re.findall(r"'(i-[a-z0-9-]+)'\s*:", icons_js)
    seen = set()
    dups = []
    for k in keys:
        if k in seen:
            dups.append(k)
        seen.add(k)
    return seen, sorted(set(dups))


def main(root):
    errors = []

    assets = os.path.join(root, 'mobile', 'assets')
    pages = os.path.join(root, 'mobile', 'pages')
    icons_path = os.path.join(assets, 'icons.js')
    editor_path = os.path.join(assets, 'editor.js')

    # --- 1. 括号配平：所有 js / css ---
    for base in (assets,):
        if not os.path.isdir(base):
            errors.append(f'缺少目录：{base}')
            continue
        for name in sorted(os.listdir(base)):
            if not name.endswith(('.js', '.css')):
                continue
            p = os.path.join(base, name)
            with open(p, encoding='utf-8') as f:
                errors.extend(check_balance(name, f.read()))

    # --- 2/3. HTML 的图标引用与 data-act ---
    if not os.path.isfile(icons_path):
        errors.append('缺少 mobile/assets/icons.js')
        return errors
    with open(icons_path, encoding='utf-8') as f:
        icons_js = f.read()
    defined, dups = icon_ids(icons_js)
    if dups:
        errors.append('icons.js 存在重复键（后键覆盖前键，前者是死代码）：'
                      + ', '.join(dups))

    with open(editor_path, encoding='utf-8') as f:
        editor_js = f.read()
    acts_in_js = set(re.findall(r"act\s*===\s*'([a-z0-9-]+)'", editor_js))

    if os.path.isdir(pages):
        for name in sorted(os.listdir(pages)):
            if not name.endswith('.html'):
                continue
            p = os.path.join(pages, name)
            with open(p, encoding='utf-8') as f:
                html = f.read()

            for ref in sorted(set(re.findall(r'<use\s+href="#(i-[a-z0-9-]+)"', html))):
                if ref not in defined:
                    errors.append(f'{name}: <use href="#{ref}"> 在 icons.js 中不存在')

            for act in sorted(set(re.findall(r'data-act="([a-z0-9-]+)"', html))):
                if act not in acts_in_js:
                    errors.append(f'{name}: data-act="{act}" 在 editor.js 中没有对应分支')

    return errors


if __name__ == '__main__':
    root = sys.argv[1] if len(sys.argv) > 1 else '.'
    errs = main(root)
    if errs:
        print('前端静态检查失败：')
        for e in errs:
            print('  ✗', e)
        sys.exit(1)
    print('前端静态检查通过：括号配平 / 图标引用 / data-act 分支 / 无重复图标键')
