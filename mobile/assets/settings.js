/* NairoClip Mobile · 设置页
   ---------------------------------------------------------------------------
   范围：只放**系统级设置**（账号 / 通用 / 通知 / 存储 / 隐私安全 / 帮助 / 关于）。
   剪辑相关项（吸附、拖拽缩略图、代理剪辑、撤销上限等）已从本页移出，
   它们是剪辑器内的就地控制，放在「设置」里既不属于系统设置、也与实际作用域不符。
   数据来源：存储与引擎状态读平台注入的 window.NCEngine（JNI / C interop 直调，
   不用 HTTP），版本号读打包注入的 NC_APP_VERSION；取不到就不显示，不编造。
   ------------------------------------------------------------------------- */
'use strict';

const $ = (id) => document.getElementById(id);

/* 平台桥（同步）。没有桥就如实显示“不可用”，不伪造数值。 */
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
function readModels() {
  const b = bridge();
  if (!b || typeof b.models !== 'function') throw new Error('无模型接口');
  const raw = b.models();
  const d = (raw && typeof raw === 'object') ? raw : JSON.parse(String(raw || '{}'));
  return d.models || d.items || [];
}

const LS_USER = 'nairoclip.user';
const LS_PREF = 'nairoclip.pref';

const APP_VERSION = window.NC_APP_VERSION || '';

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
  el._t = setTimeout(() => { el.style.opacity = '0'; }, 2200);
}

const pageUrl = (name) => `${name}${qp.get('api') ? '?api=' + encodeURIComponent(qp.get('api')) : ''}`;

/* ---------- 偏好读写 ---------- */
function loadPref() {
  try { return JSON.parse(localStorage.getItem(LS_PREF) || '{}'); } catch (e) { return {}; }
}
function savePref(p) {
  try { localStorage.setItem(LS_PREF, JSON.stringify(p)); } catch (e) {}
}
function getPref(key, def) {
  const p = loadPref();
  return key in p ? p[key] : def;
}
function setPref(key, val) {
  const p = loadPref();
  p[key] = val;
  savePref(p);
}

/* ---------- 开关 ---------- */
function bindSwitch(id, key, defVal, onChange) {
  const el = $(id);
  if (!el) return;
  const on = !!getPref(key, defVal);
  el.setAttribute('aria-pressed', on ? 'true' : 'false');
  el.addEventListener('click', () => {
    const next = el.getAttribute('aria-pressed') !== 'true';
    el.setAttribute('aria-pressed', next ? 'true' : 'false');
    setPref(key, next);
    if (onChange) onChange(next);
  });
}

/* ---------- 下拉 ---------- */
function bindSelect(id, key, defVal, onChange) {
  const el = $(id);
  if (!el) return;
  el.value = String(getPref(key, defVal));
  el.addEventListener('change', () => {
    setPref(key, el.value);
    if (onChange) onChange(el.value);
  });
}

/* ---------- 账号 ---------- */
function readUser() {
  try { return JSON.parse(localStorage.getItem(LS_USER) || 'null'); } catch (e) { return null; }
}

const METHOD_LABEL = { phone: '手机号', wechat: '微信', qq: 'QQ', apple: 'Apple ID' };

function paintUser() {
  const u = readUser();
  const logged = !!(u && u.loggedIn);

  if ($('acc-state')) $('acc-state').textContent = logged ? (u.nickname || '已登录') : '未登录';
  if ($('vip-state')) $('vip-state').textContent = (logged && u.tier > 0) ? '已开通' : '未开通';
  if ($('login-method')) $('login-method').textContent = logged ? (METHOD_LABEL[u.method] || '—') : '—';
  if ($('dev-state')) $('dev-state').textContent = logged ? '1 台（本机）' : '—';

  const rl = $('row-login');
  if (rl && rl.firstChild && rl.firstChild.nodeType === 3) {
    rl.firstChild.textContent = logged ? '切换账号' : '登录 / 注册';
  }
  if ($('row-logout')) $('row-logout').hidden = !logged;
}

if ($('row-login')) {
  $('row-login').addEventListener('click', () => { location.href = pageUrl('03-auth.html'); });
}

if ($('row-logout')) {
  $('row-logout').addEventListener('click', () => {
    if (!window.confirm('退出后本地草稿仍保留，确认退出登录？')) return;
    try { localStorage.setItem(LS_USER, JSON.stringify({ loggedIn: false })); } catch (e) {}
    toast('已退出登录');
    paintUser();
  });
}

