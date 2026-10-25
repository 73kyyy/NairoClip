/* NairoClip Mobile · 登录页组
   ---------------------------------------------------------------------------
   页面结构：
     03-auth.html     方式列表（手机号 / 微信 / QQ / Apple）→ 点哪个跳哪个
     03a-phone.html   手机号 + 短信验证码
     03b/03c/03d      微信 / QQ / Apple 授权页
   平台：Apple 仅 iOS。列表页在非 iOS 上移除该按钮；03d 页在非 iOS 上直接拒绝。
   登录仍非使用前提：「稍后再说」随时可跳过。
   ------------------------------------------------------------------------- */
'use strict';

const $ = (id) => document.getElementById(id);
const qp = new URLSearchParams(location.search);

const LS_USER = 'nairoclip.user';
const LS_AGREED = 'nairoclip.agreedDocs';

/* 沙箱无短信网关：本地体验模式显式标注，不伪装真实发送 */
const SMS_MODE = 'local';

/* 当前页身份：由 body 上的 data-page 决定（auth / phone / wechat / qq / apple） */
const PAGE = document.body.dataset.page || 'auth';

/* ---------- 平台探测 ----------
   原生层注入 window.NC_PLATFORM（'ios'/'android'/…）为权威；
   纯浏览器评审宿主回落 UA + 多点触控（iPadOS 13+ 伪装成 macOS）。 */
function detectPlatform() {
  const injected = (window.NC_PLATFORM || '').toString().toLowerCase();
  if (injected) return injected;

  const ua = (navigator.userAgent || '').toLowerCase();
  const touch = navigator.maxTouchPoints || 0;
  if (/iphone|ipod/.test(ua)) return 'ios';
  if (/ipad/.test(ua) || (/macintosh/.test(ua) && touch > 1)) return 'ios';
  if (/android/.test(ua)) return 'android';
  if (/macintosh|mac os x/.test(ua)) return 'macos';
  if (/windows/.test(ua)) return 'windows';
  return 'web';
}

const PLATFORM = detectPlatform();
const IS_IOS = PLATFORM === 'ios';

/* ---------- 工具 ---------- */
function toast(msg) {
  let el = $('toast');
  if (!el) {
    el = document.createElement('div');
    el.id = 'toast';
    el.style.cssText =
      'position:fixed;left:50%;bottom:80px;transform:translateX(-50%);' +
      'padding:9px 15px;border-radius:9px;background:rgba(28,28,31,.96);' +
      'border:1px solid #33333A;color:#EDEFF1;font-size:12.5px;z-index:999;' +
      'max-width:78vw;text-align:center;line-height:1.5;pointer-events:none;' +
      'transition:opacity .2s';
    document.body.appendChild(el);
  }
  el.textContent = msg;
  el.style.opacity = '1';
  clearTimeout(el._t);
  el._t = setTimeout(() => { el.style.opacity = '0'; }, 2300);
}

const withApi = (name) => `${name}${qp.get('api') ? '?api=' + encodeURIComponent(qp.get('api')) : ''}`;

const goHome = () => { location.href = withApi('01-home.html'); };
const goList = () => { location.href = withApi('03-auth.html'); };

/* ---------- 登录态收尾 ---------- */
function persistLogin(nickname, extra) {
  const user = Object.assign({
    loggedIn: true,
    nickname,
    uid: 'NC' + Date.now().toString(36).toUpperCase().slice(-8),
    tier: 0,
    since: new Date().toISOString().slice(0, 10)
  }, extra || {});
  try {
    localStorage.setItem(LS_USER, JSON.stringify(user));
    localStorage.setItem(LS_AGREED, '1');
  } catch (e) {
    toast('本地存储不可用，登录态无法保存');
  }
  return user;
}

/* 协议是否已同意。
   列表页（PAGE==='auth'）的协议是纯说明文字、没有勾选框（与参考稿一致），
   点「使用微信登录」这类按钮本身即视为已确认——因此列表页恒为 true。
   手机号页有显式勾选框，必须真的勾上。 */
function agreed() {
  if (PAGE === 'auth') return true;
  const a = $('agree');
  return !!(a && a.checked);
}


