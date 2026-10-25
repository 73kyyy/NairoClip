import { ICONS } from './icons.js';

export const icon = (name, cls = 'ic') =>
  `<svg class="${cls}" viewBox="0 0 16 16" aria-hidden="true">${ICONS[name] || ''}</svg>`;

export const el = (tag, attrs = {}, html = '') => {
  const node = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) {
    if (k === 'class') node.className = v;
    else if (k === 'text') node.textContent = v;
    else if (k.startsWith('on')) node.addEventListener(k.slice(2), v);
    else if (v !== null && v !== undefined && v !== false) node.setAttribute(k, v === true ? '' : v);
  }
  if (html) node.innerHTML = html;
  return node;
};

export const $ = (sel, root = document) => root.querySelector(sel);
export const $$ = (sel, root = document) => [...root.querySelectorAll(sel)];

export async function api(path, body) {
  const res = await fetch(path, body === undefined ? {} : {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  });
  if (!res.ok && res.status !== 409) {
    const text = await res.text();
    throw new Error(text || `HTTP ${res.status}`);
  }
  return res.json();
}

/** 微秒 → 时间码。fps 用有理数分子分母。 */
export function timecode(us, fpsNum = 30, fpsDen = 1, frames = true) {
  const fps = fpsNum / fpsDen;
  const total = Math.max(0, us);
  const totalSec = total / 1e6;
  const h = Math.floor(totalSec / 3600);
  const m = Math.floor((totalSec % 3600) / 60);
  const s = Math.floor(totalSec % 60);
  const f = Math.floor((totalSec - Math.floor(totalSec)) * fps + 1e-6);
  const p2 = (n) => String(n).padStart(2, '0');
  return frames ? `${p2(h)}:${p2(m)}:${p2(s)}:${p2(f)}` : `${p2(h)}:${p2(m)}:${p2(s)}`;
}

/** 微秒 → 紧凑时长（片段标签用）。 */
export function duration(us) {
  const sec = us / 1e6;
  if (sec < 60) return `${sec.toFixed(sec < 10 ? 1 : 0)}s`;
  const m = Math.floor(sec / 60);
  const s = Math.round(sec % 60);
  return `${m}:${String(s).padStart(2, '0')}`;
}

export function bytes(n) {
  if (!n) return '0 B';
  const units = ['B', 'KB', 'MB', 'GB'];
  let v = n;
  let i = 0;
  while (v >= 1024 && i < units.length - 1) { v /= 1024; i += 1; }
  return `${v >= 10 || i === 0 ? Math.round(v) : v.toFixed(1)} ${units[i]}`;
}

export function toast(message, kind = '') {
  const old = $('.toast');
  if (old) old.remove();
  const node = el('div', { class: `toast ${kind === 'alarm' ? 'is-alarm' : ''}` },
    `${icon(kind === 'alarm' ? 'warning' : 'info', 'ic ic-sm')}<span>${message}</span>`);
  document.body.append(node);
  clearTimeout(toast._t);
  toast._t = setTimeout(() => node.remove(), 2600);
}

/** 吸附到最近一帧（使用有理帧率）。 */
export function snapFrame(us, fpsNum = 30, fpsDen = 1) {
  const frameUs = (1e6 * fpsDen) / fpsNum;
  return Math.round(us / frameUs) * frameUs;
}
