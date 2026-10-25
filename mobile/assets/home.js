/* NairoClip Mobile · 首页
   -----------------------------------------------------------------------------
   数据来源（不伪造）：
     · 单文件版：window.NC_STATIC_PROJECTS（构建时内联的快照）
     · 原生版：NCEngine.listProjects()（平台层扫描应用私有目录）
   首页不展示任何素材画面：草稿只用「标题 + 时长 + 修改时间」文字行承载。
   无底部 Tab（按需求移除）；「我的」入口在左上角头像。 */
'use strict';

const H = {
  projects: [],
  filter: '',
  managing: false,
  picked: new Set(),
  base: (new URLSearchParams(location.search).get('api')
         || window.NC_API_BASE || location.origin)
};

const $ = (id) => document.getElementById(id);

const qs = () => new URLSearchParams(location.search).get('api');
const editorUrl = () => {
  const q = qs();
  return `10-editor.html${q ? `?api=${encodeURIComponent(q)}` : ''}`;
};
const pageUrl = (name) => {
  const q = qs();
  return `${name}${q ? `?api=${encodeURIComponent(q)}` : ''}`;
};
const meUrl = () => pageUrl('02-me.html');
const settingsUrl = () => pageUrl('04-settings.html');
/* 登录入口：先进「登录方式」页（手机号/微信/QQ/AppleID），
   不在首页直接展开手机号表单。 */
const authUrl = () => pageUrl('03-auth.html');

/* 登录态：头像是「我的」入口；未登录时先去登录页。
   登录只影响会员/云同步，不影响本地剪辑。 */
const isLoggedIn = () => {
  try {
    const u = JSON.parse(localStorage.getItem('nairoclip.user') || 'null');
    return !!(u && u.loggedIn);
  } catch (e) {
    return false;
  }
};

function toast(text, ms = 2000) {
  document.querySelectorAll('.toast').forEach((n) => n.remove());
  const d = document.createElement('div');
  d.className = 'toast';
  d.textContent = text;
  $('screen').appendChild(d);
  setTimeout(() => d.remove(), ms);
}

function fmtDur(sec) {
  const s = Math.max(0, Math.round(sec || 0));
  if (!s) return '';
  const h = Math.floor(s / 3600);
  const m = Math.floor((s % 3600) / 60);
  const r = s % 60;
  const pad = (n) => String(n).padStart(2, '0');
  return h ? `${h}:${pad(m)}:${pad(r)}` : `${pad(m)}:${pad(r)}`;
}

function fmtSize(bytes) {
  if (!bytes) return '';
  const mb = bytes / 1048576;
  return mb >= 1 ? `${mb.toFixed(1)} MB` : `${Math.max(1, Math.round(bytes / 1024))} KB`;
}

/* ---------- 载入 ---------- */
async function loadProjects() {
  if (Array.isArray(window.NC_STATIC_PROJECTS)) {
    H.projects = window.NC_STATIC_PROJECTS;
    return;
  }
  /* 草稿列表由平台层提供（C ABI 没有工程目录接口）：
     · Android/iOS：桥的 listProjects() 扫描应用私有目录
     · 无桥：如实为空，不编造草稿 */
  const br = window.NCEngine;
  if (!br || typeof br.listProjects !== 'function') {
    H.projects = [];
    return;
  }
  try {
    const raw = br.listProjects();
    const j = (raw && typeof raw === 'object') ? raw : JSON.parse(String(raw || '{}'));
    H.projects = (j.projects || []).map((p) => ({
      name: p.name,
      file: p.file,
      bytes: p.bytes,
      dur: p.duration_us ? p.duration_us / 1e6 : 0,
      updated: p.updated || ''
    }));
  } catch (err) {
    H.projects = [];
  }
}

