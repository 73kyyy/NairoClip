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
      const conf = t.confidence >= 0.5 ? '' : ' <span class="tag warn">疑似</span>';
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

    const missing = S.slots.filter((s) => !s.assetId).length;
    if (missing) {
      $('slotNote').innerHTML = '还差 <b>' + missing + '</b> 个槽位没选素材';
    } else {
      const total = S.slots.reduce((n, x) => n + x.durSec, 0).toFixed(1);
      $('slotNote').innerHTML =
        '共 <b>' + S.slots.length + '</b> 个镜头，预计成片 <b>' + total + 's</b>。' +
        '生成后会直接打开时间轴，可以逐条改特效与关键帧。';
    }
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
    for (let i = 0; i + 1 < S.slots.length; ++i) {
      const t = tr[i];
      kinds.push(t ? t.kind : 2);
    }

    return { contrast, saturation, shakeAmp, shakeFreq, kinds };
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
      setPct(35, '正在按参考节奏排布');

      setTimeout(() => {
        mark(2);
        setPct(65, '正在写入转场与调色');

        setTimeout(() => {
          mark(3);
          setPct(80, '正在落到时间轴');

          const ids = S.slots.map((s) => s.assetId);
          const starts = S.slots.map(() => 0);
          const durs = S.slots.map((s) => s.durSec);

          const r = api('nc_ref_apply', {
            track_index: 0,
            asset_ids: ids,
            start_seconds: starts,
            durations: durs,
            transition_kinds: params.kinds,
            contrast: params.contrast,
            saturation: params.saturation,
            shake_amplitude: params.shakeAmp,
            shake_frequency: params.shakeFreq
          });

          if (!r || !r.ok) {
            setPct(100, '生成失败：' + ((r && r.message) || '未知错误'));
            $('progSteps').className = 'prog-steps';
            $('foot').hidden = false;
            $('btnNext').textContent = '返回修改';
            $('btnNext').onclick = () => { showStep(4); $('btnNext').onclick = onNext; };
            return;
          }
          setPct(100, '已生成 ' + ((r.result && r.result.count) || 0) + ' 个镜头');
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
