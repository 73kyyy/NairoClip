#!/usr/bin/env python3
"""Compare the runtime-registered C++ test inventory with the checked-in baseline."""
from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, help="built nairoclip_tests executable")
    parser.add_argument("--inventory", default="tools/test_inventory.json")
    parser.add_argument("--summary", help="full-suite stdout captured from the same executable")
    args = parser.parse_args()

    inventory_path = Path(args.inventory)
    inventory = json.loads(inventory_path.read_text(encoding="utf-8"))
    baseline = inventory.get("baseline_total")
    if not isinstance(baseline, int) or baseline <= 0:
        print("ERROR: test inventory baseline_total must be a positive integer", file=sys.stderr)
        return 2

    listed = subprocess.run(
        [args.binary, "--list"], text=True, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, check=False
    )
    if listed.returncode != 0:
        print(f"ERROR: test executable --list failed ({listed.returncode})", file=sys.stderr)
        print(listed.stderr, file=sys.stderr)
        return 2

    names = [
        line[len("[TEST] "):].strip()
        for line in listed.stdout.splitlines()
        if line.startswith("[TEST] ")
    ]
    if not names:
        print("ERROR: runtime test inventory is empty", file=sys.stderr)
        return 2
    duplicates = sorted({name for name in names if names.count(name) > 1})
    if duplicates:
        print("ERROR: duplicate registered test names: " + ", ".join(duplicates), file=sys.stderr)
        return 2

    actual = len(names)
    print(f"[TEST_INVENTORY] baseline={baseline} actual={actual}")
    if actual < baseline:
        print(f"ERROR: runtime test count decreased by {baseline - actual}", file=sys.stderr)
        return 1
    if actual > baseline:
        print(f"NOTICE: {actual - baseline} new test(s); advance baseline_total after review")

    if args.summary:
        summary = Path(args.summary).read_text(encoding="utf-8", errors="replace")
        match = re.search(
            r"^\[SHARD_STATS\] shard=0 shards=1 executed=(\d+) total=(\d+) failed=(\d+)$",
            summary, re.MULTILINE
        )
        if not match:
            print("ERROR: full-suite summary missing; run aborted or did not execute all tests", file=sys.stderr)
            return 1
        executed, reported_total, failed = map(int, match.groups())
        excluded = inventory.get("excluded_tests", [])
        excluded_names = [item.get("name") for item in excluded if isinstance(item, dict)]
        if len(excluded_names) != len(excluded):
            print("ERROR: malformed excluded_tests entry", file=sys.stderr)
            return 2
        stale = sorted(set(excluded_names) - set(names))
        if stale:
            print("ERROR: stale excluded test name(s): " + ", ".join(stale), file=sys.stderr)
            return 1
        expected_executed = actual - len(set(excluded_names))
        print(
            f"[TEST_RUN] executed={executed} registered={reported_total} "
            f"expected_executed={expected_executed} failed={failed}"
        )
        if reported_total != actual or executed != expected_executed or failed != 0:
            print("ERROR: executed test coverage does not match runtime inventory", file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
