import { ICONS } from './icons.js';
import { icon, el, $, $$, api, timecode, duration, snapFrame, toast, bytes } from './ui.js';

// =============================================================================
// NairoClip 剪辑台 —— 前端仅负责呈现与手势；
// 所有编辑事实（片段/轨道/字幕/标记/撤销栈/导出进度）来自 C ABI 真实状态。
// =============================================================================

const state = {
  data: null,            // /api/state 原样
  playhead: 0,           // 微秒
  selection: null,       // { track, clip }
  tool: 'select',
  pxPerSec: 74,
  snap: true,
  loop: false,
  playing: false,
  quality: 0.5,
  frameSeq: 0,
  thumbs: new Map(),     // assetId → HTMLImageElement
  waves: new Map(),      // assetId → number[]
  inspectorOpen: true,
};

const FPS = () => {
  const p = state.data?.project;
  return p ? p.fps_num / p.fps_den : 30;
};

// ---------------------------------------------------------------- 基础读取
const project = () => state.data.project;
const durationUs = () => project().duration_us || 0;
const canvasRatio = () => {
  const { width, height } = project();
  return `${width} / ${height}`;
};

function trackAt(index) {
  return state.data.tracks.find((t) => t.index === index);
}

function findClip(trackIndex, clipId) {
  const track = trackAt(trackIndex);
  return track ? track.clips.find((c) => c.id === clipId) : null;
}

function assetOf(assetId) {
  return state.data.assets.find((a) => a.id === assetId) || null;
}

function selectionValid() {
  const sel = state.selection;
  if (!sel) return false;
  return !!findClip(sel.track, sel.clip);
}

// ---------------------------------------------------------------- 刷新
async function refresh({ keepSelection = true } = {}) {
  const seq = ++state.frameSeq;
  const data = await api('/api/state');
  if (seq !== state.frameSeq) return; // 丢弃过期响应
  state.data = data;
  if (!keepSelection || !selectionValid()) state.selection = null;
  render();
}

let rafPending = false;
function requestRefresh() {
  if (rafPending) return;
  rafPending = true;
  requestAnimationFrame(async () => {
    rafPending = false;
    await refresh();
  });
}

// ---------------------------------------------------------------- 渲染：顶栏
function renderTopbar() {
  const p = project();
  $('#projectName').textContent = p.name;
  $('#projectMeta').textContent = `${p.width}×${p.height} · ${FPS()}fps`;
  $('#sbFormat').textContent = `${p.width}×${p.height} · ${FPS()}fps · ${timecode(p.duration_us, p.fps_num, p.fps_den)}`;

  const undo = $('#undoBtn');
  undo.innerHTML = icon('undo');
  undo.disabled = !p.can_undo;
  undo.title = p.can_undo ? `撤销：${p.undo_name} (Ctrl+Z)` : '没有可撤销的操作';

  const redo = $('#redoBtn');
  redo.innerHTML = icon('redo');
  redo.disabled = !p.can_redo;

  $('#splitBtn').innerHTML = icon('split');
  $('#markerBtn').innerHTML = icon('marker');
  $('#deleteBtn').innerHTML = icon('close');
  $('#deleteBtn').disabled = !state.selection;

  $('#sbHistory').textContent = `历史 ${p.undo_count} 步`;
  $('#engineDot').className = `dot ${p.ai_engine_available ? '' : 'is-alarm'}`;
  $('#engineText').textContent = p.ai_engine_available
    ? `推理引擎 ${p.ai_engine_version || '可用'}`
    : '推理引擎不可用';
  $('#engineText').title = p.ai_engine_note || '';
}

// ---------------------------------------------------------------- 渲染：舞台
async function renderStage() {
  const p = project();
  const t = Math.round(state.playhead);
  const hasContent = (state.data.tracks || []).some((tr) => tr.clips.length > 0);
  const stageCanvas = $('.stage-canvas');
  let emptyNode = $('#stageEmpty');
  if (!hasContent) {
    $('#frame').classList.add('hidden');
    if (!emptyNode) {
      emptyNode = el('div', { class: 'stage-empty', id: 'stageEmpty' });
      emptyNode.innerHTML = `
        <div style="width:46px;height:46px;display:grid;place-items:center;border:1px solid var(--ink-500);border-radius:2px;color:var(--paper-faint)">
          ${icon('film', 'ic ic-lg')}</div>
        <h3>时间轴还是空的</h3>
        <p>导入一段视频或图片开始剪辑。素材只在本机解码，不会上传。</p>
        <div style="display:flex;gap:8px">
          <button class="btn btn-primary chamfer-sm" id="importBtn">导入素材</button>
          <button class="btn" id="importDemoBtn">载入示例草稿</button>
        </div>`;
      stageCanvas.append(emptyNode);
      emptyNode.querySelector('#importBtn').addEventListener('click', importAssetFlow);
      emptyNode.querySelector('#importDemoBtn').addEventListener('click', loadDemoProject);
    }
    $('#subtitleLayer').innerHTML = '';
    return;
  }
  $('#frame').classList.remove('hidden');
  emptyNode?.remove();
  $('#frame').style.aspectRatio = canvasRatio();
  $('#stageRatio').textContent = `${p.width}×${p.height}`;
  $('#timecode').innerHTML = `${timecode(t, p.fps_num, p.fps_den, false)}:<span class="f">${String(
    Math.floor(((t / 1e6) % 1) * FPS() + 1e-6)).padStart(2, '0')}</span>`;
  $('#durationTotal').textContent = `/ ${timecode(p.duration_us, p.fps_num, p.fps_den)}`;
  $('#frameBadge').innerHTML = `<i class="dot"></i><span>${timecode(t, p.fps_num, p.fps_den)}</span>`;

  // 字幕：来自真实字幕轨（当前时刻）
  const lines = (state.data.subtitles || []).filter(
    (c) => t >= c.start_us && t < c.start_us + c.duration_us);
  const layer = $('#subtitleLayer');
  layer.innerHTML = lines.map((c) => `<div class="subtitle-line">${escapeHtml(c.text)}</div>`).join('');

  // 预览帧：按当前预览质量请求（真实渲染；拖动时用步进桶命中缓存）
  const width = Math.round(p.width * state.quality * 0.62);
  const img = $('#frameImg');
  const url = `/api/frame?t=${t}&w=${Math.max(180, Math.min(720, width))}`;
  const loader = new Image();
  loader.decoding = 'async';
  loader.onload = () => { img.src = url; };
  loader.onerror = () => {
    img.removeAttribute('src');
    $('#stageSource').textContent = '该时刻无画面（引擎返回空帧）';
  };
  loader.src = url;
}

// 预览质量：真实影响取帧宽度（对应 core 的 previewQuality 分级思路）
function renderQualityLabel() {
  $('#qualityBtn').textContent = `预览质量 ${Math.round(state.quality * 100)}%`;
}

// ---------------------------------------------------------------- 渲染：轨道头
function renderTrackHeads() {
  const activeTrack = state.selection?.track ?? -1;
  const host = $('#trackHeads');
  host.innerHTML = '';
  for (const track of state.data.tracks) {
    const typeName = ['video', 'audio', 'subtitle', 'effect'][track.type] || 'video';
    const head = el('div', {
      class: `track-head ${track.index === activeTrack ? 'is-active' : ''}`,
      'data-type': typeName,
      'data-index': track.index,
    });
    // 折叠态（B2-10）：只保留序号 + 名字 + 展开按钮。
    // 小眼睛/锁等图标收起后，它们原本占的位置变成**空白**；
    // 这块空白与轨道头其它空白一样可以长按搬动整条轨道
    // （见 initTrackDrag —— 它只排除 button，不排除空白区域）。
    if (track.collapsed) {
      head.classList.add('is-collapsed');
      head.innerHTML = `
        <span class="track-index">${String(track.index + 1).padStart(2, '0')}</span>
        <button class="track-name" title="重命名轨道">${escapeHtml(track.name)}</button>
        <div class="track-toggles">
          <button data-collapse="0" aria-pressed="true" title="展开轨道">
            ${icon('chevronRight', 'ic')}</button>
        </div>`;
      host.append(head);
      continue;
    }
    head.innerHTML = `
      <span class="track-index">${String(track.index + 1).padStart(2, '0')}</span>
      <button class="track-name" title="重命名轨道">${escapeHtml(track.name)}</button>
      <div class="track-toggles">
        <button data-collapse="1" aria-pressed="false" title="收起图标">
          ${icon('chevronDown', 'ic')}</button>
        <button data-flag="locked" aria-pressed="${track.locked}" title="锁定">
          ${icon(track.locked ? 'lock' : 'unlock', 'ic')}</button>
        <button data-flag="${typeName === 'audio' ? 'muted' : 'hidden'}"
                aria-pressed="${typeName === 'audio' ? track.muted : track.hidden}"
                title="${typeName === 'audio' ? '静音' : '隐藏'}">
          ${icon(typeName === 'audio' ? (track.muted ? 'mute' : 'volume') : (track.hidden ? 'eyeOff' : 'eye'), 'ic')}</button>
      </div>
      <div class="track-sub">
        <span>${typeName === 'audio' ? 'A' : typeName === 'video' ? 'V' : typeName === 'subtitle' ? 'T' : 'FX'}</span>
        <span>${track.clips.length} 片段</span>
        ${typeName === 'audio' ? `<span>${track.gain_db.toFixed(1)}dB</span>` : ''}
        ${track.muted && typeName === 'audio' ? '<span style="color:var(--tungsten)">静音</span>' : ''}
        ${track.hidden && typeName !== 'audio' ? '<span style="color:var(--tungsten)">隐藏</span>' : ''}
      </div>`;
    host.append(head);
  }
}

