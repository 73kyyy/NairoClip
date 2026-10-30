/* =============================================================================
   NairoClip Mobile · 剪辑页交互
   -----------------------------------------------------------------------------
   数据全部来自真实引擎（Engine → C ABI）：
     NCEngine.state()          → 轨道 / 片段 / 素材 / 撤销栈（JNI 直调 nc_track_get 等）
     NCEngine.frameDataUrl()   → 真实解码帧（nc_preview_render）
     NCEngine.thumbDataUrl()   → 真实缩略图（nc_thumbnail_generate）
     NCEngine.call(op, args)   → 真实编辑（nc_split_clip / nc_remove_clip / ...）
     不使用任何 HTTP 接口（见 docs/UI_INTEGRATION.md §7）。
   -----------------------------------------------------------------------------
   本版规则（按需求）：
     · 播放头白色 2px，固定在屏幕水平中心，贯穿刻度尺到轨道底；滑动轨道/刻度移动时间
     · 刻度可双指缩放，标签随缩放级别自动改变间隔（无负数时间）
     · 轨道区不再有整条底色；只有素材本身带白色描边，选中加亮
     · 悬浮轨道控件（👁 🔊 🔒）随轨道数量生成，覆盖在有素材的轨道上
     · 一级工具栏点击 → 同一行向右横向推出二级（首项为返回）
     · 关键帧按钮在选中片段上打 ♦，并在片段内绘制
     · 不实现三级调节面板
   ========================================================================== */
'use strict';

const FPS = 30;

/* DPR 分级：按物理像素密度决定渲染倍率（剪映同样做法）
   1x/2x 屏请求较小尺寸，3x 屏请求更大尺寸，避免糊或浪费内存 */
const DPR = Math.min(4, Math.max(1, window.devicePixelRatio || 1));
const NEED = {
  frameW: DPR >= 3 ? 420 : DPR >= 2 ? 320 : 282,   // 预览帧请求宽度
  thumbW: DPR >= 3 ? 120 : DPR >= 2 ? 100 : 92      // 时间轴缩略图宽度
};

const S = {
  tracks: [],
  assets: [],
  subtitles: [],
  markers: [],
  project: null,
  selected: null,        // {trackId, clipId}
  keyframes: [],         // {trackId, clipId, t, property}
  time: 0,
  pxPerSec: 44,
  playing: false,
  level: 1,              // 1 = 一级工具栏；2 = 二级
  activeL1: null,
  markerMode: false,
  headsOpen: false
};

const $ = (id) => document.getElementById(id);
const E = {
  app: $('app'), stage: $('stage'), canvas: $('canvas'), still: $('still'),
circles: $('circles'), strip: $('strip-done'),
  ruler: $('ruler'), rulerCv: $('ruler-canvas'), tracks: $('tracks'),
  hint: $('empty-hint'), heads: $('floating-heads'), barTrack: $('bar-track'),
  play: $('btn-play'), prev: $('btn-prev'), next: $('btn-next'),
  undo: $('btn-undo'), redo: $('btn-redo'),
  brand: $('btn-rename'), phRead: $('ph-read'),
  sheet: $('sheet-mask'), sheetList: $('sheet-list'), sheetClose: $('sheet-close')
};

const clamp = (v, a, b) => Math.min(b, Math.max(a, v));
const viewW = () => E.tracks.clientWidth || 444;
const center = () => viewW() / 2;
const t2x = (t) => center() + (t - S.time) * S.pxPerSec;
const x2t = (x) => Math.max(0, S.time + (x - center()) / S.pxPerSec);
const fmt = (t) => {
  const s = Math.max(0, t);
  const m = Math.floor(s / 60);
  const sec = s % 60;
  // 缩放足够大时显示到 0.1s
  if (S.pxPerSec >= 60) return `${m}:${sec.toFixed(1).padStart(4, '0')}`;
  return `${m}:${String(Math.floor(sec)).padStart(2, '0')}`;
};
const seqEnd = () => S.tracks.reduce((m, tr) =>
  tr.clips.reduce((mm, c) => Math.max(mm, c.start + c.dur), m), 0);

function toast(text, ms = 2200) {
  document.querySelectorAll('.toast').forEach((n) => n.remove());
  const d = document.createElement('div');
  d.className = 'toast';
  d.textContent = text;
  E.app.appendChild(d);
  setTimeout(() => d.remove(), ms);
}

/* ---------------- 引擎 → 视图模型 ---------------- */
const kindOf = (type) => (type === 1 ? 'audio' : type === 2 ? 'subtitle' : 'video');

function applyState(st) {
  if (!st) return;
  S.assets = (st.assets || []).map((a) => ({
    id: a.id, name: a.name, dur: a.duration_us / 1e6, kind: a.type
  }));
  S.tracks = (st.tracks || []).map((t) => ({
    id: t.id, index: t.index, type: t.type, kind: kindOf(t.type), name: t.name,
    hidden: t.hidden, locked: t.locked, muted: t.muted,
    gainDb: typeof t.gain_db === 'number' ? t.gain_db : 0,
    clips: (t.clips || []).map((c) => {
      const asset = (st.assets || []).find((a) => a.id === c.asset_id);
      return {
        id: c.id, index: c.index, assetId: c.asset_id,
        name: asset ? asset.name : `片段 ${c.id}`,
        start: c.start_us / 1e6, dur: c.duration_us / 1e6,
        enabled: c.enabled, opacity: c.opacity,
        filterStyle: c.filter_style, filterIntensity: c.filter_intensity,
        reversed: !!c.reversed, frozen: !!c.frozen,
        blendMode: typeof c.blend_mode === 'number' ? c.blend_mode : 0
      };
    }),
    // 转场：交界标记需要（from/to 是片段 id，at 是交界时刻）
    transitions: (t.transitions || []).map((x) => ({
      id: x.id, type: x.type, name: x.name,
      dur: x.duration_us / 1e6, from: x.from, to: x.to
    }))
  }));
  S.subtitles = st.subtitles || [];
  S.markers = st.markers || [];
  S.project = st.project || {};
  if (S.selected) {
    const alive = S.tracks.some((t) => t.id === S.selected.trackId &&
      t.clips.some((c) => c.id === S.selected.clipId));
    if (!alive) S.selected = null;
  }
}

/* 工程快照：直接读平台注入的桥（同步，无网络） */
function fetchState() {
  if (!Engine.bridge) throw new Error('引擎未连接');
  const s = Engine.refresh();
  if (!s) throw new Error(Engine.lastError || '读取工程状态失败');
  return s;
}

/* 编辑：调桥，成功后刷新快照并重绘 */
function edit(payload) {
  if (!Engine.bridge) throw new Error('引擎未连接');
  const op = payload && payload.op;
  const args = Object.assign({}, payload);
  delete args.op;
  const r = engineCall(op, args);
  if (!r || !r.ok) throw new Error((r && r.message) || `操作失败（${(r && r.code) ?? '未知'}）`);
  applyState(fetchState());
  renderAll();
  return r;
}

/* 桥调用统一入口：桥同步返回 JSON 字符串 */
function engineCall(fn, args) {
  if (!Engine.bridge) return { ok: false, code: -1, message: '引擎未连接' };
  try {
    const raw = Engine.bridge.call(fn, JSON.stringify(args || {}));
    if (raw === null || raw === undefined) return { ok: false, code: -1, message: '桥无返回' };
    return typeof raw === 'object' ? raw : JSON.parse(String(raw));
  } catch (err) {
    return { ok: false, code: -1, message: `桥调用失败：${err.message}` };
  }
}

async function guardedEdit(payload, okText) {
  if (!Engine.online) return false;
  try {
    await edit(payload);
    if (okText) toast(okText);
    return true;
  } catch (err) {
    toast(`引擎拒绝：${err.message}`, 3000);
    return false;
  }
}

/* ---------------- 工具栏数据（文档 §11–§22） ---------------- */
const L1 = [
  { id: 'clip',   name: '剪辑',   icon: 'i-clip' },
  { id: 'audio',  name: '音频',   icon: 'i-audio' },
  { id: 'text',   name: '文字',   icon: 'i-text' },
  { id: 'pip',    name: '画中画', icon: 'i-pip' },
  { id: 'fx',     name: '特效',   icon: 'i-fx' },
  { id: 'trans',  name: '转场',   icon: 'i-trans' },
  { id: 'filter', name: '滤镜',   icon: 'i-filter' },
  { id: 'adjust', name: '调节',   icon: 'i-adjust' },
  { id: 'ratio',  name: '比例',   icon: 'i-ratio' },
  { id: 'bg',     name: '背景',   icon: 'i-bg' },
  { id: 'mask',   name: '蒙版',   icon: 'i-mask' },
  { id: 'ai',     name: 'AI',     icon: 'i-ai' }
];

/* kind: exec 直接执行 / panel 需调参（本版不弹三级面板，改为提示） */
const L2 = {
  clip: [
    { id: 'split', name: '分割', icon: 'i-split', kind: 'exec' },
    { id: 'speed', name: '变速', icon: 'i-speed' },
    { id: 'volume', name: '音量', icon: 'i-volume' },
    { id: 'delete', name: '删除', icon: 'i-trash', kind: 'exec', danger: true },
    { id: 'copy', name: '复制', icon: 'i-copy', kind: 'exec' },
    { id: 'reverse', name: '倒放', icon: 'i-reverse' },
    { id: 'freeze', name: '定格', icon: 'i-freeze' },
    { id: 'denoise', name: '降噪', icon: 'i-denoise' },
    { id: 'opacity', name: '不透明度', icon: 'i-opacity' },
    { id: 'ripple', name: '波纹删除', icon: 'i-trash', kind: 'exec', danger: true },
    { id: 'toggle', name: '启用/停用', icon: 'i-eye', kind: 'exec' }
  ],
  audio: [
    { id: 'music', name: '音乐', icon: 'i-note' },
    { id: 'sfx', name: '音效', icon: 'i-bell' },
    { id: 'extract', name: '提取音乐', icon: 'i-music-in', kind: 'exec' },
    { id: 'record', name: '录音', icon: 'i-mic' },
    { id: 'fade', name: '淡化', icon: 'i-fade' },
    { id: 'stem', name: '人声分离', icon: 'i-split-audio' }
  ],
  text: [
    { id: 'newtext', name: '新建文本', icon: 'i-text-add' },
    { id: 'subtitle', name: '识别字幕', icon: 'i-caption' },
    { id: 'lyrics', name: '识别歌词', icon: 'i-lyrics' },
    { id: 'ctrack', name: '镜头追踪', icon: 'i-track' },
    { id: 'translate', name: '字幕翻译', icon: 'i-translate' },
    { id: 'tts', name: '文本转语音', icon: 'i-tts' }
  ],
  pip: [
    { id: 'pipadd', name: '新增画中画', icon: 'i-pip-add', kind: 'exec' },
    { id: 'pipmix', name: '混合模式', icon: 'i-layers' },
    { id: 'pipmask', name: '蒙版', icon: 'i-mask' }
  ],
  fx: [
    { id: 'fxscreen', name: '画面特效', icon: 'i-fx' },
    { id: 'fxperson', name: '人物特效', icon: 'i-person' },
    { id: 'fxatmo', name: '氛围特效', icon: 'i-bolt' },
    { id: 'fxmotion', name: '动感特效', icon: 'i-anim' }
  ],
  trans: [
    { id: 'transpreset', name: '预设转场', icon: 'i-trans' },
    { id: 'transkf', name: '自定义关键帧', icon: 'i-key' }
  ],
  filter: [
    { id: 'filterall', name: '滤镜库', icon: 'i-filter' },
    { id: 'lut', name: 'LUT', icon: 'i-adjust' },
    { id: 'filterall2', name: '应用到全部', icon: 'i-copy', kind: 'exec' }
  ],
  adjust: [
    { id: 'adj', name: '参数调节', icon: 'i-adjust' },
    { id: 'reset', name: '重置全部', icon: 'i-reset', kind: 'exec' }
  ],
  ratio: [
    { id: 'ratio916', name: '9:16', icon: 'i-ratio', kind: 'exec' },
    { id: 'ratio169', name: '16:9', icon: 'i-ratio', kind: 'exec' },
    { id: 'ratio11', name: '1:1', icon: 'i-ratio', kind: 'exec' },
    { id: 'ratio34', name: '3:4', icon: 'i-ratio', kind: 'exec' },
    { id: 'ratio43', name: '4:3', icon: 'i-ratio', kind: 'exec' },
    { id: 'ratio235', name: '2.35:1', icon: 'i-ratio', kind: 'exec' },
    { id: 'ratiocustom', name: '自定义', icon: 'i-adjust', kind: 'exec' }
  ],
  bg: [
    { id: 'bgcolor', name: '颜色', icon: 'i-palette' },
    { id: 'bgblur', name: '模糊', icon: 'i-bg' },
    { id: 'bgstyle', name: '画布样式', icon: 'i-layers' }
  ],
  mask: [
    { id: 'maskshape', name: '形状', icon: 'i-mask' },
    { id: 'maskfeather', name: '羽化', icon: 'i-adjust' },
    { id: 'chroma', name: '色度键', icon: 'i-palette' }
  ],
  ai: [
    { id: 'refclip', name: '参考仿剪', icon: 'i-ai' },
    { id: 'depth', name: '深度图', icon: 'i-layers' }
  ]
};

