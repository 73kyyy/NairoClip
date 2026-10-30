#!/usr/bin/env python3
"""检查 Kotlin 源文件的块注释完整性。

为什么需要：Kotlin 的块注释**可以嵌套**。
注释正文里出现 "/*" 会再开一层，只要之后不写等量的 "*/"，
从那里起到文件末尾的全部代码都会被当成注释内容 —— 而且**不报错**。

真实事故（NairoClip android/app/build.gradle.kts，2026-10-30）：

    第  87 行  /* 把 mobile/ 的前端资源同步进 Android assets。
    第  91 行    真机上必须用**页面版**：pages/*.html + assets/*.js，
    第  96 行      mobile/pages/*.html   → assets/pages/*.html

第 91/96 行注释正文里的 "/*" 各自开了一层嵌套注释。
结果第 99 行 `val syncMobileUi`、第 142 行 `dependencies {}`、
第 159 行 `tasks.register("printDeps")` 全部被当成注释，
从未执行。下游表现是：

    - Gradle 任务列表里看不到 syncMobileUi / printDeps
    - releaseRuntimeClasspath 里没有 appcompat
    - AAPT: error: resource style/Theme.AppCompat.NoActionBar not found

三处现象全部由此解释，与依赖、缓存、AGP 版本无关。

本脚本按 Kotlin 词法扫描：区分字符串与注释，
在注释内部检测到 "/*" 即记录行号并判定失败。

用法：
    python3 tools/check_kotlin_block_comments.py android/app/build.gradle.kts
退出码：0 正常，1 存在未闭合或嵌套注释。
"""

from __future__ import annotations

import sys


def scan(path: str) -> tuple[int, list[int]]:
    """返回 (最终注释深度, 注释内出现嵌套 /* 的行号列表)。"""
    with open(path, encoding="utf-8") as fh:
        src = fh.read()

    i = 0
    n = len(src)
    line = 1
    depth = 0
    nested_lines: list[int] = []

    while i < n:
        ch = src[i]

        if ch == "\n":
            line += 1
            i += 1
            continue

        # 行注释：仅在注释外生效
        if depth == 0 and src.startswith("//", i):
            j = src.find("\n", i)
            i = n if j < 0 else j
            continue

        # 块注释开始（仅在注释外）
        if depth == 0 and src.startswith("/*", i):
            depth = 1
            i += 2
            continue

        # 注释内部
        if depth > 0:
            if src.startswith("/*", i):
                nested_lines.append(line)
                depth += 1
                i += 2
                continue
            if src.startswith("*/", i):
                depth -= 1
                i += 2
                continue
            i += 1
            continue

        # 注释外：跳过字符串字面量，避免把 "META-INF/*.x" 误判为注释
        if ch == '"':
            j = i + 1
            while j < n:
                if src[j] == "\\":
                    j += 2
                    continue
                if src[j] == '"':
                    break
                j += 1
            i = j + 1
            continue

        i += 1

    return depth, nested_lines


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print("用法: check_kotlin_block_comments.py <file.kt> [...]")
        return 2

    failed = False
    for path in argv[1:]:
        depth, nested = scan(path)
        print(f"[{path}]")
        print(f"  块注释最终深度 = {depth} (必须为 0)")
        if nested:
            failed = True
            print(f"  注释内出现嵌套 /* 的行号: {nested}")
            with open(path, encoding="utf-8") as fh:
                lines = fh.read().split("\n")
            for ln in nested:
                text = lines[ln - 1].strip()[:110] if ln - 1 < len(lines) else ""
                print(f"    行 {ln}: {text}")
        if depth != 0:
            failed = True
            print("  FAIL: 有块注释未闭合，其后的代码会被当作注释内容")
        elif nested:
            failed = True
            print("  FAIL: 注释正文含 /*（Kotlin 会嵌套），请改写为 <name> 或 -*")
        else:
            print("  OK: 注释闭合，且注释正文内无 /*")

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
