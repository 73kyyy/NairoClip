#!/usr/bin/env python3
"""check_capi_calls.py 的自检（纯标准库，可在 CI 直接跑）。

背景：2026-11 一次真实 CI 失败，原因是 check_capi_calls.py 把
`/* ... nc_xxx() ... */` 注释里的函数名当成了调用点，误报
「传了 0 个参数」。修好剥注释逻辑后，必须同时证明：

  1. 注释里的引用不再被当成调用点（本次修复点）；
  2. 真正的参数个数不符**仍然**被抓住（不能因为修 1 而放过真错）；
  3. 报错行号在多行注释之后依然准确。

用法：
    python3 tools/test_check_capi_calls.py
退出码 0 = 全部通过。
"""

import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_capi_calls as C  # noqa: E402


class MaskCommentsTest(unittest.TestCase):
    """mask_comments：等长、保留换行、保留字符串。"""

    def test_line_comment_masked(self):
        src = 'int x; // nc_demo(1)\nint y;\n'
        m = C.mask_comments(src)
        self.assertEqual(len(m), len(src))
        self.assertNotIn('nc_demo', m)
        self.assertEqual(m.count('\n'), src.count('\n'))

    def test_block_comment_masked(self):
        src = '/*\n  nc_demo()\n*/\nint z;\n'
        m = C.mask_comments(src)
        self.assertEqual(len(m), len(src))
        self.assertNotIn('nc_demo', m)
        self.assertEqual(m.count('\n'), src.count('\n'))

    def test_single_line_block_comment_masked(self):
        src = 'int a; /* nc_demo() */ int b;\n'
        m = C.mask_comments(src)
        self.assertEqual(len(m), len(src))
        self.assertNotIn('nc_demo', m)

    def test_string_literal_preserved(self):
        src = 'f("a,b", 2);\n'
        self.assertEqual(C.mask_comments(src), src)

    def test_string_with_comment_marker_preserved(self):
        src = 'f("http://example/x");\n'
        self.assertEqual(C.mask_comments(src), src)

    def test_escaped_quote_in_string(self):
        src = 'f("a\\"//b");\n'
        self.assertEqual(C.mask_comments(src), src)


class ScanCallsTest(unittest.TestCase):
    """scan_calls：注释忽略 + 真错仍抓 + 行号准确。"""

    def _scan(self, body):
        fd, path = tempfile.mkstemp(suffix='.cpp', text=True)
        try:
            with os.fdopen(fd, 'w', encoding='utf-8') as f:
                f.write(body)
            return C.scan_calls(path, {'nc_demo': 3})
        finally:
            os.unlink(path)

    def test_comment_reference_ignored(self):
        """回归：注释里的 nc_demo() 不得报错（本次修复的真实场景）。"""
        self.assertEqual(self._scan('/*\n  nc_demo() 返回文本\n*/\nint a = 0;\n'), [])

    def test_line_comment_reference_ignored(self):
        self.assertEqual(self._scan('int a = 0; // 见 nc_demo()\n'), [])

    def test_real_arity_mismatch_caught(self):
        """真错必须仍被抓住：2 个实参 vs 3 个形参。"""
        errs = self._scan('void g() { nc_demo(1, 2); }\n')
        self.assertEqual(len(errs), 1)
        line, name, actual, expected = errs[0]
        self.assertEqual(line, 1)
        self.assertEqual(name, 'nc_demo')
        self.assertEqual(actual, 2)
        self.assertEqual(expected, 3)

    def test_correct_arity_ok(self):
        self.assertEqual(self._scan('void g() { nc_demo(1, 2, 3); }\n'), [])

    def test_zero_args_mismatch_caught(self):
        errs = self._scan('void g() { nc_demo(); }\n')
        self.assertEqual(len(errs), 1)
        self.assertEqual(errs[0][2], 0)

    def test_line_number_after_multiline_comment(self):
        body = '/*\n a\n b\n*/\nvoid g() { nc_demo(1); }\n'
        errs = self._scan(body)
        self.assertEqual(len(errs), 1)
        self.assertEqual(errs[0][0], 5)

    def test_string_arg_with_comma_counts_once(self):
        """字符串里的逗号不参与切分。"""
        self.assertEqual(self._scan('void g() { nc_demo("a,b", 2, 3); }\n'), [])


if __name__ == '__main__':
    unittest.main(verbosity=2)
