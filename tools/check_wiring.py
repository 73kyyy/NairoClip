#!/usr/bin/env python3
"""接口连通性检查：C ABI → JNI/Kotlin → 前端 JS。

目的：抓「有实现但没人调用」和「有人调用但没实现」两类断点。

这不是单元测试，是**静态对账**：
  · core 里每个 nc_* 函数声明，是否有对应实现
  · JNI 里每个 Java_..._nativeXxx，Kotlin 是否有 external 声明
  · Kotlin 每个 external，native 侧是否有同名实现
  · 前端 nc-api.js 的函数清单，是否都存在于 C ABI

用法：python3 tools/check_wiring.py [repo_root]
退出码：0 = 全部对上；1 = 有断点
"""
import os
import re
import sys


def read(path):
    with open(path, encoding='utf-8', errors='ignore') as f:
        return f.read()


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else '.'
    problems = []
    notes = []

    # ---------- 1. C ABI：声明 vs 实现 ----------
    capi_h = os.path.join(root, 'core/include/nairoclip/core/capi.h')
    capi_cpp = os.path.join(root, 'core/src/capi.cpp')
    if not os.path.exists(capi_h):
        print('找不到 capi.h'); return 1
    h = read(capi_h)
    c = read(capi_cpp)

    # 声明：形如  int32_t nc_xxx(...);
    decls = set(re.findall(r'\b(nc_[a-z0-9_]+)\s*\(', h))
    impls = set(re.findall(r'^(?:[a-zA-Z_][\w:*\s]*?)\s+(nc_[a-z0-9_]+)\s*\(', c, re.M))
    missing_impl = sorted(decls - impls)
    if missing_impl:
        problems.append(('C ABI 声明但未实现', missing_impl))

    # ---------- 2. Kotlin external vs JNI 实现 ----------
    kt_path = os.path.join(root, 'mobile/native/android/NativeEngineBridge.kt')
    jni_path = os.path.join(root, 'mobile/native/android/nc_jni.cpp')
    if os.path.exists(kt_path) and os.path.exists(jni_path):
        kt = read(kt_path)
        jni = read(jni_path)

        externals = set(re.findall(r'external\s+fun\s+(native[A-Za-z0-9_]+)', kt))
        # JNI 实现名：Java_com_nairoclip_app_NativeEngineBridge_<name>
        jni_impls = set(re.findall(
            r'Java_com_nairoclip_app_NativeEngineBridge_([A-Za-z0-9_]+)', jni))

        no_jni = sorted(externals - jni_impls)
        if no_jni:
            problems.append(('Kotlin external 无 JNI 实现', no_jni))

        # JNI 有实现但 Kotlin 没声明（可能是历史残留）
        orphan_jni = sorted(jni_impls - externals)
        if orphan_jni:
            notes.append(('JNI 有实现但 Kotlin 未声明（可能残留）', orphan_jni))

    # ---------- 3. 前端 nc-api.js 清单 vs C ABI ----------
    api_path = os.path.join(root, 'mobile/assets/nc-api.js')
    if os.path.exists(api_path):
        api = read(api_path)
        listed = set(re.findall(r'"(nc_[a-z0-9_]+)"', api))
        unknown = sorted(listed - decls)
        if unknown:
            problems.append(('nc-api.js 列了 C ABI 里不存在的函数', unknown))

        # 反向：C ABI 有但前端没列（可能是正常 —— 有些只给原生用）
        not_listed = sorted(decls - listed)
        if not_listed:
            notes.append((f'C ABI 有但 nc-api.js 未列（{len(not_listed)} 个）',
                          not_listed[:12]))

    # ---------- 4. 编辑 op → C ABI 映射（最容易断的一环） ----------
    ed_path = os.path.join(root, 'mobile/assets/editor.js')
    if os.path.exists(ed_path):
        ed = read(ed_path)
        ops = set()
        for f in ('mobile/assets/editor.js', 'mobile/assets/l3-panel.js'):
            fp = os.path.join(root, f)
            if os.path.exists(fp):
                ops |= set(re.findall(r"op:\s*'([a-z_0-9]+)'", read(fp)))

        # 映射表的键：形如 「  xxx: {」
        mapped = set(re.findall(r'^\s{2}([a-z_0-9]+):\s*\{', ed, re.M))
        unmapped = sorted(ops - mapped)
        if unmapped:
            problems.append(('编辑 op 未映射到 C ABI（调用会失败）', unmapped))

        # 映射目标必须是真实存在的 C ABI 函数
        targets = set(re.findall(r"fn:\s*'([a-z_0-9]+)'", ed))
        bad_targets = sorted(targets - decls)
        if bad_targets:
            problems.append(('映射目标不是 C ABI 函数', bad_targets))

    # ---------- 输出 ----------
    if notes:
        print('=== 提示（不一定错）===')
        for title, items in notes:
            print(f'  · {title}:')
            for it in items[:12]:
                print(f'      {it}')
        print()

    if problems:
        print('=== ❌ 断点 ===')
        for title, items in problems:
            print(f'  · {title}:')
            for it in items[:20]:
                print(f'      {it}')
        print(f'\n共 {len(problems)} 类断点')
        return 1

    print('✅ 接口连通性检查通过（C ABI / JNI / 前端清单一致）')
    return 0


if __name__ == '__main__':
    sys.exit(main())
