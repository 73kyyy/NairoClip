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
    lutError: '',    // LUT 校验/写入失败的常驻说明
    dirty: false
  };

  /* 宿主提供：当前选中片段、编辑器状态、写入接口 */
  const host = () => window.L3_HOST || {};

  /* 取当前选中片段。
     坑：L3_HOST.selClip 是 **getter**（返回对象），不是函数；
     早期写成 `h.selClip && h.selClip()` 会把对象当函数调用，
     抛出的 TypeError 发生在 try 之前 —— catch 接不到，
     于是所有面板的「改值」都静默失效（没有提示、引擎也不变）。
     这里统一兼容「函数」与「对象」两种形态。 */
  function currentSelection() {
    const h = host();
    const v = h.selClip;
    if (typeof v === 'function') return v();
    return v || null;
  }

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
    /* 用片段当前的真值初始化（否则开关永远从「关」起步，
       已倒放的片段一点就被设成「关」，看起来像没反应）。 */
    {
      const cur = currentSelection();
      if (cur && cur.clip) {
        if (spec.groups && spec.groups.flat().includes('reverseon')) {
          ST.values.reverseon = !!cur.clip.reversed;
        }
        if (spec.groups && spec.groups.flat().includes('blendmode')) {
          ST.values.blendmode = typeof cur.clip.blendMode === 'number' ? cur.clip.blendMode : 0;
        }
        /* LUT 强度同理：已设过 0.3 的片段，面板一打开必须显示 0.3，
           否则用户看到的滑块位置和画面不一致，一动就把强度弹回 1。 */
        if (spec.groups && spec.groups.flat().includes('lutstrength')) {
          const hv = host();
          const cl = hv.clipLut ? hv.clipLut() : null;
          if (cl && typeof cl.strength === 'number') ST.values.lutstrength = cl.strength;
        }
      }
    }
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
  /* ---------------- LUT 头部（P-01）----------------
     结构：文件名行 + 导入/清除按钮 + 校验错误提示。
     不用 ctl()：C 表只描述 slider/switch/color 这类「值控件」，
     文件选择不是值控件，塞进去会把控件概念搞浑。 */
  function lutHeader() {
    const box = document.createElement('div');
    box.className = 'lut-head';

    const hh = host();
    const cur = (hh.clipLut && hh.clipLut()) || { path: '', strength: 1 };
    const name = cur.path ? cur.path.split('/').pop() : '';

    const line = document.createElement('div');
    line.className = 'lut-file' + (cur.path ? '' : ' empty');
    line.textContent = cur.path ? name : '未使用 LUT';
    if (cur.path) line.title = cur.path;
    box.appendChild(line);

    const row = document.createElement('div');
    row.className = 'lut-btns';

    const pick = document.createElement('button');
    pick.className = 'lut-btn primary';
    pick.textContent = cur.path ? '更换 .cube' : '导入 .cube';
    pick.addEventListener('click', () => pickLutFile());
    row.appendChild(pick);

    if (cur.path) {
      const clear = document.createElement('button');
      clear.className = 'lut-btn';
      clear.textContent = '清除';
      clear.addEventListener('click', async () => { await commitLut(''); });
      row.appendChild(clear);
    }
    box.appendChild(row);

    /* 校验失败说明常驻显示：用户会反复点同一个坏文件，飘过的 toast 没用。 */
    if (ST.lutError) {
      const err = document.createElement('p');
      err.className = 'chint lut-err';
      err.textContent = ST.lutError;
      box.appendChild(err);
    }
    return box;
  }

  /* 选文件 -> 内核校验 -> 写引擎。
     校验走 nc_lut_validate_file 而不是前端猜后缀：
     只有内核知道 1D LUT 不支持、数据行数对不对、DOMAIN 是否合法。 */
  function pickLutFile() {
    const input = document.createElement('input');
    input.type = 'file';
    input.accept = '.cube';
    input.style.display = 'none';
    input.addEventListener('change', async () => {
      const f = input.files && input.files[0];
      if (input.parentNode) input.parentNode.removeChild(input);
      if (!f) return;

      /* 外部 LUT 是**路径引用**（工程里存路径，见 capi.h nc_set_clip_lut 注释）。
         浏览器/WebView 的 File 对象没有绝对路径，拿不到就无法引用——
         这种情况明确报错，不假装设置成功。 */
      const path = f.path || '';
      if (!path) {
        ST.lutError = '当前环境拿不到文件绝对路径，无法引用外部 LUT。'
                    + '请先把 .cube 放进应用可访问的目录再重试。';
        renderBody();
        return;
      }

      const hv = host();
      const v = (hv.validateLut && hv.validateLut(path)) || { ok: false, message: '校验不可用' };
      if (!v || !v.ok) {
        ST.lutError = '无法使用该 LUT：' + ((v && v.message) || '格式不支持');
        renderBody();
        return;
      }

      ST.lutError = '';
      await commitLut(path);
      toast('已应用 LUT' + (v.size ? '（' + v.size + '³）' : ''));
    });
    document.body.appendChild(input);
    input.click();
  }

  /* 单独提交 LUT。
     不走 commit()：后者遍历整张值表，会把当前面板无关的默认值也写回
     （例如从「滤镜」切过来时携带 fxamt），对 LUT 这种「一改就生效」
     的动作会顺手改掉别的属性。 */
  async function commitLut(path) {
    const sel = currentSelection();
    if (!sel) { toast('请先选中片段'); return; }
    const strength = (typeof ST.values.lutstrength === 'number') ? ST.values.lutstrength : 1;
    const hv2 = host();
    try {
      await hv2.edit({
        op: 'lut', track: sel.track.index, clip: sel.clip.id,
        path: path, strength: strength
      });
      if (hv2.reload) await hv2.reload();
      renderBody();
    } catch (err) {
      ST.lutError = '写入失败：' + (err.message || err);
      renderBody();
    }
  }

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
    if (c.kind === 'blend') return blendRow(key, c);
    return null;
  }

  /* 数值显示。
     坑：早期写成 v * (c.scale || 1) * 100 —— 对不透明度（min 0 / max 1）
     会得到 1*100*100 = 10000%。scale 的语义是「存储值 → 显示值」的倍率，
     用户看到的百分数已经包含了 ×100，不能重复相乘。 */
  function valText(c, v) {
    if (c.unit === '%') {
      /* 存储值域 0..1 的通道（不透明度/音量/羽化/圆角）统一显示 0..100%；
         存储值域本身就是百分数的（强度 0..100 等）原样显示。
         判据用 max <= 1，不靠 scale 标志位 —— scale 只影响内部换算，
         显示口径由值域决定更不容易再错。 */
      const isUnitRange = typeof c.max === 'number' && c.max <= 1;
      return `${Math.round(isUnitRange ? v * 100 : v)}%`;
    }
    if (c.unit === 'x') return `${Number(v).toFixed(2)}x`;
    if (c.unit === '°') return `${Math.round(v)}°`;
    if (c.unit === 'px') return `${Math.round(v)}px`;
    if (c.unit === 's') return `${Number(v).toFixed(1)}s`;
    if (c.unit === 'dB') return `${v > 0 ? '+' : ''}${Number(v).toFixed(1)}dB`;
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

  /* 混合模式选择器：16 种，宫格式两列滚动。
     名称来自内核（window.NC_BLEND_MODES，由 editor.js 启动时拉取），
     未拉到时降级为编号，不伪造名称。 */
  function blendRow(key, c) {
    const row = document.createElement('div');
    row.className = 'prow blend-row';
    const cur = ST.values[key] ?? 0;
    const list = Array.isArray(window.NC_BLEND_MODES) ? window.NC_BLEND_MODES : [];
    row.innerHTML = `<span class="plabel wide">${c.name}</span>
      <div class="blend-grid"></div>`;
    const grid = row.querySelector('.blend-grid');
    if (!list.length) {
      grid.innerHTML = '<div class="empty-hint" style="font-size:11px">混合模式列表读取失败：引擎未提供 /api/blend_modes</div>';
      return row;
    }
    list.forEach((m) => {
      const b = document.createElement('button');
      b.className = 'blend-item' + (m.mode === cur ? ' on' : '');
      b.textContent = m.label;
      b.addEventListener('click', () => {
        ST.values[key] = m.mode;
        grid.querySelectorAll('button').forEach((x) => x.classList.remove('on'));
        b.classList.add('on');
        markDirty();
      });
      grid.append(b);
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

    const sel = currentSelection();
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
    const sel = currentSelection();
    if (!sel) return;
    const v = ST.values;
    const track = sel.track.index;
    const clip = sel.clip.id;
    const us = typeof h.nowUs === 'function' ? h.nowUs() : 0;
    try {
      /* 混合模式（P-16）：选了就写引擎，不静默丢弃。
         必须排在关键帧循环之前：后者会遍历全部可动画通道，
         任何一个抛错都会中断整段 commit（catch 只弹 toast），
         混合模式就永远轮不到执行。
         判据用 !== undefined 而非 typeof === 'number'：
         默认值 0 是合法模式（normal），不能被 falsy 判断吃掉。 */
      if (v.blendmode !== undefined && v.blendmode !== null) {
        await h.edit({ op: 'blend', track, clip, mode: Number(v.blendmode) });
      }

      /* LUT 强度（P-01）：只有片段已经设了 LUT 才提交。
         没设 LUT 时拖动强度滑块是无意义的（改的是「不存在的映射」），
         静默跳过比写一条空 LUT 更诚实。 */
      if (typeof v.lutstrength === 'number') {
        const cur = h.clipLut ? h.clipLut() : null;
        if (cur && cur.path) {
          await h.edit({ op: 'lut', track, clip, path: cur.path,
                         strength: v.lutstrength });
        }
      }
      /* 关键帧通道 */
      for (const key of Object.keys(v)) {
        const c = C[key] || SPEC.adjustBase.find((x) => x.key === key);
        if (!c || c.prop === undefined) continue;
        await h.edit({ op: 'keyframe', track, clip, property: c.prop, time_us: us, value: v[key] });
        if (c.prop === 0) await h.edit({ op: 'opacity', track, clip, value: v[key] });
      }
      /* 定格（P-17）：在片段当前位置取一帧，插入一个静帧片段 */
      if (ST.key === 'freeze') {
        const dur = typeof v.freezedur === 'number' ? v.freezedur : 2;
        await h.edit({ op: 'freeze', track, clip,
                       at_us: Math.round((h.nowUs ? h.nowUs() : 0)),
                       duration_us: Math.round(dur * 1e6) });
        await h.reload();
        return;
      }
      /* 倒放（P-17）：内核按时长等价反转，时长不变 */
      if (ST.key === 'reverse') {
        await h.edit({ op: 'reverse', track, clip });
        await h.reload();
        return;
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
    /* 哪些宫格能用引擎真实渲染缩略帧。
       转场也在内：桥会临时插入该转场、渲染混合帧、再删除，
       格子里看到的是「这个转场作用在真实画面上」的效果。 */
    const canRender = (kind === 'filter' || kind === 'effect' || kind === 'mask' ||
                       kind === 'transition');
    const base = (host().apiBase && host().apiBase()) || '';

    (ST.spec.grid || []).forEach((item) => {
      const b = document.createElement('button');
      b.className = 'cell';
      const label = item.name || item.id;
      b.innerHTML = `<span class="thumb"></span><span class="cap">${label}</span>`;

      /* 缩略图三级回退：
         1) 引擎在线 → NCEngine.presetFrame 取真实渲染帧（能反映当前片段）
         2) 引擎离线或渲染失败 → static/grid/<kind>_<id>.jpg
            （CI 生成的离线兜底图，四类全量，见 tools/gen_grid_previews.py）
         3) 都没有 → 保留占位底色，不伪造画面 */
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
         · 全部宫格统一用 static/grid/ 的离线兜底图（CI 生成，见
           tools/gen_grid_previews.py）。底图是黏土表情 —— 卡通角色，
           没有真人肖像权问题，且轮廓清晰，结构类特效/蒙版看得出一眼差异。
         · 真机上有桥时优先走 NCEngine.presetFrame 真实渲染，见下方；
           这张离线图只是「无桥 / 渲染失败」时的兜底，不伪造效果。
         每格图互不重复：由 map.json 的 `<kind>_<id>` 映射，id 与 l3-spec.js 一致。 */
      /* 主键：四类统一 `<kind>_<id>`。
         早期只有非滤镜用这个键、滤镜用 `slug_s1` 去查 preset_sm，
         而 preset_sm 已被删除 → 滤镜宫格全空白。现在统一走 grid。 */
      const key = `${kind}_${item.id}`;
      const useStatic = () => {
        /* 音频等带显式 thumb 的条目直接用给定图 */
        if (item.thumb) {
          const b2 = window.NC_THUMB_IMG;
          if (b2 && b2[item.thumb]) { paint(b2[item.thumb]); return true; }
          if (window.NC_STATIC_BASE === 'data') return false;
          paint(`../static/${item.thumb}`);
          return true;
        }
        const bank = window.NC_THUMB_IMG;
        const rel = (window.NC_THUMB_MAP || {})[key];
        /* 静态版：图片已内联成 data URL，键是相对 static/ 的路径 */
        if (bank && rel && bank[rel]) { paint(bank[rel]); return true; }
        const base = window.NC_STATIC_BASE;
        if (base === 'data') return false;
        /* 网页版：直接按相对路径加载 */
        if (rel) { paint(`${base || '../static'}/${rel}`); return true; }
        /* 既没有映射也没有内联图 → 保留占位底色，不伪造画面 */
        return false;
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
    const sel = currentSelection();
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

    if (ST.spec.lutPanel && ST.page === 0) {
      el.body.appendChild(lutHeader());
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

  /* 调试出口：仅用于自动化验证「改了值 → 真的写进引擎」这条链。
     生产代码不会调用它；留着也便于现场排查面板写入失效。 */
  window.L3COMMIT = () => commit();
  window.L3VALUES = () => JSON.parse(JSON.stringify(ST.values || {}));
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