// ---------------------------------------------------------------- 渲染：时间轴
function timeToX(us) {
  return (us / 1e6) * state.pxPerSec;
}
function xToTime(x) {
  return Math.max(0, (x / state.pxPerSec) * 1e6);
}

/// 折叠态只留标题栏高度；否则用轨道自己的 height_px（B2-10），
/// 缺省时按类型给默认值（音频矮一点，波形不需要太高）。
const COLLAPSED_LANE_H = 18;
function laneHeight(track) {
  if (track.collapsed) return COLLAPSED_LANE_H;
  if (Number.isFinite(track.height_px)) return track.height_px;
  return track.type === 1 ? 46 : 56;
}

function renderTimeline() {
  const tracks = state.data.tracks;
  const contentWidth = Math.max(
    timeToX(durationUs()) + 320,
    $('#tlScroll').clientWidth - 24,
  );
  $('#tlContent').style.width = `${contentWidth}px`;
  $('#tlTracks').innerHTML = '';
  $('#trackHeads').style.setProperty('--lane', '1fr');

  const headHeight = 0;
  tracks.forEach((track) => {
    const lane = el('div', {
      class: `track-lane ${state.selection?.track === track.index ? 'is-active' : ''} ${track.locked ? 'is-locked' : ''}`,
      'data-type': ['video', 'audio', 'subtitle', 'effect'][track.type],
      'data-index': track.index,
    });
    lane.style.height = `${laneHeight(track)}px`;
    lane.dataset.height = laneHeight(track);
    if (track.collapsed) lane.dataset.collapsed = '1';

    for (const clip of track.clips) {
      lane.append(renderClip(track, clip));
    }
    // 转场块：真实 transition 端点
    if (track.transitions) {
      for (const tr of track.transitions) {
        const from = track.clips.find((c) => c.id === tr.from);
        if (!from) continue;
        const x = timeToX(from.start_us + from.duration_us - tr.duration_us / 2);
        const w = Math.max(10, timeToX(tr.duration_us));
        const node = el('div', { class: 'transition', title: `${tr.name} · ${duration(tr.duration_us)}` });
        node.style.left = `${x}px`;
        node.style.width = `${w}px`;
        node.innerHTML = icon('transition');
        lane.append(node);
      }
    }
    $('#tlTracks').append(lane);
  });

  // 轨道头与轨道行高度对齐（真实测量，不做硬编码）
  requestAnimationFrame(() => {
    const lanes = $$('#tlTracks .track-lane');
    const heads = $$('#trackHeads .track-head');
    lanes.forEach((lane, i) => {
      if (heads[i]) heads[i].style.height = `${lane.offsetHeight}px`;
    });
  });

  renderRuler(contentWidth);
  renderMarkers();
  renderPlayhead();
}

function renderClip(track, clip) {
  const asset = assetOf(clip.asset_id);
  const isAudio = track.type === 1;
  const w = Math.max(8, timeToX(clip.duration_us));
  const node = el('div', {
    class: `clip ${state.selection && state.selection.track === track.index && state.selection.clip === clip.id ? 'is-selected' : ''} ${clip.enabled ? '' : 'is-disabled'}`,
    'data-clip': clip.id,
    'data-track': track.index,
    'data-start': clip.start_us,
    'data-dur': clip.duration_us,
    title: `${asset ? asset.name : '（无素材）'}\n${timecode(clip.start_us, FPS())} · ${duration(clip.duration_us)}`,
  });
  node.style.left = `${timeToX(clip.start_us)}px`;
  node.style.width = `${w}px`;

  const speedBadge = (clip.speed_num !== clip.speed_den)
    ? `<span class="dur">${(clip.speed_num / clip.speed_den).toFixed(2)}×</span>` : '';
  const fxBadge = clip.effect_count
    ? `<span class="clip-effect-dots">${'<i></i>'.repeat(Math.min(3, clip.effect_count))}</span>` : '';
  const filterBadge = clip.filter_style
    ? `<span class="dur" title="滤镜强度 ${Math.round(clip.filter_intensity)}">F</span>` : '';

  if (isAudio) {
    node.innerHTML = `
      <div class="clip-wave"></div>
      <div class="clip-label"><span class="nm">${escapeHtml(asset ? asset.name : '空')}</span>${speedBadge}<span class="dur">${duration(clip.duration_us)}</span></div>
      <div class="clip-handle start"></div><div class="clip-handle end"></div>`;
    attachWaveform(node.querySelector('.clip-wave'), clip, asset);
  } else {
    node.innerHTML = `
      <div class="clip-sprocket"></div>
      <div class="clip-strip"></div>
      <div class="clip-label"><span class="nm">${escapeHtml(asset ? asset.name : '空')}</span>${filterBadge}${fxBadge}<span class="dur">${duration(clip.duration_us)}</span></div>
      <div class="clip-handle start"></div><div class="clip-handle end"></div>`;
    attachFilmstrip(node.querySelector('.clip-strip'), clip, asset);
  }
  return node;
}

// 胶片条：真实缩略图按源时间铺满片段宽度
function attachFilmstrip(host, clip, asset) {
  if (!asset || asset.type !== 0) {
    host.innerHTML = `<div class="no-thumb">${icon('film', 'ic')}</div>`;
    return;
  }
  const thumbs = state.thumbs.get(asset.id);
  if (!thumbs || !thumbs.length) {
    host.innerHTML = `<div class="no-thumb">${icon('film', 'ic')}</div>`;
    loadThumbs(asset.id);
    return;
  }
  const clipW = timeToX(clip.duration_us);
  const tileH = host.clientHeight || 34;
  const tileW = Math.max(24, Math.round(tileH * 0.62));
  const count = Math.max(1, Math.ceil(clipW / tileW));
  const speed = clip.speed_num / clip.speed_den;
  const srcSpan = clip.duration_us * speed;
  const first = Math.floor((clip.start_us / Math.max(1, asset.duration_us)) * thumbs.length);
  let html = '';
  for (let i = 0; i < count; i += 1) {
    const idx = (first + Math.floor((i / count) * (srcSpan / Math.max(1, asset.duration_us)) * thumbs.length)) % thumbs.length;
    html += `<img src="/api/thumb?asset=${asset.id}&i=${idx}&w=96" alt="" loading="lazy" draggable="false">`;
  }
  host.innerHTML = html;
}

function attachWaveform(host, clip, asset) {
  if (!asset) return;
  const buckets = state.waves.get(asset.id);
  if (!buckets || !buckets.length) { loadWaveform(asset.id); return; }
  const w = Math.max(8, timeToX(clip.duration_us));
  const h = host.clientHeight || 30;
  const startRatio = clip.start_us / Math.max(1, asset.duration_us);
  const spanRatio = Math.min(1, clip.duration_us / Math.max(1, asset.duration_us));
  const from = Math.floor(startRatio * buckets.length);
  const to = Math.max(from + 1, Math.floor((startRatio + spanRatio) * buckets.length));
  const slice = buckets.slice(from, to);
  const step = Math.max(1, Math.floor(slice.length / Math.max(4, w / 2)));
  let top = [];
  let bottom = [];
  for (let i = 0; i < slice.length; i += step) {
    const v = Math.min(1, slice[i] * 2.6);
    const x = (i / slice.length) * w;
    top.push(`${x.toFixed(1)},${(h / 2 - v * (h / 2 - 2)).toFixed(1)}`);
    bottom.unshift(`${x.toFixed(1)},${(h / 2 + v * (h / 2 - 2)).toFixed(1)}`);
  }
  host.innerHTML = `<svg viewBox="0 0 ${w} ${h}" preserveAspectRatio="none">
    <polygon points="${top.concat(bottom).join(' ')}" fill="rgba(87,195,182,.55)"/>
    <line x1="0" y1="${h / 2}" x2="${w}" y2="${h / 2}" stroke="rgba(87,195,182,.25)" stroke-width=".5"/>
  </svg>`;
}

async function loadThumbs(assetId) {
  if (state.thumbs.has(assetId)) return;
  state.thumbs.set(assetId, []); // 占位，避免重复请求
  const count = 24;
  const imgs = [];
  for (let i = 0; i < count; i += 1) {
    const img = new Image();
    img.src = `/api/thumb?asset=${assetId}&i=${i}&w=96`;
    imgs.push(img);
  }
  state.thumbs.set(assetId, imgs);
  // 全部就绪后重绘一次（真实解码完成才出现胶片条）
  await Promise.all(imgs.map((img) => img.decode().catch(() => {})));
  renderTimeline();
}

async function loadWaveform(assetId) {
  if (state.waves.has(assetId)) return;
  state.waves.set(assetId, []);
  try {
    const res = await fetch(`/api/waveform?asset=${assetId}`).then((r) => r.json());
    state.waves.set(assetId, Array.isArray(res.buckets) ? res.buckets : []);
    renderTimeline();
  } catch { /* 无 FFmpeg/无素材时保持空波形，不伪造 */ }
}