/* ---------------- 工具栏渲染：一级 ↔ 二级 同行横推 ---------------- */
function renderBar(animate = true) {
  const track = E.barTrack;
  const prevW = track.scrollWidth;
  track.innerHTML = '';
  track.style.transition = animate ? '' : 'none';

  // 一级页
  const p1 = document.createElement('div');
  p1.className = 'page';
  L1.forEach((c) => {
    const b = document.createElement('button');
    b.className = 'l1-item' + (S.level === 1 && S.activeL1 === c.id ? ' on' : '');
    b.innerHTML = `<svg class="i"><use href="#${c.icon}"/></svg><span>${c.name}</span>`;
    b.addEventListener('click', () => openLevel2(c.id));
    p1.appendChild(b);
  });
  track.appendChild(p1);

  // 二级页（点到一级后横向推出）
  if (S.level === 2 && S.activeL1) {
    const p2 = document.createElement('div');
    p2.className = 'page';

    const back = document.createElement('button');
    back.className = 'l2-back';
    back.innerHTML = '<svg class="i"><use href="#i-chevron"/></svg><span>返回</span>';
    back.addEventListener('click', () => { S.level = 1; S.activeL1 = null; renderBar(); });
    p2.appendChild(back);

    const scroller = document.createElement('div');
    scroller.className = 'l2-scroll';
    (L2[S.activeL1] || []).forEach((f) => {
      const b = document.createElement('button');
      b.className = 'l2-item' + (f.danger ? ' danger' : '');
      const needClip = ['clip'].includes(S.activeL1);
      if (needClip && !selClip()) b.disabled = true;
      b.innerHTML = `<svg class="i"><use href="#${f.icon}"/></svg><span>${f.name}</span>`;
      b.addEventListener('click', () => {
        if (b.disabled) return toast('请先在时间轴上选中一个片段');
        runFunction(S.activeL1, f);
      });
      scroller.appendChild(b);
    });
    p2.appendChild(scroller);
    track.appendChild(p2);
  }

  // 滑块位移：整页宽度 = 容器宽度
  requestAnimationFrame(() => {
    const w = E.barTrack.parentElement.clientWidth;
    track.style.transform = `translateX(${S.level === 2 ? -w : 0}px)`;
  });
}

function openLevel2(id) {
  S.level = 2;
  S.activeL1 = id;
  renderBar();
}

/* ---------------- 功能执行 ---------------- */
function selClip() {
  if (!S.selected) return null;
  const track = S.tracks.find((t) => t.id === S.selected.trackId);
  const clip = track && track.clips.find((c) => c.id === S.selected.clipId);
  return clip ? { track, clip } : null;
}

function runFunction(cat, f) {
  const sel = selClip();
  switch (f.id) {
    case 'split': {
      if (!sel) return toast('请先选中片段');
      const at = S.time;
      if (at <= sel.clip.start + 0.04 || at >= sel.clip.start + sel.clip.dur - 0.04) {
        return toast('播放头不在片段内部，无法分割');
      }
      return guardedEdit({ op: 'split', track: sel.track.index, clip: sel.clip.id,
                           at_us: Math.round(at * 1e6) }, `已在 ${fmt(at)} 分割`);
    }
    case 'delete': {
      if (!sel) return toast('请先选中片段');
      const payload = { op: 'delete', track: sel.track.index, clip: sel.clip.id };
      S.selected = null;
      return guardedEdit(payload, '已删除片段');
    }
    case 'copy': {
      if (!sel) return toast('请先选中片段');
      return guardedEdit({ op: 'duplicate', track: sel.track.index, clip: sel.clip.id },
                         '已复制片段');
    }
    case 'extract': return openL3('stem');   // 人声/伴奏分离（UVR，真实能力）
    case 'pipadd': return addPipTrack();     // 真实新增视频轨（add_track）
    case 'ripple': {
      const sel2 = selClip();
      if (!sel2) return toast('请先选中片段');
      return guardedEdit(
        { op: 'ripple_delete', track: sel2.track.index, clip: sel2.clip.id },
        '已波纹删除（后续片段自动前移）'
      );
    }
    case 'toggle': {
      const sel3 = selClip();
      if (!sel3) return toast('请先选中片段');
      const next = !(sel3.clip.enabled !== false);
      return guardedEdit(
        { op: 'enabled', track: sel3.track.index, clip: sel3.clip.id, value: next },
        next ? '已启用该片段' : '已停用该片段'
      );
    }
    case 'filterall2': return applyFilterToAll();
    case 'reset': return resetClipAdjust();
    case 'ratio916': return setRatio(9, 16, '9:16');
    case 'ratio169': return setRatio(16, 9, '16:9');
    case 'ratio11': return setRatio(1, 1, '1:1');
    case 'ratio34': return setRatio(3, 4, '3:4');
    case 'ratio43': return setRatio(4, 3, '4:3');
    case 'ratio235': return setRatio(235, 100, '2.35:1');
    case 'ratiocustom': return askCustomRatio();
    /* ---- 二级功能 → 三级面板（全量映射；每个 L2 都有归属） ----
       剪辑 */
    case 'speed':      return openL3('speed');
    case 'volume':     return openL3('volume');
    case 'opacity':    return openL3('opacity');
    case 'denoise':    return openL3('denoise');
    case 'reverse':    return openL3('reverse');
    case 'freeze':     return openL3('freeze');
    /* 音频 */
    case 'music':      return openL3('music', { allowNoClip: true });
    case 'sfx':        return openL3('sfx', { allowNoClip: true });
    case 'record':     return openL3('record');
    case 'fade':       return openL3('fade');
    case 'stem':       return openL3('stem');
    /* 文字 */
    case 'newtext':    return openL3('newtext', { allowNoClip: true });
    case 'subtitle':   return openL3('subtitle');
    case 'lyrics':     return openL3('lyrics');
    case 'translate':  return openL3('translate');
    case 'tts':        return openL3('tts');
    case 'ctrack':     return openL3('ctrack');
    /* 画中画 */
    case 'pipmix':     return openL3('pipmix');
    case 'pipmask':    return openL3('maskshape');
    /* 特效（四类宫格） */
    case 'fxscreen':   return openL3('fxscreen');
    case 'fxperson':   return openL3('fxperson');
    case 'fxatmo':     return openL3('fxatmo');
    case 'fxmotion':   return openL3('fxmotion');
    /* 转场 */
    case 'transpreset': return openL3('transpreset');
    case 'transkf':     return openL3('transkf');
    /* 滤镜 / 调节 */
    case 'filterall':  return openL3('filterall');
    case 'lut':        return openL3('lut');
    case 'adj':        return openL3('adj');
    /* 比例 */
    case 'ratiocustom': return openL3('ratiocustom');
    /* 背景 */
    case 'bgcolor':    return openL3('bgcolor', { allowNoClip: true });
    case 'bgblur':     return openL3('bgblur', { allowNoClip: true });
    case 'bgstyle':    return openL3('bgstyle', { allowNoClip: true });
    /* 蒙版 */
    case 'maskshape':   return openL3('maskshape');
    case 'maskfeather': return openL3('maskfeather');
    case 'chroma': return openL3('chroma');
    /* AI（诚实 AI：面板内显示真实可用状态） */
    case 'refclip':    return openL3('refclip');
    case 'depth':      return openL3('depth');
    default:
      /* 到这里说明 L2 有项没接上，直接暴露问题而不是糊过去 */
      return toast(`${f.name}：尚未绑定三级面板（请检查 L2 映射）`);
  }
}

const RATIOS = {
  ratio916: [9, 16], ratio169: [16, 9], ratio11: [1, 1],
  ratio34: [3, 4], ratio43: [4, 3], ratio235: [235, 100]
};

async function setRatio(w, h, label) {
  S.ratio = [w, h];
  E.app.style.setProperty('--ratio-w', w);
  E.app.style.setProperty('--ratio-h', h);
  E.canvas.style.aspectRatio = `${w} / ${h}`;
  // 项目级全局设置：写回引擎（nc_set_project_canvas），失败则如实提示
  const px = Math.max(64, Math.round(1080));
  const py = Math.max(64, Math.round(1080 * h / w));
  if (Engine.online) {
    try {
      await edit({ op: 'canvas', width: px, height: py });
    } catch (err) {
      toast(`引擎拒绝改画布：${err.message}`, 3000);
    }
  }
  requestAnimationFrame(() => { fitCanvas(); renderTracks(); });
  toast(`画布 ${label || `${w}:${h}`} · 预览 ${px}×${py}`);
}

function askCustomRatio() {
  const w = prompt('宽度像素（16–8192）', '1080');
  if (w === null) return;
  const h = prompt('高度像素（16–8192）', '1080');
  if (h === null) return;
  const wi = Math.round(Number(w));
  const hi = Math.round(Number(h));
  if (!(wi >= 16 && wi <= 8192 && hi >= 16 && hi <= 8192)) {
    return toast('宽高需在 16–8192 之间');
  }
  setRatio(wi, hi, `自定义 ${wi}×${hi}`);
}

/* ---------------- 关键帧 ♦ ---------------- */
function keyframesOf(trackId, clipId) {
  return S.keyframes.filter((k) => k.trackId === trackId && k.clipId === clipId);
}

