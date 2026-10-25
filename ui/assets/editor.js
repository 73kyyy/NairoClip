/* ============================================================================
   NairoClip · 剪辑页交互（Part 2 框架 + 工具栏 / 三级面板 UI）
   依据：《NairoClip 产品UI和UX开发文档》
     §5 顶部栏 / §6 预览区 / §7 刻度尺与撤销行 / §8 轨道区
     §9 悬浮播放控制 / §10 三级面板通用框架 / §11–§22 工具栏结构
     §28 新增轨道 / §29 性能提示 / §31 缩放边界 / §32 撤销栈 / §33 预览区手势
   说明：本文件是 UI/UX 原型；时间轴、拖拽、吸附、撤销、播放等交互均为真实逻辑，
        AI 类面板的进度条为界面交互演示（真实推理由 core + C ABI 承担）。
   ========================================================================== */
(function () {
  "use strict";

  var $ = function (s, r) { return (r || document).querySelector(s); };
  var $$ = function (s, r) { return Array.prototype.slice.call((r || document).querySelectorAll(s)); };
  var clamp = function (v, a, b) { return v < a ? a : v > b ? b : v; };
  var cap = function (el, id) {
    try { if (el && el.setPointerCapture) el.setPointerCapture(id); } catch (e) { /* 合成事件无捕获 */ }
  };
  var FPS = 30;

  var root = $("#ed");
  if (!root) return;

  /* ======================================================================
     A. 状态与时间轴模型
     ====================================================================== */
  var S = {
    pps: 54,              // 每秒像素（默认 1 秒 / 格，§31）
    scrollLeft: 0,
    playT: 0,
    duration: 22,
    playing: false,
    selected: null,       // {trackId, clipId}
    multi: [],
    tracks: [],
    history: [],
    redo: [],
    collapsed: false,
    panel: null,
    seq: 100
  };

  function uid(p) { return (p || "c") + (++S.seq); }

  function defaultTracks() {
    return [
      {
        id: "t-v1", type: "video", hidden: false, locked: false, muted: false, h: 48,
        clips: [
          { id: "c1", start: 0, dur: 6, label: "VID_0921", kind: "video" },
          { id: "c2", start: 6, dur: 7.5, label: "VID_0925", kind: "video" },
          { id: "c3", start: 13.5, dur: 5.2, label: "IMG_0930", kind: "video" }
        ]
      },
      {
        id: "t-v2", type: "video", hidden: false, locked: false, muted: false, h: 42,
        clips: [
          { id: "c8", start: 2.4, dur: 3.6, label: "前景 · 贴纸层", kind: "video" }
        ]
      },
      {
        id: "t-a1", type: "audio", hidden: false, locked: false, muted: false, h: 44,
        clips: [
          { id: "c4", start: 1, dur: 17.4, label: "海边日落.mp3", kind: "audio" }
        ]
      },
      {
        id: "t-a2", type: "audio", hidden: false, locked: false, muted: false, h: 40,
        clips: [
          { id: "c9", start: 4.8, dur: 3.2, label: "音效 · 海浪", kind: "audio" }
        ]
      },
      {
        id: "t-s1", type: "sub", hidden: false, locked: false, muted: false, h: 38,
        clips: [
          { id: "c5", start: 0.6, dur: 3.4, label: "我们沿着海岸线走了很久", kind: "sub" },
          { id: "c6", start: 4.6, dur: 3.2, label: "风把云吹成了金色", kind: "sub" },
          { id: "c7", start: 9.2, dur: 3.6, label: "直到太阳落进海里", kind: "sub" }
        ]
      }
    ];
  }
  S.tracks = defaultTracks();

  var TRACK_ICON = { video: "film", audio: "audio", sub: "subtitle" };
  /* 素材条随机浅色底（红/蓝/绿/橙/棕/黄）：仅作用于轨道上的素材块 */
  var TONES = ["red", "blue", "green", "orange", "brown", "yellow"];
  function toneOf(c) {
    if (!c.tone) c.tone = TONES[Math.floor(Math.random() * TONES.length)];
    return c.tone;
  }
  var TRACK_NAME = { video: "视频轨", audio: "音频轨", sub: "字幕轨" };

  function allClips() {
    var out = [];
    S.tracks.forEach(function (t) { t.clips.forEach(function (c) { out.push({ t: t, c: c }); }); });
    return out;
  }
  function findClip(id) {
    var hit = null;
    S.tracks.forEach(function (t) {
      t.clips.forEach(function (c) { if (c.id === id) hit = { t: t, c: c }; });
    });
    return hit;
  }
  function selectedEntry() { return S.selected ? findClip(S.selected.clipId) : null; }
  function clipEnd(c) { return c.start + c.dur; }
  function sortClips(t) { t.clips.sort(function (a, b) { return a.start - b.start; }); }

  function recalcDuration() {
    var end = 12;
    allClips().forEach(function (x) { end = Math.max(end, clipEnd(x.c)); });
    S.duration = end;
  }

  /* ======================================================================
     B. 撤销栈（§32：20 步，仅编辑操作）
     ====================================================================== */
  function snapshot() {
    return JSON.stringify(S.tracks);
  }
  function pushHistory() {
    S.history.push(snapshot());
    if (S.history.length > 20) S.history.shift();
    S.redo.length = 0;
    syncHistoryBtns();
  }
  function syncHistoryBtns() {
    var u = $("#btn-undo"), r = $("#btn-redo");
    if (u) u.disabled = S.history.length === 0;
    if (r) r.disabled = S.redo.length === 0;
  }
  function undo() {
    if (!S.history.length) return;
    S.redo.push(snapshot());
    S.tracks = JSON.parse(S.history.pop());
    afterModelChange(true);
    toast("已撤销");
  }
  function redo() {
    if (!S.redo.length) return;
    S.history.push(snapshot());
    S.tracks = JSON.parse(S.redo.pop());
    afterModelChange(true);
    toast("已重做");
  }
  function afterModelChange(keepTime) {
    if (!findClip(S.selected && S.selected.clipId)) S.selected = null;
    S.multi = S.multi.filter(function (id) { return !!findClip(id); });
    recalcDuration();
    renderTracks();
    renderRuler();
    syncHistoryBtns();
    syncToolbarState();
    if (!keepTime) clampScroll();
  }

  /* ======================================================================
     C. 基础 UI
     ====================================================================== */
  var toastTimer = null;
  function toast(text) {
    var el = $(".toast", root);
    if (!el) {
      el = document.createElement("div");
      el.className = "toast hidden";
      root.appendChild(el);
    }
    el.textContent = text;
    el.classList.remove("hidden");
    clearTimeout(toastTimer);
    toastTimer = setTimeout(function () { el.classList.add("hidden"); }, 1900);
  }
  function vibrate(ms) {
    try { if (navigator.vibrate) navigator.vibrate(ms || 8); } catch (e) { /* noop */ }
  }

  /* ======================================================================
     D. 时间轴渲染
     ====================================================================== */
  var tlScroll = $("#tl-scroll");
  var tlInner = $("#tl-inner");
  var rulerInner = $("#ruler-inner");
  var ruler = $("#ruler");
  var playhead = $("#playhead");
  var snapLine = $("#snap-line");

  function ctlWidth() { return S.collapsed ? 0 : 54; }
  function screenW() { return root.clientWidth || 390; }
  /* 播放头固定在屏幕水平居中（§7.3）：行进方向上的 t=0 也落在播放头处 */
  function lead() { return Math.max(20, screenW() / 2 - ctlWidth()); }
  function laneWidth() { return lead() + S.duration * S.pps + Math.max(120, lead()); }

  function timeToScreenX(t) {
    // 相对 tl-scroll 容器左侧（含控制列）
    return ctlWidth() + lead() + t * S.pps - S.scrollLeft;
  }
  function screenXToTime(x) {
    // x 为相对 .ed-timeline 左边缘的坐标
    return (x - ctlWidth() - lead() + S.scrollLeft) / S.pps;
  }
  function rulerXToTime(x) {
    // x 为相对刻度尺左边缘的坐标
    return (x - lead() + S.scrollLeft) / S.pps;
  }
  function playTime() { return Math.max(0, S.scrollLeft / S.pps); }

  function renderTracks() {
    var html = "";
    S.tracks.forEach(function (t) {
      var rowCls = "track-row" + (t.hidden ? " hidden-track" : "") + (t.locked ? " locked" : "");
      html += '<div class="' + rowCls + '" data-track="' + t.id + '" style="--h:' + t.h + 'px">';
      /* 控制列 */
      html += '<div class="ctl-cell"><div class="ctl-grid">';
      html += ctlBtn(t, "eye", t.hidden ? "eye-off" : "eye", t.hidden ? "off" : "", "显示/隐藏轨道");
      html += ctlBtn(t, "lock", t.locked ? "lock" : "unlock", t.locked ? "on" : "", "锁定/解锁轨道");
      if (t.type !== "sub") {
        html += ctlBtn(t, "mute", t.muted ? "volume-off" : "volume", t.muted ? "" : "", "静音轨道");
      } else {
        html += '<span class="ctl-btn"></span>';
      }
      html += '<span class="ctl-btn ctl-type" title="' + TRACK_NAME[t.type] + '">' +
        '<svg class="icn"><use href="#i-' + TRACK_ICON[t.type] + '"/></svg></span>';
      html += "</div></div>";

      /* 内容区 */
      html += '<div class="lane" data-lane="' + t.id + '" style="width:' + laneWidth() + 'px">';
      html += '<div class="lane-inner">';
      t.clips.forEach(function (c) { html += clipHTML(t, c); });
      html += "</div></div>";
      html += '<div class="track-edge" data-resize="' + t.id + '"></div>';
      html += "</div>";
    });
    tlInner.innerHTML = html;
    syncClipSelection();
    updateFeather();
  }

  function ctlBtn(t, action, icon, cls, title) {
    return '<button class="ctl-btn ' + cls + '" data-ctl="' + action + '" data-track="' + t.id +
      '" title="' + title + '" aria-label="' + title + '"><svg class="icn"><use href="#i-' + icon + '"/></svg></button>';
  }

  function clipHTML(t, c) {
    var w = Math.max(10, c.dur * S.pps - 4);
    var x = lead() + c.start * S.pps + 2;
    var cls = "clip " + c.kind + " tone-" + toneOf(c);
    if (S.selected && S.selected.clipId === c.id) cls += " selected";
    if (S.multi.indexOf(c.id) >= 0) cls += " selected";
    var inner = "";
    if (c.kind === "video") {
      var n = clamp(Math.round(w / 26), 1, 14);
      for (var i = 0; i < n; i++) inner += '<div class="thumb"></div>';
      if (w > 46) inner += '<span class="clip-name">' + esc(c.label) + "</span>";
    } else if (c.kind === "audio") {
      inner += '<span class="clip-name">' + esc(c.label) + "</span>";
      inner += '<svg class="wave" viewBox="0 0 200 40" preserveAspectRatio="none">' + wavePath(c.id) + "</svg>";
    } else {
      inner += '<div class="sub-text">' + esc(c.label) + "</div>";
    }
    return '<div class="' + cls + '" data-clip="' + c.id + '" style="left:' + x + "px;width:" + w + 'px">' + inner + "</div>";
  }

  function esc(s) {
    return String(s).replace(/[&<>"]/g, function (m) {
      return { "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[m];
    });
  }

  /* 波形由 clip id 派生，保证同一片段稳定不变 */
  function wavePath(seed) {
    var h = 0;
    for (var i = 0; i < seed.length; i++) h = (h * 31 + seed.charCodeAt(i)) % 9973;
    var rnd = function () { h = (h * 16807 + 7) % 9973; return h / 9973; };
    var out = "";
    for (var x = 0; x <= 200; x += 2.1) {
      var env = 0.34 + 0.66 * Math.abs(Math.sin(x / 23) * Math.sin(x / 57 + 1.1));
      var a = (0.12 + rnd() * 0.88) * env * 16 + 0.9;
      out += '<rect x="' + x.toFixed(1) + '" y="' + (20 - a).toFixed(1) +
        '" width="1.1" height="' + (a * 2).toFixed(1) + '"/>';
    }
    return out;
  }

  /* 刻度尺 */
  var TICK_STEPS = [0.1, 0.2, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600];
  function tickStep() {
    for (var i = 0; i < TICK_STEPS.length; i++) {
      if (TICK_STEPS[i] * S.pps >= 50) return TICK_STEPS[i];
    }
    return 600;
  }
  function fmtTime(sec, step) {
    var s = Math.max(0, sec);
    var m = Math.floor(s / 60);
    var rest = s - m * 60;
    var sub = step != null && step < 1;
    var rs = sub ? rest.toFixed(1) : String(Math.round(rest));
    if (rs === "60") { m += 1; rs = sub ? "0.0" : "0"; }
    if (!sub && rest < 10) rs = "0" + rs;
    if (sub && rest < 10) rs = "0" + rs;
    return m + ":" + rs;
  }
  function renderRuler() {
    if (!rulerInner) return;
    var width = ruler.clientWidth;
    var html = "";
    var step = tickStep();
    var minor = step / 5;
    var t0 = Math.floor(rulerXToTime(0) / minor) * minor;
    var t1 = rulerXToTime(width);
    for (var t = Math.max(0, t0); t <= t1 + minor; t += minor) {
      var x = lead() + t * S.pps - S.scrollLeft;
      if (x < -2 || x > width + 2) continue;
      var isMajor = Math.abs(t / step - Math.round(t / step)) < 1e-6;
      html += '<i class="ruler-tick ' + (isMajor ? "major" : "short") + '" style="left:' + x.toFixed(1) + 'px"></i>';
      if (isMajor) {
        html += '<span class="ruler-label" style="left:' + x.toFixed(1) + 'px">' + fmtTime(t, step) + "</span>";
      }
    }
    rulerInner.innerHTML = html;
  }

  function updateFeather() {
    var f = $("#feather");
    if (f) f.style.left = ctlWidth() + "px";
  }

  function syncClipSelection() {
    $$(".clip", tlInner).forEach(function (el) {
      var id = el.dataset.clip;
      var on = (S.selected && S.selected.clipId === id) || S.multi.indexOf(id) >= 0;
      el.classList.toggle("selected", !!on);
    });
  }

  function syncPlayhead() {
    playhead.style.left = "calc(" + (screenW() / 2) + "px - 0.5px)";
  }

  /* ======================================================================
     E. 滚动 / 缩放 / 刻度尺交互
     ====================================================================== */
  var maxScroll = function () {
    return Math.max(0, tlInner.scrollWidth - tlScroll.clientWidth);
  };
  function clampScroll() {
    tlScroll.scrollLeft = clamp(tlScroll.scrollLeft, 0, maxScroll());
    S.scrollLeft = tlScroll.scrollLeft;
    renderRuler();
    syncPlayheadTime();
  }
  function scrollToTime(t, smooth) {
    var s = clamp(t * S.pps, 0, maxScroll());
    if (smooth) tlScroll.scrollTo({ left: s, behavior: "smooth" });
    else tlScroll.scrollLeft = s;
    S.scrollLeft = s;
    renderRuler();
  }
  tlScroll.addEventListener("scroll", function () {
    S.scrollLeft = tlScroll.scrollLeft;
    renderRuler();
    syncPlayheadTime();
  }, { passive: true });

  function syncPlayheadTime() {
    S.playT = playTime();
    updatePreviewForTime();
  }

  /* 捏合缩放（双指）+ Ctrl/⌘ 滚轮 */
  var pinch = null;
  tlScroll.addEventListener("pointerdown", function (e) {
    if (e.pointerType !== "touch") return;
    if (!pinch) { pinch = { pts: {}, t0: playTime() }; }
    pinch.pts[e.pointerId] = { x: e.clientX, y: e.clientY };
    var ids = Object.keys(pinch.pts);
    if (ids.length === 2) {
      var a = pinch.pts[ids[0]], b = pinch.pts[ids[1]];
      pinch.dist = Math.hypot(a.x - b.x, a.y - b.y);
      pinch.t0 = playTime();
    }
  });
  tlScroll.addEventListener("pointermove", function (e) {
    if (!pinch || !pinch.pts[e.pointerId]) return;
    pinch.pts[e.pointerId] = { x: e.clientX, y: e.clientY };
    var ids = Object.keys(pinch.pts);
    if (ids.length < 2 || !pinch.dist) return;
    var a = pinch.pts[ids[0]], b = pinch.pts[ids[1]];
    var d = Math.hypot(a.x - b.x, a.y - b.y);
    if (d < 8) return;
    var k = d / pinch.dist;
    setZoom(S.pps * k, pinch.t0);
    pinch.dist = d;
    e.preventDefault();
  }, { passive: false });
  function endPinch(e) {
    if (!pinch) return;
    delete pinch.pts[e.pointerId];
    if (!Object.keys(pinch.pts).length) pinch = null;
  }
  tlScroll.addEventListener("pointerup", endPinch);
  tlScroll.addEventListener("pointercancel", endPinch);

  tlScroll.addEventListener("wheel", function (e) {
    if (!e.ctrlKey && !e.metaKey) return;
    e.preventDefault();
    setZoom(S.pps * (e.deltaY > 0 ? 0.92 : 1.08), playTime());
  }, { passive: false });

  function setZoom(next, anchorT) {
    var pps = clamp(next, 0.12, 520);
    if (Math.abs(pps - S.pps) < 0.001) return;
    S.pps = pps;
    renderTracks();
    renderRuler();
    scrollToTime(anchorT);
    $$(".ruler-scale").forEach(function (el) { el.textContent = pps.toFixed(1) + " px/s"; });
  }

  /* 刻度尺点按 / 拖动定位（§7.3） */
  var rulerDown = false;
  ruler.addEventListener("pointerdown", function (e) {
    rulerDown = true;
    cap(ruler, e.pointerId);
    var rect = ruler.getBoundingClientRect();
    seekTo(rulerXToTime(e.clientX - rect.left));
  });
  ruler.addEventListener("pointermove", function (e) {
    if (!rulerDown) return;
    var rect = ruler.getBoundingClientRect();
    seekTo(rulerXToTime(e.clientX - rect.left));
  });
  ruler.addEventListener("pointerup", function () { rulerDown = false; });
  ruler.addEventListener("pointercancel", function () { rulerDown = false; });

  function seekTo(t) {
    scrollToTime(clamp(t, 0, S.duration));
    syncPlayheadTime();
  }

  /* ======================================================================
     F. 片段交互：选中 / 长按菜单 / 拖动 / 吸附 / 框选
     ====================================================================== */
  var drag = null;
  var pressTimer = null;
  var pressInfo = null;
  var marquee = null;

  tlInner.addEventListener("pointerdown", function (e) {
    var clipEl = e.target.closest(".clip");
    var laneEl = e.target.closest(".lane");
    var ctlEl = e.target.closest("[data-ctl]");
    var resizeEl = e.target.closest("[data-resize]");

    if (ctlEl) { toggleCtl(ctlEl); return; }
    if (resizeEl) { startResize(e, resizeEl.dataset.resize); return; }

    if (clipEl && laneEl) {
      var track = trackById(laneEl.dataset.lane);
      if (!track) return;
      if (track.locked) { toast("轨道已锁定"); return; }
      var clip = track.clips.filter(function (c) { return c.id === clipEl.dataset.clip; })[0];
      if (!clip) return;

      selectClip(track, clip, e.shiftKey);
      pressInfo = { x: e.clientX, y: e.clientY, track: track, clip: clip, el: clipEl };
      clearTimeout(pressTimer);
      pressTimer = setTimeout(function () {
        if (!pressInfo) return;
        pressInfo = null;
        openClipSheet(track, clip);
      }, 480);

      drag = {
        mode: "clip", pointerId: e.pointerId, x0: e.clientX, y0: e.clientY,
        clip: clip, track: track, start0: clip.start, moved: false, el: clipEl
      };
      cap(clipEl, e.pointerId);
      return;
    }

    if (laneEl && !clipEl) {
      var t = trackById(laneEl.dataset.lane);
      if (!t || t.locked) return;
      // 长按空白 → 框选（§8.3.4）
      var startX = e.clientX, startY = e.clientY;
      pressTimer = setTimeout(function () {
        startMarquee(e, laneEl);
      }, 320);
      pressInfo = { x: startX, y: startY, blank: true };
      drag = { mode: "blank", x0: startX, y0: startY, blank: true };
      return;
    }
  });

  tlInner.addEventListener("pointermove", function (e) {
    if (pressInfo && (Math.abs(e.clientX - pressInfo.x) > 6 || Math.abs(e.clientY - pressInfo.y) > 6)) {
      clearTimeout(pressTimer);
      pressInfo = null;
    }
    if (marquee) { moveMarquee(e); return; }
    if (!drag || drag.mode !== "clip" || e.pointerId !== drag.pointerId) return;
    var dx = e.clientX - drag.x0;
    if (!drag.moved && Math.abs(dx) < 4) return;
    drag.moved = true;
    drag.el.classList.add("dragging");
    var raw = drag.start0 + dx / S.pps;
    var snap = findSnap(drag.clip, clamp(raw, 0, S.duration - drag.clip.dur));
    drag.clip.start = clamp(snap.t, 0, Math.max(0, S.duration - drag.clip.dur));
    drag.el.style.left = (lead() + drag.clip.start * S.pps + 1) + "px";
    if (snap.line != null) {
      snapLine.classList.add("on");
      snapLine.style.left = timeToScreenX(snap.line) + "px";
      if (!drag.snapped) { vibrate(8); drag.snapped = true; }
    } else {
      snapLine.classList.remove("on");
      drag.snapped = false;
    }
  });

  function endDrag(e) {
    clearTimeout(pressTimer);
    pressInfo = null;
    if (marquee) { endMarquee(); }
    if (!drag) return;
    if (drag.mode === "clip" && drag.moved) {
      drag.clip.start = Math.round(drag.clip.start * 100) / 100;
      sortClips(drag.track);
      // 拖动属于编辑操作：以拖动前的起点判断是否发生位移
      if (Math.abs(drag.clip.start - drag.start0) > 0.001) {
        pushHistoryWith(drag.start0);
        afterModelChange(true);
      }
    }
    if (drag.el) drag.el.classList.remove("dragging");
    snapLine.classList.remove("on");
    drag = null;
  }
  tlInner.addEventListener("pointerup", endDrag);
  tlInner.addEventListener("pointercancel", endDrag);

  /* 拖拽前的历史：因为模型已被修改，这里用“起点”还原一份快照入栈 */
  function pushHistoryWith(start0) {
    var copy = JSON.parse(JSON.stringify(S.tracks));
    copy.forEach(function (t) {
      t.clips.forEach(function (c) {
        if (c.id === (drag && drag.clip.id)) c.start = start0;
      });
    });
    S.history.push(JSON.stringify(copy));
    if (S.history.length > 20) S.history.shift();
    S.redo.length = 0;
    syncHistoryBtns();
  }

  function findSnap(clip, start) {
    var target = 8 / S.pps;
    var candidates = [playTime(), 0];
    allClips().forEach(function (x) {
      if (x.c.id === clip.id) return;
      candidates.push(x.c.start, clipEnd(x.c));
    });
    S.tracks.forEach(function (t) {
      if (t.locked) return;
    });
    var best = null;
    candidates.forEach(function (c) {
      var d = Math.abs(c - start);
      if (d <= target && (!best || d < best.d)) best = { d: d, line: c, t: c };
      // 吸附片段右边缘到其他片段起点
      var d2 = Math.abs(c - (start + clip.dur));
      if (d2 <= target && (!best || d2 < best.d)) best = { d: d2, line: c, t: c - clip.dur };
    });
    return best || { t: start, line: null };
  }

  function selectClip(track, clip, additive) {
    if (additive) {
      var i = S.multi.indexOf(clip.id);
      if (i >= 0) S.multi.splice(i, 1);
      else S.multi.push(clip.id);
    } else {
      S.multi = [];
      S.selected = { trackId: track.id, clipId: clip.id };
      // §8.3.4：单选后自动切到“剪辑”二级（若已停留在二级则保持当前分类）
      if (!activeCat) setCategory("edit");
    }
    syncClipSelection();
    syncBatchBar();
    syncPanelAvailability();
    updatePreviewForClip(clip);
  }

  function trackById(id) {
    return S.tracks.filter(function (t) { return t.id === id; })[0] || null;
  }

  function toggleCtl(btn) {
    var t = trackById(btn.dataset.track);
    if (!t) return;
    var act = btn.dataset.ctl;
    pushHistory();
    if (act === "eye") { t.hidden = !t.hidden; toast(t.hidden ? "轨道已隐藏（仍参与导出）" : "轨道已显示"); }
    if (act === "lock") { t.locked = !t.locked; toast(t.locked ? "轨道已锁定" : "轨道已解锁"); }
    if (act === "mute") { t.muted = !t.muted; toast(t.muted ? "轨道已静音" : "轨道已取消静音"); }
    if (t.hidden && S.selected && S.selected.trackId === t.id) S.selected = null;
    afterModelChange(true);
  }

  /* 轨道高度拖拽（§8.3.5） */
  var resizeState = null;
  function startResize(e, trackId) {
    var t = trackById(trackId);
    if (!t) return;
    resizeState = { track: t, y0: e.clientY, h0: t.h, id: trackId };
    cap(e.target, e.pointerId);
    var move = function (ev) {
      if (!resizeState) return;
      var h = clamp(resizeState.h0 + (ev.clientY - resizeState.y0), 30, 96);
      resizeState.track.h = Math.round(h);
      var row = tlInner.querySelector('[data-track="' + resizeState.id + '"]');
      if (row) row.style.setProperty("--h", resizeState.track.h + "px");
    };
    var up = function () {
      resizeState = null;
      document.removeEventListener("pointermove", move);
      document.removeEventListener("pointerup", up);
      renderRuler();
    };
    document.addEventListener("pointermove", move);
    document.addEventListener("pointerup", up);
    e.preventDefault();
  }

  /* 片段长按菜单（§8.3.4） */
  var sheetTarget = null;
  function openClipSheet(track, clip) {
    if (track.locked) return;
    sheetTarget = { t: track, c: clip };
    var sheet = $("#sheet-clip");
    $("#sheet-clip-title").textContent = clip.kind === "sub" ? clip.label : (clip.label + " · " + TRACK_NAME[track.type]);
    sheet.classList.remove("hidden");
    vibrate(12);
  }
  function closeClipSheet() { $("#sheet-clip").classList.add("hidden"); sheetTarget = null; }
  $("#sheet-clip").addEventListener("click", function (e) {
    if (e.target.id === "sheet-clip") return closeClipSheet();
    var item = e.target.closest("[data-act]");
    if (!item) return;
    var act = item.dataset.act;
    closeClipSheet();
    if (!sheetTarget) return;
    if (act === "copy") duplicateClip(sheetTarget.t, sheetTarget.c);
    if (act === "delete") deleteClip(sheetTarget.t, sheetTarget.c);
    if (act === "replace") toast("唤起相册替换素材（进入剪辑页的素材选择流程）");
  });

  function duplicateClip(track, clip) {
    pushHistory();
    var copy = JSON.parse(JSON.stringify(clip));
    copy.id = uid("c");
    copy.start = clipEnd(clip);
    track.clips.push(copy);
    S.selected = { trackId: track.id, clipId: copy.id };
    sortClips(track);
    afterModelChange();
    toast("已复制片段");
  }

  function deleteClip(track, clip) {
    var n = S.multi.length > 1 ? S.multi.length : 1;
    confirmDialog({
      title: n > 1 ? "删除 " + n + " 个片段" : "删除片段",
      desc: "删除后可用撤销恢复，确认继续吗？",
      ok: "删除",
      danger: true,
      onOk: function () {
        pushHistory();
        if (n > 1) {
          S.tracks.forEach(function (t) { t.clips = t.clips.filter(function (c) { return S.multi.indexOf(c.id) < 0; }); });
          S.multi = [];
        } else {
          track.clips = track.clips.filter(function (c) { return c.id !== clip.id; });
        }
        S.selected = null;
        S.multi = [];
        syncBatchBar();
        afterModelChange();
        toast(n > 1 ? "已删除 " + n + " 个片段" : "片段已删除");
      }
    });
  }

  /* 框选 */
  function startMarquee(e, laneEl) {
    pressInfo = null;
    var scrollRect = tlScroll.getBoundingClientRect();
    marquee = document.createElement("div");
    marquee.className = "marquee";
    tlScroll.appendChild(marquee);
    marquee._x0 = e.clientX - scrollRect.left;
    marquee._y0 = e.clientY - scrollRect.top + tlScroll.scrollTop;
    marquee._rect = scrollRect;
    S.multi = [];
    moveMarquee(e);
  }
  function moveMarquee(e) {
    if (!marquee) return;
    var r = marquee._rect;
    var x = e.clientX - r.left, y = e.clientY - r.top + tlScroll.scrollTop;
    var left = Math.min(x, marquee._x0), top = Math.min(y, marquee._y0);
    var w = Math.abs(x - marquee._x0), h = Math.abs(y - marquee._y0);
    marquee.style.left = left + "px";
    marquee.style.top = top + "px";
    marquee.style.width = w + "px";
    marquee.style.height = h + "px";
    marquee._box = { left: left + tlScroll.scrollLeft, top: top, w: w, h: h };
    syncClipSelection();
  }
  function endMarquee() {
    if (!marquee) return;
    var box = marquee._box;
    var rect = marquee._rect;
    if (box && (box.w > 8 || box.h > 8)) {
      var laneTop = 0;
      $$(".track-row", tlInner).forEach(function (row) {
        var h = parseInt(getComputedStyle(row).height, 10);
        var top = row.offsetTop;
        if (laneTop < 0) laneTop = 0;
        if (box.top < top + h && box.top + box.h > top) {
          var track = trackById(row.dataset.track);
          if (track && !track.locked) {
            $$(".clip", row).forEach(function (el) {
              var cx = el.offsetLeft, cw = el.offsetWidth;
              if (cx + cw > box.left && cx < box.left + box.w) {
                if (S.multi.indexOf(el.dataset.clip) < 0) S.multi.push(el.dataset.clip);
              }
            });
          }
        }
      });
      S.selected = null;
    }
    marquee.remove();
    marquee = null;
    syncClipSelection();
    syncBatchBar();
    if (S.multi.length) toast("已框选 " + S.multi.length + " 个片段");
    void rect;
  }

  /* 批量操作条（框选后） */
  function ensureBatchBar() {
    var bar = $(".batch-bar", root);
    if (bar) return bar;
    bar = document.createElement("div");
    bar.className = "batch-bar";
    bar.innerHTML =
      '<span class="bb-count">已选 <b>0</b> 项</span>' +
      '<button class="bb-btn" data-batch="speed">批量变速</button>' +
      '<button class="bb-btn danger" data-batch="delete">批量删除</button>' +
      '<button class="bb-close" data-batch="cancel" aria-label="取消选择">' +
      '<svg class="icn icn-16"><use href="#i-close"/></svg></button>';
    root.appendChild(bar);
    bar.addEventListener("click", function (e) {
      var b = e.target.closest("[data-batch]");
      if (!b) return;
      var act = b.dataset.batch;
      if (act === "cancel") {
        S.multi = [];
        syncClipSelection();
        syncBatchBar();
        return;
      }
      if (act === "speed") {
        openPanel("speed", { batch: true });
        return;
      }
      if (act === "delete") {
        var ids = S.multi.slice();
        confirmDialog({
          title: "删除 " + ids.length + " 个片段",
          desc: "删除后可用撤销恢复，确认继续吗？",
          ok: "删除",
          danger: true,
          onOk: function () {
            pushHistory();
            S.tracks.forEach(function (t) {
              t.clips = t.clips.filter(function (c) { return ids.indexOf(c.id) < 0; });
            });
            S.multi = [];
            syncBatchBar();
            afterModelChange();
            toast("已删除 " + ids.length + " 个片段");
          }
        });
      }
    });
    return bar;
  }
  function syncBatchBar() {
    var bar = ensureBatchBar();
    bar.classList.toggle("on", S.multi.length > 0);
    var n = $(".bb-count b", bar);
    if (n) n.textContent = S.multi.length;
  }

  /* 通用确认弹窗 */
  function confirmDialog(opts) {
    var dlg = $("#dlg-confirm");
    if (!dlg) {
      dlg = document.createElement("div");
      dlg.className = "overlay hidden";
      dlg.id = "dlg-confirm";
      dlg.innerHTML =
        '<div class="dialog"><div class="dg-icon danger"><svg class="icn icn-22"><use href="#i-trash"/></svg></div>' +
        '<div class="dg-title"></div><div class="dg-desc"></div><div class="dg-actions">' +
        '<button class="btn btn-ghost js-cancel">取消</button>' +
        '<button class="btn js-ok">确认</button></div></div>';
      root.appendChild(dlg);
    }
    $(".dg-title", dlg).textContent = opts.title || "确认操作";
    $(".dg-desc", dlg).textContent = opts.desc || "";
    var ok = $(".js-ok", dlg);
    ok.textContent = opts.ok || "确认";
    ok.className = "btn js-ok " + (opts.danger ? "btn-danger" : "btn-primary");
    dlg.classList.remove("hidden");
    var close = function () { dlg.classList.add("hidden"); };
    $(".js-cancel", dlg).onclick = close;
    ok.onclick = function () { close(); if (opts.onOk) opts.onOk(); };
  }

  /* ======================================================================
     G. 轨道增删 / 控制列收起
     ====================================================================== */
  var collapsedStore = "nc.editor.ctlCollapsed";
  var IS_CAPTURE = /[?&]capture=/.test(location.search);
  try {
    S.collapsed = !IS_CAPTURE && localStorage.getItem(collapsedStore) === "1";
  } catch (e) { /* noop */ }
  if (S.collapsed) root.classList.add("collapsed");

  function applyCollapse() {
    root.classList.toggle("collapsed", S.collapsed);
    var btn = $("#collapse-btn");
    var hint = $("#collapse-hint");
    if (btn) btn.querySelector("use").setAttribute("href", S.collapsed ? "#i-chev-up" : "#i-chev-down");
    if (hint) hint.textContent = S.collapsed ? "展开控制列" : "收起控制列";
    try { localStorage.setItem(collapsedStore, S.collapsed ? "1" : "0"); } catch (e) { /* noop */ }
    renderTracks();
    renderRuler();
    syncPlayhead();
    updateFeather();
  }
  $("#collapse-btn").addEventListener("click", function () {
    S.collapsed = !S.collapsed;
    applyCollapse();
  });

  $("#add-track").addEventListener("click", function () {
    $("#sheet-track").classList.remove("hidden");
  });
  $("#sheet-track").addEventListener("click", function (e) {
    if (e.target.id === "sheet-track") return $("#sheet-track").classList.add("hidden");
    var item = e.target.closest("[data-type]");
    if (!item) return;
    var type = item.dataset.type;
    $("#sheet-track").classList.add("hidden");
    if (type === "cancel") return;
    var newCount = S.tracks.length + 1;
    var needTip = newCount >= 15 && newCount % 5 === 0;
    var add = function () {
      pushHistory();
      S.tracks.push({
        id: uid("t-"), type: type, hidden: false, locked: false, muted: false,
        h: type === "sub" ? 38 : type === "audio" ? 40 : 44, clips: [], manual: true
      });
      afterModelChange();
      toast("已添加" + TRACK_NAME[type] + "（空白轨道）");
    };
    if (needTip) {
      $("#perf-title").textContent = "当前项目轨道较多（" + newCount + " 条）";
      $("#perf-desc").textContent = "继续添加可能影响预览流畅度；折叠/隐藏的轨道在编辑时不会参与实时渲染，导出仍按完整质量渲染。";
      $("#dlg-perf").classList.remove("hidden");
      $("#perf-cancel").onclick = function () { $("#dlg-perf").classList.add("hidden"); };
      $("#perf-ok").onclick = function () { $("#dlg-perf").classList.add("hidden"); add(); };
    } else {
      add();
    }
  });

  /* ======================================================================
     H. 播放控制 / 预览同步
     ====================================================================== */
  var playIcon = $("#btn-play use");
  function togglePlay() {
    S.playing = !S.playing;
    if (playIcon) playIcon.setAttribute("href", S.playing ? "#i-pause" : "#i-play");
    $("#btn-play").setAttribute("aria-label", S.playing ? "暂停" : "播放");
    if (S.playing) tick();
  }
  var raf = 0;
  function tick() {
    cancelAnimationFrame(raf);
    var last = performance.now();
    var loop = function (now) {
      if (!S.playing) return;
      var dt = (now - last) / 1000;
      last = now;
      var t = playTime() + dt;
      if (t >= S.duration) {
        seekTo(S.duration);
        S.playing = false;
        if (playIcon) playIcon.setAttribute("href", "#i-play");
        return;
      }
      scrollToTime(t);
      syncPlayheadTime();
      raf = requestAnimationFrame(loop);
    };
    raf = requestAnimationFrame(loop);
  }
  $("#btn-play").addEventListener("click", togglePlay);
  $("#btn-cut").addEventListener("click", function () { doSplit(); });
  $("#btn-del").addEventListener("click", function () {
    var x = selectedEntry();
    if (!x) { toast("请先选中一个片段"); return; }
    deleteClip(x.t, x.c);
  });

  /* 预览区画面内容随播放头所在片段变化（字幕 / 曲线变速示意） */
  function updatePreviewForTime() {
    var t = S.playT;
    var subs = [];
    S.tracks.forEach(function (tr) { if (tr.type === "sub" && !tr.hidden) subs = subs.concat(tr.clips); });
    var cur = subs.filter(function (c) { return t >= c.start && t <= clipEnd(c); })[0];
    var subEl = $("#p-sub");
    if (subEl) {
      subEl.textContent = cur ? cur.label : "";
      subEl.style.opacity = cur ? "1" : "0";
    }

    var vids = [];
    S.tracks.forEach(function (tr) { if (tr.type === "video" && !tr.hidden) vids = vids.concat(tr.clips); });
    var v = vids.filter(function (c) { return t >= c.start && t <= clipEnd(c); })[0];
    var mini = $("#curve-mini");
    if (mini) {
      var on = !!(v && v.params && v.params.curve && v.params.curve !== "自定义" &&
        v.params.curve !== "线性");
      mini.classList.toggle("off", !on);
      var cap = $(".cm-cap", mini);
      if (on && cap) cap.textContent = "曲线变速 · " + v.params.curve;
    }

    // 视频轨隐藏 → 预览画面变暗提示不渲染
    var videoHidden = S.tracks.filter(function (x) { return x.type === "video"; }).every(function (x) { return x.hidden; });
    var canvas = $("#ed-canvas");
    if (canvas) canvas.style.opacity = videoHidden ? "0.12" : "1";
  }

  /* 选中片段 → 预览应用其参数（不透明度 / 调节 / 滤镜 / 背景 / 蒙版） */
  function updatePreviewForClip(clip) {
    var canvas = $("#ed-canvas");
    if (!canvas || !clip) return;
    var p = clip.params || {};
    canvas.style.opacity = p.opacity != null ? String(p.opacity / 100) : "1";
    var f = [];
    if (p.brightness) f.push("brightness(" + (1 + p.brightness / 130) + ")");
    if (p.contrast) f.push("contrast(" + (1 + p.contrast / 140) + ")");
    if (p.saturation) f.push("saturate(" + (1 + p.saturation / 100) + ")");
    if (p.filter && p.filter !== "无") {
      f.push(p.filter === "黑白" ? "grayscale(1)" : "sepia(0.45) saturate(1.15) contrast(1.06)");
    }
    canvas.style.filter = f.join(" ") || "";
    if (p.bg && p.bg.color) canvas.style.backgroundColor = p.bg.color;
  }

  /* ======================================================================
     I. 工具栏 §11
     ====================================================================== */
  var CATS = [
    { id: "edit", name: "剪辑", icon: "scissors" },
    { id: "audio", name: "音频", icon: "music" },
    { id: "text", name: "文字", icon: "text" },
    { id: "pip", name: "画中画", icon: "pip" },
    { id: "fx", name: "特效", icon: "sparkles" },
    { id: "trans", name: "转场", icon: "transition" },
    { id: "filter", name: "滤镜", icon: "filter" },
    { id: "adjust", name: "调节", icon: "sliders" },
    { id: "ratio", name: "比例", icon: "aspect" },
    { id: "bg", name: "背景", icon: "image" },
    { id: "mask", name: "蒙版", icon: "mask" },
    { id: "ai", name: "AI", icon: "ai" }
  ];

  var activeCat = null;
  var activeItem = null;
  var toolbar = $("#toolbar");

  function renderToolbar() {
    if (!toolbar) return;
    var html = "";
    if (!activeCat) {
      html += CATS.map(function (c) {
        return '<button class="tb-item" data-cat="' + c.id + '">' +
          '<svg class="icn"><use href="#i-' + c.icon + '"/></svg><span>' + c.name + "</span></button>";
      }).join("");
    } else {
      var cat = CATS.filter(function (c) { return c.id === activeCat; })[0];
      html += '<button class="tb-back" data-back="1" aria-label="返回一级工具栏">' +
        '<svg class="icn"><use href="#i-chev-left"/></svg></button>' +
        '<span class="tb-title">' + (cat ? cat.name : "") + "</span>";
      var items = TOOLS[activeCat] || [];
      if (!items.length) {
        html += '<div class="tb-empty">该分类暂无二级功能</div>';
      } else {
        html += items.map(function (it) {
          return '<button class="tb-item sub" data-item="' + it.id + '">' +
            '<svg class="icn"><use href="#i-' + it.icon + '"/></svg><span>' + it.name + "</span></button>";
        }).join("");
      }
    }
    toolbar.innerHTML = html;
    toolbar.scrollLeft = 0;
    syncToolbarState();
  }

  if (toolbar) {
    toolbar.addEventListener("click", function (e) {
      var back = e.target.closest("[data-back]");
      if (back) { setCategory(null); return; }
      var catBtn = e.target.closest("[data-cat]");
      if (catBtn) {
        var id = catBtn.dataset.cat;
        setCategory(activeCat === id ? null : id);
        return;
      }
      var itemBtn = e.target.closest("[data-item]");
      if (!itemBtn) return;
      var item = (TOOLS[activeCat] || []).filter(function (x) { return x.id === itemBtn.dataset.item; })[0];
      if (!item) return;
      runTool(item, itemBtn);
    });
  }

  function setCategory(id) {
    if (id !== activeCat) closePanel();
    activeCat = id;
    activeItem = null;
    renderToolbar();
  }

  /* 面板可用性同步：轨道锁定 / 片段丢失时关闭面板，避免对无效目标调参 */
  function syncPanelAvailability() {
    if (!panelState) return;
    var x = selectedEntry();
    if (!x && panelState.opts && panelState.opts.item && panelState.opts.item.need === "clip") {
      closePanel();
      return;
    }
    if (x && x.t.locked) closePanel();
  }

  function syncToolbarState() {
    $$("#toolbar .tb-item[data-item]").forEach(function (b) {
      var item = (TOOLS[activeCat] || []).filter(function (x) { return x.id === b.dataset.item; })[0];
      if (!item) return;
      var dis = isToolDisabled(item);
      b.classList.toggle("off", dis);
      b.setAttribute("aria-disabled", dis ? "true" : "false");
      b.classList.toggle("on", !dis && activeItem === item.id);
    });
  }

  function isToolDisabled(item) {
    if (item.need === "clip") return !selectedEntry();
    if (item.need === "subs") {
      return !allClips().some(function (x) { return x.c.kind === "sub"; });
    }
    if (item.need === "adjacent") {
      var hit = selectedEntry();
      if (!hit) return true;
      var t = hit.t, c = hit.c;
      var left = t.clips.some(function (x) { return Math.abs(clipEnd(x) - c.start) < 0.02 && x.id !== c.id; });
      var right = t.clips.some(function (x) { return Math.abs(x.start - clipEnd(c)) < 0.02 && x.id !== c.id; });
      return !(left && right);
    }
    return false;
  }

  function runTool(item, btn) {
    if (isToolDisabled(item)) {
      if (item.need === "clip") toast("请先选中一个片段");
      else if (item.need === "subs") toast("轨道上还没有字幕内容");
      else if (item.need === "adjacent") toast("需要两侧都有相邻片段才能添加转场");
      return;
    }
    vibrate(6);
    if (item.type === "direct") { item.run(); return; }
    if (item.type === "page") { openPage(item.page); return; }
    activeItem = item.id;
    openPanel(item.panel, { item: item });
    syncToolbarState();
    void btn;
  }

  /* ======================================================================
     J. 二级功能清单（名称与类型严格按文档 §12–§22）
     ====================================================================== */
  var TOOLS = {
    edit: [
      { id: "split", name: "分割", icon: "scissors", type: "direct", need: "clip", run: doSplit },
      { id: "speed", name: "变速", icon: "gauge", type: "panel", need: "clip", panel: "speed" },
      { id: "volume", name: "音量", icon: "volume", type: "panel", need: "clip", panel: "volume" },
      { id: "delete", name: "删除", icon: "trash", type: "direct", need: "clip", run: function () {
        var x = selectedEntry(); if (x) deleteClip(x.t, x.c);
      } },
      { id: "copy", name: "复制", icon: "duplicate", type: "direct", need: "clip", run: function () {
        var x = selectedEntry(); if (x) duplicateClip(x.t, x.c);
      } },
      { id: "reverse", name: "倒放", icon: "refresh", type: "direct", need: "clip", run: doReverse },
      { id: "freeze", name: "定格", icon: "freeze", type: "direct", need: "clip", run: doFreeze },
      { id: "denoise", name: "降噪", icon: "denoise", type: "panel", need: "clip", panel: "denoise" },
      { id: "opacity", name: "不透明度", icon: "opacity", type: "panel", need: "clip", panel: "opacity" },
      { id: "crop", name: "裁剪", icon: "crop", type: "page", need: "clip", page: "crop" },
      { id: "cutout", name: "智能抠像", icon: "cutout", type: "panel", need: "clip", panel: "cutout" },
      { id: "interp", name: "光流补帧", icon: "frames", type: "panel", need: "clip", panel: "interp" }
    ],
    audio: [
      { id: "music", name: "音乐", icon: "music", type: "page", page: "music" },
      { id: "sfx", name: "音效", icon: "sparkles", type: "panel", panel: "sfx" },
      { id: "extract", name: "提取音乐", icon: "film", type: "direct", run: function () {
        toast("唤起相册选择视频，提取音轨后插入音频轨");
      } },
      { id: "record", name: "录音", icon: "mic", type: "page", page: "record" },
      { id: "fade", name: "淡化", icon: "fade", type: "panel", panel: "fade" },
      { id: "stem", name: "人声分离", icon: "stem", type: "panel", need: "clip", panel: "stem" }
    ],
    text: [
      { id: "newtext", name: "新建文本", icon: "text", type: "page", page: "text" },
      { id: "subs", name: "识别字幕", icon: "subtitle", type: "panel", panel: "asr" },
      { id: "lyrics", name: "识别歌词", icon: "music", type: "panel", panel: "lyrics" },
      { id: "track", name: "镜头追踪", icon: "target", type: "panel", need: "clip", panel: "track" },
      { id: "translate", name: "字幕翻译", icon: "translate", type: "panel", need: "subs", panel: "translate" },
      { id: "tts", name: "文本转语音", icon: "tts", type: "panel", panel: "tts" }
    ],
    pip: [],
    fx: [
      { id: "fx-screen", name: "画面特效", icon: "sparkles", type: "panel", need: "clip", panel: "fx-screen" },
      { id: "fx-person", name: "人物特效", icon: "user", type: "panel", need: "clip", panel: "fx-person" },
      { id: "fx-air", name: "氛围特效", icon: "atmo", type: "panel", need: "clip", panel: "fx-air" },
      { id: "fx-motion", name: "动感特效", icon: "motion", type: "panel", need: "clip", panel: "fx-motion" }
    ],
    trans: [
      { id: "transition", name: "转场", icon: "transition", type: "panel", need: "adjacent", panel: "transition" }
    ],
    filter: [
      { id: "filter", name: "滤镜", icon: "filter", type: "panel", need: "clip", panel: "filter" }
    ],
    adjust: [
      { id: "adjust", name: "调节", icon: "sliders", type: "panel", need: "clip", panel: "adjust" }
    ],
    ratio: [
      { id: "ratio", name: "比例", icon: "aspect", type: "panel", panel: "ratio" }
    ],
    bg: [
      { id: "bg", name: "背景", icon: "image", type: "panel", need: "clip", panel: "bg" }
    ],
    mask: [
      { id: "mask", name: "蒙版", icon: "mask", type: "panel", need: "clip", panel: "mask" }
    ],
    ai: [
      { id: "refcut", name: "参考仿剪", icon: "wand", type: "page", page: "refcut" },
      { id: "depth", name: "深度图", icon: "depth", type: "panel", need: "clip", panel: "depth" }
    ]
  };

  /* ── 直接执行类实现（真实作用于时间轴） ─────────────────────────────── */
  function doSplit() {
    var x = selectedEntry();
    if (!x) return;
    var t = x.t, c = x.c;
    var cut = playTime();
    if (cut <= c.start + 0.05 || cut >= clipEnd(c) - 0.05) {
      toast("请把播放头移到片段内部再分割");
      return;
    }
    pushHistory();
    var right = JSON.parse(JSON.stringify(c));
    right.id = uid("c");
    right.start = cut;
    right.dur = clipEnd(c) - cut;
    c.dur = cut - c.start;
    t.clips.push(right);
    sortClips(t);
    S.selected = { trackId: t.id, clipId: right.id };
    afterModelChange();
    toast("已在播放头处分割");
  }

  function doReverse() {
    var x = selectedEntry();
    if (!x) return;
    pushHistory();
    x.c.reversed = !x.c.reversed;
    afterModelChange();
    toast(x.c.reversed ? "倒放处理中（原型：已标记倒放）" : "已取消倒放");
  }

  function doFreeze() {
    var x = selectedEntry();
    if (!x) return;
    pushHistory();
    var at = clamp(playTime(), x.c.start, clipEnd(x.c));
    var f = {
      id: uid("c"), start: at, dur: 2, label: "定格帧", kind: x.t.type === "sub" ? "sub" : "video"
    };
    x.t.clips.push(f);
    sortClips(x.t);
    S.selected = { trackId: x.t.id, clipId: f.id };
    afterModelChange();
    toast("已生成 2 秒定格片段，可拖动边缘调整时长");
  }

  /* ======================================================================
     K. 三级面板（§10 通用外壳 + Part 4 参数规格）
     ====================================================================== */
  var panelEl = $("#panel");
  var panelState = null;

  function openPanel(key, opts) {
    var spec = PANELS[key];
    if (!spec) { toast("该功能面板待接入"); return; }
    var built = spec(opts || {});
    panelState = { key: key, spec: built, values: {}, opts: opts || {} };
    renderPanel();
    root.classList.add("panel-open");
    root.classList.remove("panel-collapsed", "panel-expanded");
    panelEl.style.height = "";
    setTimeout(function () { window.dispatchEvent(new Event("resize")); }, 240);
  }

  function closePanel() {
    root.classList.remove("panel-open", "panel-collapsed", "panel-expanded");
    panelEl.style.height = "";
    panelEl.innerHTML = "";
    panelState = null;
    activeItem = null;
    syncToolbarState();
    setTimeout(function () { renderTracks(); renderRuler(); }, 40);
  }

  function renderPanel() {
    var st = panelState;
    if (!st) return;
    var spec = st.spec;
    var html = '<div class="panel-grip" id="panel-grip"><span></span></div>' +
      '<div class="panel-head"><div class="panel-title">' + esc(spec.title) + "</div>" +
      '<button class="panel-done" id="panel-done">完成</button></div>' +
      '<div class="panel-body" id="panel-body"></div>';
    panelEl.innerHTML = html;
    var body = $("#panel-body");
    body.innerHTML = "";

    /* 顶部 Tab（§10.5：固定在内容区顶部） */
    if (spec.tabs && spec.tabs.length) {
      st.tab = spec.tabs[0].id;
      var tabs = document.createElement("div");
      tabs.className = "seg-tabs";
      tabs.style.marginBottom = "12px";
      tabs.innerHTML = spec.tabs.map(function (t, i) {
        return '<button data-tab="' + t.id + '" class="' + (i === 0 ? "on" : "") + '">' + esc(t.name) + "</button>";
      }).join("");
      tabs.addEventListener("click", function (e) {
        var b = e.target.closest("[data-tab]");
        if (!b) return;
        st.tab = b.dataset.tab;
        $$("button", tabs).forEach(function (x) { x.classList.toggle("on", x === b); });
        renderBlocks();
      });
      body.appendChild(tabs);
    }

    var holder = document.createElement("div");
    holder.id = "panel-blocks";
    body.appendChild(holder);
    st.holder = holder;
    renderBlocks();

    $("#panel-done").addEventListener("click", function () {
      if (st.spec.onDone) st.spec.onDone(st.values);
      closePanel();
    });
    bindGrip();
  }

  function currentBlocks() {
    var st = panelState;
    if (!st) return [];
    if (st.spec.tabs && st.spec.tabs.length) {
      var t = st.spec.tabs.filter(function (x) { return x.id === st.tab; })[0];
      return t ? t.blocks : [];
    }
    return st.spec.blocks || [];
  }

  function renderBlocks() {
    var st = panelState;
    if (!st) return;
    var holder = st.holder;
    holder.innerHTML = "";
    currentBlocks().forEach(function (b) {
      holder.appendChild(renderBlock(b, st));
    });
    if (st.spec.note) {
      var n = document.createElement("div");
      n.className = "panel-tip";
      n.textContent = st.spec.note;
      holder.appendChild(n);
    }
    if (st.spec.onChange) st.spec.onChange(st.values, st);
  }

  function renderBlock(b, st) {
    var el = document.createElement("div");
    if (b.t === "slider") {
      var v = st.values[b.id] != null ? st.values[b.id] : b.def;
      st.values[b.id] = v;
      el.className = "param";
      el.innerHTML =
        '<div class="param-head"><span class="param-name">' + esc(b.name) + "</span>" +
        '<input class="param-input" type="text" inputmode="decimal" value="' + v + '">' +
        (b.unit ? '<span class="param-unit">' + esc(b.unit) + "</span>" : "") +
        '<button class="param-reset" title="恢复默认值"><svg class="icn"><use href="#i-reset"/></svg></button></div>' +
        '<div class="slider"><div class="rail"></div><div class="fill"></div><div class="knob"></div></div>';
      var input = $(".param-input", el);
      var fill = $(".fill", el), knob = $(".knob", el), slider = $(".slider", el);
      var setV = function (next, fromInput) {
        next = clamp(Number(next), b.min, b.max);
        if (isNaN(next)) next = b.def;
        next = Math.round(next * 100) / 100;
        st.values[b.id] = next;
        var pct = (next - b.min) / (b.max - b.min) * 100;
        fill.style.width = pct + "%";
        knob.style.left = pct + "%";
        if (!fromInput) input.value = next;
        if (st.spec.onChange) st.spec.onChange(st.values, st);
      };
      setV(v, true);
      var dragging = false;
      var fromEvent = function (ev) {
        var r = slider.getBoundingClientRect();
        var pct = clamp((ev.clientX - r.left) / r.width, 0, 1);
        setV(b.min + pct * (b.max - b.min));
      };
      slider.addEventListener("pointerdown", function (ev) {
        dragging = true;
        cap(slider, ev.pointerId);
        fromEvent(ev);
      });
      slider.addEventListener("pointermove", function (ev) { if (dragging) fromEvent(ev); });
      slider.addEventListener("pointerup", function () { dragging = false; });
      slider.addEventListener("pointercancel", function () { dragging = false; });
      input.addEventListener("change", function () { setV(input.value); });
      input.addEventListener("blur", function () { setV(input.value); });
      $(".param-reset", el).addEventListener("click", function () {
        setV(b.def);
        toast(b.name + "已重置");
      });
    } else if (b.t === "check") {
      var on = st.values[b.id] != null ? st.values[b.id] : !!b.def;
      st.values[b.id] = on;
      el.className = "check-row" + (on ? " on" : "");
      el.innerHTML = '<span class="check-box"><svg class="icn"><use href="#i-check"/></svg></span><span>' + esc(b.name) + "</span>";
      el.addEventListener("click", function () {
        on = !on;
        st.values[b.id] = on;
        el.classList.toggle("on", on);
        if (st.spec.onChange) st.spec.onChange(st.values, st);
      });
    } else if (b.t === "radio") {
      var val = st.values[b.id] != null ? st.values[b.id] : b.def;
      st.values[b.id] = val;
      el.className = "radio-group";
      el.innerHTML = '<div class="rg-label">' + esc(b.name) + "</div>" + b.options.map(function (o) {
        return '<button class="radio-item ' + (o === val ? "on" : "") + '" data-v="' + esc(o) + '">' +
          '<span class="radio-dot"></span><span>' + esc(o) + "</span></button>";
      }).join("");
      el.addEventListener("click", function (e) {
        var t = e.target.closest("[data-v]");
        if (!t) return;
        st.values[b.id] = t.dataset.v;
        $$(".radio-item", el).forEach(function (x) { x.classList.toggle("on", x === t); });
        if (st.spec.onChange) st.spec.onChange(st.values, st);
      });
    } else if (b.t === "seg") {
      var sv = st.values[b.id] != null ? st.values[b.id] : b.def;
      st.values[b.id] = sv;
      el.className = "param";
      el.style.marginTop = "14px";
      el.innerHTML = '<div class="param-head"><span class="param-name">' + esc(b.name) + "</span></div>" +
        '<div class="seg-tabs" style="margin-top:8px">' + b.options.map(function (o) {
          return '<button data-v="' + esc(o) + '" class="' + (o === sv ? "on" : "") + '">' + esc(o) + "</button>";
        }).join("") + "</div>";
      el.addEventListener("click", function (e) {
        var t = e.target.closest("[data-v]");
        if (!t) return;
        st.values[b.id] = t.dataset.v;
        $$("[data-v]", el).forEach(function (x) { x.classList.toggle("on", x === t); });
        if (st.spec.onChange) st.spec.onChange(st.values, st);
      });
    } else if (b.t === "grid") {
      var gv = st.values[b.id] != null ? st.values[b.id] : b.def;
      st.values[b.id] = gv;
      el.className = "param";
      el.innerHTML = '<div class="param-head"><span class="param-name">' + esc(b.name) + "</span></div>" +
        '<div class="grid-pick" style="grid-template-columns:repeat(' + (b.cols || 4) + ',1fr)">' +
        b.options.map(function (o) {
          var label = typeof o === "string" ? o : o.label;
          return '<button class="grid-item ' + (label === gv ? "on" : "") + '" data-v="' + esc(label) + '">' +
            (b.thumb ? '<span class="gi-thumb ' + ((typeof o === "object" && o.cls) || "") + '"></span>' : "") +
            "<span>" + esc(label) + "</span></button>";
        }).join("") + "</div>";
      el.addEventListener("click", function (e) {
        var t = e.target.closest("[data-v]");
        if (!t) return;
        st.values[b.id] = t.dataset.v;
        $$(".grid-item", el).forEach(function (x) { x.classList.toggle("on", x === t); });
        if (st.spec.onChange) st.spec.onChange(st.values, st);
      });
    } else if (b.t === "color") {
      var cv = st.values[b.id] != null ? st.values[b.id] : b.def;
      st.values[b.id] = cv;
      el.className = "param";
      el.innerHTML = '<div class="param-head"><span class="param-name">' + esc(b.name) + "</span>" +
        '<span class="color-val">' + esc(cv || "默认") + "</span></div>" +
        '<div class="swatches">' + b.options.map(function (o) {
          return '<button class="sw" data-v="' + o + '" style="background:' + o + '" title="' + o + '"></button>';
        }).join("") + "</div>";
      el.addEventListener("click", function (e) {
        var t = e.target.closest("[data-v]");
        if (!t) return;
        st.values[b.id] = t.dataset.v;
        $$(".sw", el).forEach(function (x) { x.classList.toggle("on", x === t); });
        if (st.spec.onChange) st.spec.onChange(st.values, st);
      });
    } else if (b.t === "progress") {
      el.className = "ai-block";
      el.innerHTML =
        '<button class="btn btn-primary quiet ai-start" style="height:38px;font-size:13px">' +
        '<svg class="icn icn-16"><use href="#i-play"/></svg>' + esc(b.label || "开始处理") + "</button>" +
        '<div class="ai-progress hidden"><div class="aip-top"><span class="aip-label">' + esc(b.running || "处理中") +
        '</span><span class="aip-pct">0%</span></div><div class="bar ai-bar"><i style="width:0%"></i></div></div>' +
        '<div class="ai-done hidden">' + esc(b.done || "处理完成") + "</div>" +
        '<div class="ai-demo">原型：进度为界面交互演示，真实推理由引擎完成</div>';
      var startBtn = $(".ai-start", el), prog = $(".ai-progress", el), doneEl = $(".ai-done", el);
      var bar = $(".ai-bar i", el), pctEl = $(".aip-pct", el);
      startBtn.addEventListener("click", function () {
        if (startBtn.dataset.busy) return;
        startBtn.dataset.busy = "1";
        startBtn.classList.add("hidden");
        doneEl.classList.add("hidden");
        prog.classList.remove("hidden");
        var p = 0;
        var timer = setInterval(function () {
          p = Math.min(100, p + 7 + Math.random() * 9);
          bar.style.width = p + "%";
          pctEl.textContent = Math.round(p) + "%";
          if (p >= 100) {
            clearInterval(timer);
            prog.classList.add("hidden");
            doneEl.classList.remove("hidden");
            delete startBtn.dataset.busy;
            if (st.spec.onAI) st.spec.onAI(st.values, st);
          }
        }, 260);
      });
    } else if (b.t === "curve") {
      el.className = "param";
      el.innerHTML =
        '<div class="param-head"><span class="param-name">' + esc(b.name || "曲线控制点") + "</span>" +
        '<span class="param-unit" style="margin-left:auto">速度轴 0–500%</span></div>' +
        '<div class="curve-editor"><svg viewBox="0 0 300 82" preserveAspectRatio="none" id="curve-svg">' +
        '<path class="ce-grid" d="M0 20.5h300M0 41h300M0 61.5h300M75 0v82M150 0v82M225 0v82" vector-effect="non-scaling-stroke"/>' +
        '<path class="ce-line" id="ce-path" d="M0 62L75 60L150 34L225 18L300 10" vector-effect="non-scaling-stroke"/>' +
        "</svg><span class=\"curve-note\">拖动节点调整速度</span></div>";
      var svg = $("#curve-svg", el);
      var raw = b.pts || [[0, 62], [75, 60], [150, 34], [225, 18], [300, 10]];
      var pts = raw.map(function (p) { return p.slice(); });
      var draw = function () {
        var d = "M" + pts[0][0] + " " + pts[0][1];
        for (var i = 1; i < pts.length; i++) d += "L" + pts[i][0] + " " + pts[i][1];
        $("#ce-path", el).setAttribute("d", d);
        $$(".ce-node", el).forEach(function (n) {
          var i = Number(n.dataset.i);
          n.setAttribute("cx", pts[i][0]);
          n.setAttribute("cy", pts[i][1]);
        });
      };
      pts.forEach(function (p, i) {
        var c = document.createElementNS("http://www.w3.org/2000/svg", "circle");
        c.setAttribute("class", "ce-node");
        c.setAttribute("r", "3");
        c.setAttribute("cx", p[0]);
        c.setAttribute("cy", p[1]);
        c.dataset.i = String(i);
        svg.appendChild(c);
      });
      var nodeDrag = null;
      svg.addEventListener("pointerdown", function (e) {
        var n = e.target.closest(".ce-node");
        if (!n) return;
        nodeDrag = Number(n.dataset.i);
        cap(svg, e.pointerId);
      });
      svg.addEventListener("pointermove", function (e) {
        if (nodeDrag == null) return;
        var r = svg.getBoundingClientRect();
        var y = clamp((e.clientY - r.top) / r.height * 82, 4, 78);
        pts[nodeDrag][1] = Math.round(y * 10) / 10;
        draw();
        st.values[b.id] = pts.map(function (p) { return p.slice(); });
        if (st.spec.onChange) st.spec.onChange(st.values, st);
      });
      var stop = function () { nodeDrag = null; };
      svg.addEventListener("pointerup", stop);
      svg.addEventListener("pointercancel", stop);
      st.values[b.id] = pts;
    } else if (b.t === "tip") {
      el.className = "panel-tip";
      el.style.marginTop = "4px";
      el.textContent = b.text;
    } else if (b.t === "dir") {
      el.className = "param";
      el.innerHTML = '<div class="param-head"><span class="param-name">' + esc(b.name) + "</span></div>" +
        '<div class="dir-pick">' + ["←", "→", "↑", "↓", "↗", "↖"].map(function (d, i) {
          return '<button class="dir-item ' + (i === 1 ? "on" : "") + '">' + d + "</button>";
        }).join("") + "</div>";
      el.addEventListener("click", function (e) {
        var t = e.target.closest(".dir-item");
        if (!t) return;
        $$(".dir-item", el).forEach(function (x) { x.classList.toggle("on", x === t); });
        st.values[b.id] = t.textContent;
      });
    } else if (b.t === "num") {
      el.className = "param";
      el.innerHTML = '<div class="param-head"><span class="param-name">' + esc(b.name) + "</span>" +
        '<input class="param-input" style="width:86px;text-align:left" type="text" placeholder="' +
        esc(b.ph || "") + '" value="' + (st.values[b.id] || "") + '">' +
        (b.unit ? '<span class="param-unit">' + esc(b.unit) + "</span>" : "") + "</div>";
      $(".param-input", el).addEventListener("input", function (e) {
        st.values[b.id] = e.target.value;
      });
    } else if (b.t === "note") {
      el.className = "panel-note";
      el.innerHTML = b.html || esc(b.text);
    } else if (b.t === "btnrow") {
      el.className = "btn-row";
      el.innerHTML = b.buttons.map(function (x, i) {
        return '<button class="btn ' + (x.primary ? "btn-primary quiet" : "btn-ghost") + '" style="height:36px;font-size:12.5px" data-i="' + i + '">' + esc(x.label) + "</button>";
      }).join("");
      el.addEventListener("click", function (e) {
        var t = e.target.closest("[data-i]");
        if (!t) return;
        var spec = b.buttons[Number(t.dataset.i)];
        if (spec.toast) toast(spec.toast);
        if (spec.act) spec.act(panelState);
      });
    }
    return el;
  }

  /* 面板拖拽手柄（§10.2 连续拖拽 + 磁吸三态） */
  function bindGrip() {
    var grip = $("#panel-grip");
    if (!grip) return;
    var h0 = 0, y0 = 0, dragging = false;
    grip.addEventListener("pointerdown", function (e) {
      dragging = true;
      h0 = panelEl.getBoundingClientRect().height;
      y0 = e.clientY;
      cap(grip, e.pointerId);
      panelEl.style.transition = "none";
      panelEl.classList.remove("no-anim");
    });
    grip.addEventListener("pointermove", function (e) {
      if (!dragging) return;
      var h = clamp(h0 + (y0 - e.clientY), 34, 330);
      panelEl.style.height = h + "px";
    });
    var stop = function () {
      if (!dragging) return;
      dragging = false;
      panelEl.style.transition = "";
      var h = panelEl.getBoundingClientRect().height;
      var states = [{ c: "panel-collapsed", h: 34 }, { c: "panel-open", h: 196 }, { c: "panel-expanded", h: 280 }];
      var best = states[0];
      states.forEach(function (s) { if (Math.abs(s.h - h) < Math.abs(best.h - h)) best = s; });
      root.classList.remove("panel-collapsed", "panel-open", "panel-expanded");
      root.classList.add("panel-open", best.c);
      panelEl.style.height = "";
      renderRuler();
    };
    grip.addEventListener("pointerup", stop);
    grip.addEventListener("pointercancel", stop);
    grip.addEventListener("click", function () {
      var expanded = root.classList.contains("panel-expanded");
      root.classList.toggle("panel-expanded", !expanded);
      root.classList.toggle("panel-open", true);
      root.classList.remove("panel-collapsed");
      panelEl.style.height = "";
      renderRuler();
    });
  }

  /* ======================================================================
     L. 面板规格（Part 4-A / 4-B / 4-C；未标注“原型演示”的均为真实控件）
     ====================================================================== */
  function clipParams() {
    var x = selectedEntry();
    return x ? (x.c.params = x.c.params || {}) : {};
  }

  var PANELS = {
    speed: function (opts) {
      return {
        title: "变速",
        tabs: [
          {
            id: "normal", name: "常规", blocks: [
              { t: "slider", id: "speed", name: "速度", min: 0.1, max: 100, def: 1, unit: "x", step: 0.1 },
              { t: "check", id: "pitch", name: "保持音调不变", def: false }
            ]
          },
          {
            id: "curve", name: "曲线", blocks: [
              { t: "grid", id: "curve", name: "曲线预设", cols: 4, def: "自定义",
                options: ["自定义", "蒙太奇", "英雄时刻", "子弹时间", "跳跃", "闪进", "闪出"] },
              { t: "curve", id: "pts", name: "曲线控制点" }
            ]
          }
        ],
        onChange: function (v) {
          var ps = clipParams();
          if (v.speed != null) ps.speed = v.speed;
          if (v.curve != null) ps.curve = v.curve;
          updatePreviewForTime();
        },
        onDone: function (v) {
          if (opts.batch) S.multi.forEach(function (id) {
            var e = findClip(id);
            if (e) { e.c.params = e.c.params || {}; e.c.params.speed = v.speed; e.c.params.curve = v.curve; }
          });
          toast(opts.batch ? "已批量应用变速" : "变速已应用");
          afterModelChange(true);
        }
      };
    },

    volume: function () {
      return {
        title: "音量",
        blocks: [
          { t: "slider", id: "vol", name: "音量", min: 0, max: 200, def: 100, unit: "%" },
          { t: "check", id: "mute", name: "静音此片段", def: false },
          { t: "slider", id: "fin", name: "淡入时长", min: 0, max: 3, def: 0, unit: "s" },
          { t: "slider", id: "fout", name: "淡出时长", min: 0, max: 3, def: 0, unit: "s" }
        ],
        onDone: function (v) {
          var ps = clipParams();
          ps.volume = v.vol; ps.mute = v.mute; ps.fadeIn = v.fin; ps.fadeOut = v.fout;
          toast("音量参数已应用");
          afterModelChange(true);
        }
      };
    },

    denoise: function () {
      return {
        title: "降噪",
        blocks: [
          { t: "slider", id: "strength", name: "降噪强度", min: 0, max: 100, def: 0 },
          { t: "seg", id: "ab", name: "试听对比", def: "原声", options: ["原声", "降噪后"] },
          { t: "tip", text: "拖动强度滑块实时试听；0 表示不处理。" }
        ],
        onChange: function (v) { clipParams().denoise = v.strength; }
      };
    },

    opacity: function () {
      return {
        title: "不透明度",
        blocks: [{ t: "slider", id: "opacity", name: "不透明度", min: 0, max: 100, def: 100, unit: "%" }],
        onChange: function (v) {
          clipParams().opacity = v.opacity;
          var x = selectedEntry();
          if (x) updatePreviewForClip(x.c);
        }
      };
    },

    cutout: function () {
      return {
        title: "智能抠像",
        tabs: [
          {
            id: "mode", name: "人像 / 自定义 / 绿幕", blocks: [
              { t: "seg", id: "mode", name: "抠像模式", def: "人像", options: ["人像", "自定义", "绿幕"] },
              { t: "slider", id: "feather", name: "边缘羽化", min: 0, max: 100, def: 35 },
              { t: "radio", id: "quality", name: "抠像精度", def: "标准", options: ["快速", "标准", "高精度"] },
              { t: "radio", id: "bg", name: "背景替换方式", def: "透明", options: ["透明", "模糊", "自定义图片或颜色"] }
            ]
          },
          {
            id: "run", name: "处理", blocks: [
              { t: "progress", id: "prog", label: "开始抠像", running: "抠像处理中", done: "抠像完成，已应用透明背景" },
              { t: "tip", text: "处理过程中其余参数项置灰禁用，防止运算冲突（文档 §4-A 校验 4）。" }
            ]
          }
        ]
      };
    },

    interp: function () {
      return {
        title: "光流补帧",
        blocks: [
          { t: "seg", id: "fps", name: "目标帧率", def: "60fps", options: ["60fps", "120fps", "240fps"] },
          { t: "radio", id: "algo", name: "算法模式", def: "标准（快速）", options: ["标准（快速）", "高精度（较慢）"] },
          { t: "slider", id: "smooth", name: "运动平滑度", min: 0, max: 100, def: 70 },
          { t: "progress", id: "prog", label: "开始补帧", running: "补帧处理中", done: "补帧完成（原型演示）" }
        ]
      };
    },

    sfx: function () {
      return {
        title: "音效",
        blocks: [
          { t: "grid", id: "cat", name: "分类", cols: 4, def: "全部",
            options: ["全部", "转场", "环境", "人声", "节奏", "搞笑", "机械"] },
          { t: "note", text: "音效库为占位内容；点击条目即插入音频轨（原型）。" },
          { t: "grid", id: "pick", name: "音效列表", cols: 2, def: "",
            options: ["转场 · 嗖", "环境 · 海浪", "环境 · 雨声", "人声 · 欢呼", "节奏 · 鼓点", "机械 · 快门"] }
        ],
        onDone: function (v) {
          if (!v.pick) { toast("请选择一个音效"); return; }
          pushHistory();
          var t = S.tracks.filter(function (x) { return x.type === "audio"; })[0];
          if (!t) {
            t = { id: uid("t-"), type: "audio", hidden: false, locked: false, muted: false, h: 40, clips: [] };
            S.tracks.push(t);
          }
          t.clips.push({ id: uid("c"), start: playTime(), dur: 2.4, label: v.pick, kind: "audio" });
          afterModelChange();
          toast("已插入音效：" + v.pick);
        }
      };
    },

    fade: function () {
      return {
        title: "淡化",
        blocks: [
          { t: "slider", id: "fin", name: "淡入", min: 0, max: 5, def: 0, unit: "s" },
          { t: "slider", id: "fout", name: "淡出", min: 0, max: 5, def: 0, unit: "s" }
        ],
        onDone: function (v) {
          var ps = clipParams(); ps.fadeIn = v.fin; ps.fadeOut = v.fout;
          toast("淡化已应用");
        }
      };
    },

    stem: function () {
      return {
        title: "人声分离",
        blocks: [
          { t: "radio", id: "mode", name: "分离模式", def: "人声+伴奏", options: ["仅提取人声", "仅提取伴奏", "人声+伴奏"] },
          { t: "slider", id: "strength", name: "分离强度", min: 0, max: 100, def: 80 },
          { t: "progress", id: "prog", label: "开始分离", running: "分离处理中", done: "分离完成：已生成人声轨与伴奏轨" },
          { t: "tip", text: "分离结果生成两条独立音频轨，不覆盖原始视频轨原声。" }
        ],
        onAI: function () {
          pushHistory();
          var a = { id: uid("t-"), type: "audio", hidden: false, locked: false, muted: false, h: 38,
            clips: [{ id: uid("c"), start: playTime(), dur: 8, label: "人声轨", kind: "audio" }] };
          var b = { id: uid("t-"), type: "audio", hidden: false, locked: false, muted: false, h: 38,
            clips: [{ id: uid("c"), start: playTime(), dur: 8, label: "伴奏轨", kind: "audio" }] };
          S.tracks.push(a, b);
          afterModelChange();
          toast("已生成“人声轨”和“伴奏轨”");
        }
      };
    },

    asr: function () {
      return {
        title: "识别字幕",
        blocks: [
          { t: "grid", id: "lang", name: "语言", cols: 4, def: "中文", options: ["中文", "英语", "日语", "韩语"] },
          { t: "progress", id: "prog", label: "开始识别", running: "识别中", done: "识别完成：已生成 6 条字幕" },
          { t: "tip", text: "识别结果使用占位示例文本（文档 §14.3）。" }
        ],
        onAI: function () {
          pushHistory();
          var t = S.tracks.filter(function (x) { return x.type === "sub"; })[0];
          if (!t) {
            t = { id: uid("t-"), type: "sub", hidden: false, locked: false, muted: false, h: 38, clips: [] };
            S.tracks.push(t);
          }
          var demo = ["海风从车窗灌进来", "远处的灯塔亮了一下", "我们没有说话"];
          demo.forEach(function (s, i) {
            t.clips.push({ id: uid("c"), start: 1 + i * 3.2, dur: 2.8, label: s, kind: "sub" });
          });
          afterModelChange();
          toast("已生成字幕轨道");
        }
      };
    },

    lyrics: function () {
      return {
        title: "识别歌词",
        blocks: [
          { t: "note", text: "对已添加的背景音乐音轨进行歌词识别；当前音乐：海边日落.mp3" },
          { t: "progress", id: "prog", label: "开始识别", running: "识别中", done: "识别完成：已生成 8 条歌词" },
          { t: "tip", text: "识别结果为占位示例文本（文档 §14.3）。" }
        ]
      };
    },

    track: function () {
      var texts = allClips().filter(function (x) { return x.c.kind === "sub"; }).map(function (x) { return x.c.label; });
      return {
        title: "镜头追踪",
        blocks: [
          { t: "tip", text: "在预览区点选/拖动锚点确定追踪目标（§33 手势规范）。" },
          { t: "radio", id: "type", name: "追踪类型", def: "位置", options: ["位置", "缩放", "位置+旋转"] },
          { t: "radio", id: "range", name: "追踪范围", def: "整段", options: ["整段", "自定义区间"] },
          { t: "grid", id: "bind", name: "绑定对象", cols: 2, def: "", options: texts.length ? texts : ["（暂无文字对象）"] },
          { t: "progress", id: "prog", label: "开始追踪", running: "追踪中", done: "已完成 · 生成 42 个追踪关键帧" }
        ],
        onChange: function () { root.classList.add("mask-edit"); },
        onDone: function () { root.classList.remove("mask-edit"); }
      };
    },

    translate: function () {
      return {
        title: "字幕翻译",
        blocks: [
          { t: "note", text: "原语言：中文（自动识别）" },
          { t: "grid", id: "lang", name: "目标语言", cols: 4, def: "英语", options: ["英语", "日语", "韩语", "法语", "西班牙语"] },
          { t: "radio", id: "mode", name: "翻译模式", def: "替换原字幕", options: ["替换原字幕", "生成双语字幕"] },
          { t: "progress", id: "prog", label: "开始翻译", running: "翻译中", done: "翻译完成（原型演示）" }
        ]
      };
    },

    tts: function () {
      return {
        title: "文本转语音",
        blocks: [
          { t: "note", text: "文本内容取自当前字幕；也可在下方直接输入。" },
          { t: "note", text: "<textarea class='panel-textarea' rows='2' placeholder='输入要合成的文本…'></textarea>" },
          { t: "grid", id: "voice", name: "音色（点击试听）", cols: 3, def: "",
            options: ["男声-磁性", "女声-温柔", "童声", "新闻播报", "活泼", "沉稳"] },
          { t: "slider", id: "rate", name: "语速", min: 0.5, max: 2, def: 1, unit: "x" },
          { t: "slider", id: "pitch", name: "音调", min: -12, max: 12, def: 0, unit: "半音" },
          { t: "progress", id: "prog", label: "生成语音", running: "合成中", done: "合成完成：已插入音频轨" }
        ],
        onAI: function () {
          pushHistory();
          var t = S.tracks.filter(function (x) { return x.type === "audio"; })[0];
          if (!t) {
            t = { id: uid("t-"), type: "audio", hidden: false, locked: false, muted: false, h: 38, clips: [] };
            S.tracks.push(t);
          }
          t.clips.push({ id: uid("c"), start: playTime(), dur: 3.2, label: "TTS 语音", kind: "audio" });
          afterModelChange();
          toast("已插入 TTS 语音片段");
        }
      };
    },

    "fx-screen": function () { return fxPanel("画面特效", ["故障", "闪光", "马赛克", "分屏", "镜像", "旋转模糊", "扫描线", "RGB分离"]); },
    "fx-person": function () { return fxPanel("人物特效", ["抖动", "放大", "瘦身", "发光轮廓", "边缘检测"]); },
    "fx-air": function () { return fxPanel("氛围特效", ["雪花", "下雨", "光斑", "胶片颗粒", "老电影划痕", "星光"]); },
    "fx-motion": function () {
      var p = fxPanel("动感特效", ["缩放冲击", "甩镜", "变速闪烁", "节奏律动", "运动模糊"]);
      p.blocks.splice(2, 0,
        { t: "slider", id: "shutter", name: "快门角度（运动模糊）", min: 0, max: 720, def: 180, unit: "°" },
        { t: "slider", id: "samples", name: "采样数", min: 2, max: 64, def: 16 },
        { t: "slider", id: "blur", name: "模糊强度", min: 0, max: 100, def: 55 },
        { t: "check", id: "highonly", name: "仅应用于高速运动片段", def: false }
      );
      p.onDone = function (v) {
        var ps = clipParams();
        ps.effect = v.preset; ps.effectStrength = v.strength;
        ps.motionBlur = { shutter: v.shutter, samples: v.samples, strength: v.blur, highOnly: v.highonly };
        toast("已应用特效：" + v.preset);
      };
      return p;
    },

    transition: function () {
      return {
        title: "转场",
        tabs: [
          {
            id: "preset", name: "预设转场", blocks: [
              { t: "grid", id: "cat", name: "分类", cols: 5, def: "基础",
                options: ["基础", "叠化", "运镜", "扭曲", "故障", "光效", "几何", "粒子", "复古"] },
              { t: "grid", id: "preset", name: "预设（基础）", cols: 4, def: "叠化",
                options: ["叠化", "闪白", "闪黑", "硬切"] },
              { t: "slider", id: "dur", name: "时长", min: 0.1, max: 2, def: 0.5, unit: "s" },
              { t: "dir", id: "dir", name: "方向" },
              { t: "check", id: "all", name: "应用到所有转场点", def: false },
              { t: "note", text: "方向选择器仅对推拉 / 擦除 / 百叶窗类转场显示。" }
            ]
          },
          {
            id: "keyframe", name: "自定义关键帧", blocks: [
              { t: "grid", id: "prop", name: "属性（可多选叠加）", cols: 3, def: "位置X",
                options: ["位置X", "位置Y", "缩放", "旋转", "不透明度"] },
              { t: "curve", id: "kf", name: "位置X 关键帧", pts: [[0, 62], [40, 62], [80, 30], [120, 30], [160, 62], [200, 62]] },
              { t: "slider", id: "kdur", name: "转场总时长", min: 0.1, max: 2, def: 0.6, unit: "s" },
              { t: "btnrow", buttons: [
                { label: "+ 添加关键帧" },
                { label: "预览转场效果" },
                { label: "保存为我的预设", primary: true, toast: "已保存到“预设转场 → 我的预设”（上限 20 个）" }
              ] },
              { t: "tip", text: "关键帧仅作用于转场区间，不作用于片段全程。" }
            ]
          }
        ],
        note: "两侧都需有相邻片段；同一衔接处仅保留一个转场。"
      };
    },

    filter: function () {
      return {
        title: "滤镜",
        blocks: [
          { t: "grid", id: "cat", name: "分类", cols: 5, def: "全部",
            options: ["全部", "电影", "复古", "清新", "黑白", "赛博", "日系", "胶片", "暖阳", "冷调", "港风", "高级灰", "奶油", "Negative"] },
          { t: "grid", id: "preset", name: "滤镜", cols: 4, def: "", thumb: true,
            options: [
              { label: "无", cls: "gi-none" }, { label: "电影", cls: "gi-film" }, { label: "复古", cls: "gi-retro" },
              { label: "清新", cls: "gi-fresh" }, { label: "黑白", cls: "gi-mono" }, { label: "暖阳", cls: "gi-warm" },
              { label: "冷调", cls: "gi-cool" }, { label: "胶片", cls: "gi-grain" }
            ] },
          { t: "slider", id: "strength", name: "强度", min: 0, max: 100, def: 80 },
          { t: "btnrow", buttons: [
            { label: "应用到全部片段", primary: true, act: function (st) {
              pushHistory();
              var name = st.values.preset || "电影";
              S.tracks.forEach(function (t) {
                t.clips.forEach(function (c) { c.params = c.params || {}; c.params.filter = name; });
              });
              afterModelChange(true);
              toast("已将“" + name + "”应用到全部片段");
            } }
          ] }
        ],
        onChange: function (v) {
          var ps = clipParams();
          ps.filter = v.preset || "无";
          ps.filterStrength = v.strength;
          var x = selectedEntry();
          if (x) updatePreviewForClip(x.c);
        }
      };
    },

    adjust: function () {
      var cats = ["亮度", "对比度", "饱和度", "锐化", "色温", "色调", "暗角", "颗粒", "超分辨率"];
      return {
        title: "调节",
        blocks: [
          { t: "grid", id: "cat", name: "参数类目", cols: 5, def: "亮度", options: cats },
          { t: "slider", id: "value", name: "亮度", min: -100, max: 100, def: 0 },
          { t: "note", text: "横滑切换参数类目只替换内容，不弹窗跳转（§8.4）。" },
          { t: "btnrow", buttons: [{ label: "重置全部（前 8 项）", act: function () {
            if (selectedEntry()) selectedEntry().c.params = {};
            var x = selectedEntry();
            if (x) updatePreviewForClip(x.c);
            toast("已重置全部常规参数");
          } }] },
          { t: "tip", text: "超分辨率：异步处理，处理中其余 8 项禁用。" }
        ],
        onChange: function (v) {
          var ps = clipParams();
          var key = { "亮度": "brightness", "对比度": "contrast", "饱和度": "saturation" }[v.cat];
          if (key) ps[key] = v.value;
          var x = selectedEntry();
          if (x) updatePreviewForClip(x.c);
          var nameEl = $(".panel-body .param-name");
          if (nameEl && v.cat) nameEl.textContent = v.cat;
        }
      };
    },

    ratio: function () {
      return {
        title: "比例",
        blocks: [
          { t: "grid", id: "ratio", name: "画布比例", cols: 4, def: "9:16",
            options: ["9:16", "16:9", "1:1", "3:4", "4:3", "2.35:1", "自定义"] },
          { t: "note", text: "点击预设立即生效，无需“完成”确认（§10.2）。" },
          { t: "num", id: "w", name: "宽", ph: "1080" },
          { t: "num", id: "h", name: "高", ph: "1920" }
        ],
        onChange: function (v) {
          var canvas = $("#ed-canvas");
          if (!canvas) return;
          var r = v.ratio, w, h;
          if (r === "自定义") {
            w = Number(v.w) || 1080; h = Number(v.h) || 1920;
          } else if (r === "2.35:1") { w = 235; h = 100; }
          else { var parts = r.split(":"); w = Number(parts[0]); h = Number(parts[1]); }
          w = clamp(w, 16, 10000); h = clamp(h, 16, 10000);
          var maxW = 300, maxH = 220;
          var k = Math.min(maxW / w, maxH / h);
          canvas.style.width = Math.round(w * k) + "px";
          canvas.style.height = Math.round(h * k) + "px";
          var label = $(".sc-label", canvas);
          if (label) label.textContent = r;
        }
      };
    },

    bg: function () {
      return {
        title: "背景",
        tabs: [
          {
            id: "color", name: "颜色", blocks: [
              { t: "color", id: "color", name: "预设色板", def: "#000000",
                options: ["#000000", "#1B1B1D", "#3A3A3D", "#F2F2F2", "#2D8CFF", "#E5484D"] },
              { t: "tip", text: "自定义取色器与色板共用同一取值；三个 Tab 互斥。" }
            ]
          },
          {
            id: "blur", name: "模糊", blocks: [
              { t: "slider", id: "blur", name: "模糊度", min: 0, max: 100, def: 50 }
            ]
          },
          {
            id: "style", name: "画布样式", blocks: [
              { t: "grid", id: "style", name: "预设纹理", cols: 3, def: "", thumb: true,
                options: [
                  { label: "木纹", cls: "gi-wood" }, { label: "纸张", cls: "gi-paper" },
                  { label: "渐变", cls: "gi-grad" }
                ] }
            ]
          }
        ],
        onChange: function (v) {
          var ps = clipParams();
          ps.bg = ps.bg || {};
          if (v.color) ps.bg.color = v.color;
          if (v.blur != null) ps.bg.blur = v.blur;
          if (v.style) ps.bg.style = v.style;
          var x = selectedEntry();
          if (x) updatePreviewForClip(x.c);
        }
      };
    },

    mask: function () {
      return {
        title: "蒙版",
        blocks: [
          { t: "grid", id: "shape", name: "形状", cols: 4, def: "无",
            options: ["无", "线性", "镜面", "圆形", "矩形", "星形", "爱心"] },
          { t: "slider", id: "feather", name: "羽化度", min: 0, max: 100, def: 40 },
          { t: "check", id: "invert", name: "反转蒙版", def: false },
          { t: "tip", text: "在预览区直接拖拽/双指缩放旋转控制框（§33）。" }
        ],
        onChange: function (v) {
          var x = selectedEntry();
          if (!x) return;
          x.c.params = x.c.params || {};
          x.c.params.mask = { shape: v.shape, feather: v.feather, invert: v.invert };
          applyMask(x.c);
        },
        onDone: function (v) {
          if (v.shape === "无") { closePanel(); toast("已关闭蒙版"); }
        }
      };
    },

    depth: function () {
      return {
        title: "深度图",
        blocks: [
          { t: "progress", id: "prog", label: "生成深度图", running: "分析中", done: "深度图已生成（灰度预览）" },
          { t: "grid", id: "effect", name: "应用效果", cols: 4, def: "景深虚化",
            options: ["景深虚化", "雾化", "色彩分层", "视差3D"] },
          { t: "slider", id: "focus", name: "对焦距离", min: 0, max: 100, def: 50, unit: "%" },
          { t: "slider", id: "aperture", name: "光圈大小", min: 1, max: 22, def: 2.8 },
          { t: "slider", id: "blur", name: "虚化强度", min: 0, max: 100, def: 70 },
          { t: "tip", text: "生成完成前，4 个应用效果 Tab 置灰禁用。" }
        ]
      };
    }
  };

  function fxPanel(title, presets) {
    return {
      title: title,
      blocks: [
        { t: "grid", id: "cat", name: "大类", cols: 4, def: title.replace("特效", ""),
          options: ["画面特效", "人物特效", "氛围特效", "动感特效"] },
        { t: "grid", id: "preset", name: "预设", cols: 4, def: presets[0], options: presets },
        { t: "slider", id: "strength", name: "强度", min: 0, max: 100, def: 50 },
        { t: "tip", text: "同一片段可叠加多个特效，强度互相独立。" }
      ],
      onDone: function (v) {
        var ps = clipParams();
        ps.effect = v.preset; ps.effectStrength = v.strength;
        toast("已应用特效：" + v.preset);
      }
    };
  }

  /* 蒙版形状 → 预览区控制框（§12 / §33） */
  function applyMask(clip) {
    var canvas = $("#ed-canvas");
    if (!canvas) return;
    var old = $(".mask-overlay", canvas);
    if (old) old.remove();
    var m = clip.params && clip.params.mask;
    if (!m || m.shape === "无") return;
    var box = document.createElement("div");
    box.className = "mask-overlay shape-" + shapeKey(m.shape);
    box.style.setProperty("--mfeather", Math.max(0, 40 - m.feather * 0.4) + "px");
    if (m.invert) box.classList.add("invert");
    if (m.shape === "线性" || m.shape === "镜面" || m.shape === "星形" || m.shape === "爱心") {
      box.innerHTML = '<span class="mo-tag">' + m.shape + '蒙版</span>';
    }
    canvas.appendChild(box);
    dragMask(box);
  }
  function shapeKey(s) {
    return { "线性": "linear", "镜面": "mirror", "圆形": "circle", "矩形": "rect", "星形": "star", "爱心": "heart" }[s] || "rect";
  }
  function dragMask(box) {
    var dragging = false, x0 = 0, y0 = 0, l0 = 0, t0 = 0;
    box.addEventListener("pointerdown", function (e) {
      dragging = true;
      x0 = e.clientX; y0 = e.clientY;
      l0 = box.offsetLeft; t0 = box.offsetTop;
      cap(box, e.pointerId);
      box.classList.add("active");
    });
    box.addEventListener("pointermove", function (e) {
      if (!dragging) return;
      var parent = box.parentElement;
      var maxL = parent.clientWidth - box.offsetWidth;
      var maxT = parent.clientHeight - box.offsetHeight;
      box.style.left = clamp(l0 + e.clientX - x0, 0, maxL) + "px";
      box.style.top = clamp(t0 + e.clientY - y0, 0, maxT) + "px";
    });
    box.addEventListener("pointerup", function () { dragging = false; box.classList.remove("active"); });
    box.addEventListener("pointercancel", function () { dragging = false; });
  }

  /* ======================================================================
     M. 全屏独立页（§12 裁剪 / §13 音乐·录音 / §14 新建文本 / §22 参考仿剪）
     ====================================================================== */
  function openPage(key) {
    var builder = PAGES[key];
    if (!builder) { toast("该页面待接入"); return; }
    closePage(true);
    var page = document.createElement("div");
    page.className = "fullpage";
    page.id = "fullpage";
    page.innerHTML = builder.html;
    root.appendChild(page);
    var pageApi = builder.init ? builder.init(page) : null;
    $(".fp-back", page).addEventListener("click", function () { closePage(); });
    $(".fp-done", page).addEventListener("click", function () {
      if (pageApi && pageApi.done) pageApi.done();
      closePage();
    });
    requestAnimationFrame(function () { page.classList.add("in"); });
  }
  function closePage(immediate) {
    var p = $("#fullpage");
    if (!p) return;
    p.classList.remove("in");
    if (immediate) { p.remove(); return; }
    setTimeout(function () { if (p.parentNode) p.remove(); }, 180);
  }

  var PAGES = {
    crop: {
      html:
        '<div class="fp-head"><button class="fp-back"><svg class="icn icn-20"><use href="#i-chev-left"/></svg></button>' +
        '<span class="fp-title">裁剪</span><button class="fp-done">完成</button></div>' +
        '<div class="fp-stage"><div class="crop-frame"><span class="cf-corner tl"></span><span class="cf-corner tr"></span>' +
        '<span class="cf-corner bl"></span><span class="cf-corner br"></span>' +
        '<div class="crop-grid"><i></i><i></i><i></i><i></i></div></div></div>' +
        '<div class="fp-panel">' +
        '<div class="param"><div class="param-head"><span class="param-name">裁剪比例</span></div>' +
        '<div class="seg-tabs" style="margin-top:8px">' +
        ["自由", "9:16", "16:9", "1:1", "3:4", "4:3"].map(function (x, i) {
          return '<button class="' + (i === 0 ? "on" : "") + '">' + x + "</button>";
        }).join("") + "</div></div>" +
        '<div class="param"><div class="param-head"><span class="param-name">旋转角度</span>' +
        '<input class="param-input" value="0" readonly><span class="param-unit">°</span></div>' +
        '<div class="slider"><div class="rail"></div><div class="fill" style="width:0%"></div><div class="knob" style="left:0%"></div></div></div>' +
        '<div class="param"><div class="param-head"><span class="param-name">镜像</span></div>' +
        '<div class="seg-tabs" style="margin-top:8px"><button>水平</button><button>垂直</button><button class="on">关闭</button></div></div>' +
        '<div class="panel-tip">裁剪为全屏独立页，不套用三级面板框架（文档 §12.2 唯一例外）。</div>' +
        "</div>"
    },

    music: {
      html:
        '<div class="fp-head"><button class="fp-back"><svg class="icn icn-20"><use href="#i-chev-left"/></svg></button>' +
        '<span class="fp-title">音乐库</span><button class="fp-done">使用</button></div>' +
        '<div class="fp-search"><svg class="icn icn-16"><use href="#i-search"/></svg><input placeholder="搜索音乐"></div>' +
        '<div class="fp-tabs">' + ["推荐", "收藏", "本地音乐", "链接导入"].map(function (t, i) {
          return '<button class="' + (i === 0 ? "on" : "") + '">' + t + "</button>";
        }).join("") + "</div>" +
        '<div class="fp-list">' +
        [["海边日落", "轻音乐 · 02:18"], ["城市清晨", "氛围 · 01:46"], ["公路旅行", "流行 · 03:05"],
          ["雨落窗台", "环境 · 02:32"], ["夜航", "电子 · 02:54"]].map(function (m, i) {
          return '<div class="music-row' + (i === 0 ? " picked" : "") + '">' +
            '<span class="mr-play"><svg class="icn icn-14"><use href="#i-play"/></svg></span>' +
            '<span class="mr-info"><b>' + m[0] + "</b><em>" + m[1] + "</em></span>" +
            '<span class="mr-use">使用</span></div>';
        }).join("") + "</div>" +
        '<div class="fp-note">音乐库为占位数据（文档 §13.3）；选中后插入新的音频轨道。</div>'
    },

    record: {
      html:
        '<div class="fp-head"><button class="fp-back"><svg class="icn icn-20"><use href="#i-chev-left"/></svg></button>' +
        '<span class="fp-title">录音</span><button class="fp-done">完成</button></div>' +
        '<div class="rec-wrap"><div class="rec-time" id="rec-time">00:00.0</div>' +
        '<div class="rec-wave" id="rec-wave"></div>' +
        '<button class="rec-btn" id="rec-btn"><span></span></button>' +
        '<div class="rec-hint" id="rec-hint">点击开始录音</div>' +
        '<div class="rec-actions"><button class="btn btn-ghost" id="rec-redo" disabled>重录</button>' +
        '<button class="btn btn-primary quiet" id="rec-ok" disabled>使用这段录音</button></div></div>' +
        '<div class="fp-note">录制完成后可试听/重录，确认后插入音频轨道。</div>',
      init: function (page) {
        var rec = false, t0 = 0, timer = 0, secs = 0;
        var btn = $("#rec-btn", page), wave = $("#rec-wave", page);
        var bars = 34;
        for (var i = 0; i < bars; i++) {
          var s = document.createElement("i");
          s.style.height = "4px";
          wave.appendChild(s);
        }
        var fmt = function (s) {
          var m = Math.floor(s / 60);
          return (m < 10 ? "0" : "") + m + ":" + ((s % 60) < 10 ? "0" : "") + (s % 60).toFixed(1);
        };
        btn.addEventListener("click", function () {
          rec = !rec;
          page.classList.toggle("recording", rec);
          $("#rec-hint", page).textContent = rec ? "正在录音…再次点击停止" : "录音已停止";
          if (rec) {
            t0 = performance.now(); secs = 0;
            timer = setInterval(function () {
              secs = (performance.now() - t0) / 1000;
              $("#rec-time", page).textContent = fmt(secs);
              $$("i", wave).forEach(function (bar, i) {
                var h = 4 + Math.abs(Math.sin(secs * 6 + i * 0.7)) * 26 * (0.4 + Math.random() * 0.6);
                bar.style.height = h.toFixed(0) + "px";
              });
            }, 90);
          } else {
            clearInterval(timer);
            $("#rec-redo", page).disabled = false;
            $("#rec-ok", page).disabled = false;
          }
        });
        $("#rec-redo", page).addEventListener("click", function () {
          $("#rec-time", page).textContent = "00:00.0";
          $$("i", wave).forEach(function (bar) { bar.style.height = "4px"; });
          $("#rec-redo", page).disabled = true;
          $("#rec-ok", page).disabled = true;
          $("#rec-hint", page).textContent = "点击开始录音";
          page.classList.remove("recording");
          rec = false;
        });
        return {
          done: function () {
            if (secs > 0.3) {
              pushHistory();
              var t = S.tracks.filter(function (x) { return x.type === "audio"; })[0];
              if (!t) {
                t = { id: uid("t-"), type: "audio", hidden: false, locked: false, muted: false, h: 38, clips: [] };
                S.tracks.push(t);
              }
              t.clips.push({ id: uid("c"), start: playTime(), dur: Math.max(1, secs), label: "录音", kind: "audio" });
              afterModelChange();
              toast("录音已插入音频轨道");
            }
          }
        };
      }
    },

    text: {
      html:
        '<div class="fp-head"><button class="fp-back"><svg class="icn icn-20"><use href="#i-chev-left"/></svg></button>' +
        '<span class="fp-title">新建文本</span><button class="fp-done">完成</button></div>' +
        '<div class="fp-textstage"><div class="text-preview" id="text-preview">点击输入文字</div></div>' +
        '<div class="fp-panel">' +
        '<div class="fp-tabs small">' + ["字体", "颜色", "描边", "阴影", "排列", "动画"].map(function (t, i) {
          return '<button class="' + (i === 0 ? "on" : "") + '">' + t + "</button>";
        }).join("") + "</div>" +
        '<textarea class="panel-textarea" id="text-input" rows="2" placeholder="输入文字内容…"></textarea>' +
        '<div class="param"><div class="param-head"><span class="param-name">描边粗细</span>' +
        '<input class="param-input" value="0" readonly><span class="param-unit">px</span></div>' +
        '<div class="slider"><div class="rail"></div><div class="fill" style="width:0%"></div><div class="knob" style="left:0%"></div></div></div>' +
        '<div class="param"><div class="param-head"><span class="param-name">阴影强度</span>' +
        '<input class="param-input" value="0" readonly></div>' +
        '<div class="slider"><div class="rail"></div><div class="fill" style="width:0%"></div><div class="knob" style="left:0%"></div></div></div>' +
        "</div>",
      init: function (page) {
        var input = $("#text-input", page), prev = $("#text-preview", page);
        input.addEventListener("input", function () {
          prev.textContent = input.value || "点击输入文字";
        });
        input.focus();
        return {
          done: function () {
            var v = input.value.trim();
            if (!v) { toast("请输入文字内容"); return; }
            pushHistory();
            var t = S.tracks.filter(function (x) { return x.type === "sub"; })[0];
            if (!t) {
              t = { id: uid("t-"), type: "sub", hidden: false, locked: false, muted: false, h: 38, clips: [] };
              S.tracks.push(t);
            }
            t.clips.push({ id: uid("c"), start: playTime(), dur: 3, label: v, kind: "sub" });
            sortClips(t);
            afterModelChange();
            toast("文本已添加到字幕轨");
          }
        };
      }
    },

    refcut: {
      html:
        '<div class="fp-head"><button class="fp-back"><svg class="icn icn-20"><use href="#i-chev-left"/></svg></button>' +
        '<span class="fp-title">参考仿剪</span><button class="fp-done">完成</button></div>' +
        '<div class="fp-steps">' +
        '<div class="step"><span class="st-no">1</span><div class="st-body"><b>上传参考视频</b>' +
        '<button class="st-pick"><svg class="icn icn-16"><use href="#i-plus"/></svg>选择参考视频</button></div></div>' +
        '<div class="step"><span class="st-no">2</span><div class="st-body"><b>选择仿照维度（可多选）</b>' +
        '<div class="chip-row">' + ["剪辑节奏", "运镜方式", "转场风格", "配乐节拍", "画面滤镜"].map(function (c, i) {
          return '<button class="chip ' + (i < 3 ? "on" : "") + '">' + c + "</button>";
        }).join("") + "</div></div></div>" +
        '<div class="step"><span class="st-no">3</span><div class="st-body"><b>匹配我的素材</b>' +
        '<div class="st-hint">已选择 6 个素材片段 <button class="st-link">重新选择</button></div></div></div>' +
        "</div>" +
        '<div class="fp-bottom"><button class="btn btn-primary" id="refcut-run" style="width:100%">开始智能仿剪</button></div>' +
        '<div class="ai-demo" style="padding:0 16px 12px">原型：分析进度为界面交互演示</div>' +
        '<div class="overlay hidden" id="refcut-confirm" style="position:absolute">' +
        '<div class="dialog"><div class="dg-icon"><svg class="icn icn-22"><use href="#i-warning"/></svg></div>' +
        '<div class="dg-title">即将重新编排时间轴</div>' +
        '<div class="dg-desc">此操作会自动调整当前项目的剪辑结构，系统将自动保存一份当前版本的备份草稿，执行后仍可在“草稿箱”中找回原版本。</div>' +
        '<div class="dg-actions"><button class="btn btn-ghost js-no">取消</button>' +
        '<button class="btn btn-primary js-yes">确认执行</button></div></div></div>',
      init: function (page) {
        $("#refcut-run", page).addEventListener("click", function () {
          $("#refcut-confirm", page).classList.remove("hidden");
        });
        $(".js-no", page).addEventListener("click", function () {
          $("#refcut-confirm", page).classList.add("hidden");
        });
        $(".js-yes", page).addEventListener("click", function () {
          $("#refcut-confirm", page).classList.add("hidden");
          toast("已生成备份草稿「项目名-仿剪前备份」，开始分析…");
        });
        $$(".chip", page).forEach(function (c) {
          c.addEventListener("click", function () { c.classList.toggle("on"); });
        });
      }
    }
  };

  /* ======================================================================
     N. 导出入口（§24 导出流程不在本页范围，仅提示后续页面）
     ====================================================================== */
  $("#btn-export").addEventListener("click", function () {
    toast("导出设置面板将在 Part 5 交付");
  });

  /* ======================================================================
     O. 初始化
     ====================================================================== */
  function init() {
    recalcDuration();
    applyCollapse();
    renderTracks();
    renderRuler();
    syncHistoryBtns();
    setCategory(null);
    var firstClip = allClips().filter(function (x) { return x.c.kind === "video"; })[0];
    if (firstClip) S.selected = { trackId: firstClip.t.id, clipId: firstClip.c.id };
    syncClipSelection();
    syncToolbarState();
    seekTo(0);
    updatePreviewForTime();
    syncPlayhead();

    var u = $("#btn-undo"), r = $("#btn-redo");
    if (u) u.addEventListener("click", undo);
    if (r) r.addEventListener("click", redo);

    window.addEventListener("resize", function () {
      renderTracks();
      renderRuler();
      syncPlayhead();
    });

    /* 深链：评审状态 */
    var hash = (location.hash || "").replace("#", "");
    if (hash.indexOf("cat-") === 0) setCategory(hash.slice(4));
    if (hash === "speed") { setCategory("edit"); openPanel("speed"); }
    if (hash === "filter") { setCategory("filter"); openPanel("filter"); }
    if (hash === "adjust") { setCategory("adjust"); openPanel("adjust"); }
    if (hash === "trans") { setCategory("trans"); openPanel("transition"); }
    if (hash === "mask") {
      setCategory("mask");
      var c = allClips().filter(function (x) { return x.c.kind === "video"; })[0];
      if (c) { S.selected = { trackId: c.t.id, clipId: c.c.id }; syncClipSelection(); }
      openPanel("mask");
    }
    if (hash === "collapsed") {
      S.collapsed = true; applyCollapse();
    }
    if (hash === "sheet") {
      var v = allClips().filter(function (x) { return x.c.kind === "video"; })[1];
      if (v) { S.selected = { trackId: v.t.id, clipId: v.c.id }; syncClipSelection(); openClipSheet(v.t, v.c); }
    }
    if (hash === "multi") {
      var vids = allClips().filter(function (x) { return x.c.kind === "video"; });
      S.multi = vids.slice(0, 2).map(function (x) { return x.c.id; });
      S.selected = null;
      syncClipSelection(); syncBatchBar();
    }
    if (hash === "perf") {
      $("#perf-title").textContent = "当前项目轨道较多（15 条）";
      $("#dlg-perf").classList.remove("hidden");
      $("#perf-cancel").onclick = function () { $("#dlg-perf").classList.add("hidden"); };
      $("#perf-ok").onclick = function () { $("#dlg-perf").classList.add("hidden"); };
    }
  }

  /* 只读调试快照（供 UI 评审与自动化验证；不参与业务逻辑） */
  window.ncEditorDebug = function () {
    return {
      selected: S.selected,
      panel: panelState ? panelState.key : null,
      tracks: S.tracks.map(function (t) {
        return { id: t.id, type: t.type, locked: t.locked, hidden: t.hidden, clips: t.clips.length };
      }),
      history: S.history.length,
      pps: S.pps,
      playT: playTime(),
      duration: S.duration,
      page: $("#fullpage") ? $("#fullpage").querySelector(".fp-title").textContent : null
    };
  };

  if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", init);
  else init();
})();