// 量尺：按缩放自适应刻度，数字等宽右对齐
function renderRuler(contentWidth) {
  const canvas = $('#rulerCanvas');
  const dpr = window.devicePixelRatio || 1;
  const cssW = contentWidth;
  const cssH = $('#tlRuler').clientHeight || 26;
  canvas.width = Math.round(cssW * dpr);
  canvas.height = Math.round(cssH * dpr);
  canvas.style.width = `${cssW}px`;
  canvas.style.height = `${cssH}px`;
  const ctx = canvas.getContext('2d');
  ctx.scale(dpr, dpr);
  ctx.clearRect(0, 0, cssW, cssH);

  const pxPerSec = state.pxPerSec;
  // 选刻度步长（秒）：帧/秒/5秒/10秒/30秒/60秒
  const steps = [1 / FPS(), 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300];
  let step = steps.find((s) => s * pxPerSec >= 54) || 600;
  const minor = step / 5;

  ctx.font = '10px "JetBrains Mono", monospace';
  ctx.textBaseline = 'alphabetic';
  ctx.fillStyle = '#756B62';
  ctx.strokeStyle = '#382F2A';
  ctx.lineWidth = 1;

  const totalSec = cssW / pxPerSec;
  for (let s = 0; s <= totalSec; s += minor) {
    const x = Math.round(s * pxPerSec) + 0.5;
    const isMajor = Math.abs(s / step - Math.round(s / step)) < 1e-6;
    ctx.beginPath();
    ctx.moveTo(x, cssH - (isMajor ? 10 : 4));
    ctx.lineTo(x, cssH);
    ctx.globalAlpha = isMajor ? 1 : 0.55;
    ctx.stroke();
    ctx.globalAlpha = 1;
    if (isMajor) {
      const mm = Math.floor(s / 60);
      const ss = Math.floor(s % 60);
      const label = `${String(mm).padStart(2, '0')}:${String(ss).padStart(2, '0')}`;
      // 首个刻度从右移一点，避免与左边界重叠
      ctx.fillText(label, x + (x < 6 ? 0 : 3), 10);
    }
  }
  // 悬停时间指示由 CSS 光标承担，不额外画线
}

function renderMarkers() {
  $$('.marker').forEach((n) => n.remove());
  for (const m of state.data.markers || []) {
    const node = el('div', { class: 'marker', title: `${m.name} · ${timecode(m.time_us, FPS())}` });
    node.style.left = `${timeToX(m.time_us)}px`;
    node.style.setProperty('--flag-color', m.color || '#F0A03C');
    node.innerHTML = '<div class="marker-line"></div>';
    node.addEventListener('dblclick', () => seek(m.time_us));
    $('#tlContent').append(node);
  }
}

// 播放头：量尺读数头
function renderPlayhead() {
  const ph = $('#playhead');
  ph.style.left = `${timeToX(state.playhead)}px`;
  const label = timecode(state.playhead, FPS());
  $('#playheadReadout').textContent = label;
  $('#frameBadge').innerHTML = `<i class="dot"></i><span>${label}</span>`;

  // 播放头超出视口时贴边指示（保证「此刻」永远可见）
  const scroll = $('#tlScroll');
  const x = timeToX(state.playhead);
  const left = scroll.scrollLeft;
  const right = left + scroll.clientWidth;
  ph.classList.toggle('is-offscreen', x < left || x > right);
}

// ---------------------------------------------------------------- 检查器
function renderInspector() {
  const host = $('#inspectorBody');
  const title = $('#inspectorTitle');
  const sub = $('#inspectorSub');
  host.innerHTML = '';

  if (!selectionValid()) {
    title.textContent = '工程';
    sub.textContent = '';
    host.append(projectSection());
    host.append(subtitleSection());
    host.append(markerSection());
    $('#sbSelection').textContent = '未选择';
    return;
  }

  const { track: trackIndex, clip: clipId } = state.selection;
  const track = trackAt(trackIndex);
  const clip = findClip(trackIndex, clipId);
  const asset = assetOf(clip.asset_id);
  title.textContent = asset ? asset.name : '片段';
  sub.textContent = `${timecode(clip.start_us, FPS())} · ${duration(clip.duration_us)}`;
  $('#sbSelection').textContent = `选中 ${asset ? asset.name : '片段'}（轨道 ${trackIndex + 1}）`;

  host.append(clipTimeSection(track, clip));
  if (track.type === 0) {
    host.append(filterSection(track, clip));
    host.append(transformSection(track, clip));
    host.append(effectSection(track, clip));
  }
  if (track.type === 1) host.append(audioSection(track, clip));
  host.append(clipActionSection(track, clip));
}

function section(title, extra = '') {
  const node = el('div', { class: 'section' });
  node.innerHTML = `<div class="section-head"><span>${title}</span><span class="grow"></span>${extra}</div>`;
  return node;
}

function projectSection() {
  const p = project();
  const node = section('工程');
  const rows = [
    ['画布', `${p.width}×${p.height}`],
    ['帧率', `${(p.fps_num / p.fps_den).toFixed(3).replace(/\.?0+$/, '')} fps`],
    ['时长', timecode(p.duration_us, p.fps_num, p.fps_den)],
    ['轨道', `${p.can_undo !== undefined ? state.data.tracks.length : 0} 条`],
    ['素材', `${state.data.assets.length} 项`],
  ];
  for (const [k, v] of rows) {
    node.append(el('div', { class: 'kv' }, `<span class="k">${k}</span><span class="v">${v}</span>`));
  }
  const hint = el('p', { class: 'empty-hint' });
  hint.style.marginTop = '10px';
  hint.textContent = '选中时间轴上的片段以编辑参数。所有修改即时写入工程并进入撤销栈。';
  node.append(hint);
  return node;
}

function clipTimeSection(track, clip) {
  const node = section('时间');
  const mk = (label, value, unit, onChange, min = 0) => {
    const row = el('div', { class: 'param' });
    row.innerHTML = `<span class="param-label">${label}</span>
      <input class="slider" type="range" min="${min}" max="${Math.max(min + 1, durationUs())}" value="${value}" step="1000">
      <input class="param-value" value="${(value / 1e6).toFixed(2)}">`;
    const slider = row.querySelector('.slider');
    const input = row.querySelector('.param-value');
    const commit = async (us) => {
      const snapped = state.snap ? snapFrame(us, FPS()) : Math.round(us);
      await onChange(Math.max(0, snapped));
    };
    slider.addEventListener('input', () => { input.value = (Number(slider.value) / 1e6).toFixed(2); });
    slider.addEventListener('change', () => commit(Number(slider.value)));
    input.addEventListener('change', () => commit(Number(input.value) * 1e6));
    node.append(row);
  };
  mk('起点', clip.start_us, 's', async (us) => {
    await edit({ op: 'move', track: track.index, clip: clip.id, start_us: us });
  });
  mk('时长', clip.duration_us, 's', async (us) => {
    await edit({ op: 'trim_end', track: track.index, clip: clip.id, end_us: clip.start_us + us });
  });
  const speedRow = el('div', { class: 'param' });
  speedRow.innerHTML = `<span class="param-label">速度</span>
    <input class="slider" type="range" min="0.1" max="4" step="0.05" value="${(clip.speed_num / clip.speed_den).toFixed(2)}">
    <input class="param-value" value="${(clip.speed_num / clip.speed_den).toFixed(2)}×">`;
  const speedSlider = speedRow.querySelector('.slider');
  const speedInput = speedRow.querySelector('.param-value');
  const applySpeed = async (value) => {
    const v = Math.max(0.1, Math.min(4, Number(value) || 1));
    await edit({ op: 'speed', track: track.index, clip: clip.id, num: Math.round(v * 100), den: 100 });
  };
  speedSlider.addEventListener('input', () => { speedInput.value = `${Number(speedSlider.value).toFixed(2)}×`; });
  speedSlider.addEventListener('change', () => applySpeed(speedSlider.value));
  speedInput.addEventListener('change', () => applySpeed(parseFloat(speedInput.value)));
  node.append(speedRow);
  return node;
}

function filterSection(track, clip) {
  const node = section('滤镜', `<span class="chip">${filterName(clip.filter_style)}</span>`);
  const grid = el('div');
  grid.style.cssText = 'display:grid;grid-template-columns:repeat(4,1fr);gap:5px;margin-bottom:9px';
  const styles = ['无', '电影', '复古', '清新', '黑白', '赛博', '日系', '胶片', '暖阳', '冷调', '港风', '高级灰', '奶油', '负片'];
  styles.forEach((name, i) => {
    const b = el('button', { class: `btn btn-sm ${clip.filter_style === i ? 'is-active' : ''}` });
    b.textContent = name;
    if (clip.filter_style === i) {
      b.style.borderColor = 'var(--tungsten)';
      b.style.color = 'var(--tungsten)';
    }
    b.addEventListener('click', () => edit({
      op: 'filter', track: track.index, clip: clip.id, style: i,
      intensity: i === 0 ? 0 : (clip.filter_intensity || 65),
    }));
    grid.append(b);
  });
  node.append(grid);
  if (clip.filter_style) {
    const row = el('div', { class: 'param' });
    row.innerHTML = `<span class="param-label">强度</span>
      <input class="slider" type="range" min="0" max="100" value="${Math.round(clip.filter_intensity)}" step="1">
      <input class="param-value" value="${Math.round(clip.filter_intensity)}">`;
    const slider = row.querySelector('.slider');
    const value = row.querySelector('.param-value');
    slider.addEventListener('input', () => { value.value = slider.value; });
    slider.addEventListener('change', () => edit({
      op: 'filter', track: track.index, clip: clip.id, style: clip.filter_style, intensity: Number(slider.value),
    }));
    node.append(row);
  }
  return node;
}