async function addKeyframe() {
  const sel = selClip();
  if (!sel) return toast('请先选中片段，再在播放头位置打关键帧');
  const local = S.time - sel.clip.start;
  if (local < 0 || local > sel.clip.dur) return toast('播放头不在该片段范围内');
  if (Engine.online) {
    const ok = await guardedEdit({
      op: 'keyframe', track: sel.track.index, clip: sel.clip.id,
      property: 0, time_us: Math.round(local * 1e6), value: 1
    }, `已在 ${fmt(S.time)} 打关键帧`);
    if (ok) S.keyframes.push({ trackId: sel.track.id, clipId: sel.clip.id, t: local, property: 0 });
    renderAll();
    return;
  }
  S.keyframes.push({ trackId: sel.track.id, clipId: sel.clip.id, t: local, property: 0 });
  renderAll();
  toast(`已在 ${fmt(S.time)} 打关键帧`);
}

/* ---------------- 波形（引擎真实数据） ---------------- */
async function loadWaveform(assetId) {
  if (!Engine.online || !assetId) return;
  if (S.waves && S.waves[assetId]) return;
  try {
    const j = Engine.waveform(assetId);
    const arr = ((j && j.buckets) || []).map((v) => Math.abs(v));
    S.waves = S.waves || {};
    S.waves[assetId] = arr.length ? arr : null;
    if (arr.length) renderTracks();
  } catch (err) { /* 无波形时保持细线，不伪造 */ }
}

/* ---------------- 渲染 ---------------- */
let frameSeq = 0;
function syncPreview() {
  // 空态提示只在轨道背板显示；预览区没有画面时就是纯黑，不再叠字
  if (!Engine.online) {
    E.still.style.visibility = 'hidden';
    return;
  }
  const us = Math.round(S.time * 1e6);
  const token = ++frameSeq;
  const url = Engine.frameUrl(us, NEED.frameW);
  if (!url) { E.still.style.visibility = 'hidden'; return; }

  // 内嵌 dataURL：直接挂载，不依赖 onload（file:// 下可能不触发）
  if (url.slice(0, 5) === 'data:') {
    E.still.src = url;
    E.still.style.visibility = 'visible';
    return;
  }
  const img = new Image();
  img.onload = () => {
    if (token !== frameSeq) return;
    E.still.src = url;
    E.still.style.visibility = 'visible';
  };
  img.onerror = () => {
    if (token !== frameSeq) return;
    E.still.style.visibility = 'hidden';
  };
  img.src = url;
}

function drawRuler() {
  const dpr = window.devicePixelRatio || 1;
  const w = viewW();
  const h = 26;
  const cv = E.rulerCv;
  if (cv.width !== Math.round(w * dpr)) {
    cv.width = Math.round(w * dpr);
    cv.height = Math.round(h * dpr);
  }
  const ctx = cv.getContext('2d');
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, w, h);

  // 主刻度间隔随缩放自适应（像素密度 56px 起步），绝不出现负数
  const steps = [0.1, 0.2, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300];
  const major = steps.find((s2) => s2 * S.pxPerSec >= 56) || 600;
  const minor = major / 5;
  const t0 = Math.max(0, S.time - center() / S.pxPerSec);
  const t1 = S.time + center() / S.pxPerSec;

  for (let t = Math.ceil(t0 / minor) * minor; t <= t1; t += minor) {
    if (t < 0) continue;
    const x = Math.round(t2x(t)) + 0.5;
    if (x < 0 || x > w) continue;
    const isMajor = Math.abs(t / major - Math.round(t / major)) < 1e-6;
    ctx.strokeStyle = isMajor ? '#5A5F62' : '#33373A';
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(x, isMajor ? h - 9 : h - 5);
    ctx.lineTo(x, h);
    ctx.stroke();
  }

  E.ruler.querySelectorAll('.ruler-label').forEach((n) => n.remove());
  for (let t = Math.ceil(t0 / major) * major; t <= t1; t += major) {
    if (t < 0) continue;
    const x = t2x(t);
    if (x < 12 || x > w - 12) continue;
    const d = document.createElement('span');
    d.className = 'ruler-label';
    d.style.left = `${x}px`;
    d.textContent = fmt(t);
    E.ruler.appendChild(d);
  }
}

/// 重入保护：renderTracks 会被 25+ 处调用（含 resize / rAF 回调）。
/// 若两次调用交错（第一次已清空、正在逐行 append 时第二次进来又清空），
/// 最终会出现「3 条轨道 → 4 个 .trow」的残留行，且该行与相邻行内容重复。
/// 用「尾随重绘」标志代替直接重入：重入时不并行执行，只标记需要再来一次。
let renderTracksBusy = false;
let renderTracksPending = false;

/// 让整段时长恰好铺满可视宽度（播放头在中心，故左右各留半屏）。
/// 素材比屏幕长时，默认缩放会让片段伸出视口右边 —— 这不是 bug，
/// 是时间轴应有的行为（可以滚动）。但首次打开工程时用户更希望
/// 「一眼看到全部」，所以引擎就绪后调用一次；之后用户手缩放即接管。
function fitToSeq() {
  const end = seqEnd();
  if (!end || end <= 0) return;
  const half = Math.max(60, viewW() / 2 - 24);
  // 播放头在中心：可见的「未来」是半个屏宽，故用半屏算比例
  S.pxPerSec = clamp(half / end, 4, 400);
}

function renderTracks() {
  if (renderTracksBusy) { renderTracksPending = true; return; }
  renderTracksBusy = true;
  try {
    renderTracksBody();
  } finally {
    renderTracksBusy = false;
  }
  if (renderTracksPending) {
    renderTracksPending = false;
    renderTracks();
  }
}

/// 宫格缩略图映射表（滤镜 / 特效 / 转场 / 蒙版）。
///
/// 数据来源，按优先级：
///   1. `static/grid/map.json` —— 由 tools/gen_grid_previews.py 生成
///      （在 CI 里跑，产物提交回仓库）。**四类全量覆盖，id 与 l3-spec.js 一致**
///   2. `static/thumb/map.json` —— 早期只有 23 特效 + 12 转场 + 6 蒙版，
///      且转场 id 从 1 开始（引擎是 0..33）。仅作旧产物兜底。
///
/// 静态版（static/*.html）由 make_static.py 把 map + 图片内联成
/// window.NC_THUMB_MAP / NC_THUMB_IMG，运行时零网络请求；
/// 网页版则由本函数 fetch map.json，图片按需加载。
const STATIC_BASE = '../static';

async function fetchJsonMaybe(url) {
  try {
    const r = await fetch(url);
    if (!r.ok) return null;
    return await r.json();
  } catch (err) {
    return null;
  }
}

async function loadThumbMap() {
  const grid = await fetchJsonMaybe(`${STATIC_BASE}/grid/map.json`);
  const map = grid || (await fetchJsonMaybe(`${STATIC_BASE}/thumb/map.json`));
  if (!map) {
    /* 映射缺失时 l3-panel 会退回占位色块，不阻塞交互 */
    return;
  }
  window.NC_THUMB_MAP = map;
  // 图片按需取：不预加载上百张，浏览器自己缓存 <img src>
  window.NC_STATIC_BASE = STATIC_BASE;
}

/// 拉取混合模式库（16 种）。标签用中文，映射表按内核英文名对齐；
/// 未匹配到的模式直接显示内核名，不猜也不隐藏。
const BLEND_LABEL = {
  normal: '正常', screen: '滤色', overlay: '叠加', darken: '变暗',
  hard_light: '强光', multiply: '正片叠底', lighten: '变亮',
  color_dodge: '颜色减淡', color_burn: '颜色加深', linear_burn: '线性加深',
  linear_dodge: '线性减淡', soft_light: '柔光', linear_light: '线性光',
  pin_light: '点光', difference: '差值', exclusion: '排除',
};

async function loadBlendModes() {
  try {
    const r = await fetch('/api/blend_modes');
    const d = await r.json();
    window.NC_BLEND_MODES = (d.items || []).map((x) => ({
      mode: x.mode,
      label: BLEND_LABEL[x.name] || x.name,
      name: x.name,
    }));
  } catch (err) {
    window.NC_BLEND_MODES = [];
  }
}

/* ============================================================================
   转场 UI（P-14）
   ----------------------------------------------------------------------------
   文档要求：可在任意两相邻片段间添加/编辑/删除转场，不足条件有明确提示。
   核心层已有 34 种转场与完整校验链（validateTransition：时长/顺序/间隙/链式）。
   此前 UI 完全没有入口 —— 本段补上。

   交互：
     · 相邻片段的交界处显示一个标记（相交处的小方块）
     · 无转场时显示「+」，点击 -> 选转场类型 -> 写入引擎
     · 有转场时显示转场图标，点击 -> 可选时长 / 删除
     · 间隙不足时如实提示，不静默失败
   ========================================================================== */

/// 找出轨道上所有「相邻片段对」（按起点排序，间隔 <= 1 帧视为相邻）
function junctionsOf(tr) {
  const clips = [...(tr.clips || [])].sort((a, b) => a.start - b.start);
  const out = [];
  for (let i = 0; i + 1 < clips.length; i += 1) {
    const a = clips[i];
    const b = clips[i + 1];
    const gap = b.start - (a.start + a.dur);
    if (gap <= 1 / FPS + 1e-6) {
      out.push({ track: tr, from: a, to: b, at: a.start + a.dur });
    }
  }
  return out;
}

/// 在交界处渲染标记（叠在轨道行上）
function renderJunctions(lane, tr) {
  const trs = tr.transitions || [];
  for (const j of junctionsOf(tr)) {
    const existing = trs.find((t) => t.from === j.from.id && t.to === j.to.id);
    const x = t2x(j.at);

    const node = document.createElement('button');
    node.className = 'junction' + (existing ? ' has-trans' : '');
    node.style.left = `${x}px`;
    node.title = existing
      ? `${existing.name} · ${Math.round(existing.duration_us / 1000)}ms（点击编辑）`
      : '添加转场';
    node.innerHTML = existing
      ? '<svg class="i"><use href="#i-trans"/></svg>'
      : '<svg class="i"><use href="#i-plus"/></svg>';

    node.addEventListener('pointerdown', (e) => e.stopPropagation());  // 不触发轨道搬家
    node.addEventListener('click', (e) => {
      e.stopPropagation();
      openTransitionSheet(tr, j, existing);
    });
    lane.append(node);
  }
}

/// 打开自定义内容的三级面板（复用页面既有的 .l3-mask / .l3 骨架，
/// 不新建一套容器 —— 否则会和 l3-panel.js 的开关打架）
function showPanel(title, body) {
  const mask = document.getElementById('l3-mask');
  const panel = document.getElementById('l3');
  const bodyEl = document.getElementById('l3-body');
  const titleEl = document.getElementById('l3-title');
  if (!mask || !panel || !bodyEl) { toast('面板容器缺失'); return; }
  if (titleEl) titleEl.textContent = title;
  bodyEl.innerHTML = '';
  bodyEl.append(body);
  // 复用既有结构：隐藏分段条与动作区（转场面板不需要）
  const seg = document.getElementById('l3-seg');
  const act = document.getElementById('l3-actions');
  if (seg) seg.hidden = true;
  if (act) act.hidden = true;
  mask.hidden = false;
  panel.hidden = false;
  requestAnimationFrame(() => panel.classList.add('open'));
  // 既有骨架的关闭按钮由 l3-panel.js 绑定；这里补一次兜底
  const x = document.getElementById('l3-close');
  if (x && !x.dataset.boundTrans) {
    x.dataset.boundTrans = '1';
    x.addEventListener('click', hidePanel);
  }
}

function hidePanel() {
  const mask = document.getElementById('l3-mask');
  const panel = document.getElementById('l3');
  if (!mask || !panel) return;
  panel.classList.remove('open');
  setTimeout(() => { mask.hidden = true; panel.hidden = true; }, 180);
}

