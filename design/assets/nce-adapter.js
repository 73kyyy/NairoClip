/* ============================================================================
   设计桥适配器：把 /api（HTTP）包装成 window.NCEngine（同步）
   ----------------------------------------------------------------------------
   背景：mobile/assets/engine.js 按 docs/UI_INTEGRATION.md §7 的设计，
   只认平台注入的同步桥 NCEngine（Android JNI / iOS C interop / Qt 直调），
   代码里不出现任何 URL。这样 WebView 里既能跑真实引擎，也不会
   把请求打到静态服务器再收到 HTML。

   设计桥（design/tools/nc_design_harness）提供的是 HTTP /api/*。
   本文件在**页面加载前**注入一个 NCEngine 兼容层，把同步调用转成
   同步 XHR。只在设计评审环境使用，不进入产品运行时（不进 mobile/）。

   为什么用同步 XHR：engine.js 的调用点全是同步的（boot() 里直接取
   state 并渲染），改成异步会牵动整个编辑器。同步 XHR 在主线程会阻塞，
   仅用于本地 127.0.0.1 的设计评审，生产环境由真正的原生桥替代。
   ========================================================================== */
(function () {
  'use strict';

  const BASE = '';   // 同源

  function syncJson(method, path, body) {
    const x = new XMLHttpRequest();
    x.open(method, BASE + path, false);          // false = 同步
    if (body !== undefined) {
      x.setRequestHeader('Content-Type', 'application/json');
    }
    try {
      x.send(body === undefined ? null : JSON.stringify(body));
    } catch (e) {
      throw new Error(`桥请求失败：${path} (${e.message})`);
    }
    if (x.status !== 200) {
      throw new Error(`桥返回 HTTP ${x.status}：${path}`);
    }
    return x.responseText;
  }

  function syncBinary(path) {
    const x = new XMLHttpRequest();
    x.open('GET', BASE + path, false);
    x.overrideMimeType('text/plain; charset=x-user-defined');
    try {
      x.send(null);
    } catch (e) {
      return '';
    }
    if (x.status !== 200) return '';
    const raw = x.responseText || '';
    let bin = '';
    for (let i = 0; i < raw.length; i += 1) {
      bin += String.fromCharCode(raw.charCodeAt(i) & 0xff);
    }
    return btoa(bin);
  }

  window.NCEngine = {
    /* ---- 必需 ---- */
    state() {
      return syncJson('GET', '/api/state');
    },
    call(fn, argsJson) {
      let args = {};
      if (argsJson) {
        try { args = JSON.parse(argsJson); } catch (e) { args = {}; }
      }
      /* 两条不同的通道，按名字分流：
         · nc_xxx  —— 直接映射 C ABI 函数，走 /api/call
         · 其余     —— 时间轴编辑操作（split/trim/transition/...），
                       桥实现为 /api/edit，参数里要带 op
         早期这里一律发 /api/call，导致所有编辑操作 404。 */
      if (/^nc_/.test(fn)) {
        return syncJson('POST', '/api/call', { fn, args });
      }
      return syncJson('POST', '/api/edit', Object.assign({ op: fn }, args));
    },

    /* ---- 预览帧：返回 data URL ---- */
    frameDataUrl(timeUs, width) {
      const t = Math.round(timeUs || 0);
      const w = Math.round(width || 420);
      try {
        return `data:image/png;base64,${syncBinary(`/api/frame?t=${t}&w=${w}`)}`;
      } catch (e) {
        return '';
      }
    },

    /* ---- 缩略图 ---- */
    thumbDataUrl(assetId, index, width) {
      const i = Math.round(index || 0);
      const w = Math.round(width || 96);
      try {
        return `data:image/png;base64,${syncBinary(`/api/thumb?asset=${assetId}&i=${i}&w=${w}`)}`;
      } catch (e) {
        return '';
      }
    },

    /* ---- 波形桶：返回 JSON 字符串 ---- */
    waveform(assetId, buckets) {
      try {
        return syncJson('GET', `/api/waveform?asset=${assetId}&n=${Math.round(buckets || 256)}`);
      } catch (e) {
        return '[]';
      }
    },
  };
}());
