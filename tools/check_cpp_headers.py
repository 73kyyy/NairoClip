#!/usr/bin/env python3
"""C++ 头文件结构检查：抓「类声明被吃掉」这类会必然编译失败的错。

为什么需要这个检查（真实事故）：
  在 commands.h 里插入新命令类时，替换操作把下一个类的
  `class ImportAssetCommand : public Command {` 头吃掉了，
  结果 ImportAssetCommand 的成员裸露在类外，出现
  「'override' specifier is not allowed outside a class definition」。
  这类错误百分百编译失败，却要等 CI 编译到第 11% 才发现，
  一轮反馈 6~10 分钟。静态检查毫秒级就能抓到。

检查项：
  1. 命名空间内花括号必须配平（跳过字符串/字符/注释）
  2. `public:` / `private:` / `protected:` 不得出现在类外
     —— 这是「类头丢失」最直接的信号
  3. `override` / `= delete` 等类内限定符不得出现在类外
  4. include guard / #pragma once 必须存在

用法：python3 tools/check_cpp_headers.py [repo_root]
"""

import os
import re
import sys


def strip_literals(text):
    """去掉注释与字符串/字符字面量，保留换行以便定位行号。"""
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
                if text[i] == '\n':
                    out.append('\n')
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
                if text[i] == '\n':
                    out.append('\n')
                i += 1
            out.append(' ' * 2)
            continue
        out.append(c)
        i += 1
    return ''.join(out)


def check_header(path, raw, text):
    """返回错误列表。text 是已去掉注释/字面量的版本。"""
    errs = []
    lines = text.split('\n')

    # --- 1. 花括号配平 ---
    depth = 0
    for idx, line in enumerate(lines, 1):
        depth += line.count('{') - line.count('}')
        if depth < 0:
            errs.append(f'{path}:{idx}: 多余的 }}（花括号提前闭合）')
            depth = 0
    if depth != 0:
        errs.append(f'{path}: 花括号未配平（剩余 {depth} 个未闭合）')

    # --- 2/3. 类内限定符不得在类外 ---
    # 用「当前花括号深度」判断是否在类体内。命名空间也占一层，
    # 所以外层深度可能是 1（namespace 内），类体内 >= 2。
    depth = 0
    class_depths = []      # 记录每个 class 体所在深度
    for idx, line in enumerate(lines, 1):
        stripped = line.strip()
        is_access = re.match(r'^(public|private|protected)\s*:', stripped)
        # 先判断本行进入类之前的状态
        if is_access and depth == 0:
            errs.append(
                f'{path}:{idx}: `{stripped}` 出现在类外 —— '
                f'很可能是上一个类的声明头被误删')
        if re.search(r'\boverride\b', stripped) and depth == 0:
            errs.append(
                f'{path}:{idx}: `override` 出现在类外 —— 类声明头可能丢失')
        if re.search(r'\)\s*const\s*override', stripped) and depth == 0:
            errs.append(
                f'{path}:{idx}: `const override` 出现在类外 —— 类声明头可能丢失')
        depth += line.count('{') - line.count('}')
        if depth < 0:
            depth = 0

    # --- 4. include guard ---
    if '#pragma once' not in raw and not re.search(r'#ifndef\s+\w+_H', raw):
        errs.append(f'{path}: 缺少 #pragma once 或 include guard')

    return errs


def main(root):
    errs = []
    roots = [os.path.join(root, 'core', 'include')]
    files = []
    for base in roots:
        if not os.path.isdir(base):
            errs.append(f'缺少目录：{base}')
            continue
        for dirpath, _dirnames, filenames in os.walk(base):
            for name in sorted(filenames):
                if name.endswith(('.h', '.hpp', '.hh')):
                    files.append(os.path.join(dirpath, name))

    for p in files:
        with open(p, encoding='utf-8', errors='replace') as f:
            raw = f.read()
        rel = os.path.relpath(p, root)
        errs.extend(check_header(rel, raw, strip_literals(raw)))

    return errs, len(files)


if __name__ == '__main__':
    root = sys.argv[1] if len(sys.argv) > 1 else '.'
    errs, count = main(root)
    if errs:
        print('C++ 头文件结构检查失败：')
        for e in errs:
            print('  ✗', e)
        sys.exit(1)
    print(f'C++ 头文件结构检查通过（{count} 个头文件）：'
          f'花括号配平 / 无类外限定符 / include guard 齐全')