/// 转场选择/编辑面板（真实调用 nc_add_transition / nc_remove_transition）
async function openTransitionSheet(tr, junction, existing) {
  // 转场库来自引擎（/api/transitions），不在前端写死
  let library = [];
  try {
    const r = await fetch('/api/transitions');
    const d = await r.json();
    library = d.items || [];
  } catch (err) {
    library = [];
  }

  const body = document.createElement('div');
  /* 标题固定为功能名「转场」。当前效果名不放在标题里：
     一是它只是 34 种里的一种（「信号干扰」「闪白」都是效果名），
     二是标题栏已经用高亮标出当前选中项。 */
  const title = '转场';

  // 可用时长上限：取两片段较短者的一半（内核据此校验）
  const maxUs = Math.floor(Math.min(junction.from.dur, junction.to.dur) * 1e6 / 2);
  const curUs = existing ? Math.round(existing.dur * 1e6) : Math.min(500000, maxUs);

  body.innerHTML = `
    <div class="trans-dur">
      <span>时长</span>
      <input type="range" id="transDur" min="100000" max="${Math.max(100000, maxUs)}"
             step="50000" value="${Math.min(curUs, Math.max(100000, maxUs))}">
      <b id="transDurVal">${Math.round(Math.min(curUs, maxUs) / 1000)}ms</b>
    </div>
    <div class="trans-lib" id="transLib"></div>
    ${existing ? '<button class="btn" id="transDel" style="width:100%;margin-top:10px;color:#E06C5A">删除转场</button>' : ''}
  `;

  const lib = body.querySelector('#transLib');
  if (!library.length) {
    lib.innerHTML = '<div class="empty-hint" style="font-size:11px">转场库读取失败：引擎未提供 /api/transitions</div>';
  } else {
    for (const item of library) {
      const b = document.createElement('button');
      b.className = 'trans-item' + (existing && existing.type === item.type ? ' on' : '');
      b.innerHTML = `<svg class="i"><use href="#i-trans"/></svg>
        <span>${item.name}</span><em>${item.category}</em>`;
      b.addEventListener('click', async () => {
        const dur = Number(body.querySelector('#transDur').value);
        // 内核语义：一个交界只允许一个转场（validateTransition 规则 1：
        // 同一对片段已存在转场则拒绝）。因此「换一个转场」= 先删后加。
        if (existing && existing.id) {
          const dropped = await guardedEdit(
            { op: 'remove_transition', track: tr.index, transition: existing.id });
          if (!dropped) return;
        }
        const okFlag = await guardedEdit({
          op: 'transition', track: tr.index, type: item.type, duration_us: dur,
          from: junction.from.id, to: junction.to.id,
        }, `已应用「${item.name}」`);
        if (okFlag) hidePanel();
      });
      lib.append(b);
    }
  }

  const durInput = body.querySelector('#transDur');
  const durVal = body.querySelector('#transDurVal');
  durInput.addEventListener('input', () => { durVal.textContent = `${Math.round(durInput.value / 1000)}ms`; });

  const delBtn = body.querySelector('#transDel');
  if (delBtn) {
    delBtn.addEventListener('click', async () => {
      // 需要转场 id：从 state 里找（nc_transition_get 有返回 id 的话）
      const okFlag = await removeTransitionAt(tr, existing);
      if (okFlag) hidePanel();
    });
  }

  showPanel(title, body);
}

/// 删除转场：需要转场 id，因此重新拉一次 state 找匹配项
async function removeTransitionAt(tr, existing) {
  const st = Engine.refresh();
  if (!st) { toast('无法读取工程状态'); return false; }
  const track = (st.tracks || []).find((t) => t.index === tr.index);
  const list = track?.transitions || [];
  const hit = list.find((t) => t.from === existing.from && t.to === existing.to);
  if (!hit || hit.id === undefined) {
    // 桥当前未下发转场 id：如实告知，不假装成功
    toast('引擎未下发转场 id，暂时无法按 id 删除。请改选一个转场覆盖。', 3200);
    return false;
  }
  return guardedEdit({ op: 'remove_transition', track: tr.index, transition: hit.id }, '已删除转场');
}

function renderTracksBody() {
  // 逐个摘除直到清空（不用 querySelectorAll 快照，避免清不干净）
  let stale = E.tracks.querySelector('.trow');
  while (stale) {
    stale.remove();
    stale = E.tracks.querySelector('.trow');
  }
  const hasClips = S.tracks.some((t) => t.clips && t.clips.length > 0);
  E.app.classList.toggle('has-clips', hasClips);
  E.hint.hidden = hasClips;

  S.tracks.forEach((tr) => {
    const row = document.createElement('div');
    row.className = 'trow';
    const body = document.createElement('div');
    body.className = 'tbody';
    tr.clips.forEach((c) => body.appendChild(buildClip(tr, c)));
    row.appendChild(body);
    renderJunctions(row, tr);      // 相邻片段交界处的转场标记
    E.tracks.appendChild(row);
  });

  renderHeads();
  drawRuler();
  updateStrip();
}

/* 悬浮轨道控件：跟随真实轨道数量 */
function renderHeads() {
  E.heads.innerHTML = '';
  if (!S.headsOpen) return;
  S.tracks.forEach((tr) => {
    const g = document.createElement('div');
    g.className = 'fh';

    const row = document.createElement('div');
    row.className = 'row';

    const eye = headBtn(tr.hidden ? 'i-eye-off' : 'i-eye', tr.hidden, () =>
      guardedEdit({ op: 'track_flag', track: tr.index, flag: 'hidden', value: !tr.hidden }));
    row.appendChild(eye);

    if (tr.kind !== 'subtitle') {
      const spk = headBtn(tr.muted ? 'i-speaker-off' : 'i-speaker', tr.muted, () =>
        guardedEdit({ op: 'track_flag', track: tr.index, flag: 'muted', value: !tr.muted }));
      row.appendChild(spk);
    }

    const lock = headBtn(tr.locked ? 'i-lock' : 'i-unlock', tr.locked, () =>
      guardedEdit({ op: 'track_flag', track: tr.index, flag: 'locked', value: !tr.locked }));
    row.appendChild(lock);

    g.appendChild(row);
    const nm = document.createElement('span');
    nm.className = 'fname';
    nm.textContent = tr.name;
    g.appendChild(nm);
    E.heads.appendChild(g);
  });
}

function headBtn(icon, off, fn) {
  const b = document.createElement('button');
  b.className = off ? 'off' : '';
  b.innerHTML = `<svg class="i"><use href="#${icon}"/></svg>`;
  b.addEventListener('click', (e) => { e.stopPropagation(); fn(); });
  return b;
}

function buildClip(tr, c) {
  const d = document.createElement('div');
  const selected = S.selected && S.selected.trackId === tr.id && S.selected.clipId === c.id;
  d.className = 'clip' + (selected ? ' selected' : '');
  const x = t2x(c.start);
  const w = c.dur * S.pxPerSec;
  d.style.left = `${x}px`;
  d.style.width = `${Math.max(6, w)}px`;
  if (x + w < -30 || x > viewW() + 30) { d.style.visibility = 'hidden'; }

  if (Engine.online && c.assetId) {
    const fill = document.createElement('div');
    fill.className = 'clip-fill';
    const turl = Engine.thumbUrl(c.assetId, 0, NEED.thumbW);
    if (turl) fill.style.backgroundImage = `url(${turl})`;
    fill.style.backgroundSize = '46px 100%';
    d.appendChild(fill);
  }
  if (tr.kind === 'audio') {
    const wv = document.createElement('div');
    wv.className = 'wave';
    const buckets = (S.waves && S.waves[c.assetId]) || null;
    const n = Math.min(220, Math.max(6, Math.floor(w / 2)));
    if (buckets && buckets.length) {
      // 真实波形：按片段在素材中的区间取桶
      const per = c.dur > 0 ? buckets.length / c.dur : 0;
      for (let i = 0; i < n; i += 1) {
        const idx = Math.min(buckets.length - 1,
                             Math.floor((i / n) * c.dur * per));
        const bar = document.createElement('i');
        bar.style.height = `${Math.max(2, Math.pow(buckets[idx], 0.75) * 26)}px`;
        wv.appendChild(bar);
      }
    } else {
      for (let i = 0; i < n; i += 1) {
        const bar = document.createElement('i');
        bar.style.height = '3px';
        wv.appendChild(bar);
      }
    }
    d.appendChild(wv);
    if (!buckets) loadWaveform(c.assetId);
  }

  // 关键帧 ♦（相对片段起点）
  keyframesOf(tr.id, c.id).forEach((k) => {
    const m = document.createElement('span');
    m.className = 'kf';
    m.style.left = `${k.t * S.pxPerSec}px`;
    m.style.top = '50%';
    d.appendChild(m);
  });

  const nm = document.createElement('div');
  nm.className = 'clip-name';
  nm.textContent = c.name;
  d.appendChild(nm);

  // 选中态：两端出现带箭头的胶囊把手（裁剪用）
  if (selected) {
    ['l', 'r'].forEach((side) => {
      const h = document.createElement('div');
      h.className = `trim-handle trim-${side}`;
      h.dataset.side = side;
      h.innerHTML = `<svg viewBox="0 0 10 20" aria-hidden="true"><path d="${
        side === 'l' ? 'M7 4 2 10l5 6' : 'M3 4l5 6-5 6'
      }" fill="none" stroke="#1B1F20" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"/></svg>`;
      d.appendChild(h);
    });
  }

  d.addEventListener('pointerdown', (e) => {
    e.stopPropagation();
    if (tr.locked) return toast('轨道已锁定');
    S.selected = { trackId: tr.id, clipId: c.id };
    renderTracks();
    if (S.level === 2) renderBar(false);
    syncPreview();

    const side = e.target.closest('.trim-handle');
    if (side) beginTrim(e, tr, c, side.dataset.side);
    else beginMove(e, tr, c);
  });
  return d;
}

function updateStrip() {
  const end = seqEnd();
  E.phRead.textContent = fmt(S.time);
  if (!end) { E.strip.style.width = '0%'; return; }
  E.strip.style.width = `${clamp(S.time / end, 0, 1) * 100}%`;
}

/* 画布等比适配：在预览台内按比例取最大可行尺寸，避免 CSS 冲突导致拉伸 */
function fitCanvas() {
  const stage = E.stage.getBoundingClientRect();
  const ratio = S.ratio || [9, 16];
  const rw = ratio[0], rh = ratio[1];
  if (!stage.width || !stage.height || !rw || !rh) return;
  const byHeight = { w: stage.height * rw / rh, h: stage.height };
  const byWidth = { w: stage.width, h: stage.width * rh / rw };
  const use = byHeight.w <= stage.width ? byHeight : byWidth;
  E.canvas.style.width = `${Math.floor(use.w)}px`;
  E.canvas.style.height = `${Math.floor(use.h)}px`;
}

function renderAll() {
  if (E.hint) E.hint.hidden = S.tracks.some((t) => t.clips && t.clips.length > 0);
  fitCanvas();
  renderTracks();
  renderBar(false);
  syncPreview();
  // 顶栏显示真实工程名（引擎的 project.name）
  if (E.brand) {
    const nm = (S.project && S.project.name) || 'NairoClip';
    E.brand.textContent = nm;
    E.brand.title = `重命名工程（当前：${nm}）`;
  }
  E.undo.disabled = !(S.project && S.project.can_undo);
  E.redo.disabled = !(S.project && S.project.can_redo);
}

