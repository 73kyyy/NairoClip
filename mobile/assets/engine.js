/* NairoClip Mobile · 引擎数据层
   ---------------------------------------------------------------------------
   依据 docs/UI_INTEGRATION.md §7「平台层对接（不要用 HTTP）」：

     Android  Kotlin + JNI 直调 nc_*      →  window.NCEngine（@JavascriptInterface）
     iOS      Swift + C interop           →  window.NCEngine（WKScriptMessageHandler）
     桌面 Qt  C++ 直调                     →  window.NCEngine
     Web/WASM 当前不支持                   →  这里如实报「无引擎」

   前端只认平台注入的 NCEngine；代码里不出现任何 URL、端口或 HTTP 请求。
   桥是同步的：NCEngine.call / state / frameDataUrl 都直接返回结果，
   因此不会再有「端点打到静态服务器、收到 HTML 再喂给 JSON.parse」这类故障。

   没有引擎时明确告知，不用假数据填充。
   ------------------------------------------------------------------------- */
'use strict';

/* 平台桥检测。同步、无网络。 */
function engineBridge() {
  const b = window.NCEngine;
  if (!b) return null;
  // 必须有 call 与 state，否则视为不完整实现
  if (typeof b.call !== 'function' || typeof b.state !== 'function') return null;
  return b;
}

/* 安全解析桥返回的 JSON。桥约定返回字符串；某些平台会直接返回对象。 */
function parseBridge(v) {
  if (v === null || v === undefined) return null;
  if (typeof v === 'object') return v;
  const text = String(v);
  if (!text.trim()) return null;
  try {
    return JSON.parse(text);
  } catch (err) {
    // 桥返回了非 JSON（例如异常堆栈），转换成人能看懂的信息
    throw new Error(`引擎返回内容无法解析：${text.slice(0, 80)}`);
  }
}

const Engine = {
  base: 'native',          // 不再是 URL；只为兼容旧的 apiBase() 调用
  apiBase() { return this.base; },
  state: null,
  online: false,
  lastError: '',
  bridge: null,

  /* 同步探测：平台是否注入了引擎桥 */
  boot() {
    const b = engineBridge();
    if (!b) {
      this.online = false;
      this.state = null;
      this.bridge = null;
      this.lastError = /Android/i.test(navigator.userAgent)
        ? '原生桥未注入：请在 WebView 中 addJavascriptInterface(new NativeEngineBridge(), "NCEngine")'
        : '当前环境没有 NairoClip 引擎桥（Web/WASM 版不支持 C ABI，见 docs/UI_INTEGRATION.md §7）';
      return false;
    }
    try {
      const s = parseBridge(b.state());
      if (!s || !s.project) throw new Error('桥返回的工程状态不完整');
      this.bridge = b;
      this.state = s;
      this.online = true;
      this.lastError = '';
      return true;
    } catch (err) {
      this.online = false;
      this.state = null;
      this.bridge = b;
      this.lastError = `读取工程状态失败：${err.message}`;
      return false;
    }
  },

  refresh() {
    if (!this.bridge) return this.state;
    try {
      const s = parseBridge(this.bridge.state());
      if (s && s.project) { this.state = s; this.online = true; }
      return this.state;
    } catch (err) {
      this.lastError = err.message;
      return this.state;
    }
  },

  /* 编辑：直接调桥，同步拿到结果 */
  edit(payload) {
    if (!this.bridge) throw new Error('引擎未连接');
    const op = payload && payload.op;
    const args = Object.assign({}, payload);
    delete args.op;
    const r = parseBridge(this.bridge.call(op, JSON.stringify(args)));
    if (!r || !r.ok) {
      throw new Error((r && r.message) || `操作失败（${(r && r.code) ?? '未知'}）`);
    }
    this.refresh();
    return r;
  },

  /* 预览帧：桥直接给 data:URL，<img> 无需任何网络请求 */
  frameUrl(timeUs, width = 282) {
    if (!this.bridge || typeof this.bridge.frameDataUrl !== 'function') return '';
    try {
      const u = this.bridge.frameDataUrl(Math.max(0, Math.round(timeUs)), width);
      return (typeof u === 'string' && u.slice(0, 5) === 'data:') ? u : '';
    } catch (err) {
      return '';
    }
  },

  thumbUrl(assetId, index = 0, width = 120) {
    if (!this.bridge || typeof this.bridge.thumbDataUrl !== 'function') return '';
    try {
      const u = this.bridge.thumbDataUrl(assetId, index, width);
      return (typeof u === 'string' && u.slice(0, 5) === 'data:') ? u : '';
    } catch (err) {
      return '';
    }
  },

  waveform(assetId) {
    if (!this.bridge || typeof this.bridge.waveform !== 'function') {
      return { buckets: [] };
    }
    try {
      const r = parseBridge(this.bridge.waveform(assetId));
      return (r && Array.isArray(r.buckets)) ? r : { buckets: [] };
    } catch (err) {
      return { buckets: [] };
    }
  }
};

window.Engine = Engine;
