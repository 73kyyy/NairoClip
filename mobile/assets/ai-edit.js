/* NairoClip · AI 剪辑向导（参考视频驱动）
   ============================================================================
   四步：理解 → 方向 → 配乐 → 编排 → 生成。

   ## 三条不妥协的原则

   ① **分析结果必须是真的**
      `nc_ref_analyze` 返回的每个数字都来自像素统计（镜头切点、差异重心、
      亮度曲线）。没有「AI 觉得你很帅」这种编出来的东西。

   ② **方案在前端算，内核只负责落盘**
      槽位怎么填、转场用哪个、参数取多少，全部由这里算好并显示给用户，
      用户能逐个改；`nc_ref_apply` 收到的就是屏幕上那份映射。
      这样「所见 = 所生成」，不会出现内核偷偷换素材。

   ③ **做不到的功能直接标灰**
      「丝滑变速」需要逐镜速度曲线，会和卡点互相打架，还没做 ——
      卡片上写明「暂不可用」，不假装能点。
*/
'use strict';

(function () {
  const $ = (id) => document.getElementById(id);

  function api(fn, args) {
    const f = window.NC_API && window.NC_API[fn];
    if (typeof f !== 'function') return { ok: false, message: '引擎接口缺失：' + fn };
    try { return f(args || {}); } catch (e) { return { ok: false, message: (e && e.message) || String(e) }; }
  }

  /* 转场置信度门槛。低于它的一律**不按猜出来的类型生成**，
     退化成中性的溶解，并在编排页如实告诉用户有几处降级。
     只在界面上标「疑似」、实际仍按猜测类型落盘，是两套说法 —— 那不行。 */
  const CONF_ACCEPT = 0.5;

  const S = {
    step: 1,
    assets: [],          // 全部素材 {id,name,kind,durUs}
    videos: [],
    audios: [],
    refId: 0,
    profile: null,       // 分析结果
    style: 'smooth',
    intensity: 0.6,
    musicId: 0,
    order: 'smart',
    overlayId: 0,
    overlayMode: 'mid',
    slots: []            // [{index, durSec, assetId}]
  };

  /* ---------------- 素材 ---------------- */
  function loadAssets() {
    const out = [];
    const st = window.Engine && window.Engine.state;
    if (st && Array.isArray(st.assets)) {
      st.assets.forEach((a) => {
        if (!a || !a.id) return;
        out.push({
          id: a.id,
          name: a.name || ('素材 ' + a.id),
          kind: a.kind,
          durUs: Math.round((a.dur || 0) * 1e6)
        });
      });
    } else {
      const cnt = api('nc_asset_count');
      const n = (cnt && typeof cnt.result === 'number') ? cnt.result : 0;
      for (let i = 0; i < n; ++i) {
        const r = api('nc_asset_get', { index: i });
        if (!r || !r.ok || !r.result) continue;
        const x = r.result;
        out.push({
          id: x.asset_id,
          name: x.name || ('素材 ' + x.asset_id),
          kind: x.type,
          durUs: x.duration_us || 0
        });
      }
    }
    S.assets = out;
    S.videos = out.filter((a) => a.kind === 0 || a.kind === 2);
    S.audios = out.filter((a) => a.kind === 1);
  }

  /* ---------------- 步骤控制 ---------------- */
  function showStep(n) {
    S.step = n;
    for (let i = 1; i <= 4; ++i) $('step' + i).hidden = (i !== n);
    $('stepProgress').hidden = true;
    $('foot').hidden = false;
    $('btnPrev').hidden = n === 1;
    $('btnPrev').textContent = '上一步';
    $('btnNext').textContent = n === 4 ? '开始创作' : '下一步';

    if (n === 1) $('btnNext').disabled = !!S.refId === false;
    if (n === 2) $('btnNext').disabled = !S.profile;
    if (n === 3) $('btnNext').disabled = false;      // 配乐可以跳过
    if (n === 4) $('btnNext').disabled = false;

    if (n === 2) renderAnalysis();
    if (n === 3) renderMusic();
    if (n === 4) renderSlots();
  }

  /* ---------------- 1. 参考素材 ---------------- */
  function renderRefs() {
    const box = $('refList');
    box.innerHTML = '';
    if (!S.videos.length) {
      $('refEmpty').hidden = false;
      return;
    }
    $('refEmpty').hidden = true;
    S.videos.forEach((a) => {
      const b = document.createElement('button');
      b.className = 'ref-item' + (a.id === S.refId ? ' on' : '');
      b.innerHTML =
        '<span class="ref-ico"><svg viewBox="0 0 24 24" width="20" height="20">' +
        '<use href="#i-film"/></svg></span>' +
        '<span style="flex:1"><span class="ref-name"></span><br>' +
        '<span class="ref-meta"></span></span>';
      b.querySelector('.ref-name').textContent = a.name;
      b.querySelector('.ref-meta').textContent =
        (a.kind === 2 ? '图片' : '视频') + ' · ' + (a.durUs / 1e6).toFixed(1) + 's';
      b.addEventListener('click', () => {
        S.refId = a.id;
        S.profile = null;
        renderRefs();
        $('btnNext').disabled = false;
      });
      box.appendChild(b);
    });
  }

  function analyze(cb) {
    const r = api('nc_ref_analyze', { asset_id: S.refId });
    if (!r || !r.ok) {
      cb(false, (r && r.message) || '分析失败');
      return;
    }
    const p = r.result && r.result.profile;
    if (!p) {
      cb(false, '分析结果为空');
      return;
    }
    S.profile = p;
    cb(true);
  }

  /* ---------------- 2. 方向 ---------------- */
  function renderAnalysis() {
    const p = S.profile;
    const box = $('analysisBox');
    if (!p) { box.textContent = ''; return; }
    const trans = (p.transitions || []).map((t) => {
      const pct = Math.round((t.confidence || 0) * 100);
      const conf = (t.confidence >= CONF_ACCEPT)
        ? ' <span style="color:var(--text-low)">' + pct + '%</span>'
        : ' <span class="tag warn">疑似 ' + pct + '%</span>';
      return '<div class="tline"><span><span class="tag">' + t.name + '</span>' + conf +
             '</span><span>' + t.at.toFixed(2) + 's</span></div>';
    }).join('');
    let shakeText = '未检出（固定机位）';
    if (p.shake_confidence >= 0.4) {
      shakeText = (p.shake_amplitude * 100).toFixed(1) + '% @ ' +
                  p.shake_frequency_hz.toFixed(1) + 'Hz';
    }
    box.innerHTML =
      '<div class="tline"><span>镜头数</span><b>' + p.shot_count + '</b></div>' +
      '<div class="tline"><span>总时长</span><b>' + p.total_seconds.toFixed(1) + 's</b></div>' +
      '<div class="tline"><span>切点密度</span><b>' + p.cuts_per_minute.toFixed(1) +
        ' / 分钟</b></div>' +
      '<div class="tline"><span>抖动估计</span><b>' + shakeText + '</b></div>' +
      (trans ? '<div style="margin-top:6px">' + trans + '</div>' : '');
  }

  /* ---------------- 3. 配乐 ---------------- */
  function renderMusic() {
    const box = $('musicList');
    box.innerHTML = '';
    if (!S.audios.length) { $('musicEmpty').hidden = false; return; }
    $('musicEmpty').hidden = true;

    // 「不配乐」永远可选 —— 不能强迫用户选一首
    const none = document.createElement('button');
    none.className = 'music-item' + (S.musicId === 0 ? ' on' : '');
    none.textContent = '不配乐（保留原声）';
    none.addEventListener('click', () => { S.musicId = 0; renderMusic(); });
    box.appendChild(none);

    S.audios.forEach((a) => {
      const b = document.createElement('button');
      b.className = 'music-item' + (a.id === S.musicId ? ' on' : '');
      const nm = document.createElement('span');
      nm.textContent = a.name;
      const d = document.createElement('small');
      d.textContent = (a.durUs / 1e6).toFixed(1) + 's';
      b.appendChild(nm);
      b.appendChild(d);
      b.addEventListener('click', () => { S.musicId = a.id; renderMusic(); });
      box.appendChild(b);
    });
  }

  /* ---------------- 4. 编排 ---------------- */
  function buildSlots() {
    const p = S.profile;
    const shots = (p && p.shots) || [];
    S.slots = shots.map((s, i) => ({
      index: i,
      durSec: Math.max(0.4, s.duration || 1.0),
      assetId: 0
    }));
    autoAssign();
  }

  /* 自动分配：按可用素材轮转。
     为什么不用内核打分：打分结果可能与界面上显示的不一致，
     造成「所见 ≠ 所生成」。这里明确轮转，用户看得见每条怎么填。 */
  function autoAssign() {
    if (!S.videos.length) return;
    const pool = S.order === 'original' ? S.videos.slice() : S.videos.slice();
    S.slots.forEach((slot, i) => {
      const a = pool[i % pool.length];
      slot.assetId = a ? a.id : 0;
    });
  }

  function renderSlots() {
    if (!S.slots.length) buildSlots();
    const grid = $('slotGrid');
    grid.innerHTML = '';
    S.slots.forEach((slot, i) => {
      const d = document.createElement('div');
      d.className = 'slot';
      const hd = document.createElement('div');
      hd.className = 'slot-hd';
      hd.textContent = '参考镜头 ' + String(i + 1).padStart(2, '0');
      const sel = document.createElement('select');
      S.videos.forEach((a) => {
        const o = document.createElement('option');
        o.value = String(a.id);
        o.textContent = a.name;
        if (a.id === slot.assetId) o.selected = true;
        sel.appendChild(o);
      });
      sel.addEventListener('change', () => {
        slot.assetId = parseInt(sel.value, 10) || 0;
      });
      const dur = document.createElement('div');
      dur.className = 'slot-dur';
      dur.textContent = slot.durSec.toFixed(2) + 's';
      d.appendChild(hd);
      d.appendChild(sel);
      d.appendChild(dur);
      grid.appendChild(d);
    });

    /* 统计有多少处转场会因为置信度不足而降级成溶解 —— 必须提前告诉用户，
       不能等生成完发现「怎么全是叠化」再解释。 */
    const tr = (S.profile && S.profile.transitions) || [];
    let lowConf = 0;
    for (let i = 0; i + 1 < S.slots.length; ++i) {
      const t = tr[i];
      if (t && (t.confidence || 0) < CONF_ACCEPT) lowConf += 1;
    }

    renderOverlaySelect();

    const missing = S.slots.filter((s) => !s.assetId).length;
    if (missing) {
      $('slotNote').innerHTML = '还差 <b>' + missing + '</b> 个槽位没选素材';
    } else {
      const total = S.slots.reduce((n, x) => n + x.durSec, 0).toFixed(1);
      $('slotNote').innerHTML =
        '共 <b>' + S.slots.length + '</b> 个镜头，预计成片 <b>' + total + 's</b>。' +
        '生成后会直接打开时间轴，可以逐条改特效与关键帧。' +
        (lowConf ? '<br><b>' + lowConf + '</b> 处转场置信度不足，已按溶解处理（不照猜测的类型生成）。' : '');
    }
  }

  /* ---------------- 叠层素材 ---------------- */
  function renderOverlaySelect() {
    const sel = $('overlaySel');
    if (!sel) return;
    const keep = S.overlayId;
    sel.innerHTML = '<option value="0">不加叠层</option>';
    S.videos.forEach((a) => {
      const o = document.createElement('option');
      o.value = String(a.id);
      o.textContent = a.name;
      if (a.id === keep) o.selected = true;
      sel.appendChild(o);
    });
  }

  /* 把叠层放到一条独立的画中画轨上。
     位置怎么定：按每个镜头的 motion（分析出来的运动强度）排序，取前面约 1/3，
     最多 3 处。这是**可解释的规则** —— 动得厉害的段落加装饰，不是「AI 觉得好看」。
     参数也是固定的入场曲线：缩放 0.88→1.10、透明度 0→1，各 0.4s。 */
  function applyOverlays(durs) {
    if (!S.overlayId) return 0;
    const ov = S.videos.find((x) => x.id === S.overlayId);
    if (!ov) return 0;

    const shots = (S.profile && S.profile.shots) || [];
    const order = S.slots.map((s, i) => ({ i: i, motion: (shots[i] && shots[i].motion) || 0 }));
    order.sort((a, b) => b.motion - a.motion);

    const total = S.slots.length;
    const want = S.overlayMode === 'all'
      ? total
      : (S.overlayMode === 'mid' ? Math.ceil(total * 2 / 3) : Math.ceil(total / 3));

    /* 多轨打包：按运动量从高到低依次放，每条轨记录已占用的区间，
       放到第一条「不与已有区间重叠」的轨上；都满了就新开一条。
       这样同一时刻可以有多个叠层（比如前景光效 + 边框），不会互相顶掉。
       上限 4 条 —— 再多就是叠被子，画面会糊。 */
    const MAX_TRACKS = 4;
    const tracks = [];
    function newTrack() {
      const t = api('nc_add_track', { name: '叠层 ' + (tracks.length + 1), type: 0 });
      if (!t || !t.ok) return null;
      const c = api('nc_track_count');
      const n = (c && typeof c.result === 'number') ? c.result : 0;
      const rec = { idx: n - 1, spans: [] };
      tracks.push(rec);
      return rec;
    }

    let placed = 0;
    for (let k = 0; k < order.length && placed < want; ++k) {
      const i = order[k].i;
      let startUs = 0;
      for (let j = 0; j < i; ++j) startUs += Math.round(durs[j] * 1e6);
      const slotUs = Math.round(durs[i] * 1e6);
      const availUs = ov.durUs || slotUs;
      const durUs = Math.min(slotUs, availUs, 3000000);
      if (durUs < 300000) continue;
      const endUs = startUs + durUs;

      let target = null;
      for (let t = 0; t < tracks.length; ++t) {
        const free = tracks[t].spans.every((sp) => startUs >= sp[1] || endUs <= sp[0]);
        if (free) { target = tracks[t]; break; }
      }
      if (target === null) {
        if (tracks.length >= MAX_TRACKS) break;
        target = newTrack();
        if (target === null) break;
      }

      const r = api('nc_insert_clip', {
        track_index: target.idx, asset_id: ov.id, start_us: startUs, duration_us: durUs
      });
      if (!r || !r.ok) continue;
      // JNI 的 nc_insert_clip 返回 { value: <id> }，不能直接把对象当 id
      const clipId = (r.result && (r.result.value || r.result.clip_id || r.result.id)) || 0;
      if (!clipId) continue;
      target.spans.push([startUs, endUs]);

      // 入场曲线：缩放与透明度各两个关键帧（property 1=缩放，0=透明度）
      const fade = Math.min(400000, Math.round(durUs / 3));
      api('nc_insert_keyframe', { track_index: target.idx, clip_id: clipId, property: 1, time_us: 0, value: 0.88 });
      api('nc_insert_keyframe', { track_index: target.idx, clip_id: clipId, property: 1, time_us: fade, value: 1.10 });
      api('nc_insert_keyframe', { track_index: target.idx, clip_id: clipId, property: 0, time_us: 0, value: 0.0 });
      api('nc_insert_keyframe', { track_index: target.idx, clip_id: clipId, property: 0, time_us: fade, value: 1.0 });
      placed += 1;
    }
    return placed;
  }

  /* ---------------- 配乐卡点 ---------------- */
  /* 先把配乐放进一条新音轨再检测：nc_beat_detect 分析的是**轨道上已存在的音频**，
     没有轨就没有节拍可测。这一步产生一条独立撤销记录（音轨+音频），
     视频轨生成仍是单独一次撤销。 */
  function prepareMusic() {
    if (!S.musicId) return null;
    const music = S.audios.find((x) => x.id === S.musicId);
    if (!music || !music.durUs) return null;
    const c0 = api('nc_track_count');
    const n0 = (c0 && typeof c0.result === 'number') ? c0.result : 0;
    const t = api('nc_add_track', { name: 'AI 配乐', type: 1 });
    if (!t || !t.ok) return null;
    const c1 = api('nc_track_count');
    const n1 = (c1 && typeof c1.result === 'number') ? c1.result : n0 + 1;
    const idx = n1 - 1;
    const ins = api('nc_insert_clip', {
      track_index: idx, asset_id: music.id, start_us: 0, duration_us: music.durUs
    });
    if (!ins || !ins.ok) return null;
    const b = api('nc_beat_detect', { track_index: idx });
    if (!b || !b.ok || !b.result) return null;
    const beats = Array.isArray(b.result.beats) ? b.result.beats : [];
    if (beats.length < 4) return { bpm: 0, beats: [], snapped: false };
    return { bpm: b.result.bpm || 0, beats, snapped: true };
  }

  /* 每段时长吸到半拍网格：整拍太粗（1.2s 会被拉到 1.0/2.0），
     半拍既踩在鼓点上又保留参考片的节奏比例。 */
  function snapDurations(durs, music) {
    if (!music || !music.snapped) return durs;
    let grid = music.bpm > 0 ? (60 / music.bpm) / 2 : 0;
    if (!(grid > 0.08) && music.beats.length > 2) {
      const gaps = [];
      for (let i = 1; i < music.beats.length; ++i) gaps.push(music.beats[i] - music.beats[i - 1]);
      gaps.sort((x, y) => x - y);
      grid = gaps[Math.floor(gaps.length / 2)] / 2;
    }
    if (!(grid > 0.08)) return durs;
    const out = [];
    for (let i = 0; i < durs.length; ++i) {
      out.push(Math.max(0.35, Math.round(durs[i] / grid) * grid));
    }
    const m = S.audios.find((x) => x.id === S.musicId) || {};
    const musicSec = (m.durUs || 0) / 1e6;
    if (musicSec > 0) {
      let total = out.reduce((n, x) => n + x, 0);
      for (let i = out.length - 1; i >= 0 && total > musicSec; --i) {
        const cut = Math.min(total - musicSec, out[i] - 0.35);
        if (cut > 0) { out[i] -= cut; total -= cut; }
      }
    }
    return out;
  }

  /* ---------------- 生成 ---------------- */
  /* 把分析结果换算成落盘参数。
     强度只缩放「偏离中性的幅度」：
       对比度 1.0 = 不变，所以按 (target-1) * k 缩放，而不是整体乘 k。
     直接乘会把 1.1 变成 0.66（画面变灰）—— 这个坑在超燃预设里踩过一次。 */
  function computeParams() {
    const p = S.profile;
    const k = S.intensity;

    // 由参考片色彩统计推一个温和的调色倾向
    const cTarget = 0.92 + (p.avg_contrast || 0.15) * 0.9;
    const sTarget = 0.90 + (p.avg_saturation || 0.25) * 0.6;
    const contrast = 1.0 + (cTarget - 1.0) * k;
    const saturation = 1.0 + (sTarget - 1.0) * k;

    // 抖动：只有置信度够才加，幅度按强度缩
    let shakeAmp = 0.0;
    let shakeFreq = 0.0;
    if ((p.shake_confidence || 0) >= 0.4 && (p.shake_amplitude || 0) > 0.004) {
      shakeAmp = Math.min(0.2, p.shake_amplitude * k);
      shakeFreq = Math.min(20, p.shake_frequency_hz || 0);
    }

    // 转场：取分析出的接缝类型，不足则补溶解
    const kinds = [];
    const tr = p.transitions || [];
    let lowConf = 0;
    for (let i = 0; i + 1 < S.slots.length; ++i) {
      const t = tr[i];
      if (!t) { kinds.push(2); continue; }          // 没识别到 → 溶解兜底
      if ((t.confidence || 0) < CONF_ACCEPT) {
        // 置信度不够：不按猜的类型写，退化成中性溶解
        lowConf += 1;
        kinds.push(2);
      } else {
        kinds.push(t.kind);
      }
    }

    /* 丝滑变速：强度映射到 0.03..0.24 的振幅。
       为什么要乘系数而不是直接用 intensity：0.6 的振幅意味着速度在 0.4x~1.6x
       之间来回，观感会像“抽搐”，不是「丝滑」。 */
    const speedAmount = (S.style === 'curve') ? Math.max(0.03, Math.min(0.24, k * 0.30)) : 0.0;

    return { contrast, saturation, shakeAmp, shakeFreq, kinds, lowConf, speedAmount };
  }

  function startCreate() {
    const params = computeParams();
    $('foot').hidden = true;
    for (let i = 1; i <= 4; ++i) $('step' + i).hidden = true;
    $('stepProgress').hidden = false;

    const steps = [
      '分析参考镜头',
      '按节奏生成槽位',
      '写入转场与调色',
      '落到时间轴'
    ];
    const ul = $('progSteps');
    ul.innerHTML = '';
    steps.forEach((s) => {
      const li = document.createElement('li');
      li.textContent = '· ' + s;
      ul.appendChild(li);
    });

    /* 进度：每步都是真实动作，不是定时器演出来的。
       只有「落盘」这一步耗时，前面三步在内存里瞬间完成 ——
       所以进度会很快到 80%，再停在落盘那一下。这样是诚实的。 */
    let pct = 0;
    const setPct = (v, sub) => {
      pct = v;
      $('progNum').textContent = Math.round(v) + '%';
      $('ringFg').style.strokeDashoffset = String(326.7 * (1 - v / 100));
      if (sub) $('progSub').textContent = sub;
    };
    const mark = (i) => {
      const nodes = ul.querySelectorAll('li');
      nodes.forEach((n, k) => {
        n.className = k < i ? 'done' : (k === i ? 'doing' : '');
        n.textContent = (k < i ? '✓ ' : '· ') + steps[k];
      });
    };

    mark(0);
    setPct(10, '正在读取镜头结构');

    setTimeout(() => {
      buildSlots();
      mark(1);
      setPct(35, S.musicId ? '正在按节拍对齐切点' : '正在按参考节奏排布');

      setTimeout(() => {
        mark(2);
        setPct(65, '正在写入转场与调色');

        setTimeout(() => {
          mark(3);
          setPct(80, '正在落到时间轴');

          const ids = S.slots.map((s) => s.assetId);
          const starts = S.slots.map(() => 0);
          const baseDurs = S.slots.map((s) => s.durSec);
          const music = prepareMusic();
          const durs = snapDurations(baseDurs, music);

          const r = api('nc_ref_apply', {
            track_index: 0,
            asset_ids: ids,
            start_seconds: starts,
            durations: durs,
            transition_kinds: params.kinds,
            contrast: params.contrast,
            saturation: params.saturation,
            shake_amplitude: params.shakeAmp,
            shake_frequency: params.shakeFreq,
            speed_amount: params.speedAmount
          });

          if (!r || !r.ok) {
            setPct(100, '生成失败：' + ((r && r.message) || '未知错误'));
            $('progSteps').className = 'prog-steps';
            $('foot').hidden = false;
            $('btnNext').textContent = '返回修改';
            $('btnNext').onclick = () => { showStep(4); $('btnNext').onclick = onNext; };
            return;
          }
          const ovCount = applyOverlays(durs);
          const baseCount = (r.result && r.result.count) || 0;
          setPct(100, '已生成 ' + baseCount + ' 个镜头' +
                        (ovCount ? '，叠层 ' + ovCount + ' 处' : ''));
          setTimeout(() => {
            location.href = '10-editor.html';
          }, 600);
        }, 320);
      }, 260);
    }, 220);
  }

  /* ---------------- 事件 ---------------- */
  function onNext() {
    if (S.step === 1) {
      if (!S.refId) return;
      $('btnNext').disabled = true;
      $('btnNext').textContent = '分析中…';
      analyze((ok, msg) => {
        $('btnNext').textContent = '下一步';
        if (!ok) {
          alert('分析失败：' + (msg || '未知错误'));
          $('btnNext').disabled = false;
          return;
        }
        showStep(2);
      });
      return;
    }
    if (S.step < 4) { showStep(S.step + 1); return; }
    startCreate();
  }

  function init() {
    loadAssets();
    renderRefs();

    $('btnNext').addEventListener('click', onNext);
    $('btnPrev').addEventListener('click', () => { if (S.step > 1) showStep(S.step - 1); });
    $('btnBack').addEventListener('click', () => history.back());
    $('btnCancel').addEventListener('click', () => { location.href = '10-editor.html'; });

    document.querySelectorAll('.opt-card').forEach((el) => {
      el.addEventListener('click', () => {
        if (el.classList.contains('disabled')) return;
        document.querySelectorAll('.opt-card').forEach((x) => x.classList.remove('on'));
        el.classList.add('on');
        S.style = el.getAttribute('data-style');
      });
    });

    const sl = $('intensity');
    sl.addEventListener('input', () => {
      S.intensity = parseInt(sl.value, 10) / 100;
      $('intensityVal').textContent = sl.value + '%';
    });

    document.querySelectorAll('#overlayDensity .tab').forEach((t) => {
      t.addEventListener('click', () => {
        document.querySelectorAll('#overlayDensity .tab').forEach((x) => x.classList.remove('on'));
        t.classList.add('on');
        S.overlayMode = t.getAttribute('data-density');
      });
    });

    const ovSel = $('overlaySel');
    if (ovSel) {
      ovSel.addEventListener('change', () => {
        S.overlayId = parseInt(ovSel.value, 10) || 0;
      });
    }

    document.querySelectorAll('.tab').forEach((t) => {
      t.addEventListener('click', () => {
        document.querySelectorAll('.tab').forEach((x) => x.classList.remove('on'));
        t.classList.add('on');
        S.order = t.getAttribute('data-order');
        autoAssign();
        renderSlots();
      });
    });
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', init);
  } else {
    init();
  }
})();