/* ---------------- 片段手势：移动 / 裁剪 / 吸附 ----------------
   设计要点：
     · 拖动过程中只改本地模型并重绘，松手才提交一次引擎调用（避免每帧 HTTP）
     · 吸附目标按优先级：播放头 > 相邻片段首尾 > 0 点 > 其他轨道边界 > 标记
     · 吸附阈值 8px（换算成秒），命中时画一条辅助线并轻震一下
     · 垂直移动超过半个轨道高 → 切换目标轨道（nc_move_clip_to_track） */

const SNAP_PX = 8;
const ROW_H = () => {
  const row = E.tracks.querySelector('.trow');
  return row ? row.getBoundingClientRect().height + 4 : 52;
};

function snapCandidates(excludeClipId) {
  const list = [0, S.time];
  S.tracks.forEach((t) => t.clips.forEach((c) => {
    if (c.id === excludeClipId) return;
    list.push(c.start, c.start + c.dur);
  }));
  S.markers.forEach((m) => list.push(m.time_us / 1e6));
  return list;
}

function applySnap(value, candidates, thresholdSec) {
  let best = null;
  let bestD = thresholdSec;
  candidates.forEach((c) => {
    const d = Math.abs(value - c);
    if (d < bestD) { bestD = d; best = c; }
  });
  if (best === null) return { value, snapped: false };
  return { value: best, snapped: true };
}

function showSnapLine(x) {
  let line = document.getElementById('snap-line');
  if (!line) {
    line = document.createElement('div');
    line.id = 'snap-line';
    line.className = 'snap-line';
    E.tracks.appendChild(line);
  }
  line.style.left = `${x}px`;
  line.style.display = 'block';
}
function hideSnapLine() {
  const line = document.getElementById('snap-line');
  if (line) line.style.display = 'none';
}

function buzz() {
  if (navigator.vibrate) { try { navigator.vibrate(8); } catch (err) {} }
}

function beginMove(e, tr, c) {
  const startX = e.clientX;
  const startY = e.clientY;
  const t0 = c.start;
  const trackIndex0 = tr.index;
  let moved = false;
  let targetTrack = tr;

  const onMove = (ev) => {
    const dx = ev.clientX - startX;
    if (Math.abs(dx) > 3 || Math.abs(ev.clientY - startY) > 6) moved = true;

    // 垂直换轨
    const rows = [...E.tracks.querySelectorAll('.trow')];
    const dy = ev.clientY - startY;
    if (Math.abs(dy) > ROW_H() * 0.5) {
      const cur = rows.indexOf(E.tracks.querySelector('.trow'));
      const idx = clamp(rows.findIndex((r) => {
        const b = r.getBoundingClientRect();
        return ev.clientY >= b.top && ev.clientY <= b.bottom;
      }), 0, S.tracks.length - 1);
      if (idx >= 0) targetTrack = S.tracks[idx];
    }

    let next = Math.max(0, t0 + dx / S.pxPerSec);
    const th = SNAP_PX / S.pxPerSec;
    const s1 = applySnap(next, snapCandidates(c.id), th);
    const s2 = applySnap(next + c.dur, snapCandidates(c.id), th);
    if (s1.snapped) { next = s1.value; showSnapLine(t2x(s1.value)); buzz(); }
    else if (s2.snapped) { next = s2.value - c.dur; showSnapLine(t2x(s2.value)); buzz(); }
    else hideSnapLine();

    c.start = +next.toFixed(3);
    // 立即重绘受影响的两个轨道
    renderTracks();
  };

  const onUp = async () => {
    window.removeEventListener('pointermove', onMove);
    window.removeEventListener('pointerup', onUp);
    hideSnapLine();
    if (!moved) return;

    const payload = targetTrack.index !== trackIndex0
      ? { op: 'move_to_track', track: trackIndex0, to_track: targetTrack.index,
          clip: c.id, start_us: Math.round(c.start * 1e6) }
      : { op: 'move', track: trackIndex0, clip: c.id, start_us: Math.round(c.start * 1e6) };
    const ok = await guardedEdit(payload, null);
    if (!ok) { applyState(Engine.state); renderAll(); }
    else toast(`${targetTrack.name} · ${fmt(c.start)}`, 1200);
  };

  window.addEventListener('pointermove', onMove);
  window.addEventListener('pointerup', onUp);
}

function beginTrim(e, tr, c, side) {
  const startX = e.clientX;
  const s0 = c.start;
  const d0 = c.dur;
  let changed = false;

  const onMove = (ev) => {
    const dt = (ev.clientX - startX) / S.pxPerSec;
    const th = SNAP_PX / S.pxPerSec;
    changed = true;

    if (side === 'l') {
      // 拖左端：起点变、时长反向变
      let ns = clamp(s0 + dt, 0, s0 + d0 - 0.1);
      const s1 = applySnap(ns, snapCandidates(c.id), th);
      if (s1.snapped) { ns = s1.value; showSnapLine(t2x(ns)); buzz(); } else hideSnapLine();
      c.start = +ns.toFixed(3);
      c.dur = +(d0 - (ns - s0)).toFixed(3);
    } else {
      let nd = clamp(d0 + dt, 0.1, 3600);
      const s2 = applySnap(s0 + nd, snapCandidates(c.id), th);
      if (s2.snapped) { nd = s2.value - s0; showSnapLine(t2x(s2.value)); buzz(); } else hideSnapLine();
      c.dur = +nd.toFixed(3);
    }
    renderTracks();
  };

  const onUp = async () => {
    window.removeEventListener('pointermove', onMove);
    window.removeEventListener('pointerup', onUp);
    hideSnapLine();
    if (!changed) return;
    const payload = side === 'l'
      ? { op: 'trim_start', track: tr.index, clip: c.id, start_us: Math.round(c.start * 1e6) }
      : { op: 'trim_end', track: tr.index, clip: c.id, end_us: Math.round((c.start + c.dur) * 1e6) };
    const ok = await guardedEdit(payload, null);
    if (!ok) { applyState(Engine.state); renderAll(); }
  };

  window.addEventListener('pointermove', onMove);
  window.addEventListener('pointerup', onUp);
}

/* ---------------- 时间轴手势 ---------------- */
let drag = null;
E.tracks.addEventListener('pointerdown', (e) => {
  if (e.target.closest('.clip') || e.target.closest('button')) return;
  drag = { x: e.clientX, t: S.time };
  E.tracks.setPointerCapture(e.pointerId);
});
E.tracks.addEventListener('pointermove', (e) => {
  if (!drag) return;
  S.time = Math.max(0, drag.t - (e.clientX - drag.x) / S.pxPerSec);
  renderTracks();
  syncPreview();
});
E.tracks.addEventListener('pointerup', () => { drag = null; });
E.tracks.addEventListener('pointercancel', () => { drag = null; });

E.ruler.addEventListener('pointerdown', (e) => {
  drag = { x: e.clientX, t: S.time };
  E.ruler.setPointerCapture(e.pointerId);
  S.time = x2t(e.clientX);
  renderTracks(); syncPreview();
});
E.ruler.addEventListener('pointermove', (e) => {
  if (!drag) return;
  S.time = Math.max(0, x2t(e.clientX));
  renderTracks(); syncPreview();
});
E.ruler.addEventListener('pointerup', () => { drag = null; });

/* ---------------- 双指缩放：统一手势框架 ----------------
   三处各自独立：
     · 时间轴（刻度尺 + 轨道）→ 缩放时间密度 pxPerSec，以双指中心的时间为锚点
     · 预览区 → 缩放画面 0.5×–4×，双指整体移动可平移
     · 工具栏 → 不接缩放（点击目标必须稳定）
   pointer 事件用于桌面（Ctrl+滚轮），touch 事件用于真机。 */

function makePinch(target, opts) {
  const pts = new Map();
  let start = null;
  let raf = 0;

  const dist = () => {
    const [a, b] = [...pts.values()];
    return Math.hypot(a.x - b.x, a.y - b.y);
  };
  const midX = () => {
    const [a, b] = [...pts.values()];
    return (a.x + b.x) / 2;
  };

  function onStart(e) {
    for (const t of e.changedTouches) pts.set(t.identifier, { x: t.clientX, y: t.clientY });
    if (pts.size === 2) {
      start = { d: dist(), mid: midX() };
      opts.onBegin && opts.onBegin(start.mid);
    }
  }

  function onMove(e) {
    for (const t of e.changedTouches) pts.set(t.identifier, { x: t.clientX, y: t.clientY });
    if (pts.size !== 2 || !start || start.d <= 0) return;
    const ratio = dist() / start.d;
    if (Math.abs(ratio - 1) < 0.004) return;
    const mid = midX();
    if (raf) return;
    raf = requestAnimationFrame(() => {
      raf = 0;
      opts.onScale(ratio, mid, start.mid);
    });
    e.preventDefault();
  }

  function onEnd(e) {
    for (const t of e.changedTouches) pts.delete(t.identifier);
    if (pts.size < 2) {
      if (start && opts.onEnd) opts.onEnd();
      start = null;
    }
  }

  target.addEventListener('touchstart', onStart, { passive: true });
  target.addEventListener('touchmove', onMove, { passive: false });
  target.addEventListener('touchend', onEnd);
  target.addEventListener('touchcancel', onEnd);
  return { active: () => pts.size === 2 };
}

/* ① 时间轴：缩放刻度密度（锚定双指中心对应的时间） */
makePinch(E.tracks, {
  onBegin: (mid) => {
    const r = E.tracks.getBoundingClientRect();
    S.pinchAnchor = { x: mid - r.left, time: S.time + (mid - r.left - center()) / S.pxPerSec };
  },
  onScale: (ratio) => {
    const a = S.pinchAnchor;
    if (!a) return;
    S.pxPerSec = clamp(S.basePxPerSec * ratio, 4, 900);
    // 保持锚点时间停在原来的屏幕位置
    S.time = Math.max(0, a.time - (a.x - center()) / S.pxPerSec);
    renderTracks();
    syncPreview();
  },
  onEnd: () => { S.basePxPerSec = S.pxPerSec; S.pinchAnchor = null; }
});
S.basePxPerSec = S.pxPerSec;

/* ② 刻度尺本体同样可缩放 */
makePinch(E.ruler, {
  onBegin: (mid) => {
    const r = E.ruler.getBoundingClientRect();
    S.pinchAnchor = { x: mid - r.left, time: S.time + (mid - r.left - center()) / S.pxPerSec };
  },
  onScale: (ratio) => {
    const a = S.pinchAnchor;
    if (!a) return;
    S.pxPerSec = clamp(S.basePxPerSec * ratio, 4, 900);
    S.time = Math.max(0, a.time - (a.x - center()) / S.pxPerSec);
    renderTracks();
    syncPreview();
  },
  onEnd: () => { S.basePxPerSec = S.pxPerSec; S.pinchAnchor = null; }
});

/* ③ 预览区：缩放画面内容（不改画布外框） */
S.zoom = 1;
S.pan = { x: 0, y: 0 };
makePinch(E.canvas, {
  onBegin: () => { S.baseZoom = S.zoom; },
  onScale: (ratio, mid) => {
    S.zoom = clamp(S.baseZoom * ratio, 0.5, 4);
    applyPreviewZoom();
  },
  onEnd: () => { S.baseZoom = S.zoom; }
});

