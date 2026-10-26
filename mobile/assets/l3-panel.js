/* =============================================================================
   NairoClip Mobile · 三级面板渲染器
   -----------------------------------------------------------------------------
   规格数据在 l3-spec.js（纯数据）；本文件只负责把规格画成界面并写回引擎。

   三种形态（对应参考稿）：
     single  单页 —— 功能名加粗当小标题，下面竖排控件
     dual    双页 —— 「常规」+「区间」（曲线），点应用一起提交
     grid    宫格 —— 先选预设，选中后进入参数页

   写入引擎的真实能力（其余如实标注「仅预览」）：
     keyframe         透明度/缩放/位移/旋转/音量
     speed            常规倍速
     speed_curve      速度曲线
     filter           滤镜风格 + 强度
     effect_add       特效（类型 + 强度）
     mask             蒙版形状 + 位置/尺寸/羽化/圆角/反转
     chroma           色度抠像
     denoise          降噪强度
     audio            增益 + 淡入淡出
     canvas           画布比例
   ============================================================================= */
(function () {
  'use strict';

  const $ = (id) => document.getElementById(id);

  /* 依赖宿主页面提供的小工具；缺失时用本地兜底 */
  const toast = (...a) => (window.toast ? window.toast(...a) : console.log(...a));
  const clamp = (v, a, b) => Math.min(b, Math.max(a, v));
  const fmtTime = (t) => (window.fmt ? window.fmt(t) : `${t.toFixed(1)}s`);

  const SPEC = window.L3_SPEC;
  if (!SPEC) {
    console.warn('[l3] l3-spec.js 未加载');
    return;
  }
  const C = SPEC.controls;
  const P = SPEC.spec;

  /* ---------------- 状态 ---------------- */
  const ST = {
    open: false,
    key: null,
    spec: null,
    page: 0,
    values: {},      // 控件值
    curve: null,     // { points: [{t,v}], presetIdx }
    gridPick: null,  // 宫格选中的项
    dirty: false
  };

  /* 宿主提供：当前选中片段、编辑器状态、写入接口 */
  const host = () => window.L3_HOST || {};

  const EL = () => ({
    mask: $('l3-mask'), panel: $('l3'), title: $('l3-title'),
    close: $('l3-close'), reset: $('l3-reset'),
    seg: $('l3-seg'), actions: $('l3-actions'), body: $('l3-body')
  });

  /* ---------------- 默认值 ---------------- */
  function defaults(spec) {
    const out = {};
    const list = spec.dynamic === 'adjust'
      ? SPEC.adjustBase.map((x) => x.key)
      : (spec.groups || []).flat();
    list.forEach((k) => {
      const c = C[k] || (spec.dynamic === 'adjust' ? SPEC.adjustBase.find((x) => x.key === k) : null);
      if (c) out[k] = c.def;
    });
    return out;
  }

  /* ---------------- 打开 ---------------- */
  function open(key, opts) {
    const spec = P[key] || (window.L3_SPEC && window.L3_SPEC.spec && window.L3_SPEC.spec[key]);
    if (!spec) {
      /* 面板缺失是接线问题，必须暴露具体键名，方便直接补 */
      console.warn('[L3] 缺少面板定义:', key);
      return toast(`面板未定义：${key}`);
    }

    const h = host();
    const needClip = spec.mode !== 'grid';
    if (needClip && !h.selClip && !(opts && opts.allowNoClip)) {
      return toast('请先在时间轴上选中一个片段');
    }

    ST.key = key;
    ST.spec = spec;
    ST.page = 0;
    ST.values = defaults(spec);
    ST.gridPick = null;
    ST.dirty = false;
    ST.curve = spec.curve ? { points: [], presetIdx: 0 } : null;
    ST.open = true;

    const el = EL();
    el.title.textContent = spec.title;
    bindLegalEntry(el, key, spec);
    el.panel.hidden = false;
    el.mask.hidden = false;
    requestAnimationFrame(() => el.panel.classList.add('open'));

    renderSeg();
    renderActions();
    renderBody();
  }

  /* ---------------- 法律入口（每个模块面板都有） ----------------
     规则：面板标题右侧显示「?」按钮，点击直达与该功能相关的条款。
     映射依据《人工智能生成合成内容标识办法》的触发范围：
       · 视觉类 AI（特效/抠像/超分/背景）→ AI 生成内容标识说明 §2
       · 文本类 AI（字幕/翻译）          → AI 功能与模型说明 §3
       · 音频类 AI（配音/降噪/分离）      → AI 生成内容标识说明 §2
       · 导出                            → 用户协议 §5（标识义务）
     未列入的面板统一指向「AI 功能与模型说明」，保证任何模块都有出口。 */
  const LEGAL_BY_PANEL = {
    fxscreen: ['aiNotice', 2], fxperson: ['aiNotice', 2],
    fxatmo: ['aiNotice', 2], fxmotion: ['aiNotice', 2],
    fxparam: ['aiNotice', 2],
    matting: ['aiNotice', 2], chroma: ['aiNotice', 2],
    bgfill: ['aiNotice', 2], superres: ['aiNotice', 2],
    depth: ['aiNotice', 2], reframe: ['aiNotice', 2],
    transpreset: ['aiNotice', 2], transparam: ['aiNotice', 2],
    subtitle: ['aiModels', 3], subtitlestyle: ['aiModels', 3],
    translate: ['aiModels', 3], speechcut: ['aiModels', 3],
    tts: ['aiNotice', 2], audio: ['aiNotice', 2], denoise: ['aiNotice', 2],
    export: ['terms', 5], exportpage: ['terms', 5]
  };

  function legalTarget(key, spec) {
    if (LEGAL_BY_PANEL[key]) return LEGAL_BY_PANEL[key];
    // 兜底：按面板名猜测功能类别
    const t = (spec && spec.title) || '';
    if (/字幕|翻译|文字/.test(t)) return ['aiModels', 3];
    if (/特效|抠像|超分|背景|景深|蒙版/.test(t)) return ['aiNotice', 2];
    if (/音频|配音|降噪|音量/.test(t)) return ['aiNotice', 2];
    return ['aiModels', 1];
  }

  /** 在面板标题右侧挂「?」入口（幂等：重复打开不重复插入）。 */
  function bindLegalEntry(el, key, spec) {
    if (!el || !el.title) return;
    const wrap = el.title.parentElement;
    if (!wrap) return;
    let btn = wrap.querySelector('.panel-legal');
    if (!btn) {
      btn = document.createElement('button');
      btn.className = 'panel-legal';
      btn.type = 'button';
      btn.setAttribute('aria-label', '查看功能说明与合规条款');
      btn.textContent = '?';
      btn.addEventListener('click', (ev) => {
        ev.stopPropagation();
        const [doc, sec] = btn._target || ['aiModels', 1];
        const url = `05-legal.html?doc=${encodeURIComponent(doc)}` +
                    `&from=${encodeURIComponent('10-editor.html')}` +
                    (sec ? `#sec-${sec}` : '');
        location.href = url;
      });
      // 插入到标题之后
      if (el.title.nextSibling) wrap.insertBefore(btn, el.title.nextSibling);
      else wrap.appendChild(btn);
    }
    btn._target = legalTarget(key, spec);
    const t = btn._target;
    const names = { terms: '用户协议', privacy: '隐私政策', copyright: '版权与许可',
                    aiNotice: 'AI 生成内容标识说明', aiModels: 'AI 功能与模型说明' };
    btn.title = `本功能的 AI 标识与合规说明（${names[t[0]] || t[0]} 第 ${t[1]} 条）`;
  }

  function close() {
    const el = EL();
    el.panel.classList.remove('open');
    ST.open = false;
    setTimeout(() => {
      if (!ST.open) { el.panel.hidden = true; el.mask.hidden = true; }
    }, 280);
  }

  /* ---------------- 分段 ---------------- */
  function renderSeg() {
    const el = EL();
    const names = ST.spec.pageNames;
    if (!names) { el.seg.hidden = true; el.seg.innerHTML = ''; return; }
    el.seg.hidden = false;
    el.seg.innerHTML = '';
    names.forEach((t, i) => {
      const b = document.createElement('button');
      b.textContent = t;
      if (i === ST.page) b.className = 'on';
      b.addEventListener('click', () => { ST.page = i; renderSeg(); renderBody(); });
      el.seg.appendChild(b);
    });
  }

  /* ---------------- 头部动作 ---------------- */
  function renderActions() {
    const el = EL();
    el.actions.innerHTML = '';
    const acts = [];
    if (ST.spec.mode !== 'grid') acts.push({ k: 'reset', t: '重置' });
    acts.forEach((a) => {
      const b = document.createElement('button');
      b.textContent = a.t;
      b.addEventListener('click', () => {
        ST.values = defaults(ST.spec);
        if (ST.curve) { ST.curve.points = []; ST.curve.presetIdx = 0; }
        ST.dirty = true;
        renderBody();
        markDirty();
        toast('已重置为默认值');
      });
      el.actions.appendChild(b);
    });
  }

  /* ---------------- 小标题（单页/宫格后的参数名） ---------------- */
  function titleBlock(text) {
    const d = document.createElement('div');
    d.className = 'l3-sub';
    d.textContent = text;
    return d;
  }

  /* ---------------- 控件工厂 ---------------- */
  function ctl(key) {
    const c = C[key] || SPEC.adjustBase.find((x) => x.key === key);
    if (!c) return null;

    if (c.kind === 'slider') return sliderRow(key, c);
    if (c.kind === 'switch') return switchRow(key, c);
    if (c.kind === 'seg') return segRow(key, c);
    if (c.kind === 'color') return colorRow(key, c);
    if (c.kind === 'number') return numberRow(key, c);
    if (c.kind === 'button') return buttonRow(key, c);
    return null;
  }

  function valText(c, v) {
    if (c.unit === '%') return `${Math.round(v * (c.scale || 1) * 100)}%`;
    if (c.unit === 'x') return `${Number(v).toFixed(2)}x`;
    if (c.unit === '°') return `${Math.round(v)}°`;
    if (c.unit === 'px') return `${Math.round(v)}px`;
    if (c.unit === 's') return `${Number(v).toFixed(1)}s`;
    if (c.unit === 'dB') return `${v > 0 ? '+' : ''}${Number(v).toFixed(1)}dB`;
    if (c.scale === 100) return `${Math.round(v * 100)}%`;
    return `${Number(v).toFixed(2)}`;
  }

  function sliderRow(key, c) {
    const row = document.createElement('div');
    row.className = 'prow';
    const cur = ST.values[key] ?? c.def;
    row.innerHTML = `<svg class="i"><use href="#${c.icon || 'i-adjust'}"/></svg>
      <span class="plabel">${c.name}</span>
      <span class="pwrap">
        <span class="ptrack"></span><span class="pfill"></span><span class="pknob"></span>
        <input type="range" min="${c.min}" max="${c.max}" step="${c.step}" value="${cur}">
      </span>
      <span class="pval">${valText(c, cur)}</span>`;

    const input = row.querySelector('input');
    const fill = row.querySelector('.pfill');
    const knob = row.querySelector('.pknob');
    const out = row.querySelector('.pval');

    const paint = (v) => {
      const r = (v - c.min) / (c.max - c.min);
      const p = clamp(r, 0, 1) * 100;
      fill.style.width = `${p}%`;
      knob.style.left = `${p}%`;
      out.textContent = valText(c, v);
    };
    paint(cur);
    input.addEventListener('input', () => {
      const v = parseFloat(input.value);
      ST.values[key] = v;
      markDirty();
      paint(v);
    });
    return row;
  }

  function switchRow(key, c) {
    const row = document.createElement('div');
    row.className = 'prow';
    const on = ST.values[key] ?? c.def;
    row.innerHTML = `<span class="plabel wide">${c.name}</span>
      <span class="grow"></span>
      <button class="sw${on ? ' on' : ''}"><i></i></button>`;
    const b = row.querySelector('.sw');
    b.addEventListener('click', () => {
      ST.values[key] = !ST.values[key];
      b.classList.toggle('on', ST.values[key]);
      markDirty();
    });
    return row;
  }

  function segRow(key, c) {
    const row = document.createElement('div');
    row.className = 'prow';
    const cur = ST.values[key] ?? c.def;
    row.innerHTML = `<span class="plabel wide">${c.name}</span>
      <span class="grow"></span><span class="segmini"></span>`;
    const box = row.querySelector('.segmini');
    c.opts.forEach((t, i) => {
      const b = document.createElement('button');
      b.textContent = t;
      if (i === cur) b.className = 'on';
      b.addEventListener('click', () => {
        ST.values[key] = i;
        box.querySelectorAll('button').forEach((x, j) => x.classList.toggle('on', j === i));
        markDirty();
      });
      box.appendChild(b);
    });
    return row;
  }

  function colorRow(key, c) {
    const row = document.createElement('div');
    row.className = 'prow';
    const cur = ST.values[key] ?? c.def;
    row.innerHTML = `<span class="plabel wide">${c.name}</span><span class="grow"></span><span class="swatches"></span>`;
    const box = row.querySelector('.swatches');
    c.opts.forEach((hex) => {
      const b = document.createElement('button');
      b.style.background = hex;
      if (hex === cur) b.className = 'on';
      b.addEventListener('click', () => {
        ST.values[key] = hex;
        box.querySelectorAll('button').forEach((x) => x.classList.toggle('on', x === b));
        markDirty();
      });
      box.appendChild(b);
    });
    return row;
  }

  function numberRow(key, c) {
    const row = document.createElement('div');
    row.className = 'prow';
    const cur = ST.values[key] ?? c.def;
    row.innerHTML = `<span class="plabel wide">${c.name}</span>
      <span class="grow"></span>
      <input class="num" type="number" min="${c.min}" max="${c.max}" value="${cur}">`;
    const inp = row.querySelector('.num');
    inp.addEventListener('input', () => {
      ST.values[key] = clamp(parseFloat(inp.value) || c.def, c.min, c.max);
      markDirty();
    });
    return row;
  }

  /* ---------------- AI 动作：按面板映射到真实功能 ----------------
     面板 key → core::AiFeatureType（见 core/include/nairoclip/core/ai_task.h）
     以及该功能依赖的模型 ID（见 core/src/model_manager.cpp 的内置清单）。 */
  const AI_ACTION = {
    subtitle:  { feature: 10, model: 'whisper_zh',      needClip: false, label: '识别字幕' },
    lyrics:    { feature: 10, model: 'whisper_zh',      needClip: false, label: '识别歌词' },
    translate: { feature: 5,  model: 'translate_zh_en', needClip: true,  label: '字幕翻译' },
    tts:       { feature: 6,  model: 'tts_zh',          needClip: false, label: '文本转语音' },
    stem:      { feature: 4,  model: 'stem_sep',        needClip: true,  label: '人声分离' },
    depth:     { feature: 8,  model: null,              needClip: true,  label: '深度图' },
    record:    { feature: null, model: null,            needClip: false, label: '录音' }
  };

  /** 读取模型状态：2 = 已安装（3 损坏 / 4 可更新 也算不可直接用）。 */
  function modelStatus(modelId) {
    const br = window.NCEngine;
    if (!br || typeof br.models !== 'function') return -1;   // -1：拿不到
    try {
      const raw = br.models();
      const d = (raw && typeof raw === 'object') ? raw : JSON.parse(String(raw || '{}'));
      const list = d.models || [];
      const m = list.find((x) => x.id === modelId);
      return m ? Number(m.status) : -2;                       // -2：清单里没有
    } catch (e) {
      return -1;
    }
  }

  function startAiAction(key) {
    const act = AI_ACTION[key];
    if (!act) {
      toast('该处理尚未接入引擎');
      return;
    }

    const h = host();
    if (!window.NCEngine) {
      toast('无引擎桥，无法执行本地 AI 处理');
      return;
    }

    // 录音走系统权限，不是模型推理
    if (key === 'record') {
      toast('录音需在系统设置中授权麦克风（由平台层实现）');
      return;
    }

    // 依赖模型的功能：先查真实状态，没装就如实说，不假装能跑
    if (act.model) {
      const st = modelStatus(act.model);
      if (st === -1) {
        toast('读不到模型状态（引擎桥不可用）');
        return;
      }
      if (st === -2) {
        toast(`模型清单里没有 ${act.model}`);
        return;
      }
      if (st === 0) {
        toast(`「${act.label}」需要模型 ${act.model}，尚未下载`);
        return;
      }
      if (st === 1) {
        toast(`模型 ${act.model} 正在下载中`);
        return;
      }
      if (st === 3) {
        toast(`模型 ${act.model} 已损坏，需重新下载`);
        return;
      }
      // 2 = 已安装；4 = 可更新（可继续用）→ 往下走
    }

    const sel = h.selClip && h.selClip();
    if (act.needClip && !sel) {
      toast('请先在时间轴上选中一个片段');
      return;
    }

    // 开一个 AI 任务：core 负责状态机与进度，平台层执行真实推理
    // 声明：int32_t nc_ai_task_begin(editor, feature, target_id, char** out_error)
    // 面板参数不在此处传：任务开始后由平台层读取 UI 状态执行推理。
    const r = h.callNative
      ? h.callNative('nc_ai_task_begin', {
          feature: act.feature,
          target_id: sel && sel.clip ? sel.clip.id : 0
        })
      : null;

    if (r && r.ok) {
      toast(`已提交「${act.label}」，等待平台层推理`);
      if (h.reload) h.reload();
    } else {
      toast(`「${act.label}」提交失败：${(r && r.message) || '引擎未响应'}`);
    }
  }

  function buttonRow(key, c) {
    const row = document.createElement('div');
    row.className = 'prow';
    row.innerHTML = `<button class="act"><svg class="i"><use href="#${c.icon || 'i-ai'}"/></svg>${c.name}</button>`;
    row.querySelector('.act').addEventListener('click', () => {
      startAiAction(ST.key);
    });
    return row;
  }

  /* 面板不再有「应用」：参数改动即时写入引擎。
     用 200ms 防抖合并连续拖动，避免每个像素都产生一条撤销记录。 */
  let commitTimer = 0;
  function markDirty() {
    ST.dirty = true;
    clearTimeout(commitTimer);
    commitTimer = setTimeout(() => { commit(); }, 200);
  }

  async function commit() {
    const h = host();
    if (typeof h.edit !== 'function') return;
    const sel = h.selClip && h.selClip();
    if (!sel) return;
    const v = ST.values;
    const track = sel.track.index;
    const clip = sel.clip.id;
    const us = typeof h.nowUs === 'function' ? h.nowUs() : 0;
    try {
      /* 关键帧通道 */
      for (const key of Object.keys(v)) {
        const c = C[key] || SPEC.adjustBase.find((x) => x.key === key);
        if (!c || c.prop === undefined) continue;
        await h.edit({ op: 'keyframe', track, clip, property: c.prop, time_us: us, value: v[key] });
        if (c.prop === 0) await h.edit({ op: 'opacity', track, clip, value: v[key] });
      }
      /* 变速 */
      if (ST.key === 'speed' && typeof v.speed === 'number') {
        await h.edit({ op: 'speed', track, clip,
                       num: Math.round(v.speed * 100), den: 100 });
      }
      /* 速度曲线：仅在「曲线」页配置了控制点时提交。
         times 用相对片段起点的秒数；speeds 为倍率。 */
      if (ST.curve && Array.isArray(ST.curve.points) && ST.curve.points.length >= 2) {
        const times = ST.curve.points.map((p) => p.t);
        const speeds = ST.curve.points.map((p) => p.v);
        await h.edit({ op: 'speed_curve', track, clip, times, speeds, easing: 0 });
      }
      /* 滤镜强度 */
      if (typeof v.filteramt === 'number') {
        await h.edit({ op: 'filter', track, clip,
                       style: (h.clipStyle && h.clipStyle()) || 0, intensity: v.filteramt });
      }
      /* 特效强度：type 必须用宫格里选中的那个特效，
         否则「雪花 / 下雨 / 老电影」等全部会被写成同一个（曾是 13=故障）。 */
      if (typeof v.fxamt === 'number') {
        const picked = ST.gridPick && ST.gridPick.id;
        if (!picked) {
          toast('请先在宫格中选择一个特效');
        } else {
          await h.edit({ op: 'effect_add', track, clip, type: picked, value: v.fxamt / 100 });
        }
      }
      /* 蒙版 */
      if (typeof v.feather === 'number' || typeof v.invert === 'boolean') {
        await h.edit({ op: 'mask', track, clip,
                       shape: (h.maskShape && h.maskShape()) || 4,
                       x: 0.5 + (v.posx || 0) / 20, y: 0.5 + (v.posy || 0) / 20,
                       rotation: v.maskrot || 0, width: v.maskw || 1, height: v.maskh || 1,
                       feather: v.feather ?? 0.02, radius: v.radius || 0,
                       invert: v.invert ? 1 : 0 });
      }
      /* 色度键：任一抠像参数变化即整体提交 */
      if (ST.key === 'chroma') {
        // segRow 存的是索引（0/1/2），直接当 quality 用
        const q = typeof v.chromaq === 'number' ? v.chromaq : 1;
        await h.edit({
          op: 'chroma', track, clip,
          key: v.chromakey || '#00FF00',
          inner: typeof v.chromain === 'number' ? v.chromain : 0.35,
          outer: typeof v.chromaout === 'number' ? v.chromaout : 0.55,
          spill: typeof v.spill === 'number' ? v.spill : 0.3,
          quality: q,
          enabled: v.chromaon === false ? 0 : 1
        });
      }
      /* 降噪 */
      if (ST.key === 'denoise') {
        await h.edit({ op: 'denoise', track, clip, strength: v.denoiselv ?? 0 });
      }
      /* 音频 */
      if (typeof v.fadein === 'number' || typeof v.fadeout === 'number' ||
          typeof v.gain === 'number') {
        await h.edit({ op: 'audio', track, clip, gain_db: v.gain || 0,
                       fade_in_us: Math.round((v.fadein || 0) * 1e6),
                       fade_out_us: Math.round((v.fadeout || 0) * 1e6) });
      }
    } catch (err) {
      toast(`写入失败：${err.message || err}`, 2600);
    }
  }

  /* ---------------- 曲线编辑器 ---------------- */
  function curvePreset(i) {
    switch (i) {
      case 1: return [{t:0,v:1},{t:.18,v:3.2},{t:.34,v:.6},{t:.52,v:2.6},{t:.7,v:.7},{t:.86,v:2.8},{t:1,v:1}];
      case 2: return [{t:0,v:1},{t:.32,v:1},{t:.5,v:.28},{t:.68,v:1},{t:1,v:1}];
      case 3: return [{t:0,v:1},{t:.36,v:.9},{t:.5,v:.12},{t:.64,v:.9},{t:1,v:1}];
      case 4: return [{t:0,v:1},{t:.16,v:3.4},{t:.32,v:.7},{t:.5,v:3.4},{t:.66,v:.7},{t:.84,v:3.4},{t:1,v:1}];
      case 5: return [{t:0,v:4.5},{t:.2,v:1.6},{t:.4,v:1},{t:1,v:1}];
      case 6: return [{t:0,v:1},{t:.6,v:1},{t:.8,v:1.6},{t:1,v:4.5}];
      default: return [{t:0,v:1},{t:1,v:1}];
    }
  }

  function drawCurve(cv, points, unit) {
    const dpr = Math.min(3, window.devicePixelRatio || 1);
    const w = cv.clientWidth || 320;
    const h = cv.clientHeight || 200;
    cv.width = Math.round(w * dpr);
    cv.height = Math.round(h * dpr);
    const g = cv.getContext('2d');
    g.setTransform(dpr, 0, 0, dpr, 0, 0);
    g.clearRect(0, 0, w, h);

    const pad = 14;
    const X = (t) => pad + t * (w - pad * 2);
    /* 对数轴：0.1x–10x，1x 落在正中（线性轴会让 1x 贴底） */
    const LMIN = Math.log(0.1);
    const LR = Math.log(10) - LMIN;
    const Y = (v) => h - pad - (Math.log(clamp(v, 0.1, 10)) - LMIN) / LR * (h - pad * 2);

    g.strokeStyle = 'rgba(255,255,255,.06)';
    g.lineWidth = 1;
    [0.1, 1, 10].forEach((v) => {
      g.beginPath(); g.moveTo(pad, Y(v)); g.lineTo(w - pad, Y(v)); g.stroke();
    });
    g.strokeStyle = 'rgba(255,255,255,.14)';
    g.setLineDash([3, 3]);
    g.beginPath(); g.moveTo(pad, Y(1)); g.lineTo(w - pad, Y(1)); g.stroke();
    g.setLineDash([]);

    const pts = points.length >= 2 ? points : [{ t: 0, v: 1 }, { t: 1, v: 1 }];
    g.strokeStyle = '#FFFFFF';
    g.lineWidth = 1.6;
    g.lineJoin = 'round';
    g.beginPath();
    pts.forEach((pt, i) => {
      const x = X(pt.t); const y = Y(pt.v);
      if (i === 0) g.moveTo(x, y);
      else {
        const pv = pts[i - 1];
        const cx = (X(pv.t) + x) / 2;
        g.bezierCurveTo(cx, Y(pv.v), cx, y, x, y);
      }
    });
    g.stroke();

    pts.forEach((pt) => {
      g.beginPath();
      g.arc(X(pt.t), Y(pt.v), 3.6, 0, Math.PI * 2);
      g.fillStyle = '#F0A63E';
      g.fill();
    });
  }

  function curvePanel() {
    const box = document.createElement('div');
    const cfg = ST.spec.curve || {};

    if (cfg.presets) {
      const pre = document.createElement('div');
      pre.className = 'cpresets';
      cfg.presets.forEach((name, i) => {
        const b = document.createElement('button');
        b.textContent = name;
        if (ST.curve.presetIdx === i) b.className = 'on';
        b.addEventListener('click', () => {
          ST.curve.presetIdx = i;
          ST.curve.points = curvePreset(i);
          markDirty();
          pre.querySelectorAll('button').forEach((x, j) => x.classList.toggle('on', j === i));
          drawCurve(cv, ST.curve.points, cfg.unit);
        });
        pre.appendChild(b);
      });
      box.appendChild(pre);
    }

    const wrap = document.createElement('div');
    wrap.className = 'cwrap';
    const cv = document.createElement('canvas');
    wrap.appendChild(cv);
    box.appendChild(wrap);

    const h = host();
    const dur = (h.clipDur && h.clipDur()) || 8;
    const ax = document.createElement('div');
    ax.className = 'caxis';
    ax.innerHTML = `<span>0.0s</span><span>${(dur / 2).toFixed(1)}s</span><span>${dur.toFixed(1)}s</span>`;
    box.appendChild(ax);

    const hint = document.createElement('p');
    hint.className = 'chint';
    hint.textContent = cfg.presets
      ? '单击曲线加点；纵向为速度倍率（0.1x / 1x / 10x 对数刻度，虚线为原速）'
      : '单击曲线加点；纵向为该参数的取值范围，虚线为默认值';
    box.appendChild(hint);

    cv.addEventListener('click', (e) => {
      const r = cv.getBoundingClientRect();
      const t = clamp((e.clientX - r.left - 14) / (r.width - 28), 0, 1);
      const v = clamp(0.1 + (1 - (e.clientY - r.top - 14) / (r.height - 28)) * 9.9, 0.1, 10);
      ST.curve.points.push({ t, v });
      ST.curve.points.sort((a, b) => a.t - b.t);
      markDirty();
      drawCurve(cv, ST.curve.points, cfg.unit);
    });

    requestAnimationFrame(() => {
      if (!ST.curve.points.length) ST.curve.points = curvePreset(0);
      drawCurve(cv, ST.curve.points, cfg.unit);
    });
    return box;
  }

  /* ---------------- 宫格 ---------------- */
  function gridPanel() {
    const box = document.createElement('div');
    const wrap = document.createElement('div');
    wrap.className = 'l3grid' + (ST.spec.gridKind === 'ratio' ? ' ratio' : '');

    const kind = ST.spec.gridKind;
    /* 滤镜/特效/蒙版：向引擎要按预设真实渲染的缩略帧（NCEngine.presetFrame） */
    const canRender = (kind === 'filter' || kind === 'effect' || kind === 'mask');
    const base = (host().apiBase && host().apiBase()) || '';

    (ST.spec.grid || []).forEach((item) => {
      const b = document.createElement('button');
      b.className = 'cell';
      const label = item.name || item.id;
      b.innerHTML = `<span class="thumb"></span><span class="cap">${label}</span>`;

      /* 缩略图三级回退：
         1) 引擎在线 → NCEngine.presetFrame 取真实渲染帧（能反映当前片段）
         2) 引擎离线或失败 → static/preset/<slug>_sN.jpg（离线烘焙的实拍底图）
         3) 都没有 → 保留占位底色 */
      const th = b.querySelector('.thumb');
      const paint = (url) => {
        const img = new Image();
        img.alt = label;
        /* 不用 loading=lazy：格子常在滚动容器视口外，懒加载会让 onload 永不触发。
           也不用 onload 挂 DOM：data URL 批量解码时 onload 顺序不可靠，
           直接先插进 DOM 再 decode，失败才清空。 */
        th.style.background = 'none';
        th.innerHTML = '';
        th.appendChild(img);
        img.src = url;
        const drop = () => { th.innerHTML = ''; };
        if (img.decode) {
          img.decode().catch(drop);
        } else {
          img.onerror = drop;
        }
      };

      /* 底图分工（重要）：
         · 滤镜/调色 → 人像实拍（色调映射需要连续色阶，纯色块看不出差异）
         · 其他全部 → 黏土表情（结构变换/区域裁切需要清晰轮廓）
         每格的表情互不重复：由 NC_THUMB_MAP 的 <kind>_<id> 映射到唯一一张脸。 */
      const slug = item.slug || '';
      const isColorGrid = (kind === 'filter');
      const key = isColorGrid ? `${slug}_s1` : `${kind}_${item.id}`;
      const useStatic = () => {
        /* 音频等带显式 thumb 的条目直接用给定图 */
        if (item.thumb) {
          const b2 = window.NC_THUMB_IMG;
          if (b2 && b2[item.thumb]) { paint(b2[item.thumb]); return true; }
          if (window.NC_STATIC_BASE === 'data') return false;
          paint(`../static/${item.thumb}`);
          return true;
        }
        const bank = isColorGrid ? window.NC_PRESET_IMG : window.NC_THUMB_IMG;
        const id = isColorGrid ? key : (window.NC_THUMB_MAP || {})[key];
        if (bank && id && bank[id]) { paint(bank[id]); return true; }
        const base = window.NC_STATIC_BASE;
        if (base === 'data') return false;
        if (isColorGrid) paint(`${base || '../static'}/preset_sm/${key}.jpg`);
        else if (id) paint(`${base || '../static'}/${id}`);
        else return false;
        return true;
      };

      /* 宫格缩略帧：优先用平台桥 presetFrame（桥内临时套预设→渲染→还原）；
         无桥时退回内联静态图，不伪造画面。 */
      const br = window.NCEngine;
      const canBridge = !!(br && typeof br.presetFrame === 'function');
      if (!canBridge) {
        useStatic();
      } else if (canRender) {
        const t = (host().nowUs && host().nowUs()) || 1600000;
        let url = '';
        try {
          url = br.presetFrame(kind, item.id, t, 160) || '';
        } catch (e) {
          url = '';
        }
        if (url && url.slice(0, 5) === 'data:') {
          const img = new Image();
          img.alt = label;
          img.onload = () => {
            th.style.background = 'none';
            th.innerHTML = '';
            th.appendChild(img);
          };
          img.onerror = () => { useStatic(); };
          img.src = url;
        } else {
          useStatic();
        }
      } else {
        useStatic();
      }
      if (ST.gridPick && ST.gridPick.id === item.id) b.classList.add('on');
      b.addEventListener('click', () => {
        ST.gridPick = item;
        wrap.querySelectorAll('.cell').forEach((x) => x.classList.toggle('on', x === b));
        applyGrid(item);
      });
      wrap.appendChild(b);
    });
    box.appendChild(wrap);

    if (ST.spec.gridKind === 'audio' && !(ST.spec.grid || []).length) {
      const p = document.createElement('p');
      p.className = 'chint';
      p.textContent = '本机暂无音频素材，先从「＋」导入';
      box.appendChild(p);
    }
    return box;
  }

  /* 宫格点选即生效（无需「应用」） */
  async function applyGrid(item) {
    const h = host();
    if (typeof h.edit !== 'function') return;
    const sel = h.selClip && h.selClip();
    const track = sel ? sel.track.index : -1;
    const clip = sel ? sel.clip.id : 0;
    const g = item;
    const kind = ST.spec.gridKind;
    try {
      if (kind === 'ratio') {
        await h.edit({ op: 'canvas', width: g.w, height: g.h });
      } else if (kind === 'mask') {
        await h.edit({ op: 'mask', track, clip, shape: g.id, x: 0.5, y: 0.5,
                       rotation: 0, width: 1, height: 1, feather: 0.02, radius: 0, invert: 0 });
      } else if (kind === 'filter') {
        await h.edit({ op: 'filter', track, clip, style: g.id, intensity: 60 });
      } else if (kind === 'effect') {
        await h.edit({ op: 'effect_add', track, clip, type: g.id, value: 0.5 });
      } else if (kind === 'transition') {
        /* 转场发生在两个相邻片段之间：from = 当前片段，
           to = 同轨道上紧接着的下一个片段。找不到后继片段就无法建转场。 */
        const tr = (h.tracks && h.tracks()) ? h.tracks().find((t) => t.index === track) : null;
        const clips = (tr && tr.clips) ? tr.clips : [];
        const idx = clips.findIndex((c) => c.id === clip);
        const next = idx >= 0 ? clips[idx + 1] : null;
        if (!next) {
          toast('转场需要两个相邻片段：请在其后接一个片段');
          return;
        }
        const durUs = Math.round((g.dur || 0.5) * 1e6) || 500000;
        await h.edit({ op: 'transition', track, type: g.id,
                       duration_us: durUs, from: clip, to: next.id });
      } else {
        toast('该预设暂未接入引擎，仅预览');
        return;
      }
      if (typeof h.reload === 'function') await h.reload();
      toast('已应用');
    } catch (err) {
      toast(`写入失败：${err.message || err}`, 3000);
    }
  }

  /* ---------------- 主体路由 ---------------- */
  function renderBody() {
    const el = EL();
    el.body.innerHTML = '';

    if (ST.spec.mode === 'grid') {
      if (ST.gridPick && ST.spec.then) {
        // 选中预设后：上面回到宫格，下面展开参数
        const back = document.createElement('button');
        back.className = 'l3-backrow';
        back.textContent = `‹ ${ST.spec.title}`;
        back.addEventListener('click', () => { ST.gridPick = null; renderBody(); });
        el.body.appendChild(back);
        el.body.appendChild(titleBlock(ST.gridPick.name));
        const target = P[ST.spec.then];
        if (target) {
          (target.groups || []).flat().forEach((k) => {
            const r = ctl(k);
            if (r) el.body.appendChild(r);
          });
        }
        return;
      }
      el.body.appendChild(gridPanel());
      return;
    }

    // 双页：第 2 页是曲线
    if (ST.spec.mode === 'dual' && ST.page === 1) {
      el.body.appendChild(curvePanel());
      return;
    }

    // 单页 / 双页第 1 页：功能名加粗 + 竖排控件
    const keys = ST.spec.dynamic === 'adjust'
      ? SPEC.adjustBase.map((x) => x.key)
      : (ST.spec.groups || []).flat();

    if (!keys.length) {
      const p = document.createElement('p');
      p.className = 'chint';
      p.textContent = '此功能暂无调节参数';
      el.body.appendChild(p);
    } else {
      /* 头部已显示功能名：单页/双页首屏不再重复小标题；仅 hidden 面板需要 */
      keys.forEach((k) => {
        const r = ctl(k);
        if (r) el.body.appendChild(r);
      });
    }

    if (ST.spec.note) {
      const n = document.createElement('p');
      n.className = 'chint';
      n.textContent = ST.spec.note;
      el.body.appendChild(n);
    }
  }

  /* ---------------- 绑定 ---------------- */
  function bind() {
    const el = EL();
    if (!el.panel) return;
    el.close.addEventListener('click', close);
    el.mask.addEventListener('click', close);
    if (el.reset) {
      el.reset.addEventListener('click', () => {
        ST.values = defaults(ST.spec);
        if (ST.curve) { ST.curve.points = []; ST.curve.presetIdx = 0; }
        renderBody();
        markDirty();
        toast('已重置为默认值');
      });
    }
  }

  window.L3OPEN = open;
  window.L3CLOSE = close;
  /** 供其他模块（如导出确认、AI 任务提示）复用同一套法律跳转。 */
  window.L3LEGAL = bindLegalEntry;
  window.L3LEGAL_TARGET = legalTarget;
  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', bind);
  } else {
    bind();
  }
})();
