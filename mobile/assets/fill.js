/* NairoClip · 素材填充页逻辑（UI_BACKLOG P-05）
   ============================================================================
   流程：模板选择页带 id 进来 → 逐槽位指定素材 → 调 nc_template_apply_slots

   ## 一个关键决定：自动填充在**前端**分配，不用内核的 nc_template_apply

   内核的 `nc_template_apply` 会用 B2-2 打分算法自动分配，看起来更"智能"。
   但那样会出问题：**用户看到的槽位分配与实际生成结果不一致**。

   本项目反复强调「预览 = 导出」，同理这里要保证「所见 = 所生成」。
   所以：
     · 「自动填充」在前端按可用素材轮转填满槽位（用户看得见每条怎么填的）；
     · 「开始生成」用 `nc_template_apply_slots` 提交**用户看到的那个映射**。

   打分算法的价值保留在内核里（供无 UI 的调用方使用），
   但本页面选择显式映射 —— 可预期性比"聪明"更重要。
*/
'use strict';

(function () {
  const $ = (id) => document.getElementById(id);

  /* ---------------- URL 参数 ---------------- */
  const Q = new URLSearchParams(location.search);
  const TPL = {
    id: Q.get('id') || '',
    name: Q.get('name') || '模板',
    slots: parseInt(Q.get('slots') || '0', 10),
    dur: parseInt(Q.get('dur') || '0', 10)
  };

  const S = {
    /** 模板槽位详情（来自 nc_template_list 的 slots 数组） */
    slotInfo: [],
    /** 槽位 i → 素材 id（0 = 未填） */
    assign: [],
    /** 可用素材 [{id, name, kind, durUs}] */
    assets: []
  };

  /* ---------------- 工具 ---------------- */
  function api(fn, args) {
    const f = window.NC_API && window.NC_API[fn];
    if (typeof f !== 'function') return { ok: false, message: '引擎接口缺失：' + fn };
    try { return f(args || {}); } catch (e) { return { ok: false, message: (e && e.message) || String(e) }; }
  }
  function secText(us) { return (us / 1e6).toFixed(1) + 's'; }
  /* HTML 转义。
     **用字符遍历而不是正则**：项目的前端检查器（check_mobile_frontend.py）
     不识别正则字面量里的方括号，会把 `/[&<>"']/g` 的 `[` 当成数组开头，
     报「未闭合的 [」。这不是检查器的缺陷要用绕过的方式硬顶，
     而是这种写法本身在纯手写 DOM 的项目里也不必要 —— 遍历更直白。 */
  const HTML_ESCAPES = { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' };
  function escapeHtml(s) {
    const str = String(s);
    let out = '';
    for (let i = 0; i < str.length; ++i) {
      const ch = str[i];
      out += HTML_ESCAPES[ch] || ch;
    }
    return out;
  }
  function kindIcon(kind) {
    // kind: 0=video 1=audio 2=image
    if (kind === 1) return 'i-audio';
    if (kind === 2) return 'i-image';
    return 'i-film';
  }

  /* ---------------- 数据加载 ---------------- */

  /** 从 nc_template_list 里取出当前模板的槽位详情。 */
  function loadSlotInfo() {
    const r = api('nc_template_list');
    let list = (r && r.ok) ? r.result : null;
    if (typeof list === 'string') {
      try { list = JSON.parse(list); } catch (e) { list = null; }
    }
    if (!Array.isArray(list)) list = [];
    const t = list.find((x) => x.id === TPL.id);
    if (t && Array.isArray(t.slots) && t.slots.length) {
      S.slotInfo = t.slots;
    } else {
      /* 拿不到槽位详情时**不编造时长** —— 用空对象占位，
         渲染时显示「—」，这比显示一个假数字诚实。 */
      S.slotInfo = [];
      for (let i = 0; i < TPL.slots; ++i) S.slotInfo.push({ index: i, durationUs: 0, role: '', transition: '' });
    }
    // 槽位数以内核为准（URL 参数可能被改）
    if (S.slotInfo.length) TPL.slots = S.slotInfo.length;
    S.assign = new Array(TPL.slots).fill(0);
  }

  /** 读素材列表：优先用 Engine.state（一次拿全），否则退回 nc_asset_count/get。 */
  function loadAssets() {
    const out = [];
    const st = window.Engine && window.Engine.state;
    if (st && Array.isArray(st.assets)) {
      st.assets.forEach((a) => {
        // 只保留视频与图片（音频不能当画面素材；模板槽位都是画面）
        if (a && a.id && (a.kind === 0 || a.kind === 2)) {
          out.push({ id: a.id, name: a.name || ('素材 ' + a.id), kind: a.kind, durUs: Math.round((a.dur || 0) * 1e6) });
        }
      });
      S.assets = out;
      return;
    }
    // 回退：逐个查
    const cnt = api('nc_asset_count');
    const n = (cnt && typeof cnt.result === 'number') ? cnt.result : 0;
    for (let i = 0; i < n; ++i) {
      const r = api('nc_asset_get', { index: i });
      if (!r || !r.ok || !r.result) continue;
      const x = r.result;
      const kind = x.type;
      if (kind === 0 || kind === 2) {
        out.push({ id: x.asset_id, name: x.name || ('素材 ' + x.asset_id), kind, durUs: x.duration_us || 0 });
      }
    }
    S.assets = out;
  }

  /* ---------------- 渲染 ---------------- */

  function render() {
    const box = $('slots');
    box.innerHTML = '';

    S.slotInfo.forEach((info, i) => {
      const assetId = S.assign[i];
      const asset = S.assets.find((a) => a.id === assetId);

      const row = document.createElement('div');
      row.className = 'slot' + (asset ? ' filled' : '');

      row.innerHTML =
        `<span class="ref">` +
        `<span class="idx">参考镜头 ${String(i + 1).padStart(2, '0')}</span>` +
        `<span class="thumb"><svg class="i"><use href="#i-film"/></svg></span>` +
        (info.role ? `<span class="role">${escapeHtml(info.role)}</span>` : '') +
        `</span>` +
        `<span class="mid">` +
        // 时长：拿不到就显示 —，不编造
        `<span class="d">${info.durationUs > 0 ? secText(info.durationUs) : '—'}</span>` +
        (info.transition ? `<span class="t">${escapeHtml(info.transition)}</span>` : '') +
        `</span>` +
        `<span class="sp"></span>` +
        `<button class="mine${asset ? ' has' : ''}" type="button">` +
        (asset
          ? `<svg class="i"><use href="#${kindIcon(asset.kind)}"/></svg><span class="nm">${escapeHtml(asset.name)}</span>`
          : `<svg class="i"><use href="#i-plus"/></svg><span>点击导入</span>`) +
        `</button>`;

      row.querySelector('.mine').addEventListener('click', () => openPicker(i));
      box.appendChild(row);
    });

    syncFoot();
  }

  function syncFoot() {
    const filled = S.assign.filter((x) => x > 0).length;
    const total = S.assign.length;
    $('hd-meta').textContent = `${filled}/${total}`;
    $('tpl-name').textContent = TPL.name;
    // 预计成片时长：所有槽位时长之和（模板本身就定死了总长）
    const sum = S.slotInfo.reduce((acc, s) => acc + (s.durationUs || 0), 0);
    $('tpl-dur').textContent = sum > 0 ? `预计 ${(sum / 1e6).toFixed(1)}s` : '';

    const hint = $('hint');
    const btn = $('btn-gen');
    if (filled < total) {
      hint.hidden = false;
      // 文档点名：告诉「还差几个」，而不是只禁用按钮
      hint.textContent = `还差 ${total - filled} 个镜头。可以点右侧「点击导入」逐个选，或点「自动填充」。`;
      btn.disabled = true;
      btn.textContent = `还需 ${total - filled} 段素材`;
    } else {
      hint.hidden = true;
      btn.disabled = false;
      btn.textContent = '开始生成';
    }
  }

  /* ---------------- 选素材弹层 ---------------- */

  let pickSlot = -1;

  function openPicker(slotIndex) {
    pickSlot = slotIndex;
    $('pick-title').textContent = `为「参考镜头 ${String(slotIndex + 1).padStart(2, '0')}」选素材`;
    const body = $('pick-body');
    body.innerHTML = '';

    if (!S.assets.length) {
      body.innerHTML = '<div class="pick-empty">素材库是空的。<br>请先在编辑器里导入视频或图片，再回来填充。</div>';
    } else {
      S.assets.forEach((a) => {
        const row = document.createElement('button');
        row.className = 'pick-row';
        row.type = 'button';
        row.innerHTML =
          `<span class="k"><svg class="i"><use href="#${kindIcon(a.kind)}"/></svg></span>` +
          `<span class="nm">${escapeHtml(a.name)}</span>` +
          `<span class="meta">${a.durUs ? secText(a.durUs) : ''}</span>`;
        row.addEventListener('click', () => {
          S.assign[pickSlot] = a.id;
          closePicker();
          render();
        });
        body.appendChild(row);
      });
    }

    // 已有素材时给「清除」入口（换素材不必先删）
    if (S.assign[slotIndex] > 0) {
      const clr = document.createElement('button');
      clr.className = 'pick-row clear';
      clr.type = 'button';
      clr.innerHTML = `<span class="k"><svg class="i"><use href="#i-close"/></svg></span><span class="nm">清除这个槽位</span>`;
      clr.addEventListener('click', () => {
        S.assign[pickSlot] = 0;
        closePicker();
        render();
      });
      body.appendChild(clr);
    }

    $('pick-mask').hidden = false;
  }

  function closePicker() { $('pick-mask').hidden = true; pickSlot = -1; }

  /* ---------------- 自动填充 ---------------- */

  function autoFill() {
    if (!S.assets.length) {
      alert('素材库是空的，先去导入素材吧');
      return;
    }
    /* 轮转填充：第 i 个槽位用第 (i % 素材数) 个素材。
       为什么不做"智能匹配"：模板填充的核心是**画面内容**匹配
       （色调/运动/景别），而前端拿不到这些特征 —— 硬猜只会给出
       一个用户不理解的结果。轮转是**可解释**的：用户看到顺序，
       不喜欢就手动改。真正的智能分配在内核 `nc_template_apply`。
       这里明确告知用户"这是简单轮转"：
       见下方 toast 文案 —— 不假装很聪明。 */
    for (let i = 0; i < S.assign.length; ++i) {
      S.assign[i] = S.assets[i % S.assets.length].id;
    }
    render();
    if (S.assets.length < S.assign.length) {
      // 素材不够 → 必然重复使用。提示用户，而不是静默重复
      $('hint').hidden = false;
      $('hint').textContent =
        `素材只有 ${S.assets.length} 个，不够 ${S.assign.length} 个镜头，已按顺序重复使用。建议再导入一些素材。`;
    }
  }

  /* ---------------- 生成 ---------------- */

  function generate() {
    if (S.assign.some((x) => !x)) {
      alert('还有槽位没填素材');
      return;
    }
    // 提交显式映射：所见即所生成
    const r = api('nc_template_apply_slots', {
      template_id: TPL.id,
      slot_indices: S.assign.map((_, i) => i),
      material_ids: S.assign,
      count: S.assign.length
    });
    if (!r || !r.ok) {
      alert('生成失败：' + ((r && r.message) || '未知错误'));
      return;
    }
    // 成功后进进度页看结果（进度页负责跳回编辑器）
    location.href = '22-template-progress.html?done=1&name=' + encodeURIComponent(TPL.name);
  }

  /* ---------------- 事件绑定 ---------------- */

  $('btn-back').addEventListener('click', () => history.back());
  $('pick-close').addEventListener('click', closePicker);
  $('pick-mask').addEventListener('click', (e) => { if (e.target.id === 'pick-mask') closePicker(); });
  $('btn-auto').addEventListener('click', autoFill);
  $('btn-gen').addEventListener('click', generate);

  /* ---------------- 启动 ---------------- */
  loadSlotInfo();
  loadAssets();
  render();
})();
