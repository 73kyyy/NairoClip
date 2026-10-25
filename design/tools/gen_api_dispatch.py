#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""从 core/include/nairoclip/core/capi.h 自动生成「通用 API 直通」派发表。

目的：让设计桥 /api/call 能调用接口清单里全部 C ABI，而不必手写几百段代码。

约定（从 capi.h 归纳，已全量验证）：
  · 第 1 个参数恒为 NcEditor* / const NcEditor* → 用全局 gEditor，不在 JSON 里传
  · 参数名以 out_ 开头            → 出参，按名字写回 JSON 的 result
  · 指针 + const                  → 入参（字符串 / 数字数组）
  · 指针 + 非 const + 非 out_ 前缀 → 出参（少数几个，如 char** detail）
  · 其余                          → 入参标量，按名字从 args 读

输出 design/tools/api_dispatch.inc，由 nc_design_harness.cpp #include。

用法：
    python3 design/tools/gen_api_dispatch.py [--check]
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HEADER = os.path.join(ROOT, 'core', 'include', 'nairoclip', 'core', 'capi.h')
OUT = os.path.join(ROOT, 'design', 'tools', 'api_dispatch.inc')

# 生命周期由桥自己管，不开放给前端
SKIP = {'nc_editor_create', 'nc_editor_destroy', 'nc_free_string',
        'nc_free_string_array', 'nc_free_buffer'}

SCALAR = {
    'int32_t': ('static_cast<int32_t>(jint(args, "{n}", {d}))', '0'),
    'int64_t': ('static_cast<std::int64_t>(jint(args, "{n}", {d}))', '0'),
    'uint64_t': ('static_cast<std::uint64_t>(jint(args, "{n}", {d}))', '0'),
    'uint32_t': ('static_cast<std::uint32_t>(jint(args, "{n}", {d}))', '0'),
    'double': ('jnum(args, "{n}", {d})', '0.0'),
    'float': ('static_cast<float>(jnum(args, "{n}", {d}))', '0.0'),
}

OUT_SCALAR = {
    'int32_t': ('int32_t {v} = 0;', 'JsonValue::integer({v})'),
    'int64_t': ('int64_t {v} = 0;', 'JsonValue::integer({v})'),
    'uint64_t': ('std::uint64_t {v} = 0;',
                 'JsonValue::integer(static_cast<std::int64_t>({v}))'),
    'uint32_t': ('std::uint32_t {v} = 0;',
                 'JsonValue::integer(static_cast<std::int64_t>({v}))'),
    'double': ('double {v} = 0.0;', 'JsonValue::number({v})'),
    'float': ('float {v} = 0.0f;', 'JsonValue::number(static_cast<double>({v}))'),
}


def strip_comments(text):
    text = re.sub(r'//[^\n]*', '', text)
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    return text


def split_params(text):
    if not text.strip() or text.strip() == 'void':
        return []
    out, buf, depth = [], '', 0
    for ch in text:
        if ch == '(':
            depth += 1
        elif ch == ')':
            depth -= 1
        if ch == ',' and depth == 0:
            out.append(buf.strip())
            buf = ''
        else:
            buf += ch
    if buf.strip():
        out.append(buf.strip())
    return out


def parse_param(p):
    """→ (基类型, 变量名, 是否指针, 是否 const, 指针层数)"""
    is_const = re.search(r'\bconst\b', p) is not None
    stars = p.count('*')
    m = re.search(r'([A-Za-z_]\w*)\s*(\[[^\]]*\])?\s*$', p)
    name = m.group(1) if m else ''
    head = p[:m.start()] if m else p
    head = re.sub(r'\bconst\b', '', head).replace('*', '').strip()
    head = ' '.join(head.split())
    return head, name, stars > 0, is_const, stars


def parse_header():
    src = strip_comments(open(HEADER, encoding='utf-8').read())
    pat = re.compile(r'^\s*([A-Za-z_][\w\s\*]*?)\s*\b(nc_[a-z0-9_]+)\s*\(([^;{]*?)\)\s*;',
                     re.M | re.S)
    out = []
    for m in pat.finditer(src):
        ret = ' '.join(m.group(1).split())
        out.append((ret, m.group(2), split_params(' '.join(m.group(3).split()))))
    return out


