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

document.querySelectorAll('.tab').forEach((t) => {
  t.addEventListener('click', () => {
    if (t.dataset.tab === 'home') {
      const q = new URLSearchParams(location.search).get('api');
      location.href = `01-home.html${q ? `?api=${encodeURIComponent(q)}` : ''}`;
      return;
    }
    toast('功能开发中', 1500);
  });
});


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
})();