function filterName(style) {
  return ['无', '电影', '复古', '清新', '黑白', '赛博', '日系', '胶片', '暖阳', '冷调', '港风', '高级灰', '奶油', '负片'][style] || '无';
}

function transformSection(track, clip) {
  const node = section('画面');
  const row = el('div', { class: 'param' });
  row.innerHTML = `<span class="param-label">不透明度</span>
    <input class="slider" type="range" min="0" max="1" step="0.01" value="${clip.opacity.toFixed(2)}">
    <input class="param-value" value="${Math.round(clip.opacity * 100)}%">`;
  const slider = row.querySelector('.slider');
  const value = row.querySelector('.param-value');
  slider.addEventListener('input', () => { value.value = `${Math.round(Number(slider.value) * 100)}%`; });
  slider.addEventListener('change', () => edit({
    op: 'opacity', track: track.index, clip: clip.id, value: Number(slider.value),
  }));
  node.append(row);

  const enableRow = el('div', { class: 'param' });
  enableRow.innerHTML = `<span class="param-label">启用片段</span><span></span>
    <button class="switch" role="switch" aria-checked="${clip.enabled}"></button>`;
  enableRow.querySelector('.switch').addEventListener('click', () => edit({
    op: 'enabled', track: track.index, clip: clip.id, value: !clip.enabled,
  }));
  node.append(enableRow);
  return node;
}

function effectSection(track, clip) {
  const node = section('特效', `<span class="num faint">${clip.effect_count} 个</span>`);
  if (!clip.effect_count) {
    const p = el('p', { class: 'empty-hint' });
    p.textContent = '此片段未添加特效。特效按顺序叠加，导出时以完整精度渲染。';
    node.append(p);
    return node;
  }
  const list = el('div');
  list.innerHTML = `<div class="kv"><span class="k">已应用</span><span class="v">${clip.effect_count} 项（真实渲染）</span></div>`;
  node.append(list);
  return node;
}

function audioSection(track, clip) {
  const node = section('音频');
  node.innerHTML += `<div class="kv"><span class="k">增益</span><span class="v">${track.gain_db.toFixed(1)} dB（轨道）</span></div>
    <div class="kv"><span class="k">降噪</span><span class="v">按片段强度处理</span></div>`;
  const row = el('div', { class: 'param' });
  row.innerHTML = `<span class="param-label">轨道增益</span>
    <input class="slider" type="range" min="-24" max="12" step="0.5" value="${track.gain_db}">
    <input class="param-value" value="${track.gain_db.toFixed(1)}">`;
  const slider = row.querySelector('.slider');
  slider.addEventListener('change', async () => {
    await api('/api/edit', { op: 'track_gain', track: track.index, gain_db: Number(slider.value) });
    await refresh();
  });
  node.append(row);
  return node;
}

function clipActionSection(track, clip) {
  const node = section('操作');
  const wrap = el('div');
  wrap.style.cssText = 'display:flex;flex-wrap:wrap;gap:6px';
  const actions = [
    ['split', '在播放头分割', () => edit({ op: 'split', track: track.index, clip: clip.id, at_us: Math.round(state.playhead) })],
    ['duplicate', '复制片段', () => edit({ op: 'duplicate', track: track.index, clip: clip.id })],
    ['ripple', '波纹删除', () => edit({ op: 'ripple_delete', track: track.index, clip: clip.id })],
    ['trash', '删除', () => edit({ op: 'delete', track: track.index, clip: clip.id })],
  ];
  for (const [ic, label, fn] of actions) {
    const b = el('button', { class: `btn btn-sm ${ic === 'trash' ? 'btn-danger' : ''}` });
    b.innerHTML = `${icon(ic, 'ic ic-sm')}<span>${label}</span>`;
    b.addEventListener('click', fn);
    wrap.append(b);
  }
  node.append(wrap);
  return node;
}

function subtitleSection() {
  const node = section('字幕', `<span class="num faint">${(state.data.subtitles || []).length} 条</span>`);
  const list = el('div');
  for (const c of (state.data.subtitles || []).slice(0, 6)) {
    const row = el('button', { class: 'row' });
    row.style.cssText = 'width:100%;text-align:left;height:auto;padding:6px 0;border-bottom:1px solid var(--ink-700)';
    row.innerHTML = `<span class="num faint" style="font-size:10px;flex:none">${timecode(c.start_us, FPS())}</span>
      <span style="flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;font-size:11px">${escapeHtml(c.text)}</span>`;
    row.addEventListener('click', () => seek(c.start_us));
    list.append(row);
  }
  if (!list.children.length) {
    const p = el('p', { class: 'empty-hint' });
    p.textContent = '还没有字幕。识别字幕与手动添加都在「文字」页签。';
    node.append(p);
  } else node.append(list);
  return node;
}

function markerSection() {
  const node = section('标记', `<span class="num faint">${(state.data.markers || []).length}</span>`);
  const list = el('div');
  for (const m of state.data.markers || []) {
    const row = el('button', { class: 'row' });
    row.style.cssText = 'width:100%;text-align:left;height:auto;padding:6px 0;border-bottom:1px solid var(--ink-700)';
    row.innerHTML = `<i style="width:6px;height:6px;background:${m.color};flex:none;clip-path:polygon(0 0,100% 0,0 100%)"></i>
      <span class="num faint" style="font-size:10px">${timecode(m.time_us, FPS())}</span>
      <span style="flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;font-size:11px">${escapeHtml(m.name)}</span>`;
    row.addEventListener('click', () => seek(m.time_us));
    list.append(row);
  }
  node.append(list);
  return node;
}

// 真实导入：/api/call 直通 nc_import_asset / nc_add_track / nc_insert_clip。
// 素材路径由设计桥自带（design/media），时长由平台层探测后传入。
async function importAssetFlow() {
  const media = await api('/api/media_library').catch(() => ({ items: [] }));
  const body = el('div');
  if (!media.items.length) {
    body.innerHTML = `<p class="empty-hint">本机没有可用素材。真机上这里会打开系统相册/文件选择器。</p>`;
    openSheet('导入素材', body);
    return;
  }
  body.innerHTML = `<div class="section-head" style="margin-bottom:8px">本机素材（真实文件）</div>`;
  const list = el('div');
  for (const item of media.items) {
    const row = el('button', { class: 'row' });
    row.style.cssText = 'width:100%;height:auto;padding:8px 0;text-align:left;border-bottom:1px solid var(--ink-700)';
    row.innerHTML = `<span class="chip">${item.type === 1 ? '音频' : '视频'}</span>
      <span style="flex:1">${escapeHtml(item.name)}</span>
      <span class="num faint" style="font-size:10px">${duration(item.duration_us)}</span>`;
    row.addEventListener('click', async () => {
      const res = await api('/api/call', {
        fn: 'nc_import_asset',
        args: { name: item.name, path: item.path, type: item.type, duration_us: item.duration_us },
      });
      const assetId = res.result?.value;
      if (!assetId) { toast('导入失败', 'alarm'); return; }
      // 没有目标轨道时先建一条（视频/音频各一类）
      const kind = item.type === 1 ? 1 : 0;
      let track = state.data.tracks.find((tr) => tr.type === kind);
      if (!track) {
        await api('/api/call', { fn: 'nc_add_track', args: { name: kind ? '音频' : '主画面', track_type: kind } });
        await refresh();
        track = [...state.data.tracks].reverse().find((tr) => tr.type === kind);
      }
      await api('/api/call', {
        fn: 'nc_insert_clip',
        args: { track_index: track.index, asset_id: assetId, start_us: 0, duration_us: Math.min(item.duration_us, 6000000) },
      });
      sheet.close();
      await refresh();
      await renderStage();
      toast(`已导入 ${item.name}`);
    });
    list.append(row);
  }
  body.append(list);
  const sheet = openSheet('导入素材', body);
}

async function loadDemoProject() {
  const list = await api('/api/projects');
  const file = list.projects?.[0]?.file;
  if (!file) { toast('没有已保存的草稿', 'alarm'); return; }
  const res = await api('/api/load', { file });
  if (!res.ok) { toast(res.message || '载入失败', 'alarm'); return; }
  await refresh({ keepSelection: false });
  await renderStage();
  toast(`已载入 ${file}`);
}

// ---------------------------------------------------------------- 交互
async function edit(payload) {
  const res = await api('/api/edit', payload);
  if (!res.ok) {
    toast(res.message || '操作未生效', 'alarm');
    return res;
  }
  await refresh();
  await renderStage();
  return res;
}

function seek(us) {
  state.playhead = Math.max(0, Math.min(durationUs(), Math.round(us)));
  renderPlayhead();
  renderStage();
  if (state.playing) scheduleTick();
}