/* ===========================================================================
   原生授权桥
   ---------------------------------------------------------------------------
   第三方登录**不经过中间的「授权确认页」**：点按钮即调起对应 App，
   授权在对方 App 内完成，回到本应用时由原生层回调结果。

   约定（平台层需实现其一）：
     · Android：window.NCAndroid.authorize(provider)   // @JavascriptInterface
     · iOS    ：window.webkit.messageHandlers.ncAuth.postMessage({provider})
   两者都不存在时（Web 评审宿主），如实提示「无原生层」，不伪造授权流程。
   =========================================================================== */
const PROVIDER_NAME = { wechat: '微信', qq: 'QQ', apple: 'Apple' };

function hasNativeAuth() {
  if (window.NCAndroid && typeof window.NCAndroid.authorize === 'function') return 'android';
  const wk = window.webkit;
  if (wk && wk.messageHandlers && wk.messageHandlers.ncAuth) return 'ios';
  return null;
}

/* 调起第三方授权。返回 true 表示已交给原生层（结果走回调）。 */
function launchAuthorize(provider) {
  const bridge = hasNativeAuth();
  if (!bridge) {
    toast(`${PROVIDER_NAME[provider] || '第三方'}授权需要 App 内的原生登录 SDK，当前为 Web 评审宿主`);
    return false;
  }
  try {
    if (bridge === 'android') {
      window.NCAndroid.authorize(provider);
    } else {
      window.webkit.messageHandlers.ncAuth.postMessage({ provider });
    }
    return true;
  } catch (e) {
    toast('调起授权失败：' + (e && e.message ? e.message : '未知错误'));
    return false;
  }
}

/* 原生层授权完成后调用它落登录态。
   用法（平台层）：
     Android: webView.evaluateJavascript("NC_AUTH_DONE('wechat', {...})", null)
     iOS    : evaluateJavaScript("NC_AUTH_DONE('wechat', {...})")
   失败时传 ok=false，只显示原因，不写登录态。 */
window.NC_AUTH_DONE = function (provider, payload) {
  const p = payload || {};
  if (p.ok === false || p.error) {
    toast((PROVIDER_NAME[provider] || '授权') + '失败：' + (p.error || '用户取消或授权被拒'));
    return;
  }
  const name = p.nickname || p.name ||
               ({ wechat: '微信用户', qq: 'QQ 用户', apple: 'Apple 用户' }[provider] || '用户');
  persistLogin(name, { method: provider, uid: p.uid, avatar: p.avatar });
  toast('已登录');
  setTimeout(goHome, 420);
};

/* ---------- 列表页 ---------- */
if (PAGE === 'auth') {
  if (!IS_IOS) {
    const apple = $('btn-apple');
    if (apple) apple.remove();      // 移除而非隐藏：DOM 里不存在就无法被重新显示
  }

  document.querySelectorAll('.auth-btn').forEach((btn) => {
    btn.addEventListener('click', () => {
      const m = btn.dataset.m;
      if (m === 'apple' && !IS_IOS) {
        toast('Apple 登录仅在 iOS 设备可用');
        return;
      }

      // 手机号没有第三方 App 可拉，走应用内表单页
      if (m === 'phone') {
        location.href = withApi('03a-phone.html');
        return;
      }

      // 微信 / QQ / Apple：直接调起对应 App 授权，不插中间页
      if (agreed()) {
        // 调起外部 App 后本页会切到后台；给出「正在唤醒」反馈，
        // 万一切后台失败（如未安装该 App），用户也能看出是点了没反应。
        const label = btn.querySelector('.ab-txt');
        const original = label ? label.textContent : '';
        if (label) label.textContent = `正在唤醒${PROVIDER_NAME[m]}…`;
        btn.disabled = true;
        const launched = launchAuthorize(m);
        setTimeout(() => {
          if (label) label.textContent = original;
          btn.disabled = false;
        }, launched ? 2600 : 600);
        return;
      }
    });
  });

  if ($('btn-back')) $('btn-back').addEventListener('click', goHome);
  if ($('btn-help')) $('btn-help').addEventListener('click', () => toast('登录说明：待接入正式文案'));
  if ($('btn-later')) {
    $('btn-later').addEventListener('click', () => {
      try { localStorage.setItem(LS_USER, JSON.stringify({ loggedIn: false })); } catch (e) {}
      goHome();
    });
  }
}

/* ---------- 子页通用：返回 ---------- */
if ($('btn-back') && PAGE !== 'auth') {
  $('btn-back').addEventListener('click', goList);
}

