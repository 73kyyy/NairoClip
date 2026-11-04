/* =============================================================================
   法律文档阅读页（05-legal.html）
   -----------------------------------------------------------------------------
   入口约定（全应用统一，任何模块都可直接跳转）：
     05-legal.html?doc=terms              打开「用户协议」
     05-legal.html?doc=privacy            打开「隐私政策」
     05-legal.html?doc=copyright          打开「版权与许可」
     05-legal.html?doc=aiNotice           打开「AI 生成内容标识说明」
     05-legal.html?doc=aiModels           打开「AI 功能与模型说明」
     05-legal.html?doc=aiNotice#sec-3     打开并定位到第 3 段

   另支持从其他页面用 NCLegal.open('terms', 3) 直接跳转（见文件末尾）。
   依赖：legal-text.js（window.NCLegal）。
   ============================================================================= */
(function () {
  'use strict';

  const L = window.NCLegal;
  if (!L) {
    document.body.insertAdjacentHTML('beforeend',
      '<p style="padding:24px;color:#e66">法律文本模块加载失败。</p>');
    return;
  }

  const $ = (id) => document.getElementById(id);
  const params = new URLSearchParams(location.search);
  const DOC_ORDER = ['terms', 'privacy', 'copyright', 'aiNotice', 'aiModels'];

  /** 当前文档 id（非法值回落到用户协议）。 */
  let currentId = params.get('doc');
  if (!L.getDoc(currentId)) currentId = 'terms';

  /** 目标段落（来自 hash 或 query）。 */
  let targetSection = 0;
  const hash = (location.hash || '').replace('#', '');
  if (hash.startsWith('sec-')) targetSection = parseInt(hash.slice(4), 10) || 0;

  /** 返回目标页。
   *
   * 规则（用户 2026-11-04 定）：
   *   从弹窗进来  → 回首页（弹窗会重新弹出，用户仍需明确选择）
   *   从我的页进来 → 回我的页
   *   其他/直接访问 → 有历史就后退，否则回设置页
   *
   * 为什么「从弹窗进来」是回首页而不是「回弹窗」：
   *   弹窗是 01-home.html 的一个 DOM 层，不是一个页面。
   *   回首页后 `consent-gate.js` 发现还没同意，会重新弹出 ——
   *   效果就是「回到弹窗」，且状态正确（未选择）。
   *   如果强行用 history.back()，某些 WebView 会恢复到
   *   「已经点过同意」的缓存页面，反而不对。
   */
  function back() {
    const from = params.get('from');
    if (from === 'gate') {
      // 弹窗来源：回首页，让 consent-gate 重新弹
      location.href = '01-home.html';
      return;
    }
    if (from) { location.href = from; return; }
    if (history.length > 1) { history.back(); return; }
    location.href = '04-settings.html';
  }

  /** 文档切换标签 —— **已停用**。
   *
   * 用户明确要求（2026-11-04）：
   *   「单独页面展示，页面互不干涉，不能在里面点击其他的。
   *     用户协议里面不能选择隐私政策，单独的页面，里面不包含。」
   *
   * 所以这里不再渲染 Tab 栏。每篇文档是独立页面，
   * 想换一篇必须先返回、再点另一个链接。
   *
   * 为什么保留函数名而不是直接删：paintTabs 在下方初始化里被调用，
   * 直接删会 ReferenceError。改成空实现最安全。
   * DOC_ORDER 保留给将来「文档索引页」复用（如果要做的话）。
   */
  function paintTabs() {
    const box = $('legal-tabs');
    if (box) box.remove();   // 连容器一起移除，避免留一条空白带
    return;
  }

  /** 旧实现（保留备查，不调用）。
   *  注意：切换 Tab 会让用户在没有明确返回动作的情况下换到另一篇文档，
   *  这与「独立页面」的产品要求冲突。 */
  function paintTabsLegacy() {
    const box = $('legal-tabs');
    if (!box) return;
    box.innerHTML = '';
    DOC_ORDER.forEach((id) => {
      const doc = L.getDoc(id);
      if (!doc) return;
      const b = document.createElement('button');
      b.className = 'legal-tab';
      b.type = 'button';
      b.setAttribute('role', 'tab');
      b.setAttribute('aria-selected', id === currentId ? 'true' : 'false');
      b.textContent = doc.title;
      b.addEventListener('click', () => {
        if (id === currentId) return;
        currentId = id;
        targetSection = 0;
        history.replaceState(null, '', `?doc=${id}`);
        paint();
        window.scrollTo({ top: 0, behavior: 'smooth' });
      });
      box.appendChild(b);
    });
  }

  /** 渲染正文。 */
  function paintBody() {
    const doc = L.getDoc(currentId);
    if (!doc) return;

    $('doc-title').textContent = doc.title;
    $('doc-heading').textContent = doc.title;
    $('doc-meta').textContent = `更新日期：${doc.updated}　·　版本：${L.LEGAL_VERSION}`;

    const body = $('doc-body');
    body.innerHTML = '';
    doc.sections.forEach((sec, i) => {
      const wrap = document.createElement('section');
      wrap.className = 'legal-sec' + (targetSection === i + 1 ? ' highlight' : '');
      wrap.id = `sec-${i + 1}`;

      const h = document.createElement('h3');
      h.textContent = sec.h;
      wrap.appendChild(h);

      (sec.ps || []).forEach((text) => {
        const p = document.createElement('p');
        p.textContent = text;   // textContent：不解析 HTML，避免注入
        wrap.appendChild(p);
      });

      body.appendChild(wrap);
    });

    $('doc-foot').textContent =
      `本文档由 NairoClip 提供，随版本更新。若对条款有异议，可通过「帮助与反馈」联系我们。` +
      `\n共 ${doc.sections.length} 条，当前为 ${doc.title}。`;
  }

  /** 定位到指定段落。 */
  function scrollToSection() {
    if (!targetSection) return;
    const el = document.getElementById(`sec-${targetSection}`);
    if (el) el.scrollIntoView({ behavior: 'smooth', block: 'start' });
  }

  function paint() {
    paintTabs();
    paintBody();
    scrollToSection();
  }

  /* -------------------------------------------------------------- 交互绑定 */
  if ($('btn-back')) $('btn-back').addEventListener('click', back);

  if ($('btn-copy')) {
    $('btn-copy').addEventListener('click', async () => {
      const text = L.toPlainText(currentId);
      try {
        if (navigator.clipboard && navigator.clipboard.writeText) {
          await navigator.clipboard.writeText(text);
        } else {
          const ta = document.createElement('textarea');
          ta.value = text;
          ta.style.position = 'fixed';
          ta.style.opacity = '0';
          document.body.appendChild(ta);
          ta.select();
          document.execCommand('copy');
          document.body.removeChild(ta);
        }
        if (window.toast) window.toast('全文已复制');
      } catch (e) {
        if (window.toast) window.toast('复制失败，请长按选择文本');
      }
    });
  }

  if ($('btn-top')) {
    $('btn-top').addEventListener('click', () => {
      window.scrollTo({ top: 0, behavior: 'smooth' });
    });
  }

  paint();
})();