// 播放：真实按时间推进（由 requestAnimationFrame 驱动；不是引擎渲染帧率）
let playStart = 0;
let playAnchor = 0;
function startPlay() {
  if (!durationUs()) { toast('时间轴为空，先导入素材', 'alarm'); return; }
  state.playing = true;
  playAnchor = state.playhead;
  playStart = performance.now();
  $('#playBtn').innerHTML = icon('pause');
  // 播放前预取后续帧（真实渲染），减少拖动时等待
  const from = state.playhead;
  const to = Math.min(durationUs(), from + 2_000_000);
  fetch(`/api/prefetch?from=${from}&to=${to}&n=20&w=360`).catch(() => {});
  scheduleTick();
}
function stopPlay() {
  state.playing = false;
  $('#playBtn').innerHTML = icon('play');
}
function scheduleTick() {
  if (!state.playing) return;
  const elapsed = (performance.now() - playStart) * 1000;
  let t = playAnchor + elapsed;
  const total = durationUs();
  if (t >= total) {
    if (state.loop) {
      playStart = performance.now();
      playAnchor = 0;
      t = 0;
    } else {
      seek(total);
      stopPlay();
      return;
    }
  }
  state.playhead = t;
  renderPlayhead();
  renderStage();
  requestAnimationFrame(scheduleTick);
}

// ---------------------------------------------------------------- 轨道搬家
// 两条路径都能把「整条轨道」连同其中全部内容上下移动：
//   A. 长按轨道头（小眼睛向右的空白处，以及折叠后图标所在的整个区域）
//   B. 长按轨道上的片段
//
// 「整条轨道」的语义由核心层保证：片段、片段上的效果/关键帧/速度曲线、
// 转场、调整图层特效栈、静音/隐藏/锁定/独奏标志、轨道增益、高度
// 全都存放在 Track 结构体内部（core/include/nairoclip/core/track.h），
// moveTrack 只换 tracks 数组中的位置，因此这些内容自动整体搬移。
//
// 轨道头图标（小眼睛等）不是独立图层：它们由 renderTrackHeads 按
// tracks 数组顺序重绘，轨道换位后自然跟到新位置。折叠态下高度变矮，
// 轨道头同样按数组顺序重绘，因此折叠/展开都不影响跟手。
const TRACK_HOLD_MS = 350;   // 长按判定；短于此视为点击（选中/改播放头）
/// 长按搬家结束后，浏览器的补发 click 要吃掉一次。
/// 用计数器而非布尔：极端情况下可能连发多次。
let suppressNextClick = false;
let suppressClickTimer = 0;
const TRACK_HOLD_SLOP = 8;   // 长按前允许的抖动像素

function initTrackDrag() {
  const scroll = $('#tlScroll');
  const heads = $('#trackHeads');
  let pending = null;  // { from, startX, startY, timer, pointerId }
  let active = null;   // { from, to, node, placeholder }

  const trackIndexAt = (clientY) => {
    // 以轨道头为准做命中：轨道头与轨道行一一对应且已对齐高度。
    // 这样即使长按发生在片段上，也能命中片段所属的那一条轨道。
    const nodes = $$('#trackHeads .track-head');
    for (const n of nodes) {
      const r = n.getBoundingClientRect();
      if (clientY >= r.top && clientY <= r.bottom) return Number(n.dataset.index);
    }
    // 落在最后一条之下：贴到末尾；落在第一条之上：贴到开头
    if (!nodes.length) return -1;
    const first = nodes[0].getBoundingClientRect();
    return clientY < first.top ? 0 : nodes.length - 1;
  };

  const begin = (event) => {
    if (active) return;
    const headNode = event.target.closest('#trackHeads .track-head');
    const clipNode = event.target.closest('.clip');
    // 轨道头上的图标按钮（小眼睛/锁）与轨道名：点击即切换，不参与长按搬家
    if (headNode && event.target.closest('button[data-flag], button.track-name')) return;
    if (!headNode && !clipNode) return;

    const from = Number((headNode ?? clipNode).dataset.index);
    if (!Number.isInteger(from) || from < 0) return;
    const track = state.data.tracks.find((t) => t.index === from);
    if (!track) return;
    if (track.locked) {
      // 锁定轨道不可移动（与核心层 MoveTrackCommand 的校验一致）
      toast('轨道已锁定，无法移动', 'alarm');
      return;
    }

    pending = {
      from,
      startX: event.clientX,
      startY: event.clientY,
      pointerId: event.pointerId,
      timer: setTimeout(() => {
        pending = null;
        enterDragMode(from, event.clientX, event.clientY);
      }, TRACK_HOLD_MS),
    };
  };

  const enterDragMode = (from, clientX, clientY) => {
    const headNode = $$('#trackHeads .track-head').find((n) => Number(n.dataset.index) === from);
    const laneNode = $$('#tlTracks .track-lane').find((n) => Number(n.dataset.index) === from);
    if (!headNode) return;

    // 视觉：抬起原始行，插入一条占位
    const rect = headNode.getBoundingClientRect();
    const laneRect = laneNode?.getBoundingClientRect();
    ([headNode, laneNode].filter(Boolean)).forEach((n) => n.classList.add('is-dragging'));

    const ghost = headNode.cloneNode(true);
    ghost.classList.add('track-drag-ghost');
    ghost.style.width = `${rect.width}px`;
    ghost.style.top = `${rect.top}px`;
    document.body.append(ghost);

    active = { from, to: from, ghost, headNode, laneNode, clientX, clientY };
    if (navigator.vibrate) navigator.vibrate(8);
    showTrackDropHint(from);
  };

  const move = (event) => {
    if (pending) {
      const dx = Math.abs(event.clientX - pending.startX);
      const dy = Math.abs(event.clientY - pending.startY);
      // 长按判定前偏移过大 -> 视为滚动/拖片段，放弃搬家
      if (dx > TRACK_HOLD_SLOP || dy > TRACK_HOLD_SLOP) {
        clearTimeout(pending.timer);
        pending = null;
      }
      return;
    }
    if (!active) return;
    active.clientX = event.clientX;
    active.clientY = event.clientY;
    active.ghost.style.top = `${event.clientY - 18}px`;
    const to = trackIndexAt(event.clientY);
    if (to >= 0 && to !== active.to) {
      active.to = to;
      showTrackDropHint(to);
    }
  };

  const finish = async (event, cancelled) => {
    if (pending) {
      clearTimeout(pending.timer);
      pending = null;
    }
    if (!active) return;
    const a = active;
    active = null;
    ([a.headNode, a.laneNode].filter(Boolean)).forEach((n) => n.classList.remove('is-dragging'));
    a.ghost.remove();
    hideTrackDropHint();
    // 只要真的进入过搬家态，就吃掉随后那次 click ——
    // 即便落点没变（用户长按后又放回原处），也不该触发重命名。
    // click 在 pointerup 之后的**下一个任务**里派发，所以不能用 0ms 定时器
    // 清标志（那样会在 click 之前就清掉，抑制失效）。给 350ms 窗口，
    // 覆盖移动端合成 click 的常见延迟，又不至于误吞用户的正常点击。
    suppressNextClick = true;
    clearTimeout(suppressClickTimer);
    suppressClickTimer = setTimeout(() => { suppressNextClick = false; }, 350);
    if (cancelled || a.to === a.from) return;
    // 一次拖动只落一条历史：核心层 MoveTrackCommand 自带可撤销快照
    await edit({ op: 'track_move', track: a.from, to_index: a.to });
  };

  heads.addEventListener('pointerdown', begin);
  scroll.addEventListener('pointerdown', begin);

  // ---- 冲突 A：轨道头与轨道行是两个独立滚动容器 ----
  // .tl-heads-list 是 overflow:hidden，.tl-scroll 是 overflow:auto，
  // 两者之间原本没有任何同步。纵向滚动后「头」不动而「行」动，
  // 轨道头就会和它对应的轨道错位 —— 长按搬家的落点判定也会跟着错。
  // 这里做单向同步：滚 tl-scroll 时把 heads 的 scrollTop 拉齐。
  const syncScroll = () => {
    if (heads.scrollTop !== scroll.scrollTop) heads.scrollTop = scroll.scrollTop;
  };
  scroll.addEventListener('scroll', syncScroll, { passive: true });
  // 轨道头侧不设可滚动：用户只能从轨道行区域滚，避免两个方向打架
  heads.style.overflowY = 'hidden';
  window.addEventListener('pointermove', move, { passive: true });
  window.addEventListener('pointerup', (e) => finish(e, false));
  window.addEventListener('pointercancel', (e) => finish(e, true));

  // 拖到边缘自动滚动：轨道多时手指停在视口底部仍能继续往上/下搬
  let autoScrollRaf = 0;
  const autoScroll = () => {
    autoScrollRaf = 0;
    if (!active) return;
    const r = scroll.getBoundingClientRect();
    const edge = 48;
    if (active.clientY > r.bottom - edge) scroll.scrollTop += 8;
    else if (active.clientY < r.top + edge) scroll.scrollTop -= 8;
    autoScrollRaf = requestAnimationFrame(autoScroll);
  };
  window.addEventListener('pointermove', () => {
    if (active && !autoScrollRaf) autoScrollRaf = requestAnimationFrame(autoScroll);
  }, { passive: true });
}

