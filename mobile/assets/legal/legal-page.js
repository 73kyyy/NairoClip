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

  /** 返回目标页（默认设置页）。 */
  function back() {
    const from = params.get('from');
    if (from) { location.href = from; return; }
    if (history.length > 1) { history.back(); return; }
    location.href = '04-settings.html';
  }

  /** 渲染顶部文档切换标签。 */
  function paintTabs() {
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