/* ---------- 手机号页 ---------- */
if (PAGE === 'phone') {
  const S = { phone: '', code: '', sending: false, countdown: 0, timer: null, demoCode: '' };

  const isPhone = (v) => /^1[3-9]\d{9}$/.test(v);
  const isCode = (v) => /^\d{6}$/.test(v);

  function setErr(which, msg) {
    const el = $('err-' + which);
    const fld = $('field-' + which);
    if (!el || !fld) return;
    if (msg) { el.textContent = msg; el.hidden = false; fld.classList.add('bad'); }
    else { el.hidden = true; fld.classList.remove('bad'); }
  }

  function refreshSubmit() {
    const btn = $('btn-submit');
    if (btn) btn.disabled = !(isPhone(S.phone) && isCode(S.code) && agreed());
  }

  function refreshSend() {
    const btn = $('btn-send');
    if (!btn) return;
    if (S.countdown > 0) { btn.disabled = true; btn.textContent = `${S.countdown}s`; return; }
    btn.disabled = !isPhone(S.phone) || S.sending;
    btn.textContent = S.sending ? '发送中…' : '获取验证码';
  }

  $('phone').addEventListener('input', (e) => {
    const raw = e.target.value.replace(/\D/g, '').slice(0, 11);
    e.target.value = raw;
    S.phone = raw;
    $('btn-clear').hidden = raw.length === 0;
    setErr('phone', raw.length === 11 && !isPhone(raw) ? '请填写有效的 11 位手机号' : '');
    refreshSend();
    refreshSubmit();
  });

  $('btn-clear').addEventListener('click', () => {
    $('phone').value = '';
    S.phone = '';
    $('btn-clear').hidden = true;
    setErr('phone', '');
    refreshSend();
    refreshSubmit();
    $('phone').focus();
  });

  $('code').addEventListener('input', (e) => {
    const raw = e.target.value.replace(/\D/g, '').slice(0, 6);
    e.target.value = raw;
    S.code = raw;
    setErr('code', raw && !isCode(raw) ? '验证码为 6 位数字' : '');
    refreshSubmit();
  });

  $('agree').addEventListener('change', refreshSubmit);

  $('btn-send').addEventListener('click', () => {
    if (!isPhone(S.phone) || S.countdown > 0 || S.sending) return;
    S.sending = true;
    refreshSend();
    setTimeout(() => {
      S.sending = false;
      if (SMS_MODE === 'local') {
        S.demoCode = String(Math.floor(100000 + Math.random() * 900000));
        toast(`本地体验模式：验证码 ${S.demoCode}（未真实发送短信）`);
        $('code').focus();
      }
      S.countdown = 60;
      refreshSend();
      S.timer = setInterval(() => {
        S.countdown -= 1;
        if (S.countdown <= 0) { clearInterval(S.timer); S.timer = null; }
        refreshSend();
      }, 1000);
    }, 420);
  });

  $('btn-submit').addEventListener('click', () => {
    if (!isPhone(S.phone)) { setErr('phone', '请填写有效手机号'); return; }
    if (!isCode(S.code)) { setErr('code', '请填写 6 位验证码'); return; }
    if (!agreed()) { toast('请先阅读并同意用户协议与隐私政策'); return; }
    if (SMS_MODE === 'local' && S.demoCode && S.code !== S.demoCode) {
      setErr('code', '验证码不正确（见上方提示）');
      return;
    }
    const masked = S.phone.slice(0, 3) + '****' + S.phone.slice(7);
    persistLogin(`创作者 ${S.phone.slice(-4)}`, { phone: masked, method: 'phone' });
    toast('已登录');
    setTimeout(goHome, 460);
  });

  refreshSend();
  refreshSubmit();
}

/* ---------- 协议文档 ---------- */
document.querySelectorAll('[data-doc]').forEach((a) => {
  a.addEventListener('click', (e) => {
    e.preventDefault();
    toast(a.dataset.doc === 'terms' ? '用户协议：待接入正式文本' : '隐私政策：待接入正式文本');
  });
});

/* ---------- 已有登录态：列表页直接回首页 ---------- */
if (PAGE === 'auth') {
  try {
    const raw = localStorage.getItem(LS_USER);
    if (raw) {
      const u = JSON.parse(raw);
      if (u && u.loggedIn) setTimeout(goHome, 320);
    }
  } catch (e) { /* 忽略损坏数据 */ }
}