let trackDropHintNode = null;
function showTrackDropHint(index) {
  const lane = $$('#tlTracks .track-lane').find((n) => Number(n.dataset.index) === index);
  if (!lane) return;
  if (!trackDropHintNode) {
    trackDropHintNode = el('div', { class: 'track-drop-hint' });
    document.body.append(trackDropHintNode);
  }
  const r = lane.getBoundingClientRect();
  trackDropHintNode.style.top = `${r.top}px`;
  trackDropHintNode.style.height = `${r.height}px`;
  trackDropHintNode.style.left = `${r.left}px`;
  trackDropHintNode.style.width = `${r.width}px`;
  trackDropHintNode.style.display = 'block';
}
function hideTrackDropHint() {
  if (trackDropHintNode) trackDropHintNode.style.display = 'none';
}

// 播放头拖拽（命中 24px 宽区）
function initPlayheadDrag() {
  const scroll = $('#tlScroll');
  let dragging = false;
  const pick = (event) => {
    const rect = scroll.getBoundingClientRect();
    const x = event.clientX - rect.left + scroll.scrollLeft;
    return xToTime(x);
  };
  scroll.addEventListener('pointerdown', (event) => {
    const target = event.target;
    if (target.closest('.clip') || target.closest('.transition')) return;
    if (target.closest('.tl-ruler') || target.closest('.track-lane') || target === scroll) {
      dragging = true;
      scroll.setPointerCapture(event.pointerId);
      seek(state.snap ? snapFrame(pick(event), FPS()) : pick(event));
    }
  });
  scroll.addEventListener('pointermove', (event) => {
    if (!dragging) return;
    seek(state.snap ? snapFrame(pick(event), FPS()) : pick(event));
  });
  scroll.addEventListener('pointerup', (event) => {
    if (dragging) {
      dragging = false;
      scroll.releasePointerCapture?.(event.pointerId);
      // 松手后请求高质量帧（预览分级：交互中用粗、静止后细）
      renderStageHighQuality();
    }
  });
}

async function renderStageHighQuality() {
  const p = project();
  const t = Math.round(state.playhead);
  const url = `/api/frame?t=${t}&w=${Math.min(900, p.width)}`;
  try {
    const img = new Image();
    img.src = url;
    await img.decode();
    $('#frameImg').src = url;
  } catch { /* 保留当前帧 */ }
}

// 片段拖动 / 裁剪 / 选中（Pointer Events，真实落盘）
function initClipGestures() {
  const scroll = $('#tlScroll');
  let drag = null;

  scroll.addEventListener('pointerdown', (event) => {
    const clipNode = event.target.closest('.clip');
    if (!clipNode) return;
    if (state.tool === 'blade') {
      // 切割工具：点击位置即切割点
      const rect = scroll.getBoundingClientRect();
      const x = event.clientX - rect.left + scroll.scrollLeft;
      const at = Math.max(0, xToTime(x));
      edit({ op: 'split', track: Number(clipNode.dataset.track), clip: Number(clipNode.dataset.clip), at_us: Math.round(at) });
      return;
    }
    const trackIndex = Number(clipNode.dataset.track);
    state.selection = { track: trackIndex, clip: Number(clipNode.dataset.clip) };
    renderInspector();
    $$('.clip').forEach((n) => n.classList.remove('is-selected'));
    clipNode.classList.add('is-selected');

    const handle = event.target.closest('.clip-handle');
    const startUs = Number(clipNode.dataset.start);
    const durUs = Number(clipNode.dataset.dur);
    drag = {
      mode: handle ? (handle.classList.contains('start') ? 'trimStart' : 'trimEnd') : 'move',
      trackIndex,
      clipId: Number(clipNode.dataset.clip),
      startX: event.clientX,
      origStart: startUs,
      origDur: durUs,
      moved: false,
      node: clipNode,
    };
    scroll.setPointerCapture(event.pointerId);
    event.preventDefault();
  });

  scroll.addEventListener('pointermove', (event) => {
    if (!drag) return;
    const dx = event.clientX - drag.startX;
    if (Math.abs(dx) < 2 && !drag.moved) return;
    drag.moved = true;
    const deltaUs = (dx / state.pxPerSec) * 1e6;
    const minDur = (1e6 / FPS()) * 2; // 最短 2 帧（引擎会校验真实范围）
    if (drag.mode === 'move') {
      const target = Math.max(0, drag.origStart + deltaUs);
      drag.previewStart = state.snap ? snapFrame(target, FPS()) : target;
      drag.node.style.left = `${timeToX(drag.previewStart)}px`;
      if (state.snap) showSnapGuide(drag.previewStart);
    } else if (drag.mode === 'trimStart') {
      const target = drag.origStart + deltaUs;
      const maxStart = drag.origStart + drag.origDur - minDur;
      const next = Math.min(maxStart, Math.max(0, target));
      drag.previewStart = state.snap ? snapFrame(next, FPS()) : next;
      drag.node.style.left = `${timeToX(drag.previewStart)}px`;
      drag.node.style.width = `${timeToX(drag.origStart + drag.origDur - drag.previewStart)}px`;
      showReadout(`${timecode(drag.previewStart, FPS())}  ·  ${duration(drag.origStart + drag.origDur - drag.previewStart)}`);
    } else {
      const end = drag.origStart + drag.origDur + deltaUs;
      const target = Math.max(drag.origStart + minDur, end);
      drag.previewEnd = state.snap ? snapFrame(target, FPS()) : target;
      drag.node.style.width = `${timeToX(drag.previewEnd - drag.origStart)}px`;
      showReadout(`${timecode(drag.previewEnd, FPS())}  ·  ${duration(drag.previewEnd - drag.origStart)}`);
    }
  });

  scroll.addEventListener('pointerup', async (event) => {
    if (!drag) return;
    const d = drag;
    drag = null;
    hideSnapGuide();
    hideReadout();
    if (!d.moved) return;
    if (d.mode === 'move') {
      await edit({ op: 'move', track: d.trackIndex, clip: d.clipId, start_us: Math.round(d.previewStart ?? d.origStart) });
    } else if (d.mode === 'trimStart') {
      await edit({ op: 'trim_start', track: d.trackIndex, clip: d.clipId, start_us: Math.round(d.previewStart ?? d.origStart) });
    } else {
      await edit({ op: 'trim_end', track: d.trackIndex, clip: d.clipId, end_us: Math.round(d.previewEnd ?? d.origStart + d.origDur) });
    }
  });
}

// 拖动时的吸附指示与读数
function showSnapGuide(us) {
  let guide = $('#snapGuide');
  if (!guide) {
    guide = el('div', { id: 'snapGuide' });
    guide.style.cssText = 'position:absolute;top:0;bottom:0;width:1px;background:var(--signal);opacity:.7;z-index:7;pointer-events:none';
    $('#tlContent').append(guide);
  }
  guide.style.left = `${timeToX(us)}px`;
  showReadout(timecode(us, FPS()));
}
function hideSnapGuide() { $('#snapGuide')?.remove(); }

function showReadout(text) {
  let node = $('#dragReadout');
  if (!node) {
    node = el('div', { id: 'dragReadout', class: 'num' });
    node.style.cssText = `position:absolute;z-index:9;pointer-events:none;background:var(--ink-600);
      border:1px solid var(--ink-450);color:var(--paper);font-size:10px;padding:2px 6px;border-radius:2px;
      left:12px;top:6px`;
    $('#tlContent').append(node);
  }
  node.textContent = text;
}
function hideReadout() { $('#dragReadout')?.remove(); }

// ---------------------------------------------------------------- 抽屉：导出 / AI / 保存
function openSheet(title, bodyNode, footNode) {
  const scrim = el('div', { class: 'scrim' });
  const sheet = el('div', { class: 'sheet' });
  sheet.innerHTML = `<div class="sheet-head"><span class="sheet-title">${title}</span>
    <button class="btn-icon" data-close>${icon('close')}</button></div>`;
  const body = el('div', { class: 'sheet-body' });
  body.append(bodyNode);
  sheet.append(body);
  if (footNode) {
    const foot = el('div', { class: 'sheet-foot' });
    foot.append(footNode);
    sheet.append(foot);
  }
  scrim.append(sheet);
  $('#layers').append(scrim);
  const close = () => scrim.remove();
  scrim.addEventListener('click', (e) => { if (e.target === scrim) close(); });
  sheet.querySelector('[data-close]').addEventListener('click', close);
  document.addEventListener('keydown', function esc(e) {
    if (e.key === 'Escape') { close(); document.removeEventListener('keydown', esc); }
  });
  return { scrim, close };
}

