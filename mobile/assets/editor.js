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
  thumbW: DPR >= 3 ? 120 : DPR >= 2 ? 100 : 92,     // 时间轴缩略图宽度
  /* 拖动时的预览宽度。拖动中看不清细节，但要求跟手：
     宽度减半 → 像素数降到 1/4 → PNG/Base64 链路成本同步降到 1/4。
     松手后立刻用 frameW 重拉一帧高清，用户感知不到中间那段低清。 */
  frameWDrag: 112
};

/* ---------------- 轨道高度阶梯（轨道多了自动变小） ----------------
   用户要求（2026-11-03）：
     · 标准高度「不要显得大，也不要显得小」
     · 轨道装到 4~5 条就压到最小档，之后不再变小
     · 最小档「看上去不小也不大」，但轨道上的素材仍要看得清

   实测轨道区可用高度 = 235px（截图 393×852：tracks 区域 y466..y700）。
   两组数值都能满足「6 条满屏」，取前者（标准更大，视觉更舒展）：

     按轨道总数分三档（不是逐条下坡）：
       1~4 条 → 全部 44   合计 176px 起
       5  条 → 全部 37   合计 201px
       6  条 → 全部 30   合计 204px（6×30 + 5×4 = 200，留 35px 余量）

   视觉高 = 行高 − 8（.clip 上下各留 4px 内边距）：
     标准 36dp、过渡 29dp、最小 22dp
   对照你给的剪映参考图实测（3x 截图 → dp）：少轨 56dp、多轨 20.7dp。
   36dp 介于两者之间（不大不小）；22dp 略高于多轨 20.7dp（看清素材的底线）。

   7 条起必然溢出（269px），此时轨道区转为纵向滚动，不再继续压 ——
   压到 22dp 以下缩略图就糊成一条线，「看清素材」这个前提就不成立了。
*/
const ROW_STD = 44;      // 标准行高（视觉高 44-2=42px）
const ROW_MID = 37;      // 5 条轨道时的过渡档
const ROW_MIN = 30;      // 6 条及以上统一压到最小档
const ROW_GAP = 2;       // 轨道间距（固定，不随行高变化）
const ROW_PAD = 0;       // 素材上下内边距（0 = 素材填满轨道高度）
const ROW_COLLAPSED = 20;// 折叠态（不参与阶梯；老工程可能带此状态）
const ROW_VIEW_H = 235;  // 轨道区可用高度（393x852 实测）

/// 素材上下内边距。当前为 0：素材直接填满轨道高度，
/// 视觉分隔完全交给 ROW_GAP（2px）+ 素材自身的白描边。
function clipPadFor() { return ROW_PAD; }

/// 第 i 条轨（0-based）的阶梯高度；count = 轨道总数。
///
/// 用户明确要求（2026-11-03）：「超过 4 个之后，不管是第 1 个还是后面的
/// 全部」，都压到同一档 —— **不是**「越靠下越矮」的斜坡。
/// 所以这里只按总数分三档，与 i 无关：
///   n <= 4  → 全部标准 44
///   n == 5  → 全部过渡 37
///   n >= 6  → 全部最小 30（缩略图仍能看清的底线）
/// 旧注释里「前 2 条始终标准」是早期理解，已按用户原话纠正。
function ladderHeight(i, count) {
  const n = count || S.tracks.length || 0;
  if (n <= 4) return ROW_STD;    // 1~4 条：标准（不大不小）
  if (n === 5) return ROW_MID;   // 5 条：过渡档
  return ROW_MIN;                // 6 条起：统一最小，全轨一致
}

/// 按当前轨道总数，给所有「未手动调整」的轨道分配阶梯高度。
/// 手动调过的轨道（autoHeight=false）不动 —— 用户意图优先。
/// 返回是否发生了高度变化（供调用方决定要不要重绘）。
function applyLadderHeights() {
  const n = (S.tracks || []).length;
  if (!n) return false;
  let changed = false;
  S.tracks.forEach((tr, i) => {
    if (!tr.autoHeight) return;
    const want = ladderHeight(i, n);
    if (tr.heightPx !== want) { tr.heightPx = want; changed = true; }
  });
  return changed;
}

/// 所有轨道的实际总高度（含间距）。用真实 heightPx，
/// 这样用户手动拉高某条轨后，滚动判断也是准的。
function ladderTotal() {
  const list = S.tracks || [];
  let h = 0;
  list.forEach((tr) => {
    const rh = tr.collapsed ? ROW_COLLAPSED : (tr.heightPx || ROW_STD);
    h += rh + ROW_GAP;
  });
  return h;
}

const S = {
  tracks: [],
  groups: [],           // 轨道组（B2-7）：{id, name, trackCount}
  /* 正在拖动片段/裁剪/滚动时间轴。
     拖动期间预览降质换取跟手，松手后再拉全质量帧。 */
  dragging: false,
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
  headsOpen: false,
  guides: false          // 预览区三分线（纯前端叠加，不进工程数据）
};

const $ = (id) => document.getElementById(id);
const E = {
  app: $('app'), stage: $('stage'), canvas: $('canvas'), still: $('still'),
circles: $('circles'), strip: $('strip-done'), stripRender: $('strip-render'),
  ruler: $('ruler'), rulerCv: $('ruler-canvas'), tracks: $('tracks'),
  hint: $('empty-hint'), heads: $('floating-heads'), barTrack: $('bar-track'),
  play: $('btn-play'),
  renderBtn: document.querySelector('.c-render'),
  undo: $('btn-undo'), redo: $('btn-redo'),
  brand: $('btn-rename'), phRead: $('ph-read'),
  sheet: $('sheet-mask'), sheetList: $('sheet-list'), sheetClose: $('sheet-close')
};

const clamp = (v, a, b) => Math.min(b, Math.max(a, v));
const viewW = () => E.tracks.clientWidth || 444;
const center = () => viewW() / 2;
const t2x = (t) => center() + (t - S.time) * S.pxPerSec;
const x2t = (x) => Math.max(0, S.time + (x - center()) / S.pxPerSec);
/* 时间码：MM:SS（左分钟、右秒，各自两位补齐）。
   用户明确要求的就是这个格式，所以不再按缩放切成 0.1s 精度——
   播放头读数在播放中必须稳定可读，位数跳动反而更难看清。
   规则：
     · 分钟不设上限（超过 99 分钟自然变三位，不做截断，避免显示错误时长）
     · 秒始终两位（05、09、59）
     · 只对「秒」向下取整：播放到 3.99s 显示 00:03，与「已播放」直觉一致 */
const fmt = (t) => {
  const s = Math.max(0, t);
  const m = Math.floor(s / 60);
  const sec = Math.floor(s % 60);
  return `${String(m).padStart(2, '0')}:${String(sec).padStart(2, '0')}`;
};
const seqEnd = () => S.tracks.reduce((m, tr) =>
  tr.clips.reduce((mm, c) => Math.max(mm, c.start + c.dur), m), 0);

/* 刻度标签专用格式化：与 fmt（播放头读数 MM:SS）分开。
   为什么必须分开：缩放到 0.1s / 0.2s 步长时，若刻度也用 MM:SS，
   相邻两个标签会显示成同一个值（03:05、03:05），用户完全无法定位。
   刻度需要「按当前步长显示足够精度」，播放头读数需要「稳定两位」。
   这里按步长决定小数位：<1s 显示一位小数，>=1s 不显示小数。 */
const fmtRuler = (t, step) => {
  const s = Math.max(0, t);
  const m = Math.floor(s / 60);
  if (step < 1) {
    const sec = (s % 60).toFixed(1).padStart(4, '0');
    return `${String(m).padStart(2, '0')}:${sec}`;
  }
  const sec = Math.floor(s % 60);
  return `${String(m).padStart(2, '0')}:${String(sec).padStart(2, '0')}`;
};

function toast(text, ms = 2200) {
  document.querySelectorAll('.toast').forEach((n) => n.remove());
  const d = document.createElement('div');
  d.className = 'toast';
  d.textContent = text;
  E.app.appendChild(d);
  setTimeout(() => d.remove(), ms);
}

/* ---------------- 引擎 → 视图模型 ---------------- */
/* 轨道类型 → 前端语义名。数值与 core 的 TrackType 一一对应：
   0 Video / 1 Audio / 2 Subtitle / 3 Adjustment。
   3 是调整图层（B2-6）：没有片段，效果与 LUT 挂在轨道上。 */
const kindOf = (type) => (
  type === 1 ? 'audio' : type === 2 ? 'subtitle' : type === 3 ? 'adjustment' : 'video');

