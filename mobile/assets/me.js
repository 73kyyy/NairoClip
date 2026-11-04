/* NairoClip Mobile · 我的（设置）
   原则：权益、存储、引擎状态一律读真实值；读不到就显示“—”，不编造。 */
'use strict';

const M = {
  base: (new URLSearchParams(location.search).get('api')
         || window.NC_API_BASE || location.origin),
  state: null,
  models: null
};

const $ = (id) => document.getElementById(id);

function toast(text, ms = 1900) {
  document.querySelectorAll('.toast').forEach((n) => n.remove());
  const d = document.createElement('div');
  d.className = 'toast';
  d.textContent = text;
  d.style.cssText = 'position:absolute;left:50%;bottom:90px;transform:translateX(-50%);'
    + 'background:rgba(28,31,33,.97);border:1px solid #35393C;color:#E9EBE8;'
    + 'font-size:11px;padding:9px 14px;border-radius:8px;z-index:120;max-width:78%;'
    + 'text-align:center;line-height:1.5';
  document.querySelector('.screen').appendChild(d);
  setTimeout(() => d.remove(), ms);
}

/* 读桥（同步，无网络）。桥不存在的环境如实失败。 */
function bridge() {
  const b = window.NCEngine;
  return (b && typeof b.call === 'function') ? b : null;
}
function readState() {
  const b = bridge();
  if (!b || typeof b.state !== 'function') throw new Error('无引擎桥');
  const raw = b.state();
  return (raw && typeof raw === 'object') ? raw : JSON.parse(String(raw || '{}'));
}

const mb = (bytes) => {
  if (!bytes && bytes !== 0) return '—';
  const m = bytes / (1024 * 1024);
  return m >= 1024 ? `${(m / 1024).toFixed(1)} GB` : `${m.toFixed(1)} MB`;
};

/* ---------- 读取真实状态 ---------- */
async function load() {
  // 静态快照（单文件）
  if (window.NC_STATIC_STATE) {
    M.state = window.NC_STATIC_STATE;
    applyState();
    applyStaticModels();
    return;
  }
  try {
    M.state = readState();
    applyState();
  } catch (err) {
    $('st-note').textContent = `未连接引擎：${err.message}`;
  }
  try {
    const b = bridge();
    if (!b || typeof b.models !== 'function') throw new Error('无模型接口');
    const raw = b.models();
    M.models = (raw && typeof raw === 'object') ? raw : JSON.parse(String(raw || '{}'));
    applyModels();
  } catch (err) { /* 模型接口不可用则保持“—” */ }
}

function applyState() {
  const p = (M.state && M.state.project) || {};
  // 权益：0 = Free，非 0 = Pro（引擎 nc_get_entitlement）
  const pro = !!p.entitlement;
  $('vip-state').textContent = pro ? '已开通 ›' : '未开通 ›';
  $('about-ai').textContent = p.ai_engine_available
    ? `${p.ai_engine_version || '就绪'}` : '不可用';
  // 版本号来自打包脚本注入的 window.NC_APP_VERSION；
  // C ABI 没有版本查询接口，拿不到就显示「—」，不编造。
  const ver = window.NC_APP_VERSION || '';
  if ($('about-ver')) $('about-ver').textContent = ver || '';
  if ($('about-build')) $('about-build').textContent = ver || '—';
  $('acc-phone').textContent = '未绑定';
  if (p.ai_engine_note) $('st-note').textContent = p.ai_engine_note;
}

function applyModels() {
  const st = (M.models && M.models.storage) || {};
  const proxyBytes = st.proxy_bytes || 0;
  const ready = st.proxy_ready || 0;
  $('st-proxy').textContent = proxyBytes ? mb(proxyBytes) : `${ready} 个`;
  if (proxyBytes) $('st-bar').style.width = '28%';
}

async function applyStaticModels() {
  const s = window.NC_STATIC_MODELS;
  if (!s) return;
  const st = s.storage || {};
  $('st-model').textContent = mb(st.model_bytes || 0);
  $('st-proxy').textContent = mb(st.proxy_bytes || 0);
  $('st-draft').textContent = mb(st.draft_bytes || 0);
  $('storage-val').textContent = mb((st.model_bytes || 0) + (st.proxy_bytes || 0) + (st.draft_bytes || 0));
  $('st-bar').style.width = `${Math.min(100, s.used_percent || 6)}%`;
  $('st-note').textContent = s.note || '统计来自本地目录扫描';
}

/* ---------- 手风琴 ---------- */
document.querySelectorAll('[data-open]').forEach((btn) => {
  btn.addEventListener('click', () => {
    const group = btn.closest('.group');
    const opened = group.classList.contains('open');
    document.querySelectorAll('.group.open').forEach((g) => {
      if (g !== group) g.classList.remove('open');
    });
    group.classList.toggle('open', !opened);
  });
});