async function openExport() {
  const presets = await api('/api/presets');
  const body = el('div');
  body.innerHTML = `
    <div class="section-head" style="margin-bottom:8px">平台预设</div>
    <div id="presetGrid" style="display:grid;grid-template-columns:repeat(3,1fr);gap:6px;margin-bottom:14px"></div>
    <div class="section-head" style="margin-bottom:8px">性能模式</div>
    <div class="seg" id="modeSeg" style="margin-bottom:14px">
      <button data-mode="0">极速（暂停编辑）</button>
      <button data-mode="1" aria-selected="true">平衡</button>
      <button data-mode="2">省电（静默）</button>
    </div>
    <div class="section-head" style="margin-bottom:8px">真实预估</div>
    <div id="estimateBox"></div>
    <div id="exportNotice" style="margin-top:12px"></div>`;
  const foot = el('div');
  foot.style.cssText = 'display:flex;gap:8px;align-items:center;width:100%';
  foot.innerHTML = '<span id="exportState" class="faint" style="font-size:11px;flex:1"></span>';
  const cancel = el('button', { class: 'btn btn-danger' }); cancel.textContent = '取消';
  cancel.classList.add('hidden');
  const run = el('button', { class: 'btn btn-primary chamfer-sm' }); run.textContent = '开始导出';
  foot.append(cancel, run);

  const sheet = openSheet('导出', body, foot);
  let presetId = 1;
  let mode = 1;

  const grid = body.querySelector('#presetGrid');
  presets.presets.forEach((p) => {
    const b = el('button', { class: `btn ${p.id === presetId ? '' : ''}` });
    b.style.cssText = 'height:auto;padding:8px;flex-direction:column;align-items:flex-start;gap:3px';
    b.innerHTML = `<span style="font-weight:700;font-size:11.5px">${p.label}</span>
      <span class="num faint" style="font-size:10px">${p.width ? `${p.width}×${p.height}` : '序列设置'}${p.requires_pro ? ' · Pro' : ''}</span>`;
    if (p.id === presetId) { b.style.borderColor = 'var(--tungsten)'; b.style.color = 'var(--tungsten)'; }
    b.addEventListener('click', () => { presetId = p.id; refreshExportUi(); });
    grid.append(b);
  });
  body.querySelectorAll('#modeSeg button').forEach((b) => {
    b.addEventListener('click', () => {
      mode = Number(b.dataset.mode);
      body.querySelectorAll('#modeSeg button').forEach((x) => x.setAttribute('aria-selected', String(x === b)));
      refreshExportUi();
    });
  });

  async function refreshExportUi() {
    grid.querySelectorAll('button').forEach((b, i) => {
      const active = presets.presets[i].id === presetId;
      b.style.borderColor = active ? 'var(--tungsten)' : 'var(--ink-500)';
      b.style.color = active ? 'var(--tungsten)' : 'var(--paper-2)';
    });
    const est = await api(`/api/estimate?preset=${presetId}&mode=${mode}`);
    body.querySelector('#estimateBox').innerHTML = `
      <div class="kv"><span class="k">输出规格</span><span class="v">${est.width || project().width}×${est.height || project().height} · ${est.fps || FPS()}fps</span></div>
      <div class="kv"><span class="k">文件大小</span><span class="v">${est.size_text || '—'}</span></div>
      <div class="kv"><span class="k">本机渲染</span><span class="v">${est.time_text || '—'}</span></div>
      <div class="kv"><span class="k">编码</span><span class="v">${est.preset_name || '—'}</span></div>`;
    const notice = body.querySelector('#exportNotice');
    const msgs = [];
    if (est.downgraded) msgs.push(['warning', `权益限制：输出将降为 ${est.width}×${est.height}${est.warning ? `（${est.warning}）` : ''}`]);
    if (est.would_clamp) msgs.push(['warning', '该分辨率超出当前会员权益，导出会被降级并附加水印。']);
    if (mode === 0) msgs.push(['info', '极速模式会暂停编辑交互以换取最快速度。']);
    notice.innerHTML = msgs.map(([kind, text]) => `<div class="notice ${kind === 'warning' ? 'is-alarm' : ''}" style="margin-bottom:6px">
      ${icon(kind === 'warning' ? 'warning' : 'info', 'ic ic-sm')}<span>${text}</span></div>`).join('');
  }
  await refreshExportUi();

  run.addEventListener('click', async () => {
    run.disabled = true;
    cancel.classList.remove('hidden');
    body.querySelector('#exportState').textContent = '正在导出（真实编码）…';
    const res = await api('/api/export', {
      path: `design/out/export_${Date.now()}.mp4`, preset: presetId, mode, attach_ai_label: true,
    });
    if (!res.ok) {
      body.querySelector('#exportState').textContent = res.message || '导出未启动';
      run.disabled = false; cancel.classList.add('hidden');
      return;
    }
    const meter = el('div', { class: 'meter' });
    meter.innerHTML = '<i style="width:0%"></i>';
    body.append(meter);
    const poll = setInterval(async () => {
      const s = await api('/api/export_status');
      meter.querySelector('i').style.width = `${Math.round(s.progress * 100)}%`;
      body.querySelector('#exportState').textContent = `已编码 ${Math.round(s.progress * 100)}%`;
      if (!s.running) {
        clearInterval(poll);
        cancel.classList.add('hidden');
        run.disabled = false;
        if (s.frames >= 0) {
          body.querySelector('#exportState').textContent = `完成：${s.frames} 帧 → ${s.path}`;
          toast('导出完成');
        } else {
          body.querySelector('#exportState').textContent = `失败：${s.error || '未知原因'}`;
          toast(s.error || '导出失败', 'alarm');
        }
      }
    }, 400);
    cancel.onclick = async () => {
      await api('/api/export_cancel', {});
      body.querySelector('#exportState').textContent = '正在取消…';
    };
  });
  return sheet;
}

async function openAiPanel() {
  const [features, models] = await Promise.all([
    api(`/api/ai_features?logged_in=1`), api('/api/models'),
  ]);
  const body = el('div');
  const names = { 1: '智能抠像', 2: '光流补帧', 3: '超分辨率', 4: '人声分离', 5: '字幕翻译', 6: '文本转语音', 7: '镜头追踪', 8: '深度图', 9: '参考仿剪', 10: '识别字幕' };
  body.innerHTML = `<div class="section-head" style="margin-bottom:8px">能力与门控（真实状态）</div>`;
  const list = el('div');
  for (const f of features.features) {
    const row = el('div');
    row.style.cssText = 'padding:8px 0;border-bottom:1px solid var(--ink-700)';
    const chips = [];
    if (f.requires_network) chips.push('<span class="chip">需登录/联网</span>');
    if (f.requires_pro) chips.push('<span class="chip is-pro">Pro</span>');
    if (!f.available) chips.push('<span class="chip is-alarm">不可用</span>');
    row.innerHTML = `<div style="display:flex;align-items:center;gap:8px">
        <span style="font-size:12px;font-weight:700">${names[f.id] || f.id}</span>
        <span style="flex:1"></span>${chips.join('')}</div>
      <div class="faint" style="font-size:10.5px;margin-top:3px">${escapeHtml(f.reason || '')}</div>`;
    list.append(row);
  }
  body.append(list);
  body.insertAdjacentHTML('beforeend', `<div class="section-head" style="margin:14px 0 8px">模型安装状态</div>`);
  const mlist = el('div');
  for (const m of models.models.slice(0, 8)) {
    const statusText = ['未下载', '下载中', '已安装', '损坏', '可更新', '校验中'][m.status] || '未知';
    mlist.insertAdjacentHTML('beforeend', `<div class="kv">
      <span class="k">${escapeHtml(m.name)}</span>
      <span class="v">${statusText} · ${bytes(m.size_bytes)}</span></div>`);
  }
  body.append(mlist);
  body.insertAdjacentHTML('beforeend', `<div class="notice is-signal" style="margin-top:12px">
    ${icon('info', 'ic ic-sm')}<span>推理全部在本地完成；模型文件由平台层下载并校验，未安装时功能明确不可用。</span></div>`);
  openSheet('AI 工具', body);
}

async function openSaveDialog() {
  const body = el('div');
  body.innerHTML = `<label class="section-head" style="margin-bottom:6px">草稿名称</label>
    <input class="field" id="saveName" value="${escapeHtml(project().name)}">
    <p class="empty-hint" style="margin-top:8px">保存为真实 .nclip 工程文件（含轨道/片段/字幕/标记/效果），
    可在首页草稿列表重新打开。</p>`;
  const foot = el('div');
  foot.style.cssText = 'display:flex;gap:8px;justify-content:flex-end;width:100%';
  const cancel = el('button', { class: 'btn' }); cancel.textContent = '取消';
  const save = el('button', { class: 'btn btn-primary chamfer-sm' }); save.textContent = '保存';
  foot.append(cancel, save);
  const sheet = openSheet('保存草稿', body, foot);
  cancel.addEventListener('click', sheet.close);
  save.addEventListener('click', async () => {
    const name = body.querySelector('#saveName').value.trim() || '未命名项目';
    const res = await api('/api/save', { name });
    if (res.ok) {
      toast(`已保存 ${res.file}`);
      $('#saveState').innerHTML = `<i class="dot"></i><span>已保存 · ${bytes(res.bytes)}</span>`;
      sheet.close();
      await refresh();
    } else {
      toast(res.message || '保存失败', 'alarm');
    }
  });
}