/* ---------- 渲染 ---------- */
function render() {
  const box = $('list');
  const emptyBox = $('empty');
  box.innerHTML = '';

  const kw = H.filter.trim().toLowerCase();
  const shown = H.projects
    .map((p, i) => ({ p, i }))
    .filter(({ p }) => !kw || (p.name || '').toLowerCase().includes(kw));

  $('draft-count').textContent = String(shown.length);
  $('btn-manage').textContent = H.managing ? '完成' : '管理';
  $('btn-manage').hidden = H.projects.length === 0;
  $('screen').classList.toggle('managing', H.managing);

  const has = shown.length > 0;
  emptyBox.hidden = has;
  box.hidden = !has;

  if (!has) {
    const b = emptyBox.querySelector('b');
    const s = emptyBox.querySelector('small');
    if (kw) {
      b.textContent = '没有匹配的草稿';
      s.innerHTML = `换个关键词试试<br>或清空搜索查看全部`;
    } else {
      b.textContent = '还没有任何草稿';
      s.innerHTML = '点击上方「开始创作」<br>选好素材就能开始第一条片子';
    }
  }

  shown.forEach(({ p, i }) => {
    const row = document.createElement('button');
    row.className = 'row' + (H.picked.has(i) ? ' picked' : '');

    const dur = fmtDur(p.dur);
    const meta = [p.updated || '', fmtSize(p.bytes)].filter(Boolean).join(' · ');

    row.innerHTML = `
      <span class="mark"></span>
      <span class="chk"></span>
      <span class="info">
        <span class="t"></span>
        <span class="m"></span>
      </span>
      ${dur ? `<span class="d">${dur}</span>` : ''}`;
    row.querySelector('.t').textContent = p.name || '未命名草稿';
    row.querySelector('.m').textContent = meta || '本地草稿';

    row.addEventListener('click', () => {
      if (H.managing) {
        if (H.picked.has(i)) H.picked.delete(i); else H.picked.add(i);
        render();
        return;
      }
      openProject(p);
    });
    row.addEventListener('contextmenu', (e) => {
      e.preventDefault();
      H.managing = true;
      H.picked.add(i);
      render();
    });
    box.appendChild(row);
  });

  renderManageBar();
}

function renderManageBar() {
  let bar = document.getElementById('manage-bar');
  if (!H.managing) {
    if (bar) bar.remove();
    return;
  }
  if (bar) return;

  bar = document.createElement('div');
  bar.className = 'mbar';
  bar.id = 'manage-bar';

  const del = document.createElement('button');
  del.className = 'del';
  del.textContent = H.picked.size ? `删除（${H.picked.size}）` : '删除';
  del.addEventListener('click', deletePicked);

  const off = document.createElement('button');
  off.className = 'off';
  off.textContent = '取消';
  off.addEventListener('click', () => {
    H.managing = false;
    H.picked.clear();
    render();
  });

  bar.appendChild(del);
  bar.appendChild(off);
  $('screen').appendChild(bar);
}

async function deletePicked() {
  if (!H.picked.size) return toast('请先选择草稿');
  const n = H.picked.size;
  // 静态版与只读服务版都无法真正落盘删除，这里如实说明
  H.projects = H.projects.filter((_, i) => !H.picked.has(i));
  H.picked.clear();
  H.managing = false;
  render();
  toast(window.NC_STATIC_PROJECTS
    ? `已从列表移除 ${n} 个（静态版不落盘）`
    : `已从列表移除 ${n} 个`);
}

function openProject(p) {
  if (!p.file) {
    location.href = editorUrl();
    return;
  }
  const q = qs();
  const join = q ? '&' : '?';
  location.href = `${editorUrl()}${join}project=${encodeURIComponent(p.file)}`;
}

/* ---------- 搜索 ---------- */
function enterSearch(on) {
  $('screen').classList.toggle('searching', on);
  if (on) {
    $('q').value = H.filter;
    setTimeout(() => $('q').focus(), 30);
  } else {
    $('q').value = '';
    H.filter = '';
    render();
  }
}

/* ---------- 事件 ---------- */
$('btn-create').addEventListener('click', () => { location.href = editorUrl(); });

/* 四个能力入口：均为剪辑页内的真实功能模块，这里先说明再引导进编辑器 */
const CAP = {
  auto:   '智能成片：选中素材后按节奏自动生成一版可继续精修的工程',
  ref:    '参考仿剪：分析参考素材的节奏/转场/调色，用你的素材重剪',
  beat:   '音乐卡点：检测鼓点并自动在节拍处切分镜头',
  cutout: '智能抠像：人像/物品/绿幕分离，用于换背景与叠加'
};
document.querySelectorAll('.q').forEach((b) => {
  b.addEventListener('click', () => {
    const k = b.dataset.q;
    toast(CAP[k] || '功能开发中', 2400);
  });
});

$('btn-manage').addEventListener('click', () => {
  H.managing = !H.managing;
  if (!H.managing) H.picked.clear();
  render();
});

$('btn-search').addEventListener('click', () => enterSearch(true));
$('btn-cancel').addEventListener('click', () => enterSearch(false));
$('q').addEventListener('input', (e) => {
  H.filter = e.target.value;
  render();
});
$('q').addEventListener('keydown', (e) => {
  if (e.key === 'Escape') enterSearch(false);
});

$('btn-me').addEventListener('click', () => {
  location.href = isLoggedIn() ? meUrl() : authUrl();
});
$('btn-settings').addEventListener('click', () => { location.href = settingsUrl(); });

/* ---------- 启动 ---------- */
(async function boot() {
  await loadProjects();
  render();
})();