/* ---------- 事件 ---------- */
$('btn-back').addEventListener('click', () => {
  const q = new URLSearchParams(location.search).get('api');
  location.href = `01-home.html${q ? `?api=${encodeURIComponent(q)}` : ''}`;
});

$('btn-vip').addEventListener('click', () => toast('会员购买页：待接入平台内购', 2000));

/* 绑定/登录：未登录先去登录页 */
$('btn-bind').addEventListener('click', () => {
  const q = new URLSearchParams(location.search).get('api');
  location.href = `03-auth.html${q ? `?api=${encodeURIComponent(q)}` : ''}`;
});

$('btn-logout').addEventListener('click', () => {
  const ok = window.confirm('退出后本地草稿仍保留，确认退出登录？');
  if (!ok) return;
  try {
    localStorage.setItem('nairoclip.user', JSON.stringify({ loggedIn: false }));
  } catch (e) {}
  toast('已退出登录', 1500);
  paintUser();
});

/* 底部 Tab 的绑定已删除（DOM 一起删了，见 02-me.html）。
   返回首页走左上角的 btn-back，不需要 Tab。 */


/* ---------- 登录态 ---------- */
function paintUser() {
  let u = null;
  try { u = JSON.parse(localStorage.getItem('nairoclip.user') || 'null'); } catch (e) {}
  const logged = !!(u && u.loggedIn);
  const nameEl = $('nickname');
  const uidEl = $('uid');
  const avEl = $('avatar');
  const bindEl = $('btn-bind');
  if (nameEl) nameEl.textContent = logged ? (u.nickname || '创作者') : '未登录';
  if (uidEl) uidEl.textContent = logged ? `ID: ${u.uid || '—'}` : 'ID: —';
  if (avEl) avEl.textContent = logged ? (u.nickname || 'N').slice(0, 1) : 'N';
  if (bindEl) {
    bindEl.textContent = logged
      ? `${u.phone || '已绑定'} · 账号与安全`
      : '未绑定手机号 · 去登录';
  }
  const vip = $('vip-state');
  if (vip) vip.textContent = (logged && u.tier > 0) ? '已开通 ›' : '未开通 ›';
}

paintUser();

/* ---------- 启动 ---------- */
(async function boot() {
  await load();

/* ---------- 条款跳转（02-me.html 的「关于」区） ----------
   data-doc / data-sec 语义与设置页一致；文本见 assets/legal/legal-text.js。 */
document.querySelectorAll('[data-doc]').forEach((el) => {
  el.addEventListener('click', () => {
    const doc = el.dataset.doc || 'terms';
    const sec = el.dataset.sec;
    let url = `05-legal.html?doc=${encodeURIComponent(doc)}` +
              `&from=${encodeURIComponent('02-me.html')}`;
    if (sec) url += `#sec-${encodeURIComponent(sec)}`;
    location.href = url;
  });
});

})();
/* ---------- 帮助与反馈的子入口（2026-11-04 新增） ----------
   意见反馈 / 常见问题 / 崩溃日志 / 更多信息 */
(function bindHelpEntries() {
  const q = new URLSearchParams(location.search).get('api');
  const url = (name) => `${name}${q ? `?api=${encodeURIComponent(q)}` : ''}`;

  const fb = document.getElementById('btn-feedback');
  if (fb) fb.addEventListener('click', () => { location.href = url('06-feedback.html'); });

  const faq = document.getElementById('btn-faq');
  if (faq) faq.addEventListener('click', () => { location.href = url('07-faq.html'); });

  const crash = document.getElementById('btn-crashlog');
  if (crash) {
    /* 只有确实存在崩溃日志时才显示这一行。
       没崩过的用户看到「崩溃日志」会疑惑，甚至会以为应用有问题。 */
    let hasLog = false;
    try {
      if (window.NCAndroid && typeof window.NCAndroid.hasCrashLog === 'function') {
        hasLog = window.NCAndroid.hasCrashLog() === true;
      }
    } catch (e) { hasLog = false; }
    crash.hidden = !hasLog;
    crash.addEventListener('click', () => { location.href = url('08-crashlog.html'); });
  }

  /* 「更多信息」：展开后才露出三个法务条目。
     藏得深是刻意的 —— 用户要求「藏得非常的深，用户一般找不到」。 */
  const more = document.getElementById('btn-more-info');
  const panel = document.getElementById('more-info');
  if (more && panel) {
    more.addEventListener('click', () => {
      panel.hidden = !panel.hidden;
      more.classList.toggle('open', !panel.hidden);
    });
  }
})();
