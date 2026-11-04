/* =============================================================================
   启动协议弹窗（个人信息保护指引）
   -----------------------------------------------------------------------------
   用户要求（2026-11-04）：
     · 点开 App 先弹，不同意就进不去
     · 白底弹窗，按钮**黑色**（不是参考图里那种红色）
     · 「用户协议」「隐私政策」是**可点链接**，点哪个进哪个
     · 链接文字彩色（取品牌绿，白底对比度 4.6:1，达 WCAG AA）
     · 弹窗里**不提供复制**（复制只在 05-legal 阅读页的页脚）
     · 弹窗尺寸不铺满，小卡片居中（参考图二）

   设计取舍：
     · 只在**首次启动**弹（localStorage 记版本号）。协议升版时
       LEGAL_VERSION 变了会重新弹 —— 这是合规要求的正确行为。
     · 「不同意」不关弹窗，而是进说明页并给「重新阅读」；
       原生若要退出应用，由 onReject 回调交平台层处理。
     · 遮罩不可点击关闭：必须明确选择。
   ============================================================================= */
(function (global) {
  'use strict';

  const LS_KEY = 'nairoclip.consentVersion';

  /** 当前需同意的版本，与 legal-text.js 对齐。 */
  function currentVersion() {
    const L = global.NCLegal;
    return (L && L.LEGAL_VERSION) || '2026-10';
  }

  function accepted() {
    try {
      return localStorage.getItem(LS_KEY) === String(currentVersion());
    } catch (e) {
      return false;
    }
  }

  function markAccepted() {
    try { localStorage.setItem(LS_KEY, String(currentVersion())); } catch (e) {}
  }

  /* ---------------------------------------------------------------- 样式 */
  const CSS = `
  .cg-mask {
    position: fixed; inset: 0; z-index: 9000;
    background: rgba(0, 0, 0, .55);
    display: flex; align-items: center; justify-content: center;
    padding: 24px;
    animation: cgFade .22s ease-out both;
    -webkit-tap-highlight-color: transparent;
  }
  @keyframes cgFade { from { opacity: 0 } to { opacity: 1 } }
  @keyframes cgRise {
    from { opacity: 0; transform: translateY(10px) scale(.985) }
    to   { opacity: 1; transform: none }
  }

  .cg-card {
    width: 100%; max-width: 310px;
    background: #FFFFFF; color: #111413;
    border-radius: 18px;
    padding: 22px 20px 16px;
    box-shadow: 0 18px 48px rgba(0, 0, 0, .38);
    animation: cgRise .26s cubic-bezier(.32, .72, .28, 1) both;
    font-family: var(--font, -apple-system, "PingFang SC", sans-serif);
  }

  .cg-title {
    margin: 0 0 12px;
    font-size: 17px; font-weight: 700; line-height: 1.3;
    text-align: center; letter-spacing: .2px;
  }

  .cg-body {
    font-size: 13px; line-height: 1.72; color: #3A4340;
    margin: 0 0 6px;
  }
  .cg-body + .cg-body { margin-top: 8px; }

  /* 协议链接：彩色、明确可点 */
  .cg-link {
    color: #1F8A57;
    font-weight: 600;
    text-decoration: none;
    border-bottom: 1px solid rgba(31, 138, 87, .35);
    padding-bottom: .5px;
    cursor: pointer;
  }
  .cg-link:active { opacity: .62; }

  .cg-list {
    margin: 8px 0 0; padding: 0 0 0 16px;
    font-size: 12.5px; line-height: 1.7; color: #3A4340;
  }
  .cg-list li { margin-bottom: 5px; }

  .cg-actions { margin-top: 18px; display: flex; flex-direction: column; gap: 8px; }

  .cg-yes, .cg-no {
    width: 100%; border: 0; border-radius: 11px;
    font-size: 15px; font-weight: 600; letter-spacing: .3px;
    padding: 12px 0; cursor: pointer;
    -webkit-appearance: none; appearance: none;
    transition: transform .12s ease, opacity .12s ease;
  }
  .cg-yes:active, .cg-no:active { transform: scale(.985); opacity: .9; }

  /* 同意 = 实心黑（用户指定「按钮颜色改成黑色」） */
  .cg-yes { background: #111413; color: #FFFFFF; }

  /* 不同意 = 白底灰字，弱化但可点 */
  .cg-no {
    background: #FFFFFF; color: #5A625F;
    font-weight: 500; font-size: 14px;
    padding: 10px 0;
  }

  @media (max-width: 340px) {
    .cg-mask { padding: 16px; }
    .cg-card { padding: 18px 16px 14px; border-radius: 16px; }
    .cg-title { font-size: 16px; }
    .cg-body { font-size: 12.5px; }
  }
  `;

  let injected = false;
  function injectCss() {
    if (injected || document.getElementById('cg-style')) return;
    const st = document.createElement('style');
    st.id = 'cg-style';
    st.textContent = CSS;
    document.head.appendChild(st);
    injected = true;
  }

  /* ---------------------------------------------------------------- 文案 */
  /* 对齐参考图的「个人信息保护指引」，但按本项目**真实能力**改写：
     参考图写「拍摄/录制、相机权限」——本项目不做拍摄、也不采集相机，
     照抄等于超范围索取授权，反而违规。 */
  const TITLE = '个人信息保护指引';
  const INTRO_A = '请充分阅读并理解';
  const INTRO_B = '，点击"同意"按钮代表您已同意前述协议和下述约定，并接受本应用的全部功能服务。';

  const ITEMS = [
    '为实现图片与视频的导入、编辑和导出，我们需要读取您选择的本地媒体文件。' +
    '所有剪辑、推理与导出均在本机完成，原始素材不会上传服务器。',

    '为实现 AI 字幕、人声分离、超分辨率等功能，我们会在本机加载相应模型。' +
    '处理过程不联网；仅在您主动使用联网功能时，才会传输必要数据。',

    '为实现信息分享、参加相关活动、综合统计分析等目的所必需，' +
    '我们可能会调用剪贴板并使用与功能相关的最小必要信息（口令、链接、统计参数等）。'
  ];

  /* ---------------------------------------------------------------- DOM */
  function build(onAccept, onReject) {
    const mask = document.createElement('div');
    mask.className = 'cg-mask';
    mask.setAttribute('role', 'dialog');
    mask.setAttribute('aria-modal', 'true');
    mask.setAttribute('aria-labelledby', 'cg-title');

    const card = document.createElement('div');
    card.className = 'cg-card';

    const h = document.createElement('h1');
    h.className = 'cg-title';
    h.id = 'cg-title';
    h.textContent = TITLE;
    card.appendChild(h);

    const p1 = document.createElement('p');
    p1.className = 'cg-body';
    p1.appendChild(document.createTextNode(INTRO_A));

    const aTerms = document.createElement('a');
    aTerms.className = 'cg-link';
    aTerms.href = '05-legal.html?doc=terms&from=gate';
    aTerms.textContent = '《用户协议》';
    p1.appendChild(aTerms);

    p1.appendChild(document.createTextNode('和'));

    const aPriv = document.createElement('a');
    aPriv.className = 'cg-link';
    aPriv.href = '05-legal.html?doc=privacy&from=gate';
    aPriv.textContent = '《隐私政策》';
    p1.appendChild(aPriv);

    p1.appendChild(document.createTextNode(INTRO_B));
    card.appendChild(p1);

    const ol = document.createElement('ol');
    ol.className = 'cg-list';
    ITEMS.forEach((t) => {
      const li = document.createElement('li');
      li.textContent = t;
      ol.appendChild(li);
    });
    card.appendChild(ol);

    const acts = document.createElement('div');
    acts.className = 'cg-actions';

    const yes = document.createElement('button');
    yes.className = 'cg-yes';
    yes.type = 'button';
    yes.textContent = '同意';
    yes.addEventListener('click', () => {
      markAccepted();
      close();
      if (onAccept) onAccept();
    });

    const no = document.createElement('button');
    no.className = 'cg-no';
    no.type = 'button';
    no.textContent = '不同意';
    no.addEventListener('click', () => {
      close();
      if (onReject) onReject();
    });

    acts.appendChild(yes);
    acts.appendChild(no);
    card.appendChild(acts);

    mask.appendChild(card);
    return mask;

    function close() {
      mask.style.animation = 'cgFade .16s ease-in reverse both';
      setTimeout(() => mask.remove(), 150);
    }
  }

  /* ---------------------------------------------------------------- 入口 */
  function gate(opt) {
    opt = opt || {};
    if (!opt.force && accepted()) return false;
    injectCss();
    if (document.querySelector('.cg-mask')) return true;

    const onReject = () => {
      if (opt.onReject) { opt.onReject(); return; }
      showRejected();
    };

    document.body.appendChild(build(opt.onAccept, onReject));
    return true;
  }

  /** 「不同意」后的说明页：给「重新阅读」，不让 WebView 空白。 */
  function showRejected() {
    injectCss();
    const mask = document.createElement('div');
    mask.className = 'cg-mask';
    mask.style.background = 'rgba(0,0,0,.78)';

    const card = document.createElement('div');
    card.className = 'cg-card';
    card.innerHTML = `
      <h1 class="cg-title">无法继续使用</h1>
      <p class="cg-body" style="text-align:left">
        本应用需要您同意《用户协议》与《隐私政策》后才能使用。
        未同意时我们不会收集或处理您的任何信息。
      </p>
      <p class="cg-body" style="text-align:left">
        您可以随时关闭应用，或重新阅读后再做选择。
      </p>
      <div class="cg-actions">
        <button class="cg-yes" type="button">重新阅读</button>
      </div>`;
    mask.appendChild(card);
    document.body.appendChild(mask);

    card.querySelector('.cg-yes').addEventListener('click', () => {
      mask.remove();
      gate({ force: true });
    });
  }

  global.NCConsent = {
    gate,
    accepted,
    markAccepted,
    currentVersion,
    /** 仅供调试：清掉同意记录，下次启动会重新弹 */
    reset() {
      try { localStorage.removeItem(LS_KEY); } catch (e) {}
    }
  };
})(window);