/* ---------- 存储（真实引擎数据） ---------- */
async function paintStorage() {
  try {
    const st = readState();
    const dur = (st.project?.duration_us || 0) / 1e6;
    if ($('st-draft')) $('st-draft').textContent = `1 个（${dur.toFixed(1)}s）`;
    if ($('storage-val')) $('storage-val').textContent = '本地工程';
  } catch (e) {
    if ($('st-draft')) $('st-draft').textContent = '无本地工程';
    if ($('storage-val')) $('storage-val').textContent = '—';
    if ($('st-note')) $('st-note').textContent = '未连接到本地引擎，存储统计不可用';
  }

  try {
    const list = readModels();
    /* 引擎字段：status 0=未下载 1=下载中 2=已安装 3=损坏 4=可更新 5=校验中 */
    const ready = list.filter((m) => m.status === 2).length;
    if ($('st-model')) {
      $('st-model').textContent = list.length ? `${ready}/${list.length} 已就绪` : '无模型';
    }
    if (list.length && $('st-bar')) {
      const pct = Math.round((ready / list.length) * 100);
      $('st-bar').style.width = `${pct}%`;
      if ($('st-note')) $('st-note').textContent = `AI 模型就绪度 ${pct}%`;
    }
  } catch (e) {
    if ($('st-model')) $('st-model').textContent = '—';
  }

  if ($('st-proxy')) $('st-proxy').textContent = '未开启';
  if ($('cache-val')) $('cache-val').textContent = '›';
}

if ($('row-clear-cache')) {
  $('row-clear-cache').addEventListener('click', () => {
    toast('缓存清理由平台层执行（当前为 Web 评审宿主）');
  });
}

/* ---------- 隐私与安全 ---------- */
if ($('row-perm')) {
  $('row-perm').addEventListener('click', () => {
    toast('权限管理需跳转系统设置（相册 / 麦克风 / 通知）');
  });
}
if ($('row-export-data')) {
  $('row-export-data').addEventListener('click', () => {
    toast('数据导出由平台层打包（当前为 Web 评审宿主）');
  });
}
if ($('row-wipe')) {
  $('row-wipe').addEventListener('click', () => {
    if (!window.confirm('将清除本机的草稿、缓存与设置，不可恢复。确认继续？')) return;
    toast('清除本地数据需平台层执行（当前为 Web 评审宿主）');
  });
}

/* ---------- 帮助 ---------- */
[['row-faq', '常见问题'], ['row-feedback', '意见反馈'], ['row-logs', '诊断日志']]
  .forEach(([id, name]) => {
    if ($(id)) $(id).addEventListener('click', () => toast(`${name}：待接入正式内容`));
  });

if ($('row-update')) {
  $('row-update').addEventListener('click', () => {
    // 无更新服务端点：如实说明，不假装检查过
    toast('当前为离线构建，暂无更新服务');
  });
}

/* ---------- 关于 ---------- */
async function paintAbout() {
  if ($('about-build')) $('about-build').textContent = APP_VERSION || '评审宿主';
  if ($('about-ver')) $('about-ver').textContent = APP_VERSION || '';

  /* 本地引擎连通性 */
  try {
    const st = readState();
    if ($('about-core')) $('about-core').textContent = st.project ? '已连接' : '已连接 · 空工程';
  } catch (e) {
    if ($('about-core')) $('about-core').textContent = '未连接';
  }

  /* AI 引擎：有模型清单即视为在线 */
  try {
    const list = readModels();
    const ready = list.filter((m) => m.status === 2).length;
    if ($('about-ai')) {
      $('about-ai').textContent = list.length ? `在线 · ${ready}/${list.length} 就绪` : '在线 · 无模型';
    }
  } catch (e) {
    if ($('about-ai')) $('about-ai').textContent = '未连接';
  }
}

/* ---------- 协议文档 ---------- */
const DOC_NAME = {
  terms: '用户协议',
  privacy: '隐私政策',
  copyright: '版权与许可',
  thirdparty: '第三方组件'
};
document.querySelectorAll('[data-doc]').forEach((el) => {
  el.addEventListener('click', () => {
    toast(`${DOC_NAME[el.dataset.doc] || '文档'}：待接入正式文本`);
  });
});

/* ---------- 折叠分组 ---------- */
document.querySelectorAll('.row[data-open]').forEach((btn) => {
  btn.addEventListener('click', () => {
    const group = btn.closest('.group');
    const opened = group.classList.contains('open');
    document.querySelectorAll('.group.open').forEach((g) => {
      if (g !== group) g.classList.remove('open');
    });
    group.classList.toggle('open', !opened);
  });
});

/* ---------- 返回 ---------- */
if ($('btn-back')) {
  $('btn-back').addEventListener('click', () => { location.href = pageUrl('01-home.html'); });
}

/* ---------- 初始化 ---------- */
paintUser();
paintStorage();
paintAbout();

/* 通用 */
bindSelect('sel-lang', 'lang', 'zh-CN', (v) => {
  if (v !== 'zh-CN') toast('多语言文案待接入，当前仅简体中文完整');
});
bindSelect('sel-theme', 'theme', 'auto');
bindSelect('sel-start', 'startPage', 'home');
bindSwitch('sw-guide', 'showGuide', true);

/* 通知 */
bindSwitch('sw-draft', 'notifyDraft', false);
bindSwitch('sw-export', 'notifyExport', true);
bindSwitch('sw-promo', 'notifyPromo', false);

/* 隐私 */
bindSwitch('sw-network', 'allowNetworkAssets', false, (on) => {
  if (on) toast('联网素材需在系统设置授予网络权限');
});
