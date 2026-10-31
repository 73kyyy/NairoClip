#!/usr/bin/env python3
"""把 engine-driven 的剪辑页导出为单文件静态版（可双击打开，无需服务端）。

真实数据来源：
  /api/state     → 轨道/片段/字幕/标记（C ABI）
  /api/frame     → nc_preview_render 真实解码帧
  /api/thumb     → nc_thumbnail_generate 真实缩略图
  /api/waveform  → nc_waveform_generate 真实波形桶
内嵌为 base64，运行时零网络请求。
"""
import base64, json, os, re, mimetypes

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
IMG = os.path.join(ROOT, 'static', 'img')

def b64(path):
    ext = os.path.splitext(path)[1].lower()
    mime = {'.jpg': 'image/jpeg', '.png': 'image/png', '.json': 'application/json'}[ext]
    with open(path, 'rb') as f:
        return f'data:{mime};base64,' + base64.b64encode(f.read()).decode()

# ---- 帧表 / 缩略图 / 波形 ----
# 正式发布规格：工程为空（无素材），因此不打包任何视频帧、素材缩略图、波形。
# 页面上预览区显示空态提示，等用户导入素材后由引擎实时生成。
frames = {}
thumbs = {}
wave = []

# ---- 工程状态来源 ----
# 优先从正在运行的设计桥实时取；取不到才回落到本地缓存。
# 这样单文件版永远反映**当前工程**，不会把旧工程的素材/轨道打进去。
def _fetch_state():
    import urllib.request
    port = os.environ.get('NC_API_PORT') or os.environ.get('PORT')
    if not port:
        return None
    try:
        with urllib.request.urlopen(
                f'http://127.0.0.1:{port}/api/state', timeout=6) as r:
            ct = (r.headers.get('content-type') or '').lower()
            if 'json' not in ct:
                return None
            return json.load(r)
    except Exception:
        return None

state = _fetch_state()
state_src = 'live'
if state is None:
    _CACHE = os.path.join(ROOT, '.cache', 'state.json')
    if not os.path.isfile(_CACHE):
        raise SystemExit(
            '取不到工程状态：请先启动设计桥并设置 NC_API_PORT，'
            '或提供 .cache/state.json')
    state = json.load(open(_CACHE, encoding='utf-8'))
    state_src = 'cache'


def _dataurl(path):
    import base64
    with open(path, 'rb') as f:
        return 'data:image/jpeg;base64,' + base64.b64encode(f.read()).decode()

thumb_imgs = {}
thumb_map = {}
_face_dir = os.path.join(ROOT, 'static', 'face_sm')
if os.path.isdir(_face_dir):
    for _f in sorted(os.listdir(_face_dir)):
        if _f.endswith('.jpg'):
            thumb_imgs['face_sm/' + _f] = _dataurl(os.path.join(_face_dir, _f))

# 宫格底图：CI 生成的 static/grid/（滤镜/特效/转场/蒙版全量，每格互不相同）。
# map.json 的值形如 "grid/filter_1.jpg"，键与 l3-spec.js 的 <kind>_<id> 一致。
# 静态版必须内联这些图，否则单文件打开后四类宫格全是占位色块。
_grid_dir = os.path.join(ROOT, 'static', 'grid')
if os.path.isdir(_grid_dir):
    for _f in sorted(os.listdir(_grid_dir)):
        if _f.endswith('.jpg'):
            thumb_imgs['grid/' + _f] = _dataurl(os.path.join(_grid_dir, _f))

_map_file = os.path.join(ROOT, 'static', 'grid', 'map.json')
# 不再退回 static/thumb/map.json：那份旧产物只有 41 条，
# 且转场用旧编号（transition_1..12，引擎是 0..33）、
# 还含一个不存在的 effect_37 —— 一旦触发兜底只会给出错误 id 的图，
# 比没有图更糟。grid/map.json 由 gen_grid_previews.py 全量生成（76 条）。
if not os.path.isfile(_map_file):
    raise SystemExit(f'缺少宫格映射表：{_map_file}\n'
                     f'先跑 mobile/tools/gen_grid_previews.py（CI 的 gen-assets 工作流会自动做）')
