#!/usr/bin/env python3
"""把首页 / 我的 导出为单文件静态版（内嵌真实工程数据与缩略图）。"""
import base64, json, os, re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
IMG = os.path.join(ROOT, 'static', 'img')

def data_url(path):
    with open(path, 'rb') as f:
        return 'data:image/jpeg;base64,' + base64.b64encode(f.read()).decode()

# 真实工程快照
state = json.load(open('/tmp/state.json'))
projects = json.load(open('/tmp/projects.json'))
models = json.load(open('/tmp/models.json'))

# 草稿缩略图：用真实首帧；时长取工程总时长
# 首页不展示任何素材画面 → 不再内嵌缩略图（省 200KB+）
dur = (state.get('project', {}).get('duration_us') or 0) / 1e6
items = []
for p in []:  # 首页不展示草稿（需求：草稿区为空态）
    items.append({
        'name': p.get('name', '未命名草稿'),
        'file': p.get('file', ''),
        'bytes': p.get('bytes', 0),
        'dur': dur,
        'updated': '刚刚编辑'
    })

# 存储统计（模型接口给的是代理占用；其余按目录实际大小估算，标注来源）
st = models.get('storage', {})
storage = {
    'model_bytes': 0,
    'proxy_bytes': st.get('proxy_bytes', 0),
    'draft_bytes': sum(p.get('bytes', 0) for p in projects.get('projects', [])),
    'used_percent': 4,
    'note': '代理占用来自 nc_proxy_usage；草稿大小来自工程文件实际字节'
}

tokens = open(os.path.join(ROOT, 'assets', 'tokens.css'), encoding='utf-8').read()
# INLINE_FONT_FIX：内联后字体相对 static/*.html 解析
tokens = tokens.replace('url("fonts/', 'url("../assets/fonts/')
pages_css = open(os.path.join(ROOT, 'assets', 'pages.css'), encoding='utf-8').read()
home_css = open(os.path.join(ROOT, 'assets', 'home.css'), encoding='utf-8').read()
auth_css = open(os.path.join(ROOT, 'assets', 'auth.css'), encoding='utf-8').read()

# App 图标：单文件版不能引用外部 svg，转成 data URI 内联
_icon_path = os.path.join(ROOT, 'static', 'appicon', 'nmark.png')
APPICON_URI = ('data:image/png;base64,' +
               base64.b64encode(open(_icon_path, 'rb').read()).decode())
icons = open(os.path.join(ROOT, 'assets', 'icons.js'), encoding='utf-8').read()

def build(src_name, out_name, extra_js, css=None):
    page = open(os.path.join(ROOT, 'pages', src_name), encoding='utf-8').read()
    # body 起始标签可能带属性（如 data-page），用正则取 <body ...> 之后的内容
    mm = re.search(r'<body([^>]*)>(.*?)<script src=', page, re.S)
    if not mm:
        raise SystemExit('无法解析页面正文：' + src_name)
    body_attrs = mm.group(1).strip()      # 例如 data-page="auth"
    body = mm.group(2)
    js = open(os.path.join(ROOT, 'assets', extra_js), encoding='utf-8').read()
    # 把外链的 App 图标换成立即内联的 data URI
    body = body.replace('../static/appicon/nmark.svg', APPICON_URI)
    html = f"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>NairoClip</title>
<style>
{tokens}
{css if css is not None else pages_css}
</style>
</head>
<body {body_attrs}>
{body}
<script>
window.NC_STATIC_STATE = {json.dumps(state, ensure_ascii=False)};
window.NC_STATIC_PROJECTS = {json.dumps(items, ensure_ascii=False)};
window.NC_STATIC_MODELS = {json.dumps(storage, ensure_ascii=False)};
</script>
<script>
{icons}
</script>
<script>
{js}
</script>
</body>
</html>
"""
    out = os.path.join(ROOT, 'static', out_name)
    open(out, 'w', encoding='utf-8').write(html)
    return out, os.path.getsize(out)

a = build('01-home.html', 'nairoclip-home.html', 'home.js', css=home_css)
b = build('02-me.html', 'nairoclip-me.html', 'me.js')
_auth_css = pages_css + '\n' + auth_css
c  = build('03-auth.html',   'nairoclip-auth.html',   'auth.js', css=_auth_css)
c1 = build('03a-phone.html', 'nairoclip-phone.html',  'auth.js', css=_auth_css)
d = build('04-settings.html','nairoclip-settings.html','settings.js')
print('HOME_KB', round(a[1]/1024), 'ME_KB', round(b[1]/1024),
      'AUTH_KB', round(c[1]/1024), 'PHONE_KB', round(c1[1]/1024),
      'SET_KB', round(d[1]/1024))
print('DRAFTS', len(items), 'DUR', round(dur, 2))