def gen():
    fns = parse_header()
    L = []
    w = L.append
    w('// ===========================================================================')
    w('// 自动生成，请勿手工编辑：design/tools/gen_api_dispatch.py')
    w('// 来源：core/include/nairoclip/core/capi.h')
    w('// 用途：/api/call 通用派发，前端 {"fn":"nc_xxx","args":{...}} 调用任意 C ABI')
    w('// ===========================================================================')
    w('')
    w('struct ApiCallResult {')
    w('    int32_t code = NC_OK;')
    w('    JsonValue result = JsonValue::object();')
    w('};')
    w('')
    w('/// 通用 C ABI 派发；fn 不在表内返回 false。')
    w('static bool dispatchApi(const std::string& fn, const JsonValue& args,')
    w('                        ApiCallResult& out) {')
    w('    out.result = JsonValue::object();')

    done, skipped = [], []
    for ret, name, params in fns:
        if name in SKIP:
            continue
        args_ = []
        decls, writes = [], []
        ok = True
        has_editor = False
        for i, p in enumerate(params):
            base, pname, is_ptr, is_const, depth = parse_param(p)
            if i == 0 and base == 'NcEditor':
                has_editor = True
                continue
            if not is_ptr:
                if base in SCALAR:
                    expr, d = SCALAR[base]
                    args_.append(expr.format(n=pname, d=d))
                else:
                    ok = False
                    break
                continue
            # 指针
            if is_const:
                if depth != 1:
                    ok = False
                    break
                if base == 'char':
                    args_.append(f'jstr(args, "{pname}").c_str()')
                elif base == 'double':
                    args_.append(f'jvecNum(args, "{pname}").data()')
                elif base == 'float':
                    args_.append(f'jvecF32(args, "{pname}").data()')
                elif base == 'uint64_t':
                    args_.append(f'jvecU64(args, "{pname}").data()')
                elif base == 'int32_t':
                    args_.append(f'jvecI32(args, "{pname}").data()')
                elif base in ('int64_t', 'uint32_t'):
                    args_.append(f'jvecInt(args, "{pname}").data()')
                elif base == 'uint8_t':
                    args_.append(f'jvecByte(args, "{pname}").data()')
                else:
                    ok = False
                    break
                continue
            # 出参
            if depth == 1:
                if base == 'char':
                    decls.append(f'char* {pname} = nullptr;')
                    writes.append((pname, 'JsonValue::string(takeString({v}))'))
                    args_.append(f'&{pname}')
                elif base in OUT_SCALAR:
                    d, e = OUT_SCALAR[base]
                    decls.append(d.format(v=pname))
                    writes.append((pname, e))
                    args_.append(f'&{pname}')
                else:
                    ok = False
                    break
            elif depth == 2:
                if base == 'char':
                    # char** out_x：单个字符串出参
                    decls.append(f'char* {pname} = nullptr;')
                    writes.append((pname, 'JsonValue::string(takeString({v}))'))
                    args_.append(f'&{pname}')
                elif base == 'double':
                    decls.append(f'double* {pname} = nullptr;')
                    writes.append((pname, '__numarray__'))
                    args_.append(f'&{pname}')
                elif base == 'float':
                    decls.append(f'float* {pname} = nullptr;')
                    writes.append((pname, '__numarrayf__'))
                    args_.append(f'&{pname}')
                elif base == 'uint8_t':
                    decls.append(f'uint8_t* {pname} = nullptr;')
                    writes.append((pname, None))
                    args_.append(f'&{pname}')
                else:
                    ok = False
                    break
            elif depth == 3:
                if base == 'char':
                    decls.append(f'char** {pname} = nullptr;')
                    writes.append((pname, '__strarray__'))
                    args_.append(f'&{pname}')
                else:
                    ok = False
                    break
            else:
                ok = False
                break

        if not ok:
            skipped.append(name)
            continue
        done.append(name)

        lead = ['gEditor'] if has_editor else []
        call = f'{name}(' + ', '.join(lead + args_) + ')'
        w(f'    if (fn == "{name}") {{')
        for d in decls:
            w(f'        {d}')
        if ret == 'void':
            w(f'        {call};')
            w('        out.code = NC_OK;')
        elif ret == 'char*':
            w(f'        out.result.set("value", JsonValue::string(takeString({call})));')
            w('        out.code = NC_OK;')
        elif ret == 'const char*':
            w(f'        out.result.set("value", JsonValue::string(std::string({call})));')
            w('        out.code = NC_OK;')
        elif ret == 'NcEditor*':
            w(f'        out.result.set("value", JsonValue::boolean({call} != nullptr));')
            w('        out.code = NC_OK;')
        else:
            w(f'        const auto ret = {call};')
            if ret == 'int32_t':
                w('        out.code = ret;')
            else:
                w('        out.code = NC_OK;')
            w('        out.result.set("value", JsonValue::integer('
              'static_cast<std::int64_t>(ret)));')
        for pname, expr in writes:
            if expr is None:
                continue
            if expr == '__strarray__':
                w(f'        out.result.set("{pname}", '
                  f'jstrArrayN({pname}, static_cast<int32_t>(jint(args, "capacity", 64))));')
            elif expr == '__numarrayf__':
                w(f'        out.result.set("{pname}", '
                  f'jnumArrayF({pname}, static_cast<int32_t>(jint(args, "count", 0)), '
                  f'jint(args, "{pname.replace("out_", "")}_count", 0)));')
            elif expr == '__numarray__':
                w(f'        out.result.set("{pname}", '
                  f'jnumArray({pname}, static_cast<int32_t>(jint(args, "count", 0)), '
                  f'jint(args, "{pname.replace("out_", "")}_count", 0)));')
            else:
                w(f'        out.result.set("{pname}", {expr.format(v=pname)});')
        w('        return true;')
        w('    }')
    w('    return false;')
    w('}')
    return '\n'.join(L) + '\n', done, skipped


_inc = os.path.join(ROOT, 'design', 'tools', 'api_dispatch.inc')
MARK = '// ========== 以下为手写补充区（生成器不覆盖）=========='
if os.path.exists(_inc):
    _old = open(_inc, encoding='utf-8').read()
    MANUAL_TAIL = ('\n' + MARK + '\n' + _old.split(MARK, 1)[1]) if MARK in _old else '\n'
else:
    MANUAL_TAIL = '\n'


def main():
    text, done, skipped = gen()
    print(f'可派发 {len(done)} 个 / 跳过 {len(skipped)} 个 / 共 {len(done) + len(skipped)}')
    if skipped:
        print('跳过：', ', '.join(skipped))
    if '--check' not in sys.argv:
        open(OUT, 'w', encoding='utf-8').write(text)
        print('写入', OUT, len(text), '字节')
    else:
        print('(--check 未写文件)')


if __name__ == '__main__':
    main()
