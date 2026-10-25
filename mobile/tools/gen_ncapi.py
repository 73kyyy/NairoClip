# -*- coding: utf-8 -*-
"""生成 mobile/assets/nc-api.js：window.NC_API 全量包装 232 个 C ABI。
每个方法都是 async，返回 {ok, code, message, result}，直接对应 /api/call。"""
import json, re

import os
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))      # mobile/
REPO = os.path.dirname(ROOT)                                            # repo/
HDR = os.path.join(REPO, 'core', 'include', 'nairoclip', 'core', 'capi.h')

_src = open(HDR, encoding='utf-8').read()
_src = re.sub(r'//[^\n]*', '', _src)
_src = re.sub(r'/\*.*?\*/', '', _src, flags=re.S)
_pat = re.compile(r'^\s*([A-Za-z_][\w\s\*]*?)\s*\b(nc_[a-z0-9_]+)\s*\(([^;{]*?)\)\s*;',
                  re.M | re.S)
_seen = set()
fns = []
for m in _pat.finditer(_src):
    _name = m.group(2)
    if _name in _seen:
        continue          # capi.h 可能有重复声明，去重
    _seen.add(_name)
    fns.append({'ret': ' '.join(m.group(1).split()),
                'name': _name,
                'params': ' '.join(m.group(3).split())})
# 去掉生命周期/释放类（桥不开放）
SKIP = {'nc_editor_create','nc_editor_destroy','nc_free_string',
        'nc_free_string_array','nc_free_buffer'}

def split_params(p):
    if not p.strip() or p.strip()=='void': return []
    out=[];buf='';depth=0
    for ch in p:
        if ch=='(':depth+=1
        if ch==')':depth-=1
        if ch==',' and depth==0: out.append(buf.strip()); buf=''
        else: buf+=ch
    if buf.strip(): out.append(buf.strip())
    return out

def parse(p):
    is_const='const' in p; stars=p.count('*')
    m=re.search(r'([A-Za-z_]\w*)\s*(\[[^\]]*\])?\s*$', p)
    name=m.group(1) if m else ''
    head=re.sub(r'\bconst\b','',p[:m.start()] if m else p).replace('*','').strip()
    return ' '.join(head.split()), name, stars>0, is_const

lines=[]
L=lines.append
L('/* NairoClip Mobile · C ABI 全量直通（window.NC_API）')
L('   ---------------------------------------------------------------------------')
L('   本文件由 tools/gen_ncapi.py 自动生成，请勿手工编辑。')
L('   来源：core/include/nairoclip/core/capi.h（233 个声明，开放 228 个）')
L('')
L('   用法：')
L('     await NC_API.nc_track_count()')
L('     await NC_API.nc_set_clip_opacity({ track_index:0, clip_id:10, opacity:0.5 })')
L('   返回： { ok, code, message, result }')
L('     · ok   —— HTTP 层是否成功（函数已登记且桥正常）')
L('     · code —— C ABI 原始返回值。注意两种语义：')
L('                 状态码类：0=NC_OK，1..8=错误码')
L('                 数值类  ：直接是结果（如 nc_track_count 返回 3）')
L('               具体属于哪类见 docs/API 清单的注释；状态码类请判断 code===0。')
L('   ------------------------------------------------------------------------- */')
L("'use strict';")
L('')
L('(function () {')
L('  /**')
L('   * 只走平台注入的桥 window.NCEngine（Android JNI / iOS C interop / Qt 直调）。')
L('   * 依据 docs/UI_INTEGRATION.md §7：平台层不要用 HTTP。')
L('   * 桥是同步的，因此这里没有 fetch、没有 URL、没有端口探测。')
L('   */')
L('  function bridge() {')
L('    const b = window.NCEngine;')
L('    return (b && typeof b.call === "function") ? b : null;')
L('  }')
L('')
L('  function unavailable(fn, why) {')
L('    return { ok: false, code: -1, message: `${fn}：${why}`, result: {} };')
L('  }')
L('')
L('  function call(fn, args) {')
L('    const b = bridge();')
L('    if (!b) {')
L('      return unavailable(fn,')
L('        "当前环境没有引擎桥（Web/WASM 不支持 C ABI；原生端需注入 NCEngine）");')
L('    }')
L('    let raw;')
L('    try {')
L('      raw = b.call(fn, JSON.stringify(args || {}));')
L('    } catch (err) {')
L('      return unavailable(fn, `桥调用失败：${err.message}`);')
L('    }')
L('    if (raw === null || raw === undefined) return unavailable(fn, "桥无返回");')
L('    if (typeof raw === "object") return raw;')
L('    try { return JSON.parse(String(raw)); }')
L('    catch (err) { return unavailable(fn, `桥返回内容无法解析：${String(raw).slice(0, 60)}`); }')
L('  }')
L('')
L('  const NC_API = { call };')
L('')
count=0
names=[]
for f in fns:
    name=f['name']
    if name in SKIP: continue
    params=split_params(f['params'])
    # 第一个参数是 editor，不进 JSON
    argnames=[]
    lead=True
    for i,p in enumerate(params):
        base,pn,is_ptr,is_const=parse(p)
        if i==0 and base=='NcEditor':
            continue
        argnames.append(pn)
    names.append(name)
    if argnames:
        sig='{ '+', '.join(argnames)+' }'
        L(f'  /** {f["ret"]} {name}({", ".join(argnames)}) */')
        L(f'  NC_API.{name} = (args = {{}}) => call("{name}", args);')
    else:
        L(f'  /** {f["ret"]} {name}() */')
        L(f'  NC_API.{name} = () => call("{name}", {{}});')
    count+=1

L('')
L('  /* 接口名清单：便于 UI 侧自检「我调的函数是否存在」 */')
L(f'  NC_API.FUNCTIONS = {json.dumps(names, ensure_ascii=False)};')
L('')
L('  window.NC_API = NC_API;')
L('})();')

open(os.path.join(ROOT,'assets','nc-api.js'),'w',encoding='utf-8').write('\n'.join(lines)+'\n')
print('generated', count, 'methods,', len('\n'.join(lines)), 'chars')
