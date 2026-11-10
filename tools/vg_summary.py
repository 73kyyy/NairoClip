#!/usr/bin/env python3
"""Valgrind 分片结果汇总（NC-B1-FOLLOWUP）。

为什么需要独立脚本而不是内联在 workflow 里：
  1. 在 YAML 块标量里嵌 heredoc，缩进稍有偏差就会把整个 workflow 语法搞坏
     （已踩过一次）；
  2. 解析逻辑需要可本地单测 —— 否则「汇总对不对」只能靠肉眼看 CI 日志；
  3. 覆盖度校验是验收的关键证据，必须能被独立复核。

两个子命令：

  coverage --dir <目录> --shards <N>
      读各分片 stdout 里的 [SHARD_STATS]，校验：
        · 分片齐全（0..N-1 都有输出）
        · 执行数之和 == 用例总数（覆盖度）
      任一不满足 → 退出码 1，并打印缺失分片。

  memory --dir <目录>
      解析 vg-shard-*.xml，统计：
        · 错误条目总数与按 kind 的分布
        · 泄漏分类（Definitely / Indirectly / Possibly / StillReachable）
      同时把摘要写入 /tmp/vg-summary.txt 供 step summary 引用。

注意：`--xml=yes` 会让 valgrind 的文本 `--log-file` 变成空文件，
所以**必须**解析 XML，不能 grep 文本日志（否则会误判"没有错误"）。
"""

import argparse
import collections
import glob
import os
import re
import sys
import xml.etree.ElementTree as ET

STATS_RE = re.compile(r"\[SHARD_STATS\]\s+shard=(\d+)\s+shards=(\d+)\s+executed=(\d+)\s+total=(\d+)\s+failed=(\d+)")


def coverage(directory: str, expected_shards: int) -> int:
    files = sorted(glob.glob(os.path.join(directory, "vg-stdout-*.txt")))
    per_shard = {}
    total_cases = 0
    total_failed = 0

    for path in files:
        try:
            with open(path, encoding="utf-8", errors="replace") as f:
                text = f.read()
        except OSError:
            continue
        matches = STATS_RE.findall(text)
        if not matches:
            continue
        shard, _shards, executed, total, failed = (int(x) for x in matches[-1])
        per_shard[shard] = (executed, total, failed)
        total_cases = total          # 每片打印的都是全量总数，取最后一个即可
        total_failed += failed

    print(f"分片输出文件数: {len(per_shard)} / 期望 {expected_shards}")
    for shard in sorted(per_shard):
        executed, total, failed = per_shard[shard]
        print(f"  分片 {shard:2d}: 执行 {executed:4d}/{total} | 失败 {failed}")

    missing = [i for i in range(expected_shards) if i not in per_shard]
    ok = True
    if missing:
        print(f"::error title=VG_MISSING_SHARDS::缺失分片 {missing}")
        print(f"缺失分片: {missing}")
        ok = False

    executed_sum = sum(v[0] for v in per_shard.values())
    print("---------------------------------------------------")
    print(f"全量用例总数: {total_cases}")
    print(f"实际执行合计: {executed_sum}")
    print(f"失败用例合计: {total_failed}")

    if total_cases <= 0:
        print("::error title=VG_COVERAGE::未能读到任何 SHARD_STATS")
        return 1
    if executed_sum != total_cases:
        print(f"::error title=VG_COVERAGE::执行数({executed_sum}) != 总数({total_cases})，存在遗漏")
        ok = False
    else:
        print("✅ 覆盖度校验通过：所有分片并集覆盖全量用例。")

    if total_failed > 0:
        print(f"::error title=VG_TEST_FAILED::有 {total_failed} 个用例失败")
        ok = False

    return 0 if ok else 1


def memory(directory: str) -> int:
    files = sorted(glob.glob(os.path.join(directory, "vg-shard-*.xml")))
    kinds = collections.Counter()
    parse_failed = []

    for path in files:
        try:
            root = ET.parse(path).getroot()
        except Exception:  # noqa: BLE001 —— 解析失败要记录，不能让汇总崩掉
            parse_failed.append(os.path.basename(path))
            continue
        for err in root.findall("error"):
            kinds[err.findtext("kind", "unknown")] += 1

    total = sum(kinds.values())
    print(f"XML 文件数: {len(files)}（解析失败 {len(parse_failed)}）")
    if parse_failed:
        print(f"  解析失败: {parse_failed}")
    print(f"内存错误条目总数: {total}")
    for kind, count in sorted(kinds.items()):
        print(f"  {kind}: {count}")

    leak_def = kinds.get("Leak_DefinitelyLost", 0)
    leak_ind = kinds.get("Leak_IndirectlyLost", 0)
    leak_pos = kinds.get("Leak_PossiblyLost", 0)
    leak_reach = kinds.get("Leak_StillReachable", 0)
    print(
        "泄漏分类: "
        f"definitely={leak_def} indirectly={leak_ind} possibly={leak_pos} "
        f"still_reachable={leak_reach}"
    )

    # 只有 Definitely / Indirectly 泄漏与真实内存错误判为失败；
    # Possibly 与 StillReachable 只记录（见报告的分类说明）。
    non_leak_errors = total - (leak_def + leak_ind + leak_pos + leak_reach)
    hard = non_leak_errors + leak_def + leak_ind
    if hard > 0:
        print(f"::error title=VG_ERRORS::检测到 {hard} 条需修复的内存问题")
    else:
        print("✅ 未检测到 Invalid read/write、UAF、double free、Definitely/Indirectly lost。")

    try:
        with open("/tmp/vg-summary.txt", "w", encoding="utf-8") as f:
            f.write(f"errors={total} files={len(files)} parse_fail={len(parse_failed)}\n")
            f.write(
                f"leak_definitely={leak_def} leak_indirectly={leak_ind} "
                f"leak_possibly={leak_pos} leak_still_reachable={leak_reach}\n"
            )
    except OSError:
        pass

    if parse_failed:
        print("::error title=VG_PARSE::存在无法解析的 XML，结论不完整")
        return 1
    return 1 if hard > 0 else 0


def main() -> int:
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)

    c = sub.add_parser("coverage")
    c.add_argument("--dir", required=True)
    c.add_argument("--shards", type=int, required=True)

    m = sub.add_parser("memory")
    m.add_argument("--dir", required=True)

    args = ap.parse_args()
    if args.cmd == "coverage":
        return coverage(args.dir, args.shards)
    return memory(args.dir)


if __name__ == "__main__":
    sys.exit(main())
