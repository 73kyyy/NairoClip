#!/usr/bin/env python3
"""S1-2 · JNI op 契约静态检查工具。

用途：把「前端 OP_TO_ABI 映射」与「JNI DispatchEdit 分支」做静态对账，
在 CI / 本地即可发现三类契约缺陷（无需 NDK、无需运行）：

  1. 缺分支：OP_TO_ABI 的 fn 在 DispatchEdit 里没有对应 `op == "..."` 分支
     → 前端调用必落「未开放的操作」。
  2. 参数未读取：前端 map() 发送的键，JNI 分支从不读取
     → 参数被静默丢弃（如 at_us 被读成 time_us 的经典错位）。
  3. 空分支/假成功：分支体内没有任何 nc_* 调用
     → 返回成功但什么都没做。

设计取舍：
  · 只做**静态文本解析**，不理解运行期语义；因此对「前置变量」
    （track/clip 由 DispatchEdit 开头统一读取）与已知别名做白名单排除，
    避免误报。误报会让人关掉检查，比漏报更糟。
  · 任何一项不通过 → 退出码 1，便于接进 CI 门禁。

用法：
    python3 tools/check_op_contract.py [仓库根目录]
退出码：0 = 全部通过；1 = 存在契约问题；2 = 输入缺失/解析失败。
"""
import os
import re
import sys

# 由 DispatchEdit 开头统一读取的前置变量：分支内不再重复读取属正常。
PREAMBLE_KEYS = {"track", "track_index", "clip", "clip_id", "from_track"}

# 前端对这两个 op 做了旁路（不走 OP_TO_ABI.map），静态对账不适用：
#   · track_flag：editor.js resolveTrackFlag 直接发 nc_set_track_hidden/
#     _muted/_locked 三个全名 op，参数为 hidden/muted/locked。
#   · 这三个全名分支已在 S1-2 / SF-10 补齐，由单元测试覆盖。
SPECIAL_CASES = {"track_flag"}

# 已知合法别名组（同一 C ABI 被多个 op 使用 / 一对多映射），不算缺陷。
KNOWN_ALIASES = {
    "nc_set_frame_fit": ["frame_fit", "nc_set_frame_fit"],
    "nc_add_marker": ["marker", "marker_color"],
    "nc_set_track_view": ["set_track_view", "nc_set_track_view"],
}


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def parse_op_to_abi(js_text):
    """解析 mobile/assets/editor.js 的 OP_TO_ABI 表 → {op: {fn, keys}}。"""
    start = js_text.find("const OP_TO_ABI = {")
    if start < 0:
        raise ValueError("未找到 OP_TO_ABI 表")
    depth = 0
    i = start
    while i < len(js_text):
        if js_text[i] == "{":
            depth += 1
        elif js_text[i] == "}":
            depth -= 1
            if depth == 0:
                break
        i += 1
    table = js_text[start + len("const OP_TO_ABI = {"):i]

    out = {}
    pos = 0
    while pos < len(table):
        m = re.match(r"\s*([A-Za-z_]\w*)\s*:\s*\{", table[pos:])
        if not m:
            pos += 1
            continue
        name = m.group(1)
        pos += m.end()
        depth = 1
        body_start = pos
        while pos < len(table) and depth > 0:
            if table[pos] == "{":
                depth += 1
            elif table[pos] == "}":
                depth -= 1
            pos += 1
        body = table[body_start:pos - 1]
        fn = re.search(r"fn:\s*'([^']+)'", body)
        keys = set()
        mp = re.search(r"map:\s*\(a\)\s*=>\s*\(\{(.*)\}\)", body, re.S)
        if mp:
            # 只认以字母/下划线开头的标识符键；否则 `a.enabled ? 1 : 0`
            # 里的 "1" 会被误当成键名（曾导致假报）。
            keys = set(re.findall(r"(?<![\w.$])([A-Za-z_]\w*)\s*:", mp.group(1)))
        out[name] = {"fn": fn.group(1) if fn else None, "keys": keys}
    return out


