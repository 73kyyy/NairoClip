/* NairoClip · 模板选择页逻辑（UI_BACKLOG P-04）
   ============================================================================
   目标：让用户看到**真实的**模板清单（时长/镜头数/音乐提示都来自内核），
   选一个后带着模板 id 进填充页。

   数据来源：`nc_template_list()` 返回 JSON 数组，每项含
     id / name / category / slotCount / totalDurationUs / musicHint / coverPath

   **不伪造任何数据**：没有 coverPath 就显示「暂无封面」占位，
   绝不拿一张与模板内容无关的图冒充预览。这与项目里「不做假功能」
   的一贯要求一致。
*/
'use strict';

(function () {
  const $ = (id) => document.getElementById(id);

  const S = {
    templates: [],      // 全量（内核返回）
    filtered: [],       // 当前筛选结果
    selectedId: null,
    category: '全部',
    query: ''
  };

  /* ---------------- 工具 ---------------- */

  /** 微秒 → "16.4s"。模板时长都是几秒到十几秒，保留一位小数足够。 */
  function durText(us) {
    const sec = us / 1e6;
    return (sec >= 10 ? sec.toFixed(1) : sec.toFixed(1)) + 's';
  }

  /** 调 C ABI（统一走 NC_API，失败时给出可读原因）。 */
  function api(fn, args) {
    const f = window.NC_API && window.NC_API[fn];
    if (typeof f !== 'function') {
      return { ok: false, message: '引擎接口缺失：' + fn };
    }
    try {
      return f(args || {});
    } catch (err) {
      return { ok: false, message: (err && err.message) || String(err) };
    }
  }

  /* ---------------- 渲染 ---------------- */

  function renderChips() {
    // 分类来自真实数据，顺序按出现先后（内核里「卡点/汽车/动漫」的顺序）
    const cats = ['全部'];
    S.templates.forEach((t) => {
      if (t.category && cats.indexOf(t.category) < 0) cats.push(t.category);
    });
    const box = $('chips');
    box.innerHTML = '';
    cats.forEach((c) => {
      const b = document.createElement('button');
      b.className = 'chip' + (c === S.category ? ' on' : '');
      b.textContent = c;
      b.addEventListener('click', () => {
        S.category = c;
        applyFilter();
        renderChips();
      });
      box.appendChild(b);
    });
  }

  function applyFilter() {
    const q = S.query.trim().toLowerCase();
    S.filtered = S.templates.filter((t) => {
      if (S.category !== '全部' && t.category !== S.category) return false;
      if (!q) return true;
      // 搜索范围：名称 + 分类 + 音乐提示（用户常按风格/曲风找）
      const hay = [t.name, t.category, t.musicHint].filter(Boolean).join(' ').toLowerCase();
      return hay.indexOf(q) >= 0;
    });
    renderGrid();
  }

  function renderGrid() {
    const grid = $('grid');
    const empty = $('empty');
    grid.innerHTML = '';

    if (!S.filtered.length) {
      empty.hidden = false;
      // 空态要区分「没数据」和「筛掉了」—— 前者是引擎问题，后者是用户操作
      empty.textContent = S.templates.length
        ? '没有匹配的模板，换个关键词或分类试试'
        : '模板列表为空（引擎未返回数据）';
      return;
    }
    empty.hidden = true;

    S.filtered.forEach((t) => {
      const card = document.createElement('button');
      card.className = 'tpl-card' + (t.id === S.selectedId ? ' on' : '');
      card.type = 'button';

      const cover = t.coverPath
        ? `<img src="${escapeAttr(t.coverPath)}" alt="">`
        : `<span class="ph"><svg class="i"><use href="#i-film"/></svg>暂无封面</span>`;

      card.innerHTML =
        `<span class="tpl-cover">${cover}` +
        `<span class="tpl-dur">${durText(t.totalDurationUs || 0)}</span>` +
        `<span class="tpl-check"><svg class="i"><use href="#i-check"/></svg></span>` +
        `</span>` +
        `<span class="tpl-info">` +
        `<span class="tpl-cat">${escapeHtml(t.category || '未分类')}</span>` +
        `<span class="tpl-name">${escapeHtml(t.name || t.id)}</span>` +
        // 镜头数 + 需要几段素材：把「要准备多少素材」提前告诉用户，
        // 避免进填充页才发现不够（这是文档点名的改进点）
        `<span class="tpl-meta">${t.slotCount || 0} 镜头 · 需 ${t.slotCount || 0} 段</span>` +
        (t.musicHint ? `<span class="tpl-music">♪ ${escapeHtml(t.musicHint)}</span>` : '') +
        `</span>`;

      card.addEventListener('click', () => {
        S.selectedId = (S.selectedId === t.id) ? null : t.id;
        renderGrid();
        syncFoot();
      });
      grid.appendChild(card);
    });
  }

  function syncFoot() {
    const btn = $('btn-next');
    const meta = $('hd-meta');
    const t = S.templates.find((x) => x.id === S.selectedId);
    if (!t) {
      btn.disabled = true;
      btn.textContent = '选择一个模板';
      meta.textContent = '';
      return;
    }
    btn.disabled = false;
    btn.textContent = `用「${t.name}」开始`;
    meta.textContent = `${t.slotCount} 段素材`;
  }

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
  function escapeAttr(s) { return escapeHtml(s); }

  /* ---------------- 加载 ---------------- */

  function loadTemplates() {
    const r = api('nc_template_list');
    // C ABI 的返回结构：{ ok, result, ... } 或 { ok:false, message }
    // nc_template_list 的出参是 out_json（字符串），桥会包一层。
    let raw = r && (r.result !== undefined ? r.result : r.json);
    if (r && r.ok && typeof raw === 'string') {
      try {
        const arr = JSON.parse(raw);
        S.templates = Array.isArray(arr) ? arr : [];
      } catch (err) {
        S.templates = [];
      }
    } else if (r && r.ok && Array.isArray(raw)) {
      S.templates = raw;
    } else {
      S.templates = [];
    }

    renderChips();
    applyFilter();
    syncFoot();

    // 引擎不通时给一句可操作的提示，而不是空白网格
    if (!S.templates.length) {
      $('empty').hidden = false;
      $('empty').textContent = (r && r.message)
        ? '无法读取模板：' + r.message
        : '模板列表为空';
    }
  }

  /* ---------------- 事件 ---------------- */

  function goBack() {
    // 回首页。用 replace 避免返回栈里堆一串模板页
    if (window.history.length > 1) window.history.back();
    else location.replace('01-home.html');
  }

  function goNext() {
    const t = S.templates.find((x) => x.id === S.selectedId);
    if (!t) return;
    // 把模板信息传下去：填充页要用 slotCount 画槽位、用 id 套模板。
    // 用 URL 参数而不是全局变量 —— 页面刷新后仍能恢复。
    const q = new URLSearchParams({
      id: t.id,
      name: t.name || t.id,
      slots: String(t.slotCount || 0),
      dur: String(t.totalDurationUs || 0)
    });
    location.href = '21-template-fill.html?' + q.toString();
  }

  $('btn-back').addEventListener('click', goBack);
  $('btn-next').addEventListener('click', goNext);
  $('q').addEventListener('input', (e) => {
    S.query = e.target.value;
    applyFilter();
  });

  loadTemplates();
})();