function applyPreviewZoom() {
  const z = S.zoom || 1;
  const moved = Math.abs(z - 1) > 0.01 || Math.abs(S.pan.x) > 0.5 || Math.abs(S.pan.y) > 0.5;
  E.still.style.transform = moved
    ? `translate(${S.pan.x}px, ${S.pan.y}px) scale(${z})`
    : 'none';
  E.still.style.transformOrigin = 'center center';
  E.canvas.style.cursor = z > 1.01 ? 'grab' : 'default';
}

/* 预览区：放大后单指拖动 = 平移画面（小于 1× 时不拦截，避免误触） */
(function bindPan() {
  let pan = null;
  E.canvas.addEventListener('touchstart', (e) => {
    if (S.zoom <= 1.01 || e.touches.length !== 1) return;
    pan = { x: e.touches[0].clientX, y: e.touches[0].clientY,
            px: S.pan.x, py: S.pan.y };
  }, { passive: true });
  E.canvas.addEventListener('touchmove', (e) => {
    if (!pan || e.touches.length !== 1) return;
    S.pan.x = pan.px + (e.touches[0].clientX - pan.x);
    S.pan.y = pan.py + (e.touches[0].clientY - pan.y);
    applyPreviewZoom();
    e.preventDefault();
  }, { passive: false });
  E.canvas.addEventListener('touchend', () => { pan = null; });
  // 双击复位
  let lastTap = 0;
  E.canvas.addEventListener('touchend', () => {
    const now = Date.now();
    if (now - lastTap < 300) {
      S.zoom = 1; S.pan = { x: 0, y: 0 }; applyPreviewZoom();
    }
    lastTap = now;
  });
})();

/* 桌面评审：Ctrl/⌘ + 滚轮等价缩放 */
E.tracks.addEventListener('wheel', (e) => {
  if (!(e.ctrlKey || e.metaKey)) return;
  e.preventDefault();
  const f = e.deltaY < 0 ? 1.12 : 0.89;
  S.pxPerSec = clamp(S.pxPerSec * f, 4, 900);
  renderTracks(); syncPreview();
}, { passive: false });

E.canvas.addEventListener('wheel', (e) => {
  if (!(e.ctrlKey || e.metaKey)) return;
  e.preventDefault();
  S.zoom = clamp(S.zoom * (e.deltaY < 0 ? 1.1 : 0.9), 0.5, 4);
  applyPreviewZoom();
}, { passive: false });

/* 桌面环境下用 Ctrl+滚轮等价缩放，便于评审 */
E.tracks.addEventListener('wheel', (e) => {
  e.preventDefault();
  if (e.ctrlKey || e.metaKey) {
    S.pxPerSec = clamp(S.pxPerSec * (e.deltaY < 0 ? 1.12 : 0.89), 4, 900);
    renderTracks();
    return;
  }
  S.time = Math.max(0, S.time + e.deltaX / S.pxPerSec);
  renderTracks(); syncPreview();
}, { passive: false });

/* ---------------- 播放控制 ---------------- */
let raf = 0;
let last = 0;
function setPlaying(on) {
  S.playing = on;
  E.play.innerHTML = `<svg class="i"><use href="#i-${on ? 'pause' : 'play'}"/></svg>`;
  if (on) { last = 0; raf = requestAnimationFrame(tick); }
  else cancelAnimationFrame(raf);
}
function tick(ts) {
  if (!S.playing) return;
  if (!last) last = ts;
  S.time += (ts - last) / 1000;
  last = ts;
  const end = seqEnd();
  if (end > 0 && S.time >= end) { S.time = end; setPlaying(false); }
  renderTracks();
  if (Math.round(S.time * FPS) % 2 === 0) syncPreview();
  raf = requestAnimationFrame(tick);
}
E.play.addEventListener('click', () => setPlaying(!S.playing));
const prevBtn = document.getElementById('btn-prev');
if (prevBtn) prevBtn.addEventListener('click', () => {
  setPlaying(false);
  S.time = Math.max(0, S.time - 1 / FPS);
  renderTracks(); syncPreview();
});
const nextBtn = document.getElementById('btn-next');
if (nextBtn) nextBtn.addEventListener('click', () => {
  setPlaying(false);
  S.time = Math.min(seqEnd(), S.time + 1 / FPS);
  renderTracks(); syncPreview();
});

function toggleHeads() {
  S.headsOpen = !S.headsOpen;
  E.app.classList.toggle('heads-open', S.headsOpen);
  try { localStorage.setItem('nc.headsOpen', S.headsOpen ? '1' : '0'); } catch (err) {}
  renderHeads();
}

(function bindHeadsToggle() {
  const btn = document.getElementById('heads-toggle');
  if (btn) btn.addEventListener('click', (e) => { e.stopPropagation(); toggleHeads(); });
})();

// 「从头播放」徽标：跳到 0 并开始播放（参考稿该位置控件）
const badge = document.getElementById('start-badge');
if (badge) badge.addEventListener('click', () => {
  S.time = 0;
  renderTracks(); syncPreview();
  setPlaying(true);
});

/* 进度带点击：按比例跳转 */
E.strip.parentElement.addEventListener('click', (e) => {
  const end = seqEnd();
  if (!end) return;
  const r = E.strip.parentElement.getBoundingClientRect();
  S.time = clamp((e.clientX - r.left) / r.width, 0, 1) * end;
  renderTracks(); syncPreview();
});

/* ---------------- 圆工具行 ---------------- */
E.circles.addEventListener('click', async (e) => {
  const b = e.target.closest('.cbtn');
  if (!b) return;
  const act = b.dataset.act;
  if (act === 'import') return openSheet();
  if (act === 'undo') return guardedEdit({ op: 'undo' });
  if (act === 'redo') return guardedEdit({ op: 'redo' });
  if (act === 'keyframe') return addKeyframe();
  if (act === 'marker') {
    // # = 标记：单击在播放头加标记；长按打开标记列表（改名 / 改色）
    const name = `标记 ${S.markers.length + 1}`;
    const ok = await guardedEdit({
      op: 'marker', time_us: Math.round(S.time * 1e6), name, color: '#FFFFFF'
    }, `已在 ${fmt(S.time)} 添加标记`);
    if (ok) {
      b.classList.add('on');
      setTimeout(() => b.classList.remove('on'), 400);
    }
    return;
  }
  if (act === 'play-next') {
    const next = allClips().filter((c) => c.start > S.time + 0.01)
      .sort((a, b2) => a.start - b2.start)[0];
    if (!next) return toast('后面没有片段了');
    S.time = next.start; renderTracks(); syncPreview();
    return;
  }
  if (act === 'media') return showMediaLibrary();
  if (act === 'scene') return runSceneDetect();
  if (act === 'layers') return showLayerSheet();
  toast(act);
});

/* ---------------- 标记列表：改名 / 改色（marker_color） ---------------- */
const MARK_COLORS = ['#FFFFFF', '#C6F24E', '#8B7CFF', '#FFB020', '#E5484D'];

function showMarkerSheet() {
  const list = (S.markers || []).slice().sort((a, b) => a.time_us - b.time_us);
  E.sheetList.innerHTML = '';
  if (!list.length) {
    sheetRow('暂无标记', '在播放头点「#」添加', () => closeSheet());
  } else {
    list.forEach((m) => {
      const row = document.createElement('div');
      row.className = 'layer-row';
      const t = m.time_us / 1e6;
      row.innerHTML =
        `<span class="mk-dot" style="background:${m.color || '#FFFFFF'}"></span>` +
        `<div class="layer-main"><b>${m.name || '标记'}</b><small>${fmt(t)}</small></div>` +
        `<div class="layer-acts"><button data-c="1" title="改色">` +
        `<svg class="i"><use href="#i-palette"/></svg></button></div>`;
      row.querySelector('button[data-c]').addEventListener('click', async (ev) => {
        ev.stopPropagation();
        const cur = MARK_COLORS.indexOf(m.color);
        const nextColor = MARK_COLORS[(cur + 1 + MARK_COLORS.length) % MARK_COLORS.length];
        // marker_color = 以同名同位置重新写入一个标记（引擎语义如此）
        const ok = await guardedEdit({
          op: 'marker_color', time_us: m.time_us,
          name: m.name || '标记', color: nextColor
        }, '已换色');
        if (ok) { closeSheet(); showMarkerSheet(); }
      });
      E.sheetList.appendChild(row);
    });
  }
  const head = E.sheet && E.sheet.querySelector('.sheet-head span');
  if (head) head.textContent = `标记（${list.length}）`;
  E.sheet.hidden = false;
}

/* 长按「#」打开标记列表 */
(function bindMarkerLongPress() {
  const btn = document.querySelector('.cbtn[data-act="marker"]');
  if (!btn) return;
  let timer = null;
  const start = () => {
    timer = setTimeout(() => { timer = null; showMarkerSheet(); }, 520);
  };
  const cancel = () => { if (timer) { clearTimeout(timer); timer = null; } };
  btn.addEventListener('pointerdown', start);
  btn.addEventListener('pointerup', cancel);
  btn.addEventListener('pointerleave', cancel);
  btn.addEventListener('pointercancel', cancel);
})();

/* ---------------- 工程重命名（rename_project） ---------------- */
if (E.brand) {
  E.brand.addEventListener('click', async () => {
    const cur = (S.project && S.project.name) || '未命名项目';
    const name = window.prompt('工程名称', cur);
    if (name === null) return;
    const trimmed = name.trim();
    if (!trimmed) return toast('名称不能为空');
    if (trimmed === cur) return;
    const ok = await guardedEdit({ op: 'rename_project', name: trimmed }, '已重命名');
    if (ok && typeof load === 'function') await load();
  });
}

/* ---------------- 素材库：列出本项目真实素材 ---------------- */

/* 长按拖动排序（B2-9）。
   为什么是长按而不是直接拖：素材行本身是「点击插入」的按钮，
   直接拖会让"想点击"的手抖被识别成拖动。长按 300ms 后才进入排序态，
   与移动端常见的列表重排手势一致（iOS 列表 / 安卓 RecyclerView）。

   为什么不用 HTML5 drag & drop：移动 WebView 里 draggable 属性
   依赖原生拖放，触摸事件下不触发。必须用 pointerdown/move/up。 */

const DRAG_LONG_PRESS_MS = 300;
const DRAG_MOVE_CANCEL_PX = 8;   // 长按未成行前的位移容差

function showMediaLibrary() {
  const assets = S.assets || [];
  E.sheetList.innerHTML = '';
  if (!assets.length) {
    sheetRow('暂无素材', '点「＋」导入视频 / 图片 / 音频', () => { closeSheet(); openSheet(); });
  } else {
    assets.forEach((a, index) => {
      /* kind 与内核 media_type 对齐：0 视频 / 1 音频 / 2 图片 */
      const kind = a.kind === 1 ? '音频' : (a.kind === 2 ? '图片' : '视频');
      const row = sheetRow(a.name, `${kind} · ${a.dur.toFixed(1)}s`, () => {
        // 点素材：在当前轨道末尾插入（无轨道则先建一条）
        insertAssetToTimeline(a);
        closeSheet();
      });
      attachAssetReorder(row, index);
    });
    if (assets.length > 1) {
      const hint = document.createElement('div');
      hint.className = 'sheet-hint';
      hint.textContent = '长按可拖动排序';
      E.sheetList.appendChild(hint);
    }
  }
  const head = E.sheet && E.sheet.querySelector('.sheet-head span');
  if (head) head.textContent = `素材库（${assets.length}）`;
  E.sheet.hidden = false;
}

