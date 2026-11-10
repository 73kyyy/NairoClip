#!/usr/bin/env python3
"""`tools/vg_summary.py` 的单元测试（NC-B1-FOLLOWUP）。

为什么必须测这个工具：
  它是「Valgrind 覆盖度」与「内存错误结论」的唯一判定者。
  如果它把缺失分片算成通过、或把 XML 解析失败当成 0 错误，
  整条诊断链就会给出**假的绿色**——这正是任务书禁止的。

用法：python3 tools/test_vg_summary.py
"""

import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "vg_summary.py")


def run_tool(*args):
    """直接调用脚本（模拟 CI 的调用方式，验证 CLI 契约）。"""
    p = subprocess.run(
        [sys.executable, TOOL, *args],
        capture_output=True,
        text=True,
        timeout=60,
    )
    return p.returncode, p.stdout + p.stderr


def stats_line(shard, shards, executed, total, failed):
    return f"[SHARD_STATS] shard={shard} shards={shards} executed={executed} total={total} failed={failed}\n"


CLEAN_XML = """<?xml version="1.0"?>
<valgrindoutput>
  <protocolversion>4</protocolversion>
  <protocoltool>memcheck</protocoltool>
  <errorcounts>
  </errorcounts>
</valgrindoutput>
"""

ERROR_XML = """<?xml version="1.0"?>
<valgrindoutput>
  <protocolversion>4</protocolversion>
  <protocoltool>memcheck</protocoltool>
  <error>
    <unique>0x1</unique>
    <kind>InvalidWrite</kind>
  </error>
  <error>
    <unique>0x2</unique>
    <kind>Leak_DefinitelyLost</kind>
  </error>
  <error>
    <unique>0x3</unique>
    <kind>Leak_StillReachable</kind>
  </error>
  <errorcounts>
  </errorcounts>
</valgrindoutput>
"""


class CoverageTest(unittest.TestCase):
    def test_full_coverage_passes(self):
        with tempfile.TemporaryDirectory() as d:
            for i in range(4):
                with open(os.path.join(d, f"vg-stdout-{i}.txt"), "w") as f:
                    f.write(stats_line(i, 4, 25, 100, 0))
            rc, out = run_tool("coverage", "--dir", d, "--shards", "4")
            self.assertEqual(rc, 0, out)
            self.assertIn("覆盖度校验通过", out)

    def test_missing_shard_fails(self):
        with tempfile.TemporaryDirectory() as d:
            for i in (0, 1, 3):  # 缺 2
                with open(os.path.join(d, f"vg-stdout-{i}.txt"), "w") as f:
                    f.write(stats_line(i, 4, 25, 100, 0))
            rc, out = run_tool("coverage", "--dir", d, "--shards", "4")
            self.assertEqual(rc, 1, out)
            self.assertIn("缺失分片", out)

    def test_partial_coverage_fails(self):
        """分片都在，但执行总数 < 总数 —— 必须失败（防"少跑当跑完"）。"""
        with tempfile.TemporaryDirectory() as d:
            for i in range(4):
                with open(os.path.join(d, f"vg-stdout-{i}.txt"), "w") as f:
                    f.write(stats_line(i, 4, 20, 100, 0))  # 合计 80 < 100
            rc, out = run_tool("coverage", "--dir", d, "--shards", "4")
            self.assertEqual(rc, 1, out)
            self.assertIn("存在遗漏", out)

    def test_failed_cases_fail(self):
        with tempfile.TemporaryDirectory() as d:
            for i in range(2):
                with open(os.path.join(d, f"vg-stdout-{i}.txt"), "w") as f:
                    f.write(stats_line(i, 2, 50, 100, 1 if i == 0 else 0))
            rc, out = run_tool("coverage", "--dir", d, "--shards", "2")
            self.assertEqual(rc, 1, out)
            self.assertIn("失败", out)

    def test_no_stats_at_all_fails(self):
        with tempfile.TemporaryDirectory() as d:
            with open(os.path.join(d, "vg-stdout-0.txt"), "w") as f:
                f.write("(程序在打印统计前就崩了)\n")
            rc, out = run_tool("coverage", "--dir", d, "--shards", "1")
            self.assertEqual(rc, 1, out)


class MemoryTest(unittest.TestCase):
    def test_clean_run_passes(self):
        with tempfile.TemporaryDirectory() as d:
            with open(os.path.join(d, "vg-shard-0.xml"), "w") as f:
                f.write(CLEAN_XML)
            rc, out = run_tool("memory", "--dir", d)
            self.assertEqual(rc, 0, out)
            self.assertIn("内存错误条目总数: 0", out)

    def test_invalid_write_detected(self):
        with tempfile.TemporaryDirectory() as d:
            with open(os.path.join(d, "vg-shard-0.xml"), "w") as f:
                f.write(ERROR_XML)
            rc, out = run_tool("memory", "--dir", d)
            self.assertEqual(rc, 1, out)
            self.assertIn("InvalidWrite", out)
            self.assertIn("需修复的内存问题", out)

    def test_leak_classification(self):
        with tempfile.TemporaryDirectory() as d:
            with open(os.path.join(d, "vg-shard-0.xml"), "w") as f:
                f.write(ERROR_XML)
            _, out = run_tool("memory", "--dir", d)
            self.assertIn("definitely=1", out)
            self.assertIn("still_reachable=1", out)

    def test_still_reachable_alone_passes(self):
        """只有 StillReachable 时不判失败（它不是泄漏）。"""
        xml = CLEAN_XML.replace(
            "<errorcounts>",
            "<error><unique>0x1</unique><kind>Leak_StillReachable</kind></error><errorcounts>",
        )
        with tempfile.TemporaryDirectory() as d:
            with open(os.path.join(d, "vg-shard-0.xml"), "w") as f:
                f.write(xml)
            rc, out = run_tool("memory", "--dir", d)
            self.assertEqual(rc, 0, out)
            self.assertIn("still_reachable=1", out)

    def test_truncated_xml_is_not_silent_zero(self):
        """截断的 XML（分片被取消）必须判失败，绝不能当成 0 错误。"""
        with tempfile.TemporaryDirectory() as d:
            with open(os.path.join(d, "vg-shard-0.xml"), "w") as f:
                f.write('<?xml version="1.0"?><valgrindoutput><pid>1</pid>')
            rc, out = run_tool("memory", "--dir", d)
            self.assertEqual(rc, 1, out)
            self.assertIn("解析失败", out)
            self.assertIn("结论不完整", out)


if __name__ == "__main__":
    unittest.main(verbosity=2)