function applyState(st) {
  if (!st) return;
  S.assets = (st.assets || []).map((a) => ({
    id: a.id, name: a.name, dur: a.duration_us / 1e6, kind: a.type
  }));
  S.groups = (st.groups || []).map((g) => ({
    id: g.id, name: g.name, trackCount: g.track_count || 0
  }));
  S.tracks = (st.tracks || []).map((t) => ({
    id: t.id, index: t.index, type: t.type, kind: kindOf(t.type), name: t.name,
    hidden: t.hidden, locked: t.locked, muted: t.muted,
    gainDb: typeof t.gain_db === 'number' ? t.gain_db : 0,
    /* 轨道视图（B2-10）：高度 24~200，默认 48；collapsed 折叠时只留一条细条。
       后端读不到时给默认值，保证 UI 不会因为 undefined 画出零高度。 */
    /* 高度：用户手动调过（引擎里与默认 48 不同）就以引擎为准；
       否则交由阶梯统一计算（见 applyLadderHeights），这样
       「轨道多了自动变小」不会覆盖用户的手动调整。 */
    heightPx: (typeof t.height_px === 'number' && t.height_px !== 48)
      ? t.height_px : ROW_STD,
    autoHeight: !(typeof t.height_px === 'number' && t.height_px !== 48),
    collapsed: !!t.collapsed,
    /* 调整图层专有（P-19）：后端只在 type==3 时下发，其余轨道为 undefined。
       前端据「有没有这个字段」决定是否显示齿轮/斜纹，不靠 kind 猜。 */
    adjustment: t.adjustment || null,
    /* 所属轨道组（B2-7）。0 = 未分组。 */
    groupId: t.group_id || 0,
    clips: (t.clips || []).map((c) => {
      const asset = (st.assets || []).find((a) => a.id === c.asset_id);
      return {
        id: c.id, index: c.index, assetId: c.asset_id,
        name: asset ? asset.name : `片段 ${c.id}`,
        start: c.start_us / 1e6, dur: c.duration_us / 1e6,
        enabled: c.enabled, opacity: c.opacity,
        filterStyle: c.filter_style, filterIntensity: c.filter_intensity,
        reversed: !!c.reversed, frozen: !!c.frozen,
        blendMode: typeof c.blend_mode === 'number' ? c.blend_mode : 0,
        /* 特效栈：三级面板的「已添加效果列表」完全依赖这一项。
           后端只给 id/type/value/enabled，名称由 L3_SPEC.effectNameOf 映射。
           缺省成空数组（不是 undefined），否则调用方要处处判空。 */
        effects: (c.effects || []).map((e) => ({
          id: e.id,
          type: e.type,
          value: typeof e.value === 'number' ? e.value : 0,
          enabled: e.enabled === 0 ? 0 : 1
        }))
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
  applyLadderHeights();
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
  { id: 'tmpl',   name: '模板',   icon: 'i-auto' },
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
  { id: 'ai',     name: '更多',   icon: 'i-ai' }
];

/* kind: exec 直接执行 / panel 需调参（本版不弹三级面板，改为提示） */
const L2 = {
  tmpl: [
    { id: 'tmpllist', name: '模板库', icon: 'i-auto' },
    { id: 'tmplauto', name: '一键套用', icon: 'i-bolt', kind: 'exec' }
  ],
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
    { id: 'toggle', name: '启用/停用', icon: 'i-eye', kind: 'exec' },
    /* 场景检测原在圆工具行（已按用户指定顺序移除），
       它是剪片时的辅助动作，移到「剪辑」二级菜单保留可达性。 */
    { id: 'scene', name: '场景检测', icon: 'i-clap', kind: 'exec' }
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
    /* 「画面」：位置/大小/旋转/锚点。放在第二位 —— 新增画中画之后
       用户第一件要做的事就是把它缩小挪位（G4）。 */
    { id: 'piptransform', name: '画面', icon: 'i-expand' },
    { id: 'pipmix', name: '混合模式', icon: 'i-layers' },
    { id: 'pipmask', name: '蒙版', icon: 'i-mask' }
  ],
  fx: [
    { id: 'fxscreen', name: '画面特效', icon: 'i-fx' },
    { id: 'fxperson', name: '人物特效', icon: 'i-person' },
    { id: 'fxatmo', name: '氛围特效', icon: 'i-bolt' },
    { id: 'fxmotion', name: '动感特效', icon: 'i-anim' },
    /* 「转换」= 移动/变换类：位移、旋转、缩放、沿路径等，
       与上面四类「视觉特效」不同 —— 它改的是几何位置，不重新着色。
       图标单独设计（两个错位的方框 + 箭头），与 i-fx 区分开。 */
    { id: 'xform', name: '转换', icon: 'i-xform' }
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
    back.addEventListener('click', backToL1);
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
    case 'scene': return runSceneDetect();
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
       模板 */
    case 'tmpllist':   return showTemplateSheet();
    case 'tmplauto':   return showTemplateSheet();   // 同一个面板，套用即在列表里选
    /* 剪辑 */
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
    case 'beatcut':    return openBeatCutPanel();
    /* 文字 */
    case 'newtext':    return openL3('newtext', { allowNoClip: true });
    case 'subtitle':   return openL3('subtitle');
    case 'lyrics':     return openL3('lyrics');
    case 'translate':  return openL3('translate');
    case 'tts':        return openL3('tts');
    case 'ctrack':     return openL3('ctrack');
    /* 画中画 */
    case 'piptransform': return openL3('piptransform');
    case 'pipmix':     return openL3('pipmix');
    case 'pipmask':    return openL3('maskshape');
    /* 特效（四类宫格） */
    case 'fxscreen':   return openL3('fxscreen');
    case 'fxperson':   return openL3('fxperson');
    case 'fxatmo':     return openL3('fxatmo');
    case 'fxmotion':   return openL3('fxmotion');
    case 'xform':      return openTransformPanel();
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
  /* 拖动中降质：见 NEED.frameWDrag 的说明。
     静止时用全宽，保证松手后画面是清晰的。 */
  const w = S.dragging ? NEED.frameWDrag : NEED.frameW;
  const url = Engine.frameUrl(us, w);
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
    d.textContent = fmtRuler(t, major);
    E.ruler.appendChild(d);
  }
}

/// 二级 → 一级工具栏。l2-back 按钮与系统返回键共用这一条路径，
/// 避免两处实现漂移（曾经按钮改了返回键没跟上）。
function backToL1() {
  S.level = 1;
  S.activeL1 = null;
  renderBar();
}

/// 供原生返回键调用：退一级工具栏。
/// 返回 true = 已处理（当前在二级），false = 已在最外层，交给系统。
window.NCBARBACK = function () {
  if (S.level !== 2) return false;
  backToL1();
  return true;
};

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
/// 数据来源：`static/grid/map.json` —— 由 mobile/tools/gen_grid_previews.py
/// 生成（在 CI 里跑，产物提交回仓库）。**四类全量覆盖，id 与 l3-spec.js 一致**。
///
/// 不再退回 static/thumb/map.json：那份旧产物只有 41 条，
/// 转场用旧编号（transition_1..12，引擎是 0..33），还含不存在的 effect_37。
/// 一旦退回，用户点「交叉溶解」会看到别的转场的图 —— 属于错误展示，
/// 比没有图更糟。缺映射时 l3-panel 会退回占位色块，不阻塞交互。
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
  const map = await fetchJsonMaybe(`${STATIC_BASE}/grid/map.json`);
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

/* 直调 C ABI 取列表（nc_blend_mode_count / nc_blend_mode_name）。
 *
 * 此前这里是 fetch('/api/blend_modes') —— 那条 HTTP 路由只存在于
 * design 的桌面 harness；Android 上引擎经 JNI 直调，没有 HTTP 服务，
 * 必然抛异常，结果 NC_BLEND_MODES 恒为 []，混合模式面板是空列表。
 */
function loadBlendModes() {
  const out = [];
  const n = engineCall('nc_blend_mode_count', {});
  const count = (n && n.ok && typeof n.value === 'number') ? n.value : 0;
  for (let i = 0; i < count; i += 1) {
    const r = engineCall('nc_blend_mode_name', { mode: i });
    // ABI 返回 const char*（静态串），桥包装成 {value: "normal"}
    const name = (r && r.ok && typeof r.value === 'string') ? r.value : '';
    if (!name) continue;
    out.push({ mode: i, label: BLEND_LABEL[name] || name, name });
  }
  window.NC_BLEND_MODES = out;
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
  /* 转场库来自引擎（nc_transition_type_*），不在前端写死。
   *
   * 此前用 fetch('/api/transitions') —— 那条路由只在 design 的桌面
   * harness 里存在，Android 端没有 HTTP 服务，必然失败 → 库恒为空，
   * 面板永远显示「转场库读取失败」。改为直调 C ABI。
   */
  const library = [];
  const tc = engineCall('nc_transition_type_count', {});
  const total = (tc && tc.ok && typeof tc.value === 'number') ? tc.value : 0;
  for (let i = 0; i < total; i += 1) {
    const rn = engineCall('nc_transition_type_name', { type: i });
    const name = (rn && rn.ok && typeof rn.value === 'string') ? rn.value : '';
    if (!name) continue;
    const rc = engineCall('nc_transition_category', { type: i });
    const category = (rc && rc.ok && typeof rc.value === 'string') ? rc.value : '';
    library.push({ type: i, name, category });
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
    lib.innerHTML = '<div class="empty-hint" style="font-size:11px">转场库读取失败：nc_transition_type_count 返回 0</div>';
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
    /* 调整图层（P-19）用独立 class：CSS 给斜纹底，视觉上与普通轨道区分。
       齿轮不放这里 —— 时间轴行要响应片段拖拽/选中，按钮放轨道头更稳。 */
    row.className = 'trow'
      + (tr.kind === 'adjustment' ? ' trow-adj' : '')
      + (tr.groupId ? ' trow-grouped' : '');   // 组内成员：左侧竖线（B2-7）
    /* 轨道高度 / 折叠（B2-10）：每轨独立，来自引擎 state.tracks[i].height_px。
       折叠时只留 18px 细条（够放转场标记与拖动把手，仍可点开）。
       范围 24~200 与 core 的 Track::heightPx 一致，这里再夹一次，
       防止旧数据或异常值把轨道画成 0 高度（那样点不到、也不好查）。 */
    const hpx = Math.max(ROW_MIN, Math.min(200, tr.heightPx || ROW_STD));
    const rh = tr.collapsed ? ROW_COLLAPSED : hpx;
    row.style.height = `${rh}px`;
    row.style.marginBottom = `${ROW_GAP}px`;   // 间距随高度等比
    /* 素材内边距也随高度收缩（CSS 用 var(--clip-pad)），
       否则素材被内边距压小、空档却被它撑大。 */
    row.style.setProperty('--clip-pad', `${clipPadFor()}px`);
    row.classList.toggle('collapsed', !!tr.collapsed);
    const body = document.createElement('div');
    body.className = 'tbody';
    if (!tr.collapsed) {
      tr.clips.forEach((c) => body.appendChild(buildClip(tr, c)));
    }
    row.appendChild(body);
    if (!tr.collapsed) renderJunctions(row, tr);   // 折叠时不画转场标记：18px 里放不下
    E.tracks.appendChild(row);
  });

  /* 轨道总高超过可视区 → 允许纵向滚动。
     为什么必须做：.tracks 原本是 overflow:hidden，第 7 条起会被
     **直接裁掉且滚不到**（用户建了轨道却看不见）。阶梯压到 30px 后
     235px 只够 6 条，7 条起必须滚动。 */
  const totalH = ladderTotal();
  E.tracks.classList.toggle('scroll', totalH > ROW_VIEW_H);

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
    /* 轨道头高度与时间轴行严格一致：行高是内联 px，这里写同一个值到
       --fh-h，CSS 用 var(--fh-h, var(--row-h)) 取值。
       折叠时 18px，与 .trow.collapsed 对齐。 */
    const hpx = Math.max(ROW_MIN, Math.min(200, tr.heightPx || ROW_STD));
    const fhReal = tr.collapsed ? ROW_COLLAPSED : hpx;
    g.style.setProperty('--fh-h', `${fhReal}px`);
    /* margin-bottom 必须与 .trow 的 4px 一致，否则每跨一条就累积 4px 错位
       （实测现象：第 2 条起图标比片段低 12px 以上）。 */
    g.style.marginBottom = `${ROW_GAP}px`;
    /* data-h 让 CSS 按行高切换图标尺寸（37/30 各小一档） */
    g.dataset.h = String(tr.collapsed ? ROW_COLLAPSED : hpx);

    const row = document.createElement('div');
    row.className = 'row';

    /* 调整图层的「效果」入口：齿轮。点开直接进效果列表（L3），
       复用现有效果面板，不另做一套参数 UI。 */
    if (tr.kind === 'adjustment') {
      row.appendChild(headBtn('i-gear', false, () => openAdjustmentPanel(tr)));
    }

    const eye = headBtn(tr.hidden ? 'i-eye-off' : 'i-eye', tr.hidden, () =>
      guardedEdit({ op: 'track_flag', track: tr.index, flag: 'hidden', value: !tr.hidden }));
    row.appendChild(eye);

    // 调整图层没有声音，不给喇叭按钮（隐藏功能属于违规，但没有的东西不该占位）
    if (tr.kind !== 'subtitle' && tr.kind !== 'adjustment') {
      const spk = headBtn(tr.muted ? 'i-speaker-off' : 'i-speaker', tr.muted, () =>
        guardedEdit({ op: 'track_flag', track: tr.index, flag: 'muted', value: !tr.muted }));
      row.appendChild(spk);
    }

    const lock = headBtn(tr.locked ? 'i-lock' : 'i-unlock', tr.locked, () =>
      guardedEdit({ op: 'track_flag', track: tr.index, flag: 'locked', value: !tr.locked }));
    row.appendChild(lock);

    /* 折叠箭头已移除（用户 2026-11-03：「最右边不应该有那个小箭头，
       他只有这三个标的悬浮」）。轨道头只剩 眼 / 喇叭 / 锁 三个图标。
       折叠改由「图层」面板的长按或右键入口触发，不再占用轨道头。 */

    attachTrackReorder(row, tr);   // 长按 .row 拖动改轨道顺序
    g.appendChild(row);
    /* 轨道名不再显示（用户 2026-11-03 明确要求）：
       「小眼睛和小喇叭，那是按键，下面不要有文字。它就是一个悬浮的
        三个图标，可以运用到他所属的轨道上面。」
       → 轨道头只留图标行，名称改为长按/悬浮时的 title 提示。
       组标记改为给 .fh 加 class（见下），不再占用一行文字。 */
    const grp = tr.groupId ? (S.groups || []).find((x) => x.id === tr.groupId) : null;
    g.title = tr.name + (grp ? `（组：${grp.name}）` : '');
    if (grp) g.classList.add('grouped');

    /* 高度拖拽把手已移除（用户 2026-11-03：「小眼睛和小喇叭的三个图标，
       只有那三个图标，没有别的」）。
       轨道高度改由时间轴右上角的 − / + 统一控制，避免在 40px 宽的
       轨道头上叠加热区（那里连三个 14px 图标都已经快放不下）。 */

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

/* 设置轨道高度 / 折叠（B2-10）。
   直接走 nc_set_track_view，不经 guardedEdit —— guardedEdit 的 op 是
   editor.js 自己的语义分发（split/delete/track_flag...），
   Track 的视图状态不在那套 op 里，硬塞进去会让分发逻辑变臃肿。

   成功后重新拉状态：高度/折叠是引擎持有的属性，前端不自己改 S.tracks，
   否则撤销之后两边会不一致（撤销走的是引擎栈，前端改的那份不会被回滚）。 */
function setTrackView(tr, heightPx, collapsed) {
  if (!Engine.online) return toast('未连接引擎，无法调整轨道');
  const h = Math.max(ROW_MIN, Math.min(200, Math.round(heightPx)));
  const r = engineCall('nc_set_track_view', {
    track_index: tr.index,
    height_px: h,
    collapsed: collapsed ? 1 : 0
  });
  if (!r || !r.ok) return toast(`调整失败：${(r && r.message) || '未知错误'}`, 3000);
  try {
    applyState(fetchState());
    renderAll();
  } catch (err) {
    toast(`刷新失败：${err.message}`, 3000);
  }
}

/* 给定屏幕 Y，返回命中的轨道头 {track, index}（找不到返回 null）。
 *
 * 用 t.index 而不是数组下标：index 是引擎给的真实轨道序号，
 * S.tracks 的排列顺序理论上与之一致，但一旦某处只做了前端排序、
 * 没走引擎，两者就会错位 —— 按 index 取能把这种不一致暴露成「没高亮」，
 * 而不是悄悄把轨道移到错误位置。
 */
function hitTrackHead(clientY) {
  const gs = E.heads.querySelectorAll('.fh');
  for (const t of S.tracks) {
    const g = gs[t.index];
    if (!g) continue;
    const b = g.getBoundingClientRect();
    if (clientY >= b.top && clientY <= b.bottom) return t;
  }
  return null;
}

/* 长按轨道头拖动排序。
 *
 * 为什么不复用素材区的 beginDrag：那一套算的是「片段在时间轴上的秒数」，
 * 参数是 (tr, c, e)，服务的是 move / move_to_track 两个 op；
 * 轨道排序只关心「落到第几行」，走的是 nc_move_track，两者没有交集。
 * 硬合并会把两套坐标计算和历史语义绞在一起，故独立实现。
 *
 * 手势：按下后 420ms 未移动才武装 —— 与效果列表 fxGesture 的长按时长一致。
 * 未武装就抬手视为普通点击（不拦截），轨道头按钮的 click 照常触发。
 */
function attachTrackReorder(handle, tr) {
  let timer = 0, armed = false, startY = 0, fromIndex = -1;

  const cleanup = () => {
    clearTimeout(timer);
    timer = 0;
    armed = false;
    handle.classList.remove('reordering');
    document.querySelectorAll('.fh.drag-over').forEach((el) => el.classList.remove('drag-over'));
  };

  const onMove = (e) => {
    if (!armed) {
      // 武装前明显移动 → 用户不是要长按排序，取消计时
      if (Math.abs(e.clientY - startY) > 8) cleanup();
      return;
    }
    e.preventDefault();
    // 高亮落点行：只做视觉，真正的下标在 onUp 里按几何重算，
    // 避免拖动中轨道行被重绘导致缓存的元素引用失效
    const hit = hitTrackHead(e.clientY);
    document.querySelectorAll('.fh.drag-over').forEach((el) => el.classList.remove('drag-over'));
    if (hit) {
      const gs = E.heads.querySelectorAll('.fh');
      if (gs[hit.index]) gs[hit.index].classList.add('drag-over');
    }
  };

  const onUp = (e) => {
    window.removeEventListener('pointermove', onMove);
    window.removeEventListener('pointerup', onUp);
    window.removeEventListener('pointercancel', onUp);
    if (!armed) { cleanup(); return; }

    const hit = hitTrackHead(e.clientY);
    cleanup();
    if (!hit || hit.index === fromIndex) return;
    const target = hit.index;

    if (!Engine.online) return toast('未连接引擎，无法调整轨道顺序');
    const r = engineCall('nc_move_track', { track_index: fromIndex, new_index: target });
    if (!r || !r.ok) return toast(`排序失败：${(r && r.message) || '未知错误'}`, 3000);
    try {
      applyState(fetchState());
      renderAll();
    } catch (err) {
      toast(`刷新失败：${err.message}`, 3000);
    }
  };

  handle.addEventListener('pointerdown', (e) => {
    if (e.button !== undefined && e.button !== 0) return;
    // 按在按钮上时交给按钮自己处理，长按不生效
    if (e.target.closest('button')) return;
    startY = e.clientY;
    fromIndex = tr.index;
    clearTimeout(timer);
    timer = setTimeout(() => {
      armed = true;
      handle.classList.add('reordering');
      buzz();
    }, 420);
    window.addEventListener('pointermove', onMove);
    window.addEventListener('pointerup', onUp);
    window.addEventListener('pointercancel', onUp);
  });
}

/* 注意：轨道高度的**拖拽把手已移除**（用户 2026-11-03：轨道头只要
   眼/喇叭/锁三个图标，不要别的东西）。原 attachTrackResize() 一并删除，
   避免留下无调用点的死代码。
   轨道高度改由「阶梯自动压缩」统一管理（见 ladderHeight），
   单轨微调入口在「图层」面板（待接）。 */

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
    /* 胶片条平铺宽度：按轨道高度算，而不是写死 46px。
       为什么：行高压缩到 30 时，46px 宽的图被拉成 46x30（比例失真），
       缩略图糊成一团；按高度等比（高 x 1.5）能让画面比例正确。
       例：行高 44 → 66px；行高 30 → 45px。 */
    const rowH = Math.max(ROW_MIN, Math.min(200, tr.heightPx || ROW_STD));
    fill.style.backgroundSize = `${Math.round(rowH * 1.5)}px 100%`;
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

  /* 拖动中的「禁止态」反馈（P-18 / B0-7）。
     * 先用 nc_track_accepts_media 问内核，而不是等抬手后被内核拒绝 ——
     * 后者是「拖完才报错」，用户已经白做了一次操作。
     * 结果按 (trackType, mediaType) 缓存：一次拖动里同一组合会问很多次。
     */
  const acceptCache = new Map();
  const canDropOn = (track, mediaType) => {
    if (!track) return false;
    const key = `${track.type}:${mediaType}`;
    if (acceptCache.has(key)) return acceptCache.get(key);
    const r = engineCall('nc_track_accepts_media', {
      track_type: track.type, media_type: mediaType
    });
    // 桥不可用时不要谎报「可以」：返回 null 让调用方跳过高亮
    const ok = (r && r.ok && typeof r.result.value === 'number')
      ? r.result.value === 1 : null;
    acceptCache.set(key, ok);
    return ok;
  };

  // 被拖片段所属素材的媒体类型（0=视频 1=音频 2=图片，见 core MediaType）
  const dragMediaType = (() => {
    const a = (S.assets || []).find((x) => x.id === c.assetId);
    return a && typeof a.kind === 'number' ? a.kind : 0;
  })();

  let dropBlocked = false;
  S.dragging = true;   // 全程降质，松手在 onUp 里清

  /// 给某一行打禁止态标记（-1 = 全部清除）。定义在 onMove 之前，
  /// 避免依赖「调用时已初始化」这种隐含时序。
  const markDropBlocked = (trackIndex) => {
    E.tracks.querySelectorAll('.trow').forEach((r, i) => {
      r.classList.toggle('drop-blocked', i === trackIndex);
    });
  };

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

    /* 禁止态：整行红框 + 阻止落到该轨。
       只在真的换了轨时才判（同轨拖动永远允许）。 */
    const verdict = targetTrack.index === trackIndex0
      ? true : canDropOn(targetTrack, dragMediaType);
    dropBlocked = verdict === false;

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
    /* renderTracks() 会重建所有 .trow，上一步加的禁止态会被一起抹掉，
       所以必须在重绘**之后**再标记一次。 */
    markDropBlocked(dropBlocked ? targetTrack.index : -1);
  };


  const onUp = async () => {
    window.removeEventListener('pointermove', onMove);
    window.removeEventListener('pointerup', onUp);
    hideSnapLine();
    /* 拖动结束：撤掉降质标记并立刻重拉一帧高清。
       只改标记不重拉的话，画面会一直停在低清那帧，
       直到下次 tick 才刷新 —— 用户会觉得"松手后糊了一下"。 */
    S.dragging = false;
    syncPreview();
    if (!moved) return;

    // 清掉禁止态
    E.tracks.querySelectorAll('.drop-blocked').forEach((r) => r.classList.remove('drop-blocked'));

    /* 落到不相容轨道：不写引擎，直接告诉用户为什么。
       拿内核给的原因（nc_track_placement_reason）而不是前端自己编一句，
       这样规则改了不用同步改前端文案。 */
    if (dropBlocked) {
      const rr = engineCall('nc_track_placement_reason', {
        track_type: targetTrack.type, media_type: dragMediaType
      });
      const why = (rr && rr.ok && rr.result && rr.result.reason)
        ? rr.result.reason : '该轨道不接受这种素材';
      toast(`${targetTrack.name}：${why}`, 3000);
      applyState(Engine.state || S);
      renderAll();
      return;
    }

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

/* ---------------- 播放控制 ----------------
   性能约束（用户要求「丝滑无延迟」）：
   原来 tick 每帧调 renderTracks()，而 renderTracksBody 会：
     清空所有 .trow → 重建每条轨道 → 为每个片段重建 DOM（含缩略图 <img>）
     再 renderHeads() + drawRuler() + updateStrip()
   播放 60fps 时等于每秒重建 60 次整条时间轴。缩略图要重新解码，
   内存与布局抖动叠加，观感就是卡、播放头读数和画面不同步。

   现在把「每帧要做的事」压到最小：
     · 时间码：直接改 textContent（一次字符串赋值）
     · 播放头：时间轴是「播放头固定屏幕中央、内容滚动」模型，
       故只需让内容偏移跟随，不移动播放头元素
     · 刻度：只在跨过主刻度边界时重画
     · 片段：只在真正需要时才（节流）重建
   全量重建仍保留给编辑操作（那里必须反映结构变化）。
*/

let raf = 0;
let last = 0;
let lastTickSec = -1;      // 上次刷新刻度的整数秒
let tickFrame = 0;
let lastFullRender = 0;    // 上次全量重建的时间戳

function setPlaying(on) {
  S.playing = on;
  E.play.innerHTML = `<svg class="i"><use href="#i-${on ? 'pause' : 'play'}"/></svg>`;
  if (on) {
    last = 0;
    lastTickSec = -1;
    raf = requestAnimationFrame(tick);
  } else {
    cancelAnimationFrame(raf);
    // 停止时做一次完整同步，保证最终状态精确
    renderTracks();
    syncPreview();
  }
}

function tick(ts) {
  if (!S.playing) return;
  if (!last) last = ts;
  S.time += (ts - last) / 1000;
  last = ts;

  const end = seqEnd();
  if (end > 0 && S.time >= end) {
    S.time = end;
    setPlaying(false);
    return;
  }

  // ---- 轻量更新（每帧）----
  updateStrip();                       // 时间码 + 播放进度条

  // 刻度：只在秒数变化时重画（drawRuler 要清 canvas 并重排 label）
  const sec = Math.floor(S.time);
  if (sec !== lastTickSec) {
    lastTickSec = sec;
    drawRuler();
  }

  // 时间轴内容跟随：整体平移，不重建节点
  scrollTimelineToTime();

  // 画面：隔帧取，减轻桥与 PNG 编码压力（30fps 画面 / 60fps 时间码）
  tickFrame++;
  if ((tickFrame & 1) === 0) syncPreview();

  // 兜底：每 1.5s 做一次全量重建，修正轻量更新可能的漂移
  //（例如片段在播放中因其他原因改变）
  if (ts - lastFullRender > 1500) {
    lastFullRender = ts;
    renderTracks();
  }

  raf = requestAnimationFrame(tick);
}

/* 时间轴内容跟随播放头：所有片段与刻度的 x 都来自 t2x()，
   它已经以 S.time 为中心。这里只需把每个片段的 left 更新为新的 t2x，
   避免重建 DOM。找不到片段时静默跳过（下次全量重建会补齐）。*/
function scrollTimelineToTime() {
  const rows = E.tracks.querySelectorAll('.trow');
  for (let i = 0; i < rows.length && i < S.tracks.length; i++) {
    const tr = S.tracks[i];
    const clipEls = rows[i].querySelectorAll('.clip');
    for (let j = 0; j < clipEls.length && j < tr.clips.length; j++) {
      const c = tr.clips[j];
      const x = t2x(c.start);
      const w = Math.max(2, c.dur * S.pxPerSec);
      const el = clipEls[j];
      el.style.left = `${x}px`;
      el.style.width = `${w}px`;
      /* 阈值必须与 buildClip 完全一致（±30px 余量），
         否则轻量更新与全量重建会来回切换 visibility，边缘出现闪烁。 */
      const vis = !(x + w < -30 || x > viewW() + 30);
      const want = vis ? 'visible' : 'hidden';
      if (el.style.visibility !== want) el.style.visibility = want;
    }
  }
  // 播放头读数的横向位置由 .ph-read 居中在播放头上，无需跟随。
}
E.play.addEventListener('click', () => setPlaying(!S.playing));

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
  /* 1. 添加素材 */
  if (act === 'import') return openSheet();
  /* 2. 返回首页（退出当前工程回项目列表） */
  if (act === 'home') return goHome();
  /* 3. 向左逐段跳（双击 → 直接回到 00:00）
     实现要点：双击必然会先派发一次 click，若在第一次 click 就执行
     逐段跳，双击的效果会变成「先退一段再回零」——中间闪一下。
     所以单击延迟 220ms 执行；期间若来第二次点击就取消单击、执行回零。
     220ms 是 iOS/Android 双击判定的常用上限，再长点击会明显发钝。 */
  if (act === 'step-prev') {
    if (stepPrevTimer) {
      clearTimeout(stepPrevTimer);
      stepPrevTimer = 0;
      b.classList.remove('on');
      return jumpToStart();
    }
    b.classList.add('on');
    stepPrevTimer = setTimeout(() => {
      stepPrevTimer = 0;
      b.classList.remove('on');
      stepPrev();
    }, 220);
    return;
  }
  /* 4. 标记点 */
  if (act === 'marker') {
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
  /* 5. 渲染（预览缓存） */
  if (act === 'render') return toggleRender(b);
  /* 6. 关键帧面板 */
  if (act === 'keyframes') return showKeyframeSheet();
  /* 7. 参考线 */
  if (act === 'guides') return toggleGuides(b);
  /* 8. 图片与背景 */
  if (act === 'images') return showImageSheet();
  /* 9. AI 仿剪 */
  if (act === 'ai') return showAiSheet();
  /* 10 / 11. 撤销 / 重做 */
  if (act === 'undo') return guardedEdit({ op: 'undo' });
  if (act === 'redo') return guardedEdit({ op: 'redo' });
  toast(`${b.getAttribute('aria-label') || act}：尚未绑定实现`);
});

/* ---------------- 2. 返回首页 ----------------
   为什么用确认框：编辑页可能还有未导出的改动，
   直接跳走会让用户以为工程丢了。有未保存改动时必须问一次。 */
function goHome() {
  const dirty = !!(S.project && (S.project.can_undo || S.project.can_redo));
  if (dirty && !window.confirm('当前工程有未导出的修改，确定返回首页？')) return;
  /* 不在这里调 bridge.release()：引擎句柄由 MainActivity 持有，
     页面跳转不会销毁 Activity（只是换了 URL），提前 release 会让
     新页面拿不到引擎。真正的释放交给 onDestroy。 */
  location.href = '01-home.html';
}

/* ---------------- 3. 向左逐段跳 ----------------
   用户定义的语义：当前停在本段 → 跳到本段开头 → 再点跳到前一段结尾
   → 再点跳到前一段开头 → 以此类推。
   实现要点：终点集合是「所有片段的 start 与 end」，去重后从大到小；
   每次都跳到当前时间之前最近的一个端点。这与「一直跳到 0」不同，
   用户能明确停在前一段的结尾（编辑接缝时常要这个位置）。 */
/* 单击/双击判定用的定时器 id（0 = 无待决单击）。 */
let stepPrevTimer = 0;

function timelineAnchors() {
  const pts = new Set([0]);
  allClips().forEach((c) => {
    pts.add(Math.round((c.start || 0) * 1e6));
    pts.add(Math.round(((c.start || 0) + (c.dur || 0)) * 1e6));
  });
  return Array.from(pts).sort((a, b) => a - b);
}

function stepPrev() {
  const nowUs = Math.round(S.time * 1e6);
  const anchors = timelineAnchors();
  // 严格小于当前时间（留 1ms 容差，避免同一个点反复命中）
  let target = null;
  for (let i = anchors.length - 1; i >= 0; i--) {
    if (anchors[i] < nowUs - 1000) { target = anchors[i]; break; }
  }
  if (target === null) return toast('已经在最开头');
  setPlaying(false);
  seekToLight(target / 1e6);
}

/* 回到时间轴最开头（00:00 / 首帧）。
   用户要求：单击第 3 个按钮 = 逐段后退，双击 = 直接跳回起点。 */
function jumpToStart() {
  if (S.time <= 0.0005) {
    // 已经在开头：给一次轻反馈，避免用户以为按钮失灵
    return toast('已在起始位置');
  }
  // 先停（setPlaying(false) 自带一次完整同步），再落位。
  // 不能 setPlaying(false) + seekTo() 都全量重建 —— 双击时两次
  // 重建叠加会明显掉帧，正是用户要避免的「不丝滑」。
  setPlaying(false);
  if (S.playing) return;      // 理论上不会到这里，防御
  seekToLight(0);
  toast('已回到 00:00');
}

/* seekTo 的轻量版：只更新播放头读数、刻度、画面与片段位置，
   不销毁重建轨道 DOM（逐段跳/回零时结构没变，重建是浪费）。
   编辑操作仍走 renderTracks()。 */
function seekToLight(sec) {
  S.time = Math.max(0, sec);
  updateStrip();
  drawRuler();
  scrollTimelineToTime();
  syncPreview();
}

/* ---------------- 5. 渲染（预览缓存预热）----------------
   背景：core 原先没有帧缓存——每帧都 decode + 尺寸适配 + 特效栈，
   任何编辑还会把渲染器整个重建（连解码器一起丢）。所以「按预览」
   在真机上一直是卡的。

   本轮的 core 改动（core/include/nairoclip/core/preview.h）：
     · readers_ 按素材复用：同一素材只 open 一次
     · frames_ 是 LRU 帧缓存（容量 12 帧），命中直接返回
     · 编辑后只清帧缓存、保留解码器（不重建渲染器）
     · 新增 nc_preview_cache_stats 暴露命中率

   这个按钮现在做的是**真实预热**：按时间轴顺序把帧灌进缓存，
   让后续拖动/播放直接命中。进度取自真实完成帧数，
   结果展示真实命中率 —— 不再有编造的百分比。

   为什么仍是「预热」而不是后台常驻预渲染：
     缓存有容量上限（12 帧，约 100MB），长工程装不下全部帧。
     真正的全时间轴预渲染需要落盘缓存（帧 → 磁盘），
     那是另一个量级的改动，不在本轮范围。 */

let renderJob = null;

/// 预热采样帧数：按 10fps 覆盖整条时间轴，上限 240 帧。
/// 上限存在的原因：帧缓存只有 12 帧，超过它的预热对「命中率」帮助很小，
/// 但会实实在在占用 CPU 与桥调用时间。
function renderFrameCount() {
  const end = seqEnd();
  if (!end) return 0;
  return Math.min(240, Math.max(1, Math.round(end * 10)));
}

/// 读取引擎侧缓存统计（失败返回 null，不伪造数字）。
function readCacheStats() {
  const r = engineCall('nc_preview_cache_stats', {});
  if (!r || !r.ok || !r.result) return null;
  return r.result;
}

async function toggleRender(btn) {
  if (renderJob) {                       // 正在渲染 → 取消
    renderJob.cancelled = true;
    renderJob = null;
    btn.classList.remove('busy', 'on');
    if (E.stripRender) E.stripRender.style.width = '0%';
    return toast('已取消预热');
  }
  if (!Engine.online) return toast('未连接引擎，无法预热');

  const total = renderFrameCount();
  if (!total) return toast('时间轴为空，没有可预热的片段');

  const before = readCacheStats();
  btn.classList.add('busy', 'on');
  const job = { cancelled: false };
  renderJob = job;
  const end = seqEnd();
  let done = 0;
  let fails = 0;
  const t0 = performance.now();

  // 从播放头之后开始，绕一圈回到播放头：优先预热「接下来要看」的帧，
  // 用户一按播放立刻有画面。
  const startUs = Math.round(S.time * 1e6);
  const spanUs = Math.round(end * 1e6);

  for (let i = 0; i < total; i++) {
    if (job.cancelled) break;
    const offset = Math.round((spanUs * i) / total);
    const us = (startUs + offset) % Math.max(1, spanUs);
    const url = Engine.frameUrl(us, NEED.frameW);
    if (!url) fails++;
    done++;
    if ((i & 3) === 0) {
      if (E.stripRender) E.stripRender.style.width = `${(done / total) * 100}%`;
      /* 圆钮里的实时进度条：只改一个 CSS 变量，避免每帧重排。
         E.renderBtn 指向 .c-render（找不到就跳过，不影响功能）。 */
      if (E.renderBtn) {
        E.renderBtn.style.setProperty('--render-progress', String(done / total));
      }
      // 让出主线程，否则 UI 卡死看不到进度（也不响应取消）
      await new Promise((r) => setTimeout(r, 0));
    }
  }

  if (renderJob === job) renderJob = null;
  btn.classList.remove('busy');
  if (job.cancelled) {
    if (E.stripRender) E.stripRender.style.width = '0%';
    return;
  }

  const secs = ((performance.now() - t0) / 1000).toFixed(1);
  if (E.stripRender) setTimeout(() => { E.stripRender.style.width = '0%'; }, 700);
  btn.classList.add('on');
  setTimeout(() => btn.classList.remove('on'), 600);

  // 报告真实数字：完成帧数、耗时、引擎侧命中统计
  const after = readCacheStats();
  const parts = [`已预热 ${done} 帧（${secs}s）`];
  if (fails > 0) parts.push(`${fails} 帧无画面`);
  if (after) {
    const hits = Number(after.frame_hits || 0) - Number((before && before.frame_hits) || 0);
    const miss = Number(after.frame_misses || 0) - Number((before && before.frame_misses) || 0);
    const tot = hits + miss;
    if (tot > 0) parts.push(`命中 ${hits}/${tot}（${Math.round((hits / tot) * 100)}%）`);
    parts.push(`缓存 ${after.cached_frames}/12 帧`);
  }
  toast(parts.join(' · '), 4000);
}

/* ---------------- 6. 关键帧面板 ----------------
   读取当前播放头所在片段在 5 个属性通道上的全部关键帧，
   可逐个删除。属性映射来自 capi.h:543 的文档附录二：
     不透明度=0 缩放=1 位置X=2 位置Y=3 旋转=4 */
const KF_PROPS = [
  { p: 0, name: '不透明度' },
  { p: 1, name: '缩放' },
  { p: 2, name: '位置 X' },
  { p: 3, name: '位置 Y' },
  { p: 4, name: '旋转' }
];

function showKeyframeSheet() {
  const sel = selClip();
  E.sheetList.innerHTML = '';
  if (!sel) {
    sheetRow('未选中片段', '把播放头停在某个片段上，或先点选片段', () => closeSheet());
    E.sheet.hidden = false;
    return;
  }
  if (!Engine.online) {
    sheetRow('未连接引擎', '关键帧读取需要真实引擎', () => closeSheet());
    E.sheet.hidden = false;
    return;
  }

  /* 添加关键帧：原圆按钮的「打点」能力不能因为按钮改成面板而丢失，
     所以面板顶部给一个显式入口。属性可选（默认不透明度）。 */
  const addRow = document.createElement('div');
  addRow.className = 'sheet-hint kf-addbar';
  const lbl = document.createElement('span');
  lbl.style.flex = '1';
  lbl.textContent = '在当前播放头添加';
  addRow.appendChild(lbl);

  const propSel = document.createElement('select');
  propSel.className = 'kf-prop';
  KF_PROPS.forEach(({ p, name }) => {
    const o = document.createElement('option');
    o.value = String(p);
    o.textContent = name;
    propSel.appendChild(o);
  });
  addRow.appendChild(propSel);

  const addBtn = document.createElement('button');
  addBtn.className = 'kf-add';
  addBtn.textContent = '添加';
  addBtn.addEventListener('click', async () => {
    const p = Number(propSel.value);
    const local = S.time - sel.clip.start;
    if (local < 0 || local > sel.clip.dur) return toast('播放头不在该片段范围内');
    // 取值范围随属性而变：不透明度/缩放给 1（无变化），位移/旋转给 0
    const val = (p === 0 || p === 1) ? 1 : 0;
    const ok = await guardedEdit({
      op: 'keyframe', track: sel.track.index, clip: sel.clip.id,
      property: p, time_us: Math.round(local * 1e6), value: val
    }, '已添加关键帧');
    if (ok) showKeyframeSheet();
  });
  addRow.appendChild(addBtn);
  E.sheetList.appendChild(addRow);

  let total = 0;
  KF_PROPS.forEach(({ p, name }) => {
    const r = engineCall('nc_keyframe_count', {
      track_index: sel.track.index, clip_id: sel.clip.id, property: p
    });
    // C++ 侧 Ok() 把载荷放进 result：{"ok":true,"result":{"count":N}}
    const n = (r && r.ok && r.result && typeof r.result.count === 'number')
      ? r.result.count : 0;
    if (!n) return;
    total += n;

    const head = document.createElement('div');
    head.className = 'sheet-hint';
    head.textContent = `${name}（${n}）`;
    E.sheetList.appendChild(head);

    for (let i = 0; i < n; i++) {
      const g = engineCall('nc_keyframe_get', {
        track_index: sel.track.index, clip_id: sel.clip.id,
        property: p, index: i
      });
      const res = (g && g.result) || g || {};
      const tUs = Number(res.time_us ?? res.out_time_us ?? 0);
      const val = Number(res.value ?? res.out_value ?? 0);
      const row = sheetRow(
        `${fmt(tUs / 1e6)}`,
        `值 ${val.toFixed(3)}`,
        () => {}
      );
      // 删除按钮（模态内二次确认，避免误删）
      const del = document.createElement('button');
      del.className = 'kf-del';
      del.textContent = '删除';
      del.addEventListener('click', async (ev) => {
        ev.stopPropagation();
        if (!window.confirm(`删除 ${name} 在 ${fmt(tUs / 1e6)} 的关键帧？`)) return;
        const ok = await guardedEdit({
          op: 'remove_keyframe', track: sel.track.index, clip: sel.clip.id,
          property: p, time_us: tUs
        }, '已删除关键帧');
        if (ok) showKeyframeSheet();
      });
      row.appendChild(del);
    }
  });

  if (!total) {
    sheetRow('该片段没有关键帧', '用底部工具栏的「关键帧」来添加', () => closeSheet());
  }
  const headEl = E.sheet && E.sheet.querySelector('.sheet-head span');
  if (headEl) headEl.textContent = `关键帧（${total}）`;
  E.sheet.hidden = false;
}

/* ---------------- 7. 参考线（# 网格）----------------
   纯前端叠加，不改工程数据，因此不产生撤销记录。
   状态存在 S 里，重绘时保持。 */
function toggleGuides(btn) {
  S.guides = !S.guides;
  btn.classList.toggle('on', S.guides);
  let layer = document.getElementById('guide-layer');
  if (!S.guides) {
    if (layer) layer.remove();
    return;
  }
  if (!layer) {
    layer = document.createElement('div');
    layer.id = 'guide-layer';
    layer.className = 'guide-layer';
    // 三分线 + 中心十字：与圆工具行的 # 图标一致
    layer.innerHTML =
      '<i style="left:33.333%"></i><i style="left:66.666%"></i>' +
      '<i class="h" style="top:33.333%"></i><i class="h" style="top:66.666%"></i>' +
      '<i class="cx"></i><i class="cy"></i>';
    E.canvas.appendChild(layer);
  }
}

/* ---------------- 8. 图片与背景 ----------------
   两件事合一个面板：
   · 列出项目里所有图片素材（type=2），可删除；
   · 设置画布背景色（nc_set_background_color）。
   为什么合并：用户说的这个按钮就是「添加图片 + 添加背景（颜色）」。 */
function showImageSheet() {
  E.sheetList.innerHTML = '';
  const images = (S.assets || []).filter((a) => a.kind === 2);

  sheetRow('添加图片', '从相册/文件选择', () => { closeSheet(); openSheet(); });

  if (!images.length) {
    sheetRow('项目里还没有图片', '导入类型选「图片」', () => {});
  } else {
    images.forEach((a) => {
      const row = sheetRow(a.name, `图片 · ${a.dur.toFixed(1)}s`, () => {});
      const del = document.createElement('button');
      del.className = 'kf-del';
      del.textContent = '删除';
      del.addEventListener('click', async (ev) => {
        ev.stopPropagation();
        const refd = engineCall('nc_asset_is_referenced', { asset_id: a.id });
        // 桥统一把出参包成 result 对象（{referenced, value}），
        // 直接比较 result === 1 永远不成立，会放过“已被引用”的素材。
        // 这里兼容三种形态：裸数值 / {value} / {referenced}。
        const refVal = (refd && refd.result !== null && typeof refd.result === 'object')
          ? (refd.result.referenced !== undefined ? refd.result.referenced : refd.result.value)
          : (refd ? (refd.result !== undefined ? refd.result : refd.value) : undefined);
        const isRef = Number(refVal) === 1;
        if (Number(refVal) < 0 || !(refd && refd.ok !== false)) {
          return toast(`无法确认引用状态：${(refd && refd.message) || '引擎未返回'}`, 3000);
        }
        if (isRef) return toast('该图片已被时间轴引用，请先删除片段', 3000);
        if (!window.confirm(`删除图片「${a.name}」？`)) return;
        const r = engineCall('nc_remove_asset', { asset_id: a.id });
        if (!r || !r.ok) return toast(`删除失败：${(r && r.message) || '引擎拒绝'}`, 3000);
        try { applyState(fetchState()); renderAll(); } catch (_) {}
        showImageSheet();
      });
      row.appendChild(del);
    });
  }

  /* 背景色：8 个预设，写入引擎（画布底色，不是片段背景） */
  const head = document.createElement('div');
  head.className = 'sheet-hint';
  head.textContent = '画布背景色';
  E.sheetList.appendChild(head);

  const BG = [
    ['纯黑', 0x000000FF], ['纯白', 0xFFFFFFFF], ['深灰', 0x1C1C1EFF],
    ['米白', 0xF5F1EAFF], ['雾蓝', 0x2E3A4CFF], ['墨绿', 0x1F3A32FF],
    ['酒红', 0x3A1F28FF], ['暖沙', 0xE8DCC8FF]
  ];
  const grid = document.createElement('div');
  grid.className = 'bg-grid';
  BG.forEach(([name, rgba]) => {
    const sw = document.createElement('button');
    sw.className = 'bg-sw';
    sw.title = name;
    // 引擎收的是 RGBA，按 0xRRGGBBAA 打包；CSS 用 #RRGGBB
    const hex = '#' + (rgba >>> 8).toString(16).padStart(6, '0');
    sw.style.background = hex;
    sw.addEventListener('click', () => {
      const r = engineCall('nc_set_background_color', { rgba: rgba >>> 0 });
      if (!r || !r.ok) return toast(`设置失败：${(r && r.message) || '引擎拒绝'}`, 3000);
      try { applyState(fetchState()); renderAll(); } catch (_) {}
      toast(`背景已设为${name}`);
      closeSheet();
    });
    grid.appendChild(sw);
  });
  E.sheetList.appendChild(grid);

  const h2 = E.sheet && E.sheet.querySelector('.sheet-head span');
  if (h2) h2.textContent = `图片与背景（${images.length}）`;
  E.sheet.hidden = false;
}

/* ---------------- 9. AI 仿剪 ----------------
   用 nc_ai_feature_access(feature=9) 做真实门控：
   - 未登录 → 引导登录；需 Pro → 引导升级；都满足 → 开始任务。
   本面板只做入口与门控，真正的推理由平台层执行
   （core 只提供任务簿记状态机，不伪造进度）。 */
function showAiSheet() {
  E.sheetList.innerHTML = '';
  const FEATURE_REFERENCE_EDIT = 9;   // AiFeatureType::ReferenceEdit

  const loggedIn = !!(window.NCAndroid && window.NCAndroid.isLoggedIn &&
                      window.NCAndroid.isLoggedIn());
  const r = engineCall('nc_ai_feature_access', {
    feature: FEATURE_REFERENCE_EDIT, logged_in: loggedIn ? 1 : 0
  });
  const res = (r && r.result) || {};
  // nc_ai_feature_access 返回 1=可用 0=不可用 -1=参数错误，
  // C++ 侧字段名是 available（见 nc_jni.cpp 的组装）。
  const allowed = !!(r && r.ok && res.available === 1);
  const reason = res.reason || (r && r.message) || '';
  const needsPro = !!(res.requires_pro === 1);

  const row = sheetRow(
    '参考仿剪',
    allowed ? '可用 · 选参考视频后自动生成同风格剪辑'
            : (needsPro ? '需要 Pro 会员' : (loggedIn ? '暂不可用' : '需要登录')),
    () => {
      if (!allowed) {
        if (!loggedIn) { closeSheet(); location.href = '03-auth.html'; return; }
        return toast(reason || '需要 Pro 会员', 3000);
      }
      closeSheet();
      // 仿剪的实际入口在底部工具栏「更多 → AI」，那里有完整流程
      toast('请在「更多 → AI」里选择参考视频开始仿剪', 3500);
    }
  );
  const badge = document.createElement('span');
  badge.className = 'mk-badge' + (allowed ? ' ok' : '');
  badge.textContent = allowed ? '可用' : (needsPro ? 'PRO' : '—');
  row.appendChild(badge);

  const headEl = E.sheet && E.sheet.querySelector('.sheet-head span');
  if (headEl) headEl.textContent = 'AI';
  E.sheet.hidden = false;
}

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
      /* 调整图层没有片段，写「0 片段」会让人误以为坏了；
         它作用于下方 N 条轨道，这才是用户需要知道的信息。 */
      const below = (S.tracks || []).filter((x) => x.index > t.index).length;
      const kindText = t.kind === 'audio' ? '音频'
        : t.kind === 'subtitle' ? '字幕'
        : t.kind === 'adjustment' ? `调整图层 · 作用于下方 ${below} 轨`
        : '视频';
      const desc = `${kindText} · ${t.clips.length} 片段` +
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
  /* 已有轨道组：每组一行，显示组名与联动开关（B2-7）。
     放在轨道列表之后、新增入口之前 —— 组是对「已有轨道」的归纳，
     排在它们后面读起来才顺。 */
  (S.groups || []).forEach((g) => {
    const row = document.createElement('div');
    row.className = 'layer-row layer-group';
    const members = (S.tracks || []).filter((t) => t.groupId === g.id);
    const anyHidden = members.some((t) => t.hidden);
    const anyMuted = members.some((t) => t.muted);
    row.innerHTML =
      `<div class="layer-main"><b>▏${g.name}</b>` +
      `<small>组 · ${members.length} 轨联动</small></div>` +
      `<div class="layer-acts">` +
      `<button data-g="hidden" aria-pressed="${anyHidden}">` +
      `<svg class="i"><use href="#${anyHidden ? 'i-eye-off' : 'i-eye'}"/></svg></button>` +
      `<button data-g="muted" aria-pressed="${anyMuted}">` +
      `<svg class="i"><use href="#${anyMuted ? 'i-speaker-off' : 'i-speaker'}"/></svg></button>` +
      `<button data-g="ungroup">解组</button>` +
      `</div>`;
    row.querySelectorAll('button[data-g]').forEach((btn) => {
      btn.addEventListener('click', async (ev) => {
        ev.stopPropagation();
        const what = btn.dataset.g;
        if (what === 'ungroup') {
          await removeTrackGroup(g.id);
          closeSheet(); showLayerSheet();
          return;
        }
        const next = btn.getAttribute('aria-pressed') !== 'true';
        await setGroupFlag(g.id, what, next);
        closeSheet(); showLayerSheet();
      });
    });
    E.sheetList.appendChild(row);
  });

  /* 未分组的轨道可以多选成组。用 prompt 收组名 —— 与既有「轨道音量/重命名」
     的做法一致，不额外造一套弹窗。 */
  const ungrouped = (S.tracks || []).filter((t) => !t.groupId);
  if (ungrouped.length >= 2) {
    const gRow = document.createElement('div');
    gRow.className = 'layer-row layer-add';
    gRow.innerHTML = '<div class="layer-main"><b>＋ 组成轨道组</b>' +
      '<small>把未分组的轨道合成一组，联动静音/隐藏</small></div>';
    gRow.addEventListener('click', async () => {
      closeSheet();
      const input = window.prompt(
        `要成组的轨道下标（逗号分隔，可选：${ungrouped.map((t) => t.index).join(',')}）`,
        ungrouped.map((t) => t.index).join(','));
      if (input === null) return;
      const idxs = input.split(',')
        .map((x) => Number(x.trim()))
        .filter((x) => Number.isInteger(x) && x >= 0);
      if (!idxs.length) return toast('没有有效的轨道下标');
      const nm = window.prompt('组名', '');
      await createTrackGroup(idxs, nm === null ? '' : nm.trim());
      showLayerSheet();
    });
    E.sheetList.appendChild(gRow);
  }

  /* 新增入口：调整图层。放在列表末尾，与「已有轨道」分开，
     避免被误认成某条轨道的操作。 */
  const addRow = document.createElement('div');
  addRow.className = 'layer-row layer-add';
  addRow.innerHTML = '<div class="layer-main"><b>＋ 调整图层</b>' +
    '<small>作用于下方所有轨道：可加效果与 LUT</small></div>';
  addRow.addEventListener('click', async () => {
    const ok = await addAdjustmentTrack();
    if (ok) { closeSheet(); showLayerSheet(); }
  });
  E.sheetList.appendChild(addRow);

  const head = E.sheet && E.sheet.querySelector('.sheet-head span');
  if (head) head.textContent = `图层（${tracks.length} 轨）`;
  E.sheet.hidden = false;
}

/* ---------------- 「转换」= 移动/变换类效果（效果 L2）----------------
 *
 * 与「画面特效」的区别：转换改的是**几何位置/形状**（位移、旋转、缩放、
 * 沿路径），不重新着色；画面特效是重新计算像素颜色。
 *
 * 内核能力现状（重要，直接影响能不能用）：
 *   已实现：摆动（Shake 六维参数）、随机抖动（Jitter=31）、
 *           自动摇动（Shake Random 轨迹）、振荡（Shake Circular 轨迹）
 *   未实现：偏移/拉伸轴/摇摆/旋转/栅格转换/沿路径移动/缩放辅助/脉冲大小
 *
 * 未实现的项在面板里明确标「待接入」，点击给出说明，**不做假按钮** ——
 * 点了没反应比灰掉更糟。等内核补齐后把 xfReady 改成 true 即可。
 */
const XFORM_ITEMS = [
  /* 下列 ready 的走真实 nc_set_clip_shake；其余等内核 */
  { key: 'offset',    name: '偏移',       en: 'Offset',           ready: true, xform: true },
  { key: 'stretch',   name: '拉伸轴',     en: 'Stretch Axis',     ready: true, xform: true },
  { key: 'oscillate', name: '振荡',       en: 'Oscillate',        ready: true,
    shake: { trajectory: 2, amplitude: 0.03, frequency: 6, phase: 0 } },
  { key: 'swing',     name: '摆动',       en: 'Swing',            ready: true,
    shake: { trajectory: 0, amplitude: 0.02, frequency: 8, phase: 0 } },
  { key: 'spin',      name: '旋转',       en: 'Spin',             ready: true, xform: true },
  { key: 'raster',    name: '栅格转换',   en: 'Raster Transform', ready: true, xform: true },
  { key: 'path',      name: '沿路径移动', en: 'Move Along Path',  ready: true, xform: true },
  { key: 'scale',     name: '缩放辅助',   en: 'Scale Assist',     ready: true, xform: true },
  { key: 'pulse',     name: '脉冲大小',   en: 'Pulse Size',       ready: true, xform: true },
  { key: 'autoshake', name: '自动摇动',   en: 'Auto-Shake',       ready: true,
    shake: { trajectory: 3, amplitude: 0.025, frequency: 12, phase: 0 } },
  { key: 'randpos',   name: '随机位移',   en: 'Random Displacement', ready: true, xform: true },
  { key: 'jitter',    name: '随机抖动',   en: 'Random Jitter',    ready: true,
    effect: 31 }   /* EffectType::Jitter */
];

/* 缩略示意：用 CSS 画同色系图形，不引外部图片。
   参考图里的桃红/橙色在深色主题下刺眼，统一改用 --accent-pick 与
   --accent 的低饱和变体，保持"同色系示意"的信息量而不抢视觉。 */
function xformThumb(key) {
  const A = 'var(--accent-pick)';
  const B = 'var(--accent)';
  const box = 'position:absolute;inset:0;display:grid;place-items:center';
  const shapes = {
    offset:    `<div style="${box}"><div style="width:22px;height:22px;border-radius:5px;background:${A};opacity:.35;transform:translate(-6px,-6px)"></div><div style="width:22px;height:22px;border-radius:5px;background:${A};transform:translate(6px,6px);position:absolute"></div></div>`,
    stretch:   `<div style="${box}"><div style="width:34px;height:16px;border-radius:50%;background:${A}"></div></div>`,
    oscillate: `<div style="${box}"><div style="width:20px;height:20px;border-radius:50%;background:${A};transform:translateX(-7px)"></div><div style="width:20px;height:20px;border-radius:50%;background:${A};opacity:.35;transform:translateX(7px);position:absolute"></div></div>`,
    swing:     `<div style="${box}"><div style="width:2px;height:18px;background:${A};transform-origin:top;transform:rotate(24deg)"></div><div style="width:14px;height:14px;border-radius:50%;background:${B};position:absolute;transform:translate(9px,9px)"></div></div>`,
    spin:      `<div style="${box}"><div style="width:26px;height:26px;border-radius:50%;border:2px dashed ${A};border-top-color:transparent"></div></div>`,
    raster:    `<div style="${box}"><div style="display:grid;grid-template-columns:repeat(3,8px);gap:2px">${'<i style="width:8px;height:8px;background:'+A+';opacity:.5;display:block"></i>'.repeat(9)}</div></div>`,
    path:      `<div style="${box}"><div style="width:30px;height:18px;border:1.5px dashed ${A};border-radius:50%;opacity:.5"></div><div style="width:9px;height:9px;border-radius:50%;background:${A};position:absolute;transform:translate(14px,-8px)"></div></div>`,
    scale:     `<div style="${box}"><div style="width:16px;height:16px;border-radius:4px;background:${A};opacity:.35"></div><div style="width:24px;height:24px;border-radius:4px;border:2px solid ${A};position:absolute"></div></div>`,
    pulse:     `<div style="${box}"><div style="width:26px;height:26px;border-radius:50%;background:${A}"></div><div style="width:36px;height:36px;border-radius:50%;border:2px solid ${A};opacity:.3;position:absolute"></div></div>`,
    autoshake: `<div style="${box}"><div style="width:22px;height:22px;border-radius:5px;background:${A}"></div><div style="width:22px;height:22px;border-radius:5px;border:1.5px solid ${A};opacity:.35;position:absolute;transform:translate(-5px,4px)"></div></div>`,
    randpos:   `<div style="${box}"><div style="width:14px;height:14px;border-radius:50%;background:${A}"></div><div style="width:8px;height:8px;border-radius:50%;background:${B};position:absolute;transform:translate(-13px,-9px)"></div><div style="width:6px;height:6px;border-radius:50%;background:${A};opacity:.5;position:absolute;transform:translate(13px,10px)"></div></div>`,
    jitter:    `<div style="${box}"><div style="width:20px;height:20px;border-radius:4px;background:${A};filter:blur(.6px);transform:rotate(8deg)"></div></div>`
  };
  return shapes[key] || '';
}

async function openTransformPanel() {
  if (!Engine.online) return toast('未连接引擎');
  const sel = S.selected;
  if (!sel) return toast('请先选中一个片段');

  /* 宫格。未接入的项加 xf-soon 类并显示"待接入"角标，
     点击时说明原因，而不是静默无反应。 */
  const body = document.createElement('div');
  const grid = document.createElement('div');
  grid.className = 'xf-grid';
  XFORM_ITEMS.forEach((it) => {
    const b = document.createElement('button');
    b.className = 'xf-item' + (it.ready ? '' : ' xf-soon');
    b.innerHTML =
      `<span class="xf-thumb">${xformThumb(it.key)}</span>` +
      `<span class="xf-name">${it.name}</span>` +
      `<span class="xf-en">${it.en}</span>` +
      (it.ready ? '' : '<span class="xf-badge">待接入</span>');
    b.addEventListener('click', () => {
      if (!it.ready) {
        return toast(`「${it.name}」的渲染还没实现（当前只有摆动/振荡/自动摇动/随机抖动可用）`, 3200);
      }
      if (it.shake) return openShakePanel(it);
      if (it.xform) return openXformPanel(it);
      if (it.effect) return applyJitterEffect(it);
    });
    grid.appendChild(b);
  });
  body.appendChild(grid);

  const hint = document.createElement('div');
  hint.className = 'xf-hint';
  hint.textContent = '摆动类可直接调参数；标记「待接入」的效果等待内核实现后开放。';
  body.appendChild(hint);

  showPanel('转换', body);
}

/* 随机抖动走 EffectStack（EffectType::Jitter=31）。 */
async function applyJitterEffect(it) {
  const sel = S.selected;
  if (!sel) return toast('请先选中片段');
  const ok = await guardedEdit(
    { op: 'effect_add', track: sel.track.index, clip: sel.clip.id,
      type: it.effect, value: 0.5 },
    `已添加「${it.name}」`);
  if (ok) { hidePanel(); }
}

/* 摆动参数面板 —— 按你给的样板重做：
 * 左侧圆角标签 · 中间刻度尺 · 右侧独立数值框。
 * 颜色取 tokens.css（深色卡片 + --accent-pick 高亮），不套用样板的浅色。 */
async function openShakePanel(it) {
  const sel = S.selected;
  if (!sel) return toast('请先选中片段');

  // 读当前值（片段没设过 shake 时返回全 0，用面板默认值兜底）
  const r = engineCall('shake_get', { track: sel.track.index, clip: sel.clip.id });
  const cur = (r && r.ok && r.result) ? r.result : {};
  const base = it.shake || {};
  const v = {
    amplitude: (cur.amplitude > 0 ? cur.amplitude : base.amplitude) || 0.02,
    frequency: cur.frequency > 0 ? cur.frequency : (base.frequency || 8),
    phase: cur.phase || 0,
    decay: cur.decay_seconds || 0
  };

  const body = document.createElement('div');
  body.innerHTML = `
    <div class="xf-param-head">
      <button class="xf-title">▾ ${it.name}</button>
      <div class="xf-actions">
        <button class="xf-act" data-a="more">⋯</button>
        <button class="xf-act" data-a="reset">⟲</button>
      </div>
    </div>
    <div class="xf-rows">
      <div class="xr" data-k="frequency"></div>
      <div class="xr" data-k="amplitude"></div>
      <div class="xr" data-k="phase"></div>
      <div class="xr" data-k="decay"></div>
    </div>
    <div class="xf-param-foot">
      <button class="btn primary" id="xfApply" style="width:100%">应用</button>
    </div>`;

  const SPEC = {
    frequency: { name: '频率', min: 0,    max: 20,   step: 0.1, unit: 'Hz', def: v.frequency },
    amplitude: { name: '振幅', min: 0,    max: 0.2,  step: 0.005, pct: true, def: v.amplitude },
    phase:     { name: '相位', min: 0,    max: 360,  step: 1,   unit: '°',  def: v.phase * 180 / Math.PI },
    decay:     { name: '衰减', min: 0,    max: 10,   step: 0.1, unit: 's',  def: v.decay }
  };
  const draft = {};
  Object.keys(SPEC).forEach((k) => { draft[k] = SPEC[k].def; });

  const rows = body.querySelector('.xf-rows');
  Object.keys(SPEC).forEach((k) => {
    const sp = SPEC[k];
    const host = rows.querySelector(`.xr[data-k="${k}"]`);
    const fmt = (x) => sp.pct ? `${Math.round(x * 100)}%`
                              : (sp.unit === '°' ? `${Math.round(x)}°`
                                                 : `${(+x).toFixed(sp.step < 0.1 ? 2 : 1)}${sp.unit || ''}`);
    host.innerHTML = `
      <span class="xr-label">${sp.name}</span>
      <span class="xr-scale">
        <span class="xr-ticks"></span>
        <input type="range" min="${sp.min}" max="${sp.max}" step="${sp.step}" value="${draft[k]}">
      </span>
      <span class="xr-val">${fmt(draft[k])}</span>`;
    // 刻度线：按比例生成细竖线，视觉上是「标尺」而不是普通滑轨
    const ticks = host.querySelector('.xr-ticks');
    const n = 24;
    let html = '';
    for (let i = 0; i <= n; i += 1) {
      const major = (i % 6 === 0);
      html += `<i class="${major ? 'maj' : ''}" style="left:${(i / n) * 100}%"></i>`;
    }
    ticks.innerHTML = html;

    const input = host.querySelector('input');
    const val = host.querySelector('.xr-val');
    input.addEventListener('input', () => {
      draft[k] = parseFloat(input.value);
      val.textContent = fmt(draft[k]);
    });
  });

  body.querySelector('[data-a="reset"]').addEventListener('click', () => {
    Object.keys(SPEC).forEach((k) => { draft[k] = SPEC[k].def; });
    openShakePanel(it);   // 简单重开，避免逐控件回写
  });
  body.querySelector('[data-a="more"]').addEventListener('click', () => {
    toast('更多操作：暂只有重置', 2000);
  });

  body.querySelector('#xfApply').addEventListener('click', async () => {
    const ok = await guardedEdit({
      op: 'shake_set', track: sel.track.index, clip: sel.clip.id,
      trajectory: base.trajectory !== undefined ? base.trajectory : 0,
      amplitude: draft.amplitude,
      frequency: draft.frequency,
      phase: draft.phase * Math.PI / 180,
      decay_seconds: draft.decay,
      seed: 0
    }, `已应用「${it.name}」`);
    if (ok) hidePanel();
  });

  showPanel(it.name, body);
}

/* key → 内核 kind 值（与 core/include/nairoclip/core/xform.h 的 XformKind 一致） */
const XFORM_KIND = {
  offset: 0, stretch: 1, spin: 2, raster: 3,
  path: 4, scale: 5, pulse: 6, randpos: 7
};

/* =============================================================================
 * 几何变换参数面板（「转换」8 项）
 * =============================================================================
 * 与 openShakePanel 共用同一套三栏版式（左圆角标签 / 中刻度尺 / 右数值框），
 * 但参数表来自 XF_SPEC，不逐项手写 —— 8 项 × 2~4 行共 22 个调节项，
 * 手写会失控。
 *
 * 参数数组顺序与 capi.h 的 NC_XFORM_ARG_COUNT 严格一致：
 *   [0]=value [1]=angle [2]=freq [3]=phase [4]=axis [5]=cols [6]=rows
 *   [7]=gap   [8]=anchor
 * 改顺序必须同时改 core/include/nairoclip/core/capi.h 与 xform.cpp。
 */
const XF_ARG_ORDER = ['value', 'angle', 'freq', 'phase', 'axis', 'cols', 'rows', 'gap', 'anchor'];

/* 每项效果的调节行定义。def 与 core/src/xform.cpp 的 defaultXformParams 一致。 */
const XF_SPEC = {
  offset: [
    { k: 'value', name: '距离', min: 0, max: 100, step: 1, unit: '%', pct: true, def: 25 },
    { k: 'angle', name: '方向', min: 0, max: 360, step: 1, unit: '°', deg: true, def: 0 },
    { k: 'edge',  name: '边缘', chips: ['背景', '镜像', '拉伸'], def: 1 }
  ],
  stretch: [
    { k: 'axis',  name: '轴',   chips: ['X', 'Y', '对角'], def: 0 },
    { k: 'value', name: '倍率', min: 0, max: 100, step: 1, unit: '%', pct: true, def: 30 }
  ],
  spin: [
    { k: 'angle', name: '角度', min: 0, max: 360, step: 1, unit: '°', deg: true, def: 0 },
    { k: 'freq',  name: '自转', min: 0, max: 5, step: 0.1, unit: '/s', def: 0 }
  ],
  raster: [
    { k: 'cols',  name: '列',   step2: true, min: 2, max: 12, step: 1, def: 6 },
    { k: 'rows',  name: '行',   step2: true, min: 2, max: 12, step: 1, def: 6 },
    { k: 'value', name: '强度', min: 0, max: 100, step: 1, unit: '%', pct: true, def: 30 },
    { k: 'gap',   name: '间隙', min: 0, max: 50, step: 1, unit: '%', pct: true, def: 5 }
  ],
  path: [
    { k: 'value', name: '半径', min: 0, max: 50, step: 1, unit: '%', pct: true, def: 20 },
    { k: 'period', name: '周期', min: 0.5, max: 10, step: 0.5, unit: 's', def: 2 },
    { k: 'trajectory', name: '轨迹', chips: ['圆', '椭圆', '直线'], def: 0 }
  ],
  scale: [
    { k: 'value', name: '比例', min: 0, max: 200, step: 5, unit: '%', pct: true, def: 30 },
    { k: 'anchor', name: '锚点', anchor: true, def: 4 }
  ],
  pulse: [
    { k: 'value', name: '峰值', min: 0, max: 50, step: 1, unit: '%', pct: true, def: 20 },
    { k: 'freq',  name: '频率', min: 0.1, max: 5, step: 0.1, unit: 'Hz', def: 2 },
    { k: 'phase', name: '相位', min: 0, max: 360, step: 1, unit: '°', deg: true, def: 0 }
  ],
  randpos: [
    { k: 'value', name: '幅度', min: 0, max: 20, step: 1, unit: '%', pct: true, def: 4 },
    { k: 'freq',  name: '频率', min: 1, max: 30, step: 1, unit: '/s', def: 8 },
    { k: 'seed',  name: '种子', step2: true, min: 0, max: 999, step: 1, def: 0 }
  ]
};

/* 内部键 → 内核字段。UI 上叫「距离/倍率/半径…」，内核统一是 value。
   period / trajectory / seed 不在 9 参数数组里，单独走字段。 */
const XF_ARG_INDEX = {
  value: 0, angle: 1, freq: 2, phase: 3, axis: 4,
  cols: 5, rows: 6, gap: 7, anchor: 8,
  edge: 9, period: 10, trajectory: 11, seed: 12
};
/* 全部 13 项都进数组，不再有「额外字段」—— 避免前端发了内核收不到。 */
const XF_ARG_COUNT = 13;

function xfToArg(key) {
  return XF_ARG_INDEX[key] !== undefined ? XF_ARG_INDEX[key] : -1;
}

/* 面板值 → 内核参数数组（做单位换算：% → 0..1，度 → 弧度）
   13 项全部写入，与 capi.h 的 NC_XFORM_ARG_COUNT 严格对应。 */
function xfBuildArgs(spec, draft) {
  const args = new Array(XF_ARG_COUNT).fill(0);
  spec.forEach((row) => {
    const idx = xfToArg(row.k);
    if (idx < 0) return;
    let v = draft[row.k];
    if (row.pct) v = v / 100;
    if (row.deg) v = v * Math.PI / 180;
    args[idx] = v;
  });
  return args;
}

/* 读回内核参数 → 面板值（反向换算）。13 项全部来自 args，无额外字段。 */
function xfDraftFromArgs(spec, args) {
  const draft = {};
  spec.forEach((row) => {
    const idx = xfToArg(row.k);
    let v = (idx >= 0 && args && args[idx] !== undefined) ? args[idx] : row.def;
    if (row.pct) v = v * 100;
    if (row.deg) v = v * 180 / Math.PI;
    draft[row.k] = v;
  });
  return draft;
}

/* 通用参数面板：给定 title + spec，返回 { body, draft, spec }
   供几何变换与「其它功能面板统一改造」共用，避免两套版式。 */
function buildParamPanel(title, spec, initialDraft) {
  const body = document.createElement('div');
  body.innerHTML = `
    <div class="xf-param-head">
      <button class="xf-title">▾ ${title}</button>
      <div class="xf-actions">
        <button class="xf-act" data-a="more">⋯</button>
        <button class="xf-act" data-a="reset">⟲</button>
      </div>
    </div>
    <div class="xf-rows"></div>
    <div class="xf-param-foot">
      <button class="btn primary" id="xfApply" style="width:100%">应用</button>
    </div>`;

  const draft = Object.assign({}, initialDraft);
  const rows = body.querySelector('.xf-rows');
  /* panel 必须在使用前声明：下面滑块的 change 回调引用了 panel.onLive，
     若声明放在循环之后会命中 TDZ（ReferenceError）。 */
  const panel = { body, draft, spec, onLive: null };

  spec.forEach((row) => {
    const host = document.createElement('div');
    host.className = 'xr';
    host.dataset.k = row.k;

    const fmt = (x) => {
      if (row.unit === '°') return `${Math.round(x)}°`;
      if (row.unit === '%') return `${Math.round(x)}%`;
      return `${(+x).toFixed(row.step < 1 ? 1 : 0)}${row.unit || ''}`;
    };

    // 三种控件：选项组 / 九宫格锚点 / 刻度尺
    if (row.chips) {
      host.innerHTML = `<span class="xr-label">${row.name}</span>
        <span class="xr-chips">${row.chips.map((c, i) =>
          `<button class="xr-chip${i === draft[row.k] ? ' on' : ''}" data-i="${i}">${c}</button>`).join('')}</span>
        <span class="xr-val">${row.chips[draft[row.k]] || ''}</span>`;
      host.querySelectorAll('.xr-chip').forEach((btn) => {
        btn.addEventListener('click', () => {
          draft[row.k] = +btn.dataset.i;
          host.querySelectorAll('.xr-chip').forEach((b, i) =>
            b.classList.toggle('on', +b.dataset.i === draft[row.k]));
          host.querySelector('.xr-val').textContent = row.chips[draft[row.k]];
          host.classList.add('active');
        });
      });
    } else if (row.anchor) {
      const names = ['左上', '上中', '右上', '左中', '中心', '右中', '左下', '下中', '右下'];
      host.innerHTML = `<span class="xr-label">${row.name}</span>
        <span class="xr-chips xr-anchor">${names.map((c, i) =>
          `<button class="xr-chip${i === draft[row.k] ? ' on' : ''}" data-i="${i}">${c}</button>`).join('')}</span>
        <span class="xr-val">${names[draft[row.k]] || ''}</span>`;
      host.querySelectorAll('.xr-chip').forEach((btn) => {
        btn.addEventListener('click', () => {
          draft[row.k] = +btn.dataset.i;
          host.querySelectorAll('.xr-chip').forEach((b, i) =>
            b.classList.toggle('on', +b.dataset.i === draft[row.k]));
          host.querySelector('.xr-val').textContent = names[draft[row.k]];
        });
      });
    } else if (row.step2) {
      // 步进器：栅格行列、随机种子这类整数
      host.innerHTML = `<span class="xr-label">${row.name}</span>
        <span class="xr-step">
          <button data-d="-1">−</button><span class="v">${Math.round(draft[row.k])}</span><button data-d="1">+</button>
        </span>
        <span class="xr-val">${Math.round(draft[row.k])}</span>`;
      host.querySelectorAll('.xr-step button').forEach((btn) => {
        btn.addEventListener('click', () => {
          let v = draft[row.k] + (+btn.dataset.d) * row.step;
          v = Math.max(row.min, Math.min(row.max, v));
          draft[row.k] = v;
          host.querySelector('.xr-step .v').textContent = Math.round(v);
          host.querySelector('.xr-val').textContent = Math.round(v);
        });
      });
    } else {
      host.innerHTML = `<span class="xr-label">${row.name}</span>
        <span class="xr-scale">
          <span class="xr-ticks"></span>
          <input type="range" min="${row.min}" max="${row.max}" step="${row.step}" value="${draft[row.k]}">
        </span>
        <span class="xr-val">${fmt(draft[row.k])}</span>`;
      const ticks = host.querySelector('.xr-ticks');
      let html = '';
      const n = 24;
      for (let i = 0; i <= n; i += 1) {
        html += `<i class="${i % 6 === 0 ? 'maj' : ''}" style="left:${(i / n) * 100}%"></i>`;
      }
      ticks.innerHTML = html;

      const input = host.querySelector('input');
      const val = host.querySelector('.xr-val');
      input.addEventListener('input', () => {
        draft[row.k] = parseFloat(input.value);
        val.textContent = fmt(draft[row.k]);
        host.classList.add('active');
      });
      // 松手立即落盘（拖动中不写，避免每帧一次引擎调用）
      input.addEventListener('change', () => { if (panel.onLive) panel.onLive(); });
    }

    rows.appendChild(host);
  });

  body.querySelector('[data-a="reset"]').addEventListener('click', () => {
    spec.forEach((row) => { draft[row.k] = row.def; });
    if (panel.onReset) panel.onReset();
  });
  body.querySelector('[data-a="more"]').addEventListener('click', () => {
    if (panel.onMore) panel.onMore();
    else toast('更多操作：暂只有重置', 2000);
  });

  return panel;
}

/* 打开几何变换参数面板 */
async function openXformPanel(it) {
  const sel = S.selected;
  if (!sel) return toast('请先选中一个片段');

  const kind = XFORM_KIND[it.key];
  if (kind === undefined) return toast('未知效果', 2000);

  const spec = XF_SPEC[it.key] || [];
  const r = engineCall('xform_get', { track: sel.track.index, clip: sel.clip.id, kind });
  const curArgs = (r && r.ok && r.result && r.result.args) ? r.result.args : null;
  const draft = curArgs ? xfDraftFromArgs(spec, curArgs) : Object.fromEntries(
    spec.map((row) => [row.k, row.def]));

  const panel = buildParamPanel(it.name, spec, draft);
  const sel2 = S.selected;

  const apply = async () => {
    const args = xfBuildArgs(panel.spec, panel.draft);
    const ok = await guardedEdit({
      op: 'xform_set', track: sel2.track.index, clip: sel2.clip.id,
      kind, args
    }, `已应用「${it.name}」`);
    if (ok) hidePanel();
  };

  // 松手立即落盘：不关面板，让用户能连续微调并看到画面变化
  panel.onLive = async () => {
    const args = xfBuildArgs(panel.spec, panel.draft);
    await guardedEdit({
      op: 'xform_set', track: sel2.track.index, clip: sel2.clip.id, kind, args
    });
  };
  panel.onReset = () => {
    panel.spec.forEach((row) => { panel.draft[row.k] = row.def; });
    hidePanel();
    openXformPanel(it);   // 简单重开，避免逐控件回写
  };
  panel.onMore = async () => {
    const ok = await guardedEdit({
      op: 'xform_clear', track: sel2.track.index, clip: sel2.clip.id, kind
    }, `已移除「${it.name}」`);
    if (ok) hidePanel();
  };

  panel.body.querySelector('#xfApply').addEventListener('click', apply);
  showPanel(it.name, panel.body);
}

/* ---------------- 音乐卡点（B3-1 / P-07）----------------
 *
 * 一步完成「检测节拍 → 按节拍把素材切分并写入空轨」。
 * 内核侧 nc_beat_sync_apply 已把检测与落盘合成一次调用，
 * 前端不需要先拿 beats 再自己算切点。
 *
 * 为什么要求「空轨」：与其它自动剪辑一致，避免覆盖用户已有剪辑。
 * 面板里明确写出来，不让用户点了才被拒。
 */
async function openBeatCutPanel() {
  if (!Engine.online) return toast('未连接引擎');

  // 找音频轨（节拍来源）与空视频轨（落盘目标）
  const audioTracks = (S.tracks || []).filter((t) => t.kind === 'audio');
  const emptyVideo = (S.tracks || []).filter(
    (t) => t.kind === 'video' && !(t.clips || []).length);
  const videoAssets = (S.assets || []).filter((a) => a.kind === 0 || a.kind === 2);

  const body = document.createElement('div');
  body.innerHTML = `
    <div class="adj-hint">按音乐节拍自动切分素材，写入一条空视频轨</div>
    <div class="adj-sec">
      <div class="adj-sec-head"><span>节拍来源</span></div>
      <select id="beatAudio" class="beat-sel"></select>
    </div>
    <div class="adj-sec">
      <div class="adj-sec-head"><span>落盘到（必须是空轨）</span></div>
      <select id="beatTarget" class="beat-sel"></select>
      <div class="beat-note" id="beatNote"></div>
    </div>
    <div class="adj-sec">
      <div class="adj-sec-head"><span>素材（${videoAssets.length} 个，按顺序循环取用）</span></div>
      <div class="beat-assets" id="beatAssets"></div>
    </div>
    <div class="adj-sec">
      <div class="adj-sec-head"><span>卡片段最小长度</span></div>
      <div class="adj-lut">
        <input type="range" id="beatMin" min="10" max="100" value="25">
        <b id="beatMinVal">0.25s</b>
      </div>
    </div>
    <div class="adj-sec">
      <label class="beat-check"><input type="checkbox" id="beatTrans" checked> 片段时间插入转场</label>
    </div>
    <div class="adj-sec">
      <button class="btn primary" id="beatRun" style="width:100%">开始卡点切分</button>
    </div>`;

  const aSel = body.querySelector('#beatAudio');
  if (!audioTracks.length) {
    aSel.innerHTML = '<option value="">工程里没有音频轨</option>';
  } else {
    audioTracks.forEach((t) => {
      const o = document.createElement('option');
      o.value = String(t.index);
      o.textContent = t.name;
      aSel.append(o);
    });
  }

  const tSel = body.querySelector('#beatTarget');
  const note = body.querySelector('#beatNote');
  if (!emptyVideo.length) {
    tSel.innerHTML = '<option value="">没有空视频轨</option>';
    note.textContent = '卡点只写空轨。请先用「画中画 → 新增」建一条视频轨，再回来。';
    note.className = 'beat-note warn';
  } else {
    emptyVideo.forEach((t) => {
      const o = document.createElement('option');
      o.value = String(t.index);
      o.textContent = t.name;
      tSel.append(o);
    });
    note.textContent = '只显示空轨：写满后再卡点会覆盖已有剪辑，因此不让选。';
  }

  // 素材勾选（默认全选）
  const picked = new Set(videoAssets.map((a) => a.id));
  const aWrap = body.querySelector('#beatAssets');
  if (!videoAssets.length) {
    aWrap.innerHTML = '<div class="adj-empty">素材库里没有视频/图片</div>';
  } else {
    videoAssets.forEach((a) => {
      const lab = document.createElement('label');
      lab.className = 'beat-asset';
      lab.innerHTML = `<input type="checkbox" checked data-id="${a.id}">` +
        `<span>${a.name}</span><small>${a.dur.toFixed(1)}s</small>`;
      lab.querySelector('input').addEventListener('change', (ev) => {
        if (ev.target.checked) picked.add(a.id);
        else picked.delete(a.id);
      });
      aWrap.appendChild(lab);
    });
  }

  const minR = body.querySelector('#beatMin');
  const minV = body.querySelector('#beatMinVal');
  minR.addEventListener('input', () => {
    minV.textContent = `${(Number(minR.value) / 100).toFixed(2)}s`;
  });

  body.querySelector('#beatRun').addEventListener('click', async () => {
    if (!audioTracks.length) return toast('没有音频轨，无法检测节拍');
    if (!emptyVideo.length) return toast('没有空视频轨可写入');
    const chosen = videoAssets.filter((a) => picked.has(a.id));
    if (!chosen.length) return toast('请至少选择一个素材');

    const r = engineCall('beat_sync', {
      target_track: Number(tSel.value),
      audio_track: Number(aSel.value),
      asset_ids: chosen.map((a) => a.id),
      usable_seconds: chosen.map((a) => a.dur),
      min_shot_seconds: Number(minR.value) / 100,
      prefer_transitions: body.querySelector('#beatTrans').checked ? 1 : 0
    });
    if (!r || !r.ok) {
      return toast(`卡点失败：${(r && r.message) || '未知错误'}`, 3600);
    }
    const res = r.result || {};
    const warn = res.warning ? `；${res.warning}` : '';
    toast(`已插入 ${res.clips || 0} 段（BPM ${Math.round(res.bpm || 0)}）${warn}`, 3600);
    try {
      applyState(fetchState());
      renderAll();
    } catch (err) { /* 刷新失败不影响已落盘的结果 */ }
    hidePanel();
  });

  showPanel('音乐卡点', body);
}

/* ---------------- 轨道组（B2-7 / P-21）----------------
 *
 * 产品场景：主画面 = 视频轨 + 分离出的音频轨 + 字幕轨，用户希望
 * 「一起隐藏 / 一起静音」，而不是逐条点。
 *
 * core 与 C ABI 都已实现（含「一条轨道只能属于一个组」的校验与整体撤销），
 * 此前只差 UI。这里补三件事：成组、解组、组内联动开关。
 */

/// 把若干轨道组成一组。indices 为轨道下标。
async function createTrackGroup(indices, name) {
  if (!indices || !indices.length) return toast('请先选择至少一条轨道');
  if (!Engine.online) return toast('未连接引擎');
  const r = engineCall('group_create', {
    track_indices: indices, name: name || ''
  });
  if (!r || !r.ok) {
    /* 内核会给出具体原因（如「已属于其它轨道组，请先解组」），
       直接转达，不换成前端自己的模糊说法。 */
    return toast(`成组失败：${(r && r.message) || '未知错误'}`, 3200);
  }
  try {
    applyState(fetchState());
    renderAll();
  } catch (err) {
    toast(`刷新失败：${err.message}`, 3000);
  }
}

/// 解组。成员轨道保留，只是不再联动。
async function removeTrackGroup(groupId) {
  if (!groupId) return;
  const r = engineCall('group_remove', { group_id: groupId });
  if (!r || !r.ok) return toast(`解组失败：${(r && r.message) || '未知错误'}`, 3000);
  try {
    applyState(fetchState());
    renderAll();
  } catch (err) {
    toast(`刷新失败：${err.message}`, 3000);
  }
}

/// 组内联动开关（hidden / muted / locked）。一次写全组成员，可整体撤销。
async function setGroupFlag(groupId, flag, value) {
  if (!groupId) return;
  const r = engineCall('group_flag', {
    group_id: groupId, flag, value: value ? 1 : 0
  });
  if (!r || !r.ok) return toast(`设置失败：${(r && r.message) || '未知错误'}`, 3000);
  try {
    applyState(fetchState());
    renderAll();
  } catch (err) {
    toast(`刷新失败：${err.message}`, 3000);
  }
}

/* ---------------- 调整图层：真实新增中间层轨道（B2-6 / P-19）----------------
 *
 * 调整图层没有片段：效果与 LUT 挂在**轨道**上，作用于「下方所有轨道合成后的
 * 整帧」。core 与 C ABI（nc_adjustment_*）以及预览/导出渲染都已实现，
 * 此前只缺 UI 入口 —— 本函数补上。
 *
 * 用 add_track + type:3，与画中画同一条命令链路，因此同样可撤销。
 */
async function addAdjustmentTrack() {
  const n = (S.tracks || []).filter((t) => t.type === 3).length + 1;
  const ok = await guardedEdit(
    { op: 'add_track', name: `调整 ${n}`, type: 3 },
    '已新增调整图层：点轨道上的齿轮添加效果'
  );
  if (ok && typeof load === 'function') await load();
  return ok;
}

/* ---------------- 调整图层面板（B2-6 / P-19）----------------
 *
 * 为什么不复用现有效果列表（l3-panel 的 fxPanel）：
 * 那套模块的每个 op 都带 `clip: t.clip`，而调整图层**没有片段** ——
 * 效果挂在 Track::adjustmentEffects 上。硬塞进去要么传 clip=0 打错目标，
 * 要么把 fxTarget 改成双模式、连带 8 个调用点全改，风险与收益不成比例。
 * 因此这里做专用面板，数据源用 track.adjustment（BuildState 下发）。
 */
/* 效果名一律取仓库既有的 l3-spec.effectNameOf —— 它有完整的 1..36 映射。
 * 不要在这里另写一份：先前手写的表把 8/9/10/11 写成「暗角/模糊/锐化/噪点」，
 * 与 core 的 Sharp=8 / Vignette=9 / Grain=10 / Fade=11 完全错位，
 * 用户点到「锐化」实际加的是「暗角」。
 */
function adjEffectName(type) {
  if (window.L3_SPEC && typeof window.L3_SPEC.effectNameOf === 'function') {
    return window.L3_SPEC.effectNameOf(type);
  }
  return `效果 ${type}`;
}

/// 可添加的效果类型：无（None=0）之外的 1..36（35=发光轮廓，36=震动）。
const ADJ_EFFECT_MAX = 36;

async function openAdjustmentPanel(tr) {
  if (!Engine.online) return toast('未连接引擎');

  // 拉最新状态：面板上的计数/强度必须来自引擎，不能用 S.tracks 的旧快照
  let t = tr;
  try {
    const st = fetchState();
    applyState(st);
    t = (S.tracks || []).find((x) => x.index === tr.index) || tr;
  } catch (_) { /* 读不到就用手上的，面板仍可用 */ }

  const adj = t.adjustment || { effect_count: 0, has_lut: 0, lut_strength: 0, lut_path: '' };
  const below = (S.tracks || []).filter((x) => x.index > t.index).length;

  const body = document.createElement('div');
  body.innerHTML = `
    <div class="adj-hint">作用于下方 ${below} 条轨道（合成后整帧）</div>
    <div class="adj-sec">
      <div class="adj-sec-head"><span>效果（${adj.effect_count || 0}）</span></div>
      <div class="adj-effs" id="adjEffs"></div>
      <div class="adj-add">
        <select id="adjType"></select>
        <button class="btn" id="adjAdd">添加</button>
      </div>
    </div>
    <div class="adj-sec">
      <div class="adj-sec-head"><span>LUT</span></div>
      <div class="adj-lut">
        <input type="range" id="adjLutStrength" min="0" max="100"
               value="${Math.round((adj.lut_strength || 0) * 100)}">
        <b id="adjLutVal">${Math.round((adj.lut_strength || 0) * 100)}%</b>
      </div>
      <div class="adj-lut-path">${adj.has_lut ? (adj.lut_path || '已设置') : '未设置 LUT'}</div>
      <button class="btn" id="adjLutClear"${adj.has_lut ? '' : ' disabled'}>清除 LUT</button>
    </div>`;

  const sel = body.querySelector('#adjType');
  for (let i = 1; i <= ADJ_EFFECT_MAX; i += 1) {
    const o = document.createElement('option');
    o.value = String(i);
    o.textContent = adjEffectName(i);
    sel.append(o);
  }

  /* 逐个读真实效果：nc_adjustment_effect_get 按序号返回
     id/type/value/enabled，删除时用拿到的 id，不靠猜。 */
  const effs = body.querySelector('#adjEffs');
  if (!adj.effect_count) {
    effs.innerHTML = '<div class="adj-empty">还没有效果</div>';
  } else {
    for (let i = 0; i < adj.effect_count; i += 1) {
      const r = engineCall('adjust_effect_get', { track_index: t.index, index: i });
      const row = document.createElement('div');
      row.className = 'adj-eff';
      if (!r || !r.ok) {
        // 读不到就如实显示序号，不编造名称
        row.innerHTML = `<span>效果 ${i + 1}（读取失败）</span>`;
        effs.appendChild(row);
        continue;
      }
      const eid = r.result && r.result.effect_id;
      const etype = r.result ? r.result.type : 0;
      const pct = Math.round((r.result ? r.result.value : 0) * 100);
      const off = r.result && r.result.enabled === 0;
      row.innerHTML = `<span${off ? ' class="off"' : ''}>` +
        `${adjEffectName(etype)} · ${pct}%` +
        `${off ? ' · 已隐藏' : ''}</span>`;
      const del = document.createElement('button');
      del.className = 'adj-del';
      del.textContent = '删除';
      del.addEventListener('click', async () => {
        if (!eid) return toast('该效果没有 id，无法删除', 3000);
        const okFlag = await guardedEdit(
          { op: 'adjust_effect_remove', track_index: t.index, effect_id: eid },
          '已删除效果');
        if (okFlag) { hidePanel(); openAdjustmentPanel(t); }
      });
      row.appendChild(del);
      effs.appendChild(row);
    }
  }

  body.querySelector('#adjAdd').addEventListener('click', async () => {
    const type = Number(sel.value);
    const okFlag = await guardedEdit(
      { op: 'adjust_effect_add', track_index: t.index, type, value: 1.0 },
      `已添加「${adjEffectName(type)}」`);
    if (okFlag) { hidePanel(); openAdjustmentPanel(t); }
  });

  const lutRange = body.querySelector('#adjLutStrength');
  const lutVal = body.querySelector('#adjLutVal');
  lutRange.addEventListener('input', () => {
    lutVal.textContent = `${lutRange.value}%`;
  });
  lutRange.addEventListener('change', async () => {
    const okFlag = await guardedEdit({
      op: 'adjust_lut', track_index: t.index,
      path: adj.lut_path || '', strength: Number(lutRange.value) / 100,
    });
    if (!okFlag) return;
  });

  body.querySelector('#adjLutClear').addEventListener('click', async () => {
    const okFlag = await guardedEdit(
      { op: 'adjust_lut', track_index: t.index, path: '', strength: 1.0 },
      '已清除 LUT');
    if (okFlag) { hidePanel(); openAdjustmentPanel(t); }
  });

  showPanel('调整图层', body);
}

/* ---------------- 画中画：真实新增视频轨 ---------------- */
async function addPipTrack() {
  const n = (S.tracks || []).filter((t) => t.type === 0).length + 1;
  const ok = await guardedEdit(
    { op: 'add_track', name: `画中画 ${n}`, type: 0 },
    '已新增画中画轨道，可从素材库拖入'
  );
  if (!ok) return;
  if (typeof load === 'function') await load();

  /* 自动把新轨上的片段缩到 60%（G4）。
     为什么不是 1.0：1.0 + 居中 = 完全盖住主画面，用户会以为
     「主画面消失了」或「拖错轨了」—— 剪映也是默认缩小。
     为什么不是 0.5：0.6 在 9:16 竖屏上留出的边框更自然，
     且第一眼能确认「小的那个在动」。
     只处理「刚加进来的那条轨」，不影响既有轨道。 */
  const fresh = (S.tracks || []).filter((t) => t.type === 0)
    .sort((a, b) => b.index - a.index)[0];
  if (!fresh || !fresh.clips || !fresh.clips.length) return;
  for (const c of fresh.clips) {
    await guardedEdit({
      op: 'transform_set', track: fresh.index, clip: c.id,
      enabled: 1, scale: 0.6, pos_x: 0, pos_y: 0, rotation: 0, anchor: 4
    });
  }
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
  // 各面板会改写标题，回到「添加素材」时必须复位，否则残留上个面板的标题
  const t = E.sheet && E.sheet.querySelector('.sheet-head span');
  if (t) t.textContent = '添加素材';
  sheetRow('视频', 'mp4 / mov / mkv · 取时长并生成胶片条', () => pickFile('video'));
  sheetRow('图片', 'jpg / png / webp · 默认 5 秒静帧', () => pickFile('image'));
  sheetRow('音频', 'mp3 / wav / m4a / flac · 生成波形', () => pickFile('audio'));
  E.sheet.hidden = false;
}
/* label / sub 常来自用户数据（素材文件名、标记名），必须用 textContent
   而不是 innerHTML 拼接，否则文件名里带 < 就会被当成标签解析。
   图标位与结构在这里统一生成，调用方只提供纯文本。 */
function sheetRow(label, sub, fn) {
  const b = document.createElement('button');
  b.className = 'sheet-item';

  const ico = document.createElement('span');
  ico.className = 'sheet-ico';
  ico.textContent = '＋';

  const box = document.createElement('span');
  box.style.flex = '1';
  box.appendChild(document.createTextNode(label));
  const br = document.createElement('br');
  box.appendChild(br);
  const small = document.createElement('small');
  small.textContent = sub == null ? '' : String(sub);
  box.appendChild(small);

  b.appendChild(ico);
  b.appendChild(box);
  b.addEventListener('click', fn);
  E.sheetList.appendChild(b);
  return b;
}
/* ---------------- 模板库（B2-4）----------------
   数据来源 nc_template_list 返回的 JSON 数组：
     [{ id, name, category, slotCount, totalDurationUs, musicHint?, coverPath? }]
   套用走 nc_template_apply（自动填充，核心层按 B2-2 打分挑素材）。

   为什么不做"手动指定第几段用哪个镜头"（nc_template_apply_slots）：
   那需要先让用户在同一屏里完成"选模板 → 逐槽位挑素材"两步选择，
   移动端一屏放不下；当前先把自动填充接通，手工映射留作后续。 */
function showTemplateSheet() {
  E.sheetList.innerHTML = '';
  if (!Engine.online) {
    sheetRow('未连接引擎', '模板需要真实工程状态', () => closeSheet());
    E.sheet.hidden = false;
    return;
  }
  const r = engineCall('nc_template_list', {});
  if (!r || !r.ok) {
    sheetRow('读取模板失败', (r && r.message) || '引擎拒绝', () => closeSheet());
    E.sheet.hidden = false;
    return;
  }
  // nc_template_list 的结果可能放在 result 里（桥统一包一层），
  // 也可能是裸值。两种都接，缺了就当作空列表而不是崩。
  let list = (r.result !== undefined) ? r.result : r.value;
  if (typeof list === 'string') {
    try { list = JSON.parse(list); } catch (_) { list = []; }
  }
  if (!Array.isArray(list)) list = [];

  if (!list.length) {
    sheetRow('暂无模板', '内置模板尚未注册', () => closeSheet());
  } else {
    list.forEach((t) => {
      const secs = (t.totalDurationUs || 0) / 1e6;
      const sub = `${t.category || '模板'} · ${t.slotCount || 0} 段 · ${secs.toFixed(1)}s`;
      sheetRow(t.name || t.id, sub, () => applyTemplate(t));
    });
  }
  const head = E.sheet && E.sheet.querySelector('.sheet-head span');
  if (head) head.textContent = `模板库（${list.length}）`;
  E.sheet.hidden = false;
}

/* 套用模板：把当前工程的全部素材作为候选交给核心层打分填充。
   只传素材 id —— 核心层会用候选的顺序/时长做匹配，
   前端不参与"哪个素材配哪个槽"的决策（那是 B2-2 的职责）。 */
function applyTemplate(t) {
  if (!Engine.online) return toast('未连接引擎，无法套用模板');
  const assets = (S.assets || []).map((a) => a.id).filter((id) => id);
  if (!assets.length) {
    return toast('请先导入素材，模板需要素材才能填充', 3000);
  }
  const r = engineCall('nc_template_apply', {
    template_id: t.id,
    material_ids: assets,
    count: assets.length
  });
  if (!r || !r.ok) {
    return toast(`套用失败：${(r && r.message) || '引擎拒绝'}`, 3000);
  }
  // 摘要里含插入数/转场数/特效数/轨道数与 warnings —— 直接给用户看，
  // 不要吞掉：模板填充失败一部分时，warnings 是唯一的说明来源。
  const summary = (r.result && (r.result.summary || r.result)) || r.summary;
  const text = typeof summary === 'string' ? summary : (summary && summary.text);
  closeSheet();
  toast(text ? `已套用「${t.name}」：${text}` : `已套用「${t.name}」`, 4000);
  try {
    applyState(fetchState());
    renderAll();
  } catch (err) {
    toast(`刷新失败：${err.message}`, 3000);
  }
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
  /* 面板内的写入：只刷数据，不重建时间轴。
     为什么不用 edit()：它会 renderAll()，而拖强度滑块是连续操作，
     每帧重建整条时间轴会明显掉帧，正在拖的手感就是「黏」。
     面板里的改动不改变轨道结构，所以只换数据足够。 */
  edit: (payload) => {
    if (!Engine.bridge) throw new Error('引擎未连接');
    const op = payload && payload.op;
    const args = Object.assign({}, payload);
    delete args.op;
    const r = engineCall(op, args);
    if (!r || !r.ok) {
      throw new Error((r && r.message) || `操作失败（${(r && r.code) ?? '未知'}）`);
    }
    applyState(fetchState());
    /* 轨道上的片段视觉（缩略图/滤镜色/不透明度）确实依赖这些数据，
       但**不需要** fitCanvas/renderBar/syncPreview 这一整套。
       只重建轨道 DOM，代价小得多。 */
    renderTracks();
    return r;
  },
  /* 「画面」面板回读：取当前引擎里存的 transform 静态值。
     返回 null 表示读取失败（面板将显示默认值，不阻断操作）。 */
  transformGet: () => {
    const s = selClip();
    if (!s) return null;
    const r = engineCall('transform_get', { track: s.track.index, clip: s.clip.id });
    if (!r || !r.ok) return null;
    return r.result || null;
  },
  /* 直接调 C ABI（AI 任务等不经过 edit 分发的入口） */
  callNative: (fn, args) => engineCall(fn, args),
  apiBase: () => Engine.apiBase(),
  reload: async () => { applyState(await fetchState()); renderAll(); },
  /* 只刷新数据、不重绘时间轴。
     面板里改效果值/删除/隐藏后用它：需要的是新的 effects 数组，
     不是整条时间轴重建（那会让面板后面的轨道闪一下）。 */
  refresh: async () => { applyState(await fetchState()); },
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
  loadBlendModes();
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