/* 给一行素材挂长按拖动排序手势。
   fromIndex 是这一行当前的显示下标，与后端 assets 数组顺序一一对应
   （project.assets 的数组顺序就是显示顺序，没有额外的 order 字段）。 */
function attachAssetReorder(row, fromIndex) {
  let timer = null;
  let startX = 0;
  let startY = 0;
  let dragging = false;
  let suppressClick = false;
  let ghost = null;
  let targetIndex = fromIndex;

  const rows = () => Array.from(E.sheetList.querySelectorAll('.sheet-item'));

  const cleanup = () => {
    if (timer) { clearTimeout(timer); timer = null; }
    if (ghost) { ghost.remove(); ghost = null; }
    rows().forEach((r) => r.classList.remove('drag-src', 'drag-over'));
    dragging = false;
    targetIndex = fromIndex;
  };

  const commit = () => {
    const from = fromIndex;
    const to = targetIndex;
    cleanup();
    if (from === to) return;            // 原地：不发请求，后端也会当 no-op
    if (!Engine.online) return toast('未连接引擎，无法排序');
    // 与 insertAssetToTimeline 一致：nc_ 函数走 engineCall 直通，
    // 不经 edit()（edit 的 op 是语义操作 split/delete，桥另有分发）。
    const r = engineCall('nc_asset_reorder', { from_index: from, to_index: to });
    if (!r || !r.ok) return toast(`排序失败：${(r && r.message) || '未知错误'}`, 3000);
    // 引擎是唯一真源：成功后重新拉状态，不在前端自己挪数组。
    // 顺序：applyState 更新快照 → renderAll 重绘轨道 → 重开素材库显示新顺序。
    try {
      applyState(fetchState());
      renderAll();
      showMediaLibrary();
    } catch (err) {
      toast(`刷新失败：${err.message}`, 3000);
    }
  };

  // 长按进入排序态
  row.addEventListener('pointerdown', (e) => {
    if (e.button !== undefined && e.button !== 0) return;
    startX = e.clientX; startY = e.clientY;
    timer = setTimeout(() => {
      timer = null;
      dragging = true;
      row.classList.add('drag-src');
      if (navigator.vibrate) navigator.vibrate(12);   // 触觉确认
      ghost = row.cloneNode(true);
      ghost.className = 'sheet-item drag-ghost';
      ghost.style.cssText += 'position:fixed;pointer-events:none;opacity:.85;z-index:99;';
      ghost.style.width = `${row.offsetWidth}px`;
      ghost.style.left = `${row.getBoundingClientRect().left}px`;
      ghost.style.top = `${row.getBoundingClientRect().top}px`;
      document.body.appendChild(ghost);
      try { row.setPointerCapture(e.pointerId); } catch (_) { /* 老 WebView 不支持 */ }
    }, DRAG_LONG_PRESS_MS);
  });

  // 长按未成行前，位移超容差就取消（用户其实在滚动列表）
  row.addEventListener('pointermove', (e) => {
    if (!dragging) {
      if (timer && (Math.abs(e.clientX - startX) > DRAG_MOVE_CANCEL_PX ||
                    Math.abs(e.clientY - startY) > DRAG_MOVE_CANCEL_PX)) {
        clearTimeout(timer); timer = null;
      }
      return;
    }
    if (ghost) {
      ghost.style.top = `${e.clientY - ghost.offsetHeight / 2}px`;
    }
    // 落点：鼠标/手指所在 y 覆盖到哪一行
    const list = rows();
    for (let i = 0; i < list.length; i++) {
      const r = list[i];
      if (r === row) continue;
      const b = r.getBoundingClientRect();
      if (e.clientY >= b.top && e.clientY <= b.bottom) {
        list.forEach((x) => x.classList.remove('drag-over'));
        r.classList.add('drag-over');
        targetIndex = i;
        break;
      }
    }
  });

  const endDrag = (e) => {
    if (timer) { clearTimeout(timer); timer = null; }
    if (!dragging) return;
    try { row.releasePointerCapture(e.pointerId); } catch (_) { /* 同上 */ }
    commit();
    // commit() 内的 cleanup() 已把 dragging 置 false，所以这里必须
    // 单独置抑制标记：否则松手后浏览器补发的 click 会顺手插入素材。
    suppressClick = true;
    setTimeout(() => { suppressClick = false; }, 0);
  };
  row.addEventListener('pointerup', endDrag);
  row.addEventListener('pointercancel', cleanup);

  // 长按已激活时抑制随后的 click（否则松手会顺手插入素材）
  row.addEventListener('click', (e) => {
    if (suppressClick) { e.stopPropagation(); e.preventDefault(); }
  }, true);
}

async function insertAssetToTimeline(asset) {
  if (!Engine.online) return toast('未连接引擎，无法插入素材');
  const wantType = asset.kind === 1 ? 1 : 0;
  const track = (S.tracks || []).find((t) => t.type === wantType);
  if (!track) return toast('请先添加一条对应类型的轨道');

  const at = track.clips.reduce((m, c) => Math.max(m, c.start + c.dur), 0);
  // 插入片段：JNI 侧未开放 insert op，走桥的 nc_ 直通
  const r = engineCall('nc_insert_clip', {
    track_index: track.index,
    asset_id: asset.id,
    start_us: Math.round(at * 1e6),
    duration_us: Math.round(asset.dur * 1e6)
  });
  if (r && r.ok) toast(`已插入「${asset.name}」`);
  else toast(`插入失败：${(r && r.message) || '引擎拒绝'}`, 3000);
  if (typeof load === 'function') await load();
}

/* ---------------- 场景检测：调用真实 nc_scene_detect ---------------- */
async function runSceneDetect() {
  if (!Engine.online) return toast('未连接引擎，无法做场景检测');
  toast('正在分析分镜…', 4000);
  try {
    const r = await NC_API.call('nc_scene_detect', { step_seconds: 0.4, max_samples: 600 });
    if (!r || !r.ok) {
      return toast(`场景检测失败：${(r && r.message) || '未知错误'}`, 3000);
    }
    const res = r.result || {};
    const n = Number(res.out_boundary_count || 0);
    const arr = Array.isArray(res.out_boundaries) ? res.out_boundaries : [];
    if (!n) return toast('未检测到明显的镜头切换');

    // 按每 2 个一组 [start,end] 逐个写成标记，时间轴上可见
    let added = 0;
    for (let i = 0; i + 1 < arr.length; i += 2) {
      const startUs = Math.round(arr[i] * 1e6);
      const ok = await guardedEdit({
        op: 'marker', time_us: startUs,
        name: `镜头 ${added + 1}`, color: '#8B7CFF'
      });
      if (ok) added += 1;
    }
    toast(`检测到 ${n} 个镜头，已标记 ${added} 处`);
    if (typeof load === 'function') await load();
  } catch (err) {
    toast(`场景检测异常：${err.message || err}`, 3000);
  }
}

/* ---------------- 图层：多轨顺序 / 开关 / 音量 ---------------- */
function showLayerSheet() {
  const tracks = S.tracks || [];
  E.sheetList.innerHTML = '';
  if (!tracks.length) {
    sheetRow('暂无轨道', '导入素材后自动创建', () => closeSheet());
  } else {
    // 时间轴上越靠后的轨道视觉上越靠上，这里倒序列出
    tracks.slice().reverse().forEach((t) => {
      const flags = [
        t.hidden ? '已隐藏' : null,
        t.muted ? '静音' : null,
        t.locked ? '已锁定' : null
      ].filter(Boolean).join(' · ');
      const desc = `${t.type === 1 ? '音频' : '视频'} · ${t.clips.length} 片段` +
                   (flags ? ` · ${flags}` : '');

      // 一行一个轨道：点右侧按钮切换 显示 / 静音 / 锁定（真实写引擎）
      const row = document.createElement('div');
      row.className = 'layer-row';
      row.innerHTML =
        `<div class="layer-main"><b>${t.name}</b><small>${desc}</small></div>` +
        `<div class="layer-acts">` +
        `<button data-f="hidden" aria-pressed="${t.hidden}">` +
        `<svg class="i"><use href="#${t.hidden ? 'i-eye-off' : 'i-eye'}"/></svg></button>` +
        `<button data-f="muted" aria-pressed="${t.muted}">` +
        `<svg class="i"><use href="#${t.muted ? 'i-speaker-off' : 'i-speaker'}"/></svg></button>` +
        `<button data-f="locked" aria-pressed="${t.locked}">` +
        `<svg class="i"><use href="#${t.locked ? 'i-lock' : 'i-unlock'}"/></svg></button>` +
        `</div>`;

      // 音频轨额外给「音量」与「重命名」入口
      let extra = null;
      if (t.type === 1) {
        extra = document.createElement('div');
        extra.className = 'layer-extra';
        extra.innerHTML =
          `<button class="lx" data-g="gain">音量 ${(t.gainDb || 0).toFixed(0)} dB</button>` +
          `<button class="lx" data-g="rename">重命名</button>`;
      }
      if (extra) {
        row.querySelectorAll('button[data-g]').forEach((btn) => {
          btn.addEventListener('click', async (ev) => {
            ev.stopPropagation();
            if (btn.dataset.g === 'gain') {
              const cur = t.gainDb || 0;
              const input = window.prompt('轨道音量（dB，-60 ~ 12）', String(cur));
              if (input === null) return;
              const db = Number(input);
              if (!Number.isFinite(db) || db < -60 || db > 12) {
                return toast('音量需在 -60 ~ 12 dB 之间');
              }
              const ok = await guardedEdit({ op: 'track_gain', track: t.index, gain_db: db });
              if (ok) { closeSheet(); showLayerSheet(); }
            } else {
              const input = window.prompt('轨道名称', t.name);
              if (input === null) return;
              const nm = input.trim();
              if (!nm) return toast('名称不能为空');
              const ok = await guardedEdit({ op: 'rename_track', track: t.index, name: nm });
              if (ok) { closeSheet(); showLayerSheet(); }
            }
          });
        });
      }
      row.querySelectorAll('button[data-f]').forEach((btn) => {
        btn.addEventListener('click', async (ev) => {
          ev.stopPropagation();
          const flag = btn.dataset.f;
          const next = btn.getAttribute('aria-pressed') !== 'true';
          const ok = await guardedEdit({
            op: 'track_flag', track: t.index, flag, value: next
          });
          if (ok) { btn.setAttribute('aria-pressed', String(next)); closeSheet(); showLayerSheet(); }
        });
      });
      E.sheetList.appendChild(row);
    });
  }
  const head = E.sheet && E.sheet.querySelector('.sheet-head span');
  if (head) head.textContent = `图层（${tracks.length} 轨）`;
  E.sheet.hidden = false;
}

/* ---------------- 画中画：真实新增视频轨 ---------------- */
async function addPipTrack() {
  const n = (S.tracks || []).filter((t) => t.type === 0).length + 1;
  const ok = await guardedEdit(
    { op: 'add_track', name: `画中画 ${n}`, type: 0 },
    '已新增画中画轨道，可从素材库拖入'
  );
  if (ok && typeof load === 'function') await load();
}

