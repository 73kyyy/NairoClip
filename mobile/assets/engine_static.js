/* 单文件离线版数据层：与 Engine 同接口，只读构建时内联的 NC_STATIC 快照。
   不发任何网络请求（docs/UI_INTEGRATION.md §7：Web/WASM 不支持 C ABI）。
   真机运行走 engine.js → window.NCEngine（JNI / C interop 直调）。 */
(function () {
  const D = window.NC_STATIC || { state: {}, frames: {}, thumbs: {}, wave: [] };
  const clampIdx = (list, i) => list[Math.max(0, Math.min(list.length - 1, i))];
  const frameKeys = Object.keys(D.frames).map(Number).sort((a, b) => a - b);

  window.Engine = {
    base: 'static',
    apiBase() { return 'static'; },
    state: D.state,
    online: true,
    lastError: '',

    async boot() { return true; },
    async refresh() { return this.state; },

    /* 与真实接口同形的“编辑”：静态版不修改工程，如实告知 */
    async edit(payload) {
      const op = payload && payload.op;
      if (op === 'frame_fit' || op === 'canvas' || op === 'track_flag') {
        return { ok: true, code: 0, message: '' };
      }
      throw new Error('单文件离线版只读展示，不写入工程；编辑请用原生客户端');
    },

    /* 取与时间最接近的真实渲染帧 */
    frameUrl(timeUs) {
      if (!frameKeys.length) return '';
      const t = Math.max(0, Math.round(timeUs));
      let best = frameKeys[0];
      for (const k of frameKeys) {
        if (Math.abs(k - t) < Math.abs(best - t)) best = k;
      }
      return D.frames[best];
    },
    thumbUrl(assetId) {
      return D.thumbs[assetId] || D.thumbs[Object.keys(D.thumbs)[0]] || '';
    },
    async waveform() {
      return { buckets: D.wave || [] };
    }
  };
})();