def parse_jni_branches(cpp_text):
    """解析 DispatchEdit 的分支 → {op名: {"keys": set, "has_abi": bool}}。"""
    s = cpp_text.find("bool DispatchEdit(")
    if s < 0:
        raise ValueError("未找到 DispatchEdit")
    # 找到函数体结束（第一个顶格 '}'）
    e = cpp_text.find("\n}\n", s)
    body = cpp_text[s:e]

    branches = {}
    cur = None
    for line in body.split("\n"):
        st = line.strip()
        if st.startswith('if (op == "'):
            names = re.findall(r'op == "([^"]+)"', line)
            cur = {"names": names, "text": []}
            branches[names[0]] = cur
            for nm in names[1:]:
                branches[nm] = cur
        elif cur is not None:
            cur["text"].append(line)

    out = {}
    # 旧式：JInt(a, "k", def)；新式（S1-2）：JIntParam(a,"k",&v) /
    # JIntParamAny(a, {"k1","k2"}, &v) / JBoolParam(a,"k",&b)
    key_re_single = re.compile(
        r'J(?:Int|Num|Str|Bool|StrField|VecDouble|VecU64|IntParam|NumParam|BoolParam)\('
        r'\s*a\s*,\s*"([^"]+)"')
    key_re_any = re.compile(
        r'J(?:Int|Num)ParamAny\(\s*a\s*,\s*\{([^}]*)\}')
    for name, b in branches.items():
        text = "\n".join(b["text"])
        keys = set(key_re_single.findall(text))
        for grp in key_re_any.findall(text):
            keys.update(re.findall(r'"([^"]+)"', grp))
        if '"args"' in text:
            keys.add("args")
        if '"points"' in text:
            keys.add("points")
        if "kNamed" in text:
            # 具名参数表：static const NamedArg kNamed[9] = {{"value",0.0},...}
            for grp in re.findall(r'\{\s*"([A-Za-z_]\w*)"\s*,', text):
                keys.add(grp)
        abi = re.findall(r"\b(nc_[a-z_0-9]+)\s*\(", text)
        out[name] = {"keys": keys, "has_abi": bool(abi), "abi": abi[0] if abi else None}
    return out


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else "."
    js_path = os.path.join(root, "mobile/assets/editor.js")
    jni_path = os.path.join(root, "mobile/native/android/nc_jni.cpp")
    for p in (js_path, jni_path):
        if not os.path.isfile(p):
            print(f"[ERR] 找不到输入文件：{p}")
            return 2
    try:
        ops = parse_op_to_abi(read(js_path))
        branches = parse_jni_branches(read(jni_path))
    except ValueError as exc:
        print(f"[ERR] 解析失败：{exc}")
        return 2

    problems = []

    # 检查 1 + 2：每个映射项都要有分支（短名或全名），且参数被读取
    for op, spec in sorted(ops.items()):
        fn = spec["fn"]
        br = branches.get(fn) or branches.get(op)
        if br is None:
            problems.append(
                f"[缺分支] op={op} fn={fn} —— DispatchEdit 无对应分支"
                f"（前端调用会落「未开放的操作」）")
            continue
        if op in SPECIAL_CASES:
            continue
        sent = {k for k in spec["keys"] if k not in PREAMBLE_KEYS}
        unread = sent - br["keys"]
        if unread:
            problems.append(
                f"[参数未读取] op={op} fn={fn} —— 前端发送但 JNI 从不读取："
                f"{sorted(unread)}（JNI 实际读取：{sorted(br['keys'])}）")

    # 检查 3：空分支 / 假成功
    for name, br in sorted(branches.items()):
        if not br["has_abi"]:
            problems.append(
                f"[空分支] op={name} —— 分支体内没有任何 nc_* 调用（假成功）")

    # 检查 4：别名一致性（同一 fn 被多个 op 使用 → 必须是已知别名）
    by_fn = {}
    for op, spec in ops.items():
        if spec["fn"]:
            by_fn.setdefault(spec["fn"], []).append(op)
    for fn, users in sorted(by_fn.items()):
        if len(users) > 1:
            allowed = KNOWN_ALIASES.get(fn)
            if allowed is None or sorted(allowed) != sorted(users):
                problems.append(
                    f"[别名未登记] fn={fn} 被多个 op 共用 {sorted(users)}，"
                    f"但不在 KNOWN_ALIASES 白名单中（请确认是别名还是重复定义）")

    # 基线：把「已确认但不在本批修复范围」的既有问题登记下来，
    # 避免它们让门禁长期变红（红久了就没人看，等于没有门禁）。
    baseline_path = os.path.join(root, "tools/op_contract_baseline.txt")
    baseline = set()
    if os.path.isfile(baseline_path):
        with open(baseline_path, encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if line and not line.startswith("#"):
                    baseline.add(line)

    new_problems = []
    baselined = []
    for p_ in problems:
        kind = p_.split("]", 1)[0].lstrip("[").strip()   # 去掉方括号
        opname = p_.split("op=")[1].split(" ")[0] if "op=" in p_ else ""
        tag = kind + " " + opname
        if tag in baseline:
            baselined.append(p_)
        else:
            new_problems.append(p_)

    print(f"OP_TO_ABI 条目：{len(ops)}")
    print(f"DispatchEdit 分支：{len(branches)}")
    if baselined:
        print(f"\n已登记基线（不在本批修复范围）：{len(baselined)} 项")
        for p_ in baselined:
            print("  · " + p_)
    if new_problems:
        print(f"\n❌ 新增契约问题 {len(new_problems)} 个：")
        for p_ in new_problems:
            print("  " + p_)
        return 1
    print("\n✅ 契约检查通过：无**新增**缺分支/参数未读取/空分支/未登记别名。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