/* ---------------- 滤镜：应用到所选轨道的全部片段 ---------------- */
async function applyFilterToAll() {
  const sel = selClip();
  if (!sel) return toast('请先选中一个片段作为滤镜来源');
  const style = typeof sel.clip.filterStyle === 'number' ? sel.clip.filterStyle : 0;
  const amount = typeof sel.clip.filterIntensity === 'number' ? sel.clip.filterIntensity : 60;
  if (!style) return toast('所选片段还没有滤镜，请先在「滤镜」里选一个');

  const track = (S.tracks || []).find((t) => t.id === sel.track.id);
  if (!track || !track.clips || !track.clips.length) return toast('该轨道没有片段');

  let done = 0;
  for (const c of track.clips) {
    const ok = await guardedEdit(
      { op: 'filter', track: sel.track.index, clip: c.id, style, intensity: amount }
    );
    if (ok) done += 1;
  }
  if (done) toast(`已应用到 ${done} 个片段`);
  if (typeof load === 'function') await load();
}

/* ---------------- 重置所选片段的调节参数 ---------------- */
async function resetClipAdjust() {
  const sel = selClip();
  if (!sel) return toast('请先选中片段');
  const track = sel.track.index;
  const clip = sel.clip.id;

  let done = 0;
  // 亮度/对比度/饱和度/色温/色调/灰度/复古 全部归零
  for (const type of [1, 2, 3, 4, 5, 6, 7]) {
    const ok = await guardedEdit({ op: 'effect_add', track, clip, type, value: 0 });
    if (ok) done += 1;
  }
  // 不透明度恢复 1.0
  await guardedEdit({ op: 'opacity', track, clip, value: 1.0 });

  toast(done ? '已重置该片段的调节参数' : '重置失败');
  if (typeof load === 'function') await load();
}

function allClips() {
  const out = [];
  S.tracks.forEach((t) => t.clips.forEach((c) => out.push({ ...c, trackId: t.id })));
  return out;
}

/* ---------------- 素材导入 ---------------- */
/* 时间轴左上角「+」：只加素材本身（视频 / 图片 / 音频）。
   不放示例素材、不放滤镜/转场等效果 —— 那些在底部工具栏里。
   素材类型与内核 nc_import_asset 的 media_type 对齐：0 视频 / 1 音频 / 2 图片。 */
function openSheet() {
  E.sheetList.innerHTML = '';
  sheetRow('视频', 'mp4 / mov / mkv · 取时长并生成胶片条', () => pickFile('video'));
  sheetRow('图片', 'jpg / png / webp · 默认 5 秒静帧', () => pickFile('image'));
  sheetRow('音频', 'mp3 / wav / m4a / flac · 生成波形', () => pickFile('audio'));
  E.sheet.hidden = false;
}
function sheetRow(label, sub, fn) {
  const b = document.createElement('button');
  b.className = 'sheet-item';
  b.innerHTML = `<span style="width:52px;height:30px;border-radius:4px;background:#2F3437;
    display:grid;place-items:center">＋</span>
    <span style="flex:1">${label}<br><small>${sub}</small></span>`;
  b.addEventListener('click', fn);
  E.sheetList.appendChild(b);
  return b;
}
function closeSheet() { E.sheet.hidden = true; }
E.sheetClose.addEventListener('click', closeSheet);
E.sheet.addEventListener('click', (e) => { if (e.target === E.sheet) closeSheet(); });

/* 浏览器里读媒体时长：引擎的 nc_import_asset 要求调用方给 duration_us，
   它自己不做探测，所以必须在这里算好再传。 */
function probeDuration(file, isAudio) {
  return new Promise((resolve) => {
    const el = document.createElement(isAudio ? 'audio' : 'video');
    el.preload = 'metadata';
    const url = URL.createObjectURL(file);
    const done = (v) => { URL.revokeObjectURL(url); resolve(v); };
    el.addEventListener('loadedmetadata', () => {
      const d = Number.isFinite(el.duration) ? el.duration : 0;
      done(Math.max(0, Math.round(d * 1e6)));
    });
    el.addEventListener('error', () => done(0));
    setTimeout(() => done(0), 8000);   // 元数据读不出来就按 0 处理
    el.src = url;
  });
}

/* 真正把素材写进引擎 */
async function importToEngine(name, path, type, durationUs) {
  if (!Engine.online) {
    toast('未连接引擎，无法导入素材', 3000);
    return 0;
  }
  const r = await NC_API.call('nc_import_asset', {
    name, path, type, duration_us: durationUs
  });
  if (!r || !r.ok || !r.result || !r.result.value) {
    toast(`导入失败：${(r && r.message) || '引擎拒绝'}`, 3000);
    return 0;
  }
  return Number(r.result.value);
}

function pickFile(kind) {
  const accept = {
    video: 'video/*',
    image: 'image/*',
    audio: 'audio/*',
  }[kind] || 'video/*,image/*,audio/*';

  const input = document.createElement('input');
  input.type = 'file';
  input.accept = accept;
  input.multiple = true;
  input.addEventListener('change', async () => {
    const files = [...input.files];
    closeSheet();
    if (!files.length) return;

    let imported = 0;
    for (const f of files) {
      const isImage = /^image\//.test(f.type) || /\.(jpe?g|png|webp|bmp|gif)$/i.test(f.name);
      const isAudio = /^audio\//.test(f.type) || /\.(mp3|wav|m4a|aac|flac)$/i.test(f.name);
      toast(`正在读取「${f.name}」…`, 2500);
      /* 图片没有天然时长：给 5 秒静帧，与剪映「图片默认 5s」一致。
         视频/音频读真实时长。 */
      const durUs = isImage ? 5000000 : await probeDuration(f, isAudio);
      if (!durUs) {
        toast(`「${f.name}」无法读取时长，已跳过`, 3000);
        continue;
      }
      const mediaType = isImage ? 2 : (isAudio ? 1 : 0);
      // Web 端拿不到本地文件的绝对路径：把文件名作为 path 交给引擎，
      // 平台层（JNI/Swift）接入时会换成真实路径。
      const id = await importToEngine(f.name, f.name, mediaType, durUs);
      if (id) imported += 1;
    }
    if (imported) {
      toast(`已导入 ${imported} 个素材`);
      if (typeof load === 'function') await load();
    }
  });
  input.click();
}

/* ---------------- 三级面板宿主接口 ----------------
   l3-panel.js 只认这些接口，不直接依赖 editor.js 内部变量。 */
window.L3_HOST = {
  get selClip() {
    const s = selClip();
    return s ? { track: s.track, clip: s.clip } : null;
  },
  edit: (payload) => edit(payload),
  /* 直接调 C ABI（AI 任务等不经过 edit 分发的入口） */
  callNative: (fn, args) => engineCall(fn, args),
  apiBase: () => Engine.apiBase(),
  reload: async () => { applyState(await fetchState()); renderAll(); },
  nowUs: () => Math.round(S.time * 1e6),
  clipDur: () => { const s = selClip(); return s ? s.clip.dur : 0; },
  /* 轨道快照（只读）：转场需要知道同轨道上的下一个片段 */
  tracks: () => (S.tracks || []).map((t) => ({
    id: t.id, index: t.index, type: t.type, name: t.name,
    clips: (t.clips || []).map((c) => ({ id: c.id, start: c.start, dur: c.dur }))
  })),
  clipStyle: () => { const s = selClip(); return s ? (s.clip.filterStyle || 0) : 0; },
  nextClip: () => {
    const s = selClip();
    if (!s) return null;
    return s.track.clips
      .filter((c) => c.id !== s.clip.id && c.start >= s.clip.start)
      .sort((a, b) => a.start - b.start)[0] || null;
  },
  maskShape: () => 4,
  clipLut: () => {
    const s = selClip();
    if (!s) return { path: '', strength: 1 };
    return {
      path: s.clip.lutPath || '',
      strength: (typeof s.clip.lutStrength === 'number') ? s.clip.lutStrength : 1
    };
  },
  validateLut: (path) => {
    try {
      const r = engineCall('nc_lut_validate_file',
        { path: path, out_size: 1, out_title: 1, out_message: 1 });
      if (!r || r.ok === false) {
        return { ok: false, message: (r && (r.message || r.result)) || '校验失败' };
      }
      const res = r.result || {};
      return {
        ok: true,
        size: res.size || res.out_size || 0,
        title: res.title || res.out_title || '',
        message: ''
      };
    } catch (err) {
      return { ok: false, message: String(err && err.message ? err.message : err) };
    }
  }
};

/* 三级面板由 assets/l3-panel.js 提供（规格在 l3-spec.js）：
   这里只做转接，避免把 60 个面板的渲染逻辑塞进剪辑页主逻辑。 */
function openL3(key, opts) {
  if (typeof window.L3OPEN === 'function') return window.L3OPEN(key, opts);
  toast('三级面板未加载');
}

/* ---------------- 启动 ---------------- */
window.addEventListener('resize', () => { fitCanvas(); renderTracks(); });
window.addEventListener('orientationchange', () => setTimeout(() => { fitCanvas(); renderTracks(); }, 260));

(async function boot() {
  renderBar(false);
  renderTracks();
  try { S.headsOpen = localStorage.getItem('nc.headsOpen') === '1'; } catch (err) {}
  E.app.classList.toggle('heads-open', !!S.headsOpen);

  const ok = await Engine.boot();
  if (!ok) {
    E.hint.hidden = false;
    const t = E.hint.querySelector('.eh-text');
    t.textContent = `未连接剪辑引擎 · ${Engine.lastError}`;
    t.style.color = '#8A9092';
    t.style.maxWidth = '86%';
    t.style.textAlign = 'center';
    t.style.lineHeight = '1.6';
    t.style.fontSize = '10px';
    // 如实说明当前环境的接入方式（引擎由平台层直调，不通过网络）
    const tip = document.createElement('div');
    tip.textContent = /Android|iPhone|iPad/i.test(navigator.userAgent)
      ? '引擎桥未注入：请在 WebView 中 addJavascriptInterface(new NativeEngineBridge(ctx), "NCEngine")'
      : '浏览器环境没有引擎桥；C ABI 需由原生层直调（docs/UI_INTEGRATION.md §7）';
    tip.style.cssText = 'margin-top:8px;font:10px/1.6 var(--font-num);color:#5F6466;max-width:86%;text-align:center';
    E.hint.appendChild(tip);
    return;
  }
  applyState(Engine.state);
  // 宫格缩略图的黏土底图映射（map.json 由 tools/make_static.py 写入）
  await loadThumbMap();
  // 混合模式库：名称与顺序来自内核（nc_blend_mode_count/name），不写死
  await loadBlendModes();
  // 首次载入：把整个工程缩放进来（用户手动缩放后不再覆盖）
  if (!/\bzoom=/.test(location.search)) fitToSeq();
  // 预览按 fill 输出（铺满居中裁切）：与 9:16 画布一致，不出现上下黑边
  try { await edit({ op: 'frame_fit', mode: 1 }); } catch (err) { /* 老引擎无此接口时保持默认 */ }
  renderAll();

  const q = new URLSearchParams(location.search);
  if (q.get('l1')) {
    const id = q.get('l1');
    if (L1.some((c) => c.id === id)) openLevel2(id);
  }
  if (q.get('zoom')) {
    S.pxPerSec = clamp(parseFloat(q.get('zoom')) || S.pxPerSec, 4, 900);
    renderTracks();
  }
  if (q.get('kf') === '1') {
    // 评审用：给首个片段打三个关键帧，展示 ♦
    const first = S.tracks.find((t) => t.clips.length);
    if (first) {
      const c = first.clips[0];
      [0.25, 0.5, 0.75].forEach((r) => S.keyframes.push({
        trackId: first.id, clipId: c.id, t: c.dur * r, property: 0
      }));
      renderTracks();
    }
  }
})();