if os.path.isfile(_map_file):
    thumb_map = json.load(open(_map_file, encoding='utf-8'))

_aud_dir = os.path.join(ROOT, 'static', 'audio_sm')
if os.path.isdir(_aud_dir):
    for _f in sorted(os.listdir(_aud_dir)):
        if _f.endswith('.jpg'):
            thumb_imgs['audio_sm/' + _f] = _dataurl(os.path.join(_aud_dir, _f))

preset_imgs = {}
_preset_dir = os.path.join(ROOT, 'static', 'preset_sm')
# preset_sm 已于重构中删除（滤镜宫格改用 static/grid/，见 gen_grid_previews.py）。
# 这里保留目录探测只为兼容历史产物：目录存在就内联，不存在则 NC_PRESET_IMG 为空对象，
# l3-panel 不会读它（已统一走 NC_THUMB_MAP），所以空对象无副作用。
if os.path.isdir(_preset_dir):
    for _f in sorted(os.listdir(_preset_dir)):
        if _f.endswith('.jpg'):
            preset_imgs[_f[:-4]] = _dataurl(os.path.join(_preset_dir, _f))

css = open(os.path.join(ROOT, 'assets', 'editor.css'), encoding='utf-8').read()
tokens = open(os.path.join(ROOT, 'assets', 'tokens.css'), encoding='utf-8').read()
# INLINE_FONT_FIX：内联后字体相对 static/*.html 解析
tokens = tokens.replace('url("fonts/', 'url("../assets/fonts/')
icons = open(os.path.join(ROOT, 'assets', 'icons.js'), encoding='utf-8').read()
page = open(os.path.join(ROOT, 'pages', '10-editor.html'), encoding='utf-8').read()

body = page.split('<body>')[1].split('<script src=')[0]

data = {
    'state': state,
    'frames': frames,
    'thumbs': thumbs,
    'wave': wave,
}

html = f"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>NairoClip · 剪辑页（静态版）</title>
<style>
{tokens}
{css}
</style>
</head>
<body>
{body}
<script>
window.NC_STATIC = {json.dumps(data, ensure_ascii=False)};
</script>
<script>
{icons}
</script>
<script>
window.NC_API_BASE = 'static';
window.NC_STATIC_BASE = 'data';
/* 滤镜宫格底图：离线烘焙的实拍底图（data URL 内联，单文件版无外链） */
window.NC_PRESET_IMG = {json.dumps(preset_imgs, ensure_ascii=False)};
/* 特效/蒙版/转场宫格：黏土表情（每格互不重复） */
window.NC_THUMB_IMG = {json.dumps(thumb_imgs, ensure_ascii=False)};
window.NC_THUMB_MAP = {json.dumps(thumb_map, ensure_ascii=False)};
</script>
<script>
{open(os.path.join(ROOT, 'assets', 'engine_static.js'), encoding='utf-8').read()}
</script>
<script>
{open(os.path.join(ROOT, 'assets', 'nc-api.js'), encoding='utf-8').read()}
</script>
<script>
{open(os.path.join(ROOT, 'assets', 'l3-spec.js'), encoding='utf-8').read()}
</script>
<script>
{open(os.path.join(ROOT, 'assets', 'l3-panel.js'), encoding='utf-8').read()}
</script>
<script>
{open(os.path.join(ROOT, 'assets', 'editor.js'), encoding='utf-8').read()}
</script>
</body>
</html>
"""
out = os.path.join(ROOT, 'static', 'nairoclip-editor-static.html')
open(out, 'w', encoding='utf-8').write(html)
print('OUT_KB', round(os.path.getsize(out)/1024), 'FRAMES', len(frames),
      'THUMBS', len(thumbs), 'WAVE', len(wave),
      'STATE', state_src, 'TRACKS', len(state.get('tracks') or []),
      'ASSETS', len(state.get('assets') or []))