// ---------------------------------------------------------------- 事件绑定
function bind() {
  const map = {
    toStartBtn: 'start', prevFrameBtn: 'chevronLeft', playBtn: 'play',
    nextFrameBtn: 'chevronRight', toEndBtn: 'end', loopBtn: 'loop',
    snapBtn: 'magnet', volumeBtn: 'volume', zoomInBtn: 'plus', zoomOutBtn: 'minus',
    inspectorClose: 'panel',
  };
  for (const [id, ic] of Object.entries(map)) {
    const node = document.getElementById(id);
    if (node) node.innerHTML = icon(ic);
  }
  $('#railInspector').innerHTML = icon('panel');
  document.querySelectorAll('[data-nav]').forEach((b) => {
    b.innerHTML = icon({ home: 'home', editor: 'film', media: 'layers', ai: 'sparkle', settings: 'settings' }[b.dataset.nav]);
    b.addEventListener('click', () => {
      if (b.dataset.nav === 'ai') openAiPanel();
      else if (b.dataset.nav === 'home') location.href = '/pages/home.html';
      else if (b.dataset.nav === 'settings') location.href = '/pages/settings.html';
    });
  });

  $('#undoBtn').addEventListener('click', async () => { await edit({ op: 'undo' }); await renderStage(); });
  $('#redoBtn').addEventListener('click', async () => { await edit({ op: 'redo' }); await renderStage(); });
  $('#splitBtn').addEventListener('click', splitAtPlayhead);
  $('#markerBtn').addEventListener('click', async () => {
    await edit({ op: 'marker', time_us: Math.round(state.playhead), name: '标记', color: '#F0A03C' });
  });
  $('#deleteBtn').addEventListener('click', () => {
    if (!state.selection) return;
    edit({ op: 'delete', track: state.selection.track, clip: state.selection.clip });
  });
  $('#saveBtn').addEventListener('click', openSaveDialog);
  $('#exportBtn').addEventListener('click', openExport);
  $('#playBtn').addEventListener('click', () => (state.playing ? stopPlay() : startPlay()));
  $('#toStartBtn').addEventListener('click', () => seek(0));
  $('#toEndBtn').addEventListener('click', () => seek(durationUs()));
  $('#prevFrameBtn').addEventListener('click', () => seek(state.playhead - 1e6 / FPS()));
  $('#nextFrameBtn').addEventListener('click', () => seek(state.playhead + 1e6 / FPS()));
  $('#loopBtn').addEventListener('click', (e) => {
    state.loop = !state.loop;
    e.currentTarget.setAttribute('aria-pressed', String(state.loop));
  });
  $('#snapBtn').addEventListener('click', (e) => {
    state.snap = !state.snap;
    e.currentTarget.setAttribute('aria-pressed', String(state.snap));
    toast(state.snap ? '吸附已开启' : '吸附已关闭');
  });
  $('#safeBtn').addEventListener('click', (e) => {
    const on = $('#frameGrid').classList.toggle('is-on');
    e.currentTarget.setAttribute('aria-pressed', String(on));
  });
  $('#qualityBtn').addEventListener('click', () => {
    state.quality = state.quality >= 0.9 ? 0.4 : Math.min(1, state.quality + 0.2);
    renderQualityLabel();
    renderStage();
  });
  $('#zoomSlider').addEventListener('input', (e) => {
    state.pxPerSec = 18 + (Number(e.target.value) / 100) ** 1.6 * 420;
    renderTimeline();
  });
  $('#zoomInBtn').addEventListener('click', () => {
    state.pxPerSec = Math.min(460, state.pxPerSec * 1.35);
    $('#zoomSlider').value = Math.min(100, Number($('#zoomSlider').value) + 6);
    renderTimeline();
  });
  $('#zoomOutBtn').addEventListener('click', () => {
    state.pxPerSec = Math.max(14, state.pxPerSec / 1.35);
    $('#zoomSlider').value = Math.max(0, Number($('#zoomSlider').value) - 6);
    renderTimeline();
  });
  $('#addVideoTrackBtn').addEventListener('click', async () => {
    const res = await edit({ op: 'add_track', name: `视频 ${state.data.tracks.filter(t => t.type === 0).length + 1}`, type: 0 });
    if (!res.ok) toast(res.message || '无法新增轨道', 'alarm');
  });
  $('#addAudioTrackBtn').addEventListener('click', async () => {
    const res = await edit({ op: 'add_track', name: `音频 ${state.data.tracks.filter(t => t.type === 1).length + 1}`, type: 1 });
    if (!res.ok) toast(res.message || '无法新增轨道', 'alarm');
  });
  $('#toolSeg').addEventListener('click', (e) => {
    const btn = e.target.closest('button[data-tool]');
    if (!btn) return;
    state.tool = btn.dataset.tool;
    $$('#toolSeg button').forEach((b) => b.setAttribute('aria-selected', String(b === btn)));
    $('#tlHint').textContent = {
      select: '拖动片段移动 · 拖边缘裁剪 · S 分割',
      blade: '点击片段在指针处切割',
      ripple: '删除后自动前移（选中片段可波纹删除）',
    }[state.tool];
  });
  $('#railInspector').addEventListener('click', () => {
    state.inspectorOpen = !state.inspectorOpen;
    $('#workspace').classList.toggle('is-inspector-off', !state.inspectorOpen);
  });
  $('#inspectorClose').addEventListener('click', () => {
    state.inspectorOpen = false;
    $('#workspace').classList.add('is-inspector-off');
  });
  $('#projectName').addEventListener('click', async () => {
    const name = prompt('项目名称', project().name);
    if (name && name.trim()) await edit({ op: 'rename_project', name: name.trim() });
  });

  // 轨道头：锁定/静音/隐藏、重命名
  $('#trackHeads').addEventListener('click', async (e) => {
    // 长按搬家刚结束时，浏览器仍会补发一次 click。
    // 若不拦，手指落在轨道名上会直接弹出重命名 prompt（冲突 B）。
    if (suppressNextClick) {
      suppressNextClick = false;
      e.preventDefault();
      e.stopPropagation();
      return;
    }
    const head = e.target.closest('.track-head');
    if (!head) return;
    const index = Number(head.dataset.index);

    // 收起 / 展开图标：只改视图状态，不动轨道顺序
    const collapseBtn = e.target.closest('[data-collapse]');
    if (collapseBtn) {
      const track = trackAt(index);
      const wantCollapsed = collapseBtn.dataset.collapse === '1';
      await api('/api/edit', {
        op: 'track_view',
        track: index,
        height_px: track.height_px ?? (track.type === 1 ? 46 : 56),
        collapsed: wantCollapsed,
      });
      await refresh();
      return;
    }

    const flagBtn = e.target.closest('[data-flag]');
    if (flagBtn) {
      const track = trackAt(index);
      const flag = flagBtn.dataset.flag;
      const value = !(flag === 'locked' ? track.locked : flag === 'muted' ? track.muted : track.hidden);
      await edit({ op: 'track_flag', track: index, flag, value });
      return;
    }
    if (e.target.closest('.track-name')) {
      const track = trackAt(index);
      const name = prompt('轨道名称', track.name);
      if (name && name.trim()) {
        await api('/api/edit', { op: 'rename_track', track: index, name: name.trim() });
        await refresh();
      }
    }
  });

  // 键盘
  document.addEventListener('keydown', (e) => {
    if (e.target.matches('input, textarea')) return;
    if (e.code === 'Space') { e.preventDefault(); state.playing ? stopPlay() : startPlay(); }
    else if (e.key === 's' || e.key === 'S') splitAtPlayhead();
    else if (e.key === 'm' || e.key === 'M') edit({ op: 'marker', time_us: Math.round(state.playhead), name: '标记', color: '#F0A03C' });
    else if (e.key === 'Delete' || e.key === 'Backspace') {
      if (state.selection) edit({ op: 'delete', track: state.selection.track, clip: state.selection.clip });
    } else if ((e.metaKey || e.ctrlKey) && e.key.toLowerCase() === 'z') {
      e.preventDefault();
      edit({ op: e.shiftKey ? 'redo' : 'undo' }).then(renderStage);
    } else if (e.key === 'ArrowLeft') { e.preventDefault(); seek(state.playhead - (e.shiftKey ? 10 : 1) * 1e6 / FPS()); }
    else if (e.key === 'ArrowRight') { e.preventDefault(); seek(state.playhead + (e.shiftKey ? 10 : 1) * 1e6 / FPS()); }
    else if (e.key === 'Home') seek(0);
    else if (e.key === 'End') seek(durationUs());
    else if (e.key === 'Escape') { state.selection = null; renderInspector(); $$('.clip').forEach((n) => n.classList.remove('is-selected')); }
  });
}

async function splitAtPlayhead() {
  if (!state.selection) { toast('先在时间轴上选一个片段', 'alarm'); return; }
  const clip = findClip(state.selection.track, state.selection.clip);
  if (!clip) return;
  if (state.playhead <= clip.start_us || state.playhead >= clip.start_us + clip.duration_us) {
    toast('播放头不在所选片段内', 'alarm');
    return;
  }
  await edit({ op: 'split', track: state.selection.track, clip: clip.id, at_us: Math.round(state.playhead) });
}

function render() {
  renderTopbar();
  renderTrackHeads();
  renderTimeline();
  renderInspector();
  renderQualityLabel();
  renderStage();
}

function escapeHtml(text) {
  return String(text).replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
}

// ---------------------------------------------------------------- 启动
async function boot() {
  bind();
  initPlayheadDrag();
  initTrackDrag();
  initClipGestures();
  await refresh({ keepSelection: false });
  $('#playBtn').innerHTML = icon('play');
  // 预热胶片条：对视频素材并发取缩略图（真实解码）
  (state.data.assets || []).filter((a) => a.type === 0).forEach((a) => loadThumbs(a.id));
  (state.data.assets || []).filter((a) => a.type === 1).forEach((a) => loadWaveform(a.id));
  // 预取当前视口帧
  fetch(`/api/prefetch?from=0&to=${durationUs()}&n=16&w=360`).catch(() => {});
}

boot().catch((err) => {
  console.error(err);
  document.body.insertAdjacentHTML('beforeend',
    `<div class="toast is-alarm">无法连接设计桥：${escapeHtml(err.message)}</div>`);
});
