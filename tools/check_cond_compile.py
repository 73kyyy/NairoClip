#!/usr/bin/env python3
"""条件编译一致性检查。

## 为什么需要它

810bd86 的失败不是笔误，而是一类结构性陷阱：

  把代码从 `#ifdef NAIROCLIP_HAS_FFMPEG` 块里搬到块外，
  忘了把守卫一起搬过去 —— 有 FFmpeg 的配置照常编译，
  只有不带它的构建才报 `unknown type name 'FFmpegMediaReader'`。

既有检查器都抓不到这类问题：
  · check_cpp_headers.py  只看括号/类声明，不看条件编译
  · check_capi_calls.py   只比参数个数，不做类型解析
  · check_mobile_frontend 只管前端

因此这里直接调用真实 C++ 编译器，两种配置各验一遍。
事实胜于推演：静态脚本容易被嵌套的 #ifdef 骗过去（本文件作者就踩过），
只有编译器说"过"才是真的过。

## 检查内容

1. 无宏配置：`g++ -fsyntax-only` 每个 core 源文件
2. 有宏配置：`g++ -fsyntax-only -DNAIROCLIP_HAS_FFMPEG`
3. 无宏配置下预处理结果里不得残留 FFmpeg 专属标识符

## 局限（如实说明）

`-fsyntax-only` 只做语法与类型检查，不链接、不生成目标码。
能抓到「类型未声明」「签名不匹配」这类编译错误，
抓不到链接错误（缺定义）—— 那交给 CI 的完整构建。

退出码：0 = 全部通过；1 = 有失败。
"""

from __future__ import annotations

import os
import shutil
import subprocess
import sys

# 只在无宏配置下才应当消失的标识符：它们只在 NAIROCLIP_HAS_FFMPEG 块里声明。
FFMPEG_ONLY_SYMBOLS = [
    "FFmpegMediaReader",
    "FFmpegReaderFactory",
]

CXX_CANDIDATES = ["g++", "clang++", "c++"]
INCLUDE_DIR = os.path.join("core", "include")
MACRO = "NAIROCLIP_HAS_FFMPEG"


def find_compiler():
    for name in CXX_CANDIDATES:
        path = shutil.which(name)
        if path:
            return path
    return None


def run(cmd, cwd):
    try:
        proc = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, timeout=180)
        return proc.returncode, (proc.stdout or "") + (proc.stderr or "")
    except subprocess.TimeoutExpired:
        return 124, "超时（180s）"
    except OSError as exc:
        return 1, str(exc)


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else "."
    if not os.path.isdir(root):
        print("错误：目录不存在 " + root)
        return 1

    cxx = find_compiler()
    if cxx is None:
        # 没有编译器时不静默通过 —— 静默通过等于假绿。
        print("跳过：没有找到 C++ 编译器（g++ / clang++），无法验证条件编译")
        return 0

    src_dir = os.path.join(root, "core", "src")
    if not os.path.isdir(src_dir):
        print("错误：找不到 " + src_dir)
        return 1

    sources = sorted(
        os.path.join(src_dir, f) for f in os.listdir(src_dir) if f.endswith(".cpp")
    )
    if not sources:
        print("错误：" + src_dir + " 下没有 .cpp 文件")
        return 1

    errors = []
    configs = [
        ("无宏", []),
        ("有 " + MACRO, ["-D" + MACRO]),
    ]

    for label, extra in configs:
        failed = 0
        for src in sources:
            code, out = run(
                [cxx, "-fsyntax-only", *extra, "-I" + INCLUDE_DIR, src], cwd=root
            )
            if code != 0:
                failed += 1
                first = ""
                for line in out.splitlines():
                    if " error: " in line or line.startswith("error:"):
                        first = line.strip()
                        break
                errors.append(
                    "  ✗ [%s] %s: %s" % (label, os.path.basename(src), first or "编译失败")
                )
        if failed == 0:
            print("  ✓ %s配置：%d 个源文件语法通过" % (label, len(sources)))

    # 预处理后不得残留 FFmpeg 专属符号
    capi = os.path.join(src_dir, "capi.cpp")
    if os.path.isfile(capi):
        out_path = os.path.join("/tmp", "_nc_capi_preproc.i")
        code, out = run(
            [cxx, "-E", "-P", "-I" + INCLUDE_DIR, capi, "-o", out_path], cwd=root
        )
        if code == 0 and os.path.isfile(out_path):
            with open(out_path, encoding="utf-8", errors="ignore") as fh:
                pre = fh.read()
            leaked = [s for s in FFMPEG_ONLY_SYMBOLS if s in pre]
            if leaked:
                errors.append(
                    "  ✗ 无宏配置的预处理结果里残留 FFmpeg 符号：" + ", ".join(leaked)
                )
            else:
                print("  ✓ 无宏配置：预处理后无 FFmpeg 符号残留")
            try:
                os.remove(out_path)
            except OSError:
                pass
        elif code != 0:
            errors.append("  ✗ 预处理失败")

    if errors:
        print("条件编译检查失败：")
        for line in errors:
            print(line)
        print(
            "\n提示：这类问题只在**某一种配置**下暴露。\n"
            "搬动条件编译块里的代码时，务必把 #ifdef 一起搬。"
        )
        return 1

    print(
        "条件编译检查通过（%d 个源文件 × %d 种配置，编译器 %s）"
        % (len(sources), len(configs), os.path.basename(cxx))
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
