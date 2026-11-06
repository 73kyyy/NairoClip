/* NairoClip · 生成进度页逻辑（UI_BACKLOG P-06）
   ============================================================================
   ## 核心原则（文档硬要求）
   **进度必须来自真实任务进度，不允许用定时器假装。**

   竞品的「分析素材 / 匹配镜头 / 生成转场 / 完成合成」四步很可能是文案。
   我们不能那么做。所以本页的逻辑是：

     · 若操作**同步完成**（如 nc_template_apply_slots 已经在上一页返回）：
       不显示任何百分比 —— 直接显示「已完成」+ 内核返回的真实摘要。
       百分比没有意义，因为工作已经结束了。

     · 若存在**真实运行中的 AI 任务**（nc_ai_task_status 返回 running）：
       按 out_progress（内核维护的真实进度）画环，阶段文案取 out_stage_note。

   一句话：有真进度就画环，没有就不画 —— 绝不用 setInterval 编一个。
*/
'use strict';

(function () {
  const $ = (id) => document.getElementById(id);
  const Q = new URLSearchParams(location.search);
  const DONE = Q.get('done') === '1';
  const TPL_NAME = Q.get('name') || '模板';

  const S = {
    /** 真实进度 0..1；< 0 表示「没有真实进度可用」 */
    progress: -1,
    polling: 0,
    finished: false
  };

  function api(fn, args) {
    const f = window.NC_API && window.NC_API[fn];
    if (typeof f !== 'function') return { ok: false, message: '引擎接口缺失：' + fn };
    try { return f(args || {}); } catch (e) { return { ok: false, message: (e && e.message) || String(e) }; }
  }

  /* ---------------- 渲染 ---------------- */

  const CIRC = 2 * Math.PI * 52;

  function setProgress(p) {
    // p < 0 = 无真实进度：环退化成「不确定态」（只显示标签，不显示数字）
    const bar = $('bar');
    if (p < 0) {
      bar.style.strokeDashoffset = String(CIRC * 0.75);   // 画 1/4 弧示意"进行中"
      $('pct').textContent = '—';
      return;
    }
    const clamped = Math.max(0, Math.min(1, p));
    bar.style.strokeDashoffset = String(CIRC * (1 - clamped));
    $('pct').textContent = Math.round(clamped * 100) + '%';
  }

  /** 渲染步骤列表。步骤来自真实阶段记录，不是预设文案。 */
  function renderSteps(items) {
    const box = $('steps');
    box.innerHTML = '';
    items.forEach((it) => {
      const d = document.createElement('div');
      d.className = 'step ' + it.state;   // done / active / todo
      const icon = it.state === 'done' ? '<use href="#i-check"/>' : '';
      d.innerHTML = `<span class="dot">${icon ? `<svg class="i">${icon}</svg>` : ''}</span>` +
                    `<span>${escapeHtml(it.text)}</span>`;
      box.appendChild(d);
    });
  }

  /* HTML 转义。
     **用字符遍历而不是正则**：项目的前端检查器（check_mobile_frontend.py）
     不识别正则字面量里的方括号，会把 `/[&<>"']/g` 的 `[` 当成数组开头，
     报「未闭合的 [」。这不是检查器的缺陷要用绕过的方式硬顶，
     而是这种写法本身在纯手写 DOM 的项目里也不必要 —— 遍历更直白。 */
  const HTML_ESCAPES = { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' };
  function escapeHtml(s) {
    const str = String(s);
    let out = '';
    for (let i = 0; i < str.length; ++i) {
      const ch = str[i];
      out += HTML_ESCAPES[ch] || ch;
    }
    return out;
  }

  /* ---------------- 完成态 ---------------- */

  function showDone(summaryText) {
    S.finished = true;
    $('hd-title').textContent = '生成完成';
    $('lbl').textContent = '已完成';
    setProgress(1);
    // 完成时的步骤是**真实发生过的那一步**，不是编的四步
    renderSteps([
      { state: 'done', text: `套用模板「${TPL_NAME}」` },
      { state: 'done', text: '时间轴已生成' }
    ]);
    if (summaryText) {
      const box = $('summary');
      box.hidden = false;
      box.textContent = summaryText;
    }
    $('btn-done').hidden = false;
    $('btn-cancel').hidden = true;
    $('hint').hidden = false;
    $('hint').textContent = '回到编辑器后可以继续调整片段、音乐与特效。';
  }

  /* ---------------- 轮询真实任务 ---------------- */

  function pollOnce() {
    const r = api('nc_ai_task_status');
    if (!r || !r.ok || !r.result) return null;
    const st = r.result;
    /* 字段：feature / state / progress / stageNote / failureReason / retryable
       state 约定见 ai_task.h（0=idle 1=running 2=succeeded 3=failed 4=cancelled） */
    return st;
  }

  function startPolling() {
    const tick = () => {
      const st = pollOnce();
      if (st) {
        if (typeof st.progress === 'number' && st.state === 1) {
          // 有真实进度 → 画环
          S.progress = st.progress;
          setProgress(st.progress);
          $('lbl').textContent = st.stageNote || '处理中';
          renderSteps([
            { state: st.progress >= 0.99 ? 'done' : 'active',
              text: st.stageNote || '处理中' }
          ]);
        } else if (st.state === 2) {
          showDone(st.resultPayload || '');
          return;
        } else if (st.state === 3) {
          // 失败：如实显示原因，不吞掉
          $('hd-title').textContent = '生成失败';
          $('lbl').textContent = '失败';
          setProgress(0);
          renderSteps([{ state: 'active', text: st.failureReason || '未知原因' }]);
          $('btn-cancel').textContent = '返回';
          $('btn-cancel').hidden = false;
          return;
        }
      }
      S.polling = requestAnimationFrame(tick);
    };
    tick();
  }

  /* ---------------- 事件 ---------------- */

  $('btn-cancel').addEventListener('click', () => {
    cancelAnimationFrame(S.polling);
    history.back();
  });
  $('btn-done').addEventListener('click', () => {
    // 回编辑器（模板已套用到工程，用户继续编辑）
    location.replace('10-editor.html');
  });

  /* ---------------- 启动 ---------------- */

  if (DONE) {
    /* 上一页已同步完成套用 —— **不显示假进度**，直接展示结果。
       这里不查 nc_ai_task_status，因为模板套用不是 AI 任务
       （没有推理过程，查了也是 idle）。 */
    showDone('');
  } else {
    // 有真实任务时才轮询
    const st = pollOnce();
    if (st && st.state === 1) {
      startPolling();
    } else if (st && st.state === 2) {
      showDone(st.resultPayload || '');
    } else {
      /* 既没有运行中任务、也不是完成态 —— 说明这个页面被直接打开了。
         如实说明，不编造进度。 */
      $('lbl').textContent = '无进行中的任务';
      setProgress(-1);
      renderSteps([]);
      $('hint').hidden = false;
      $('hint').textContent = '当前没有正在运行的生成任务。请从「AI 模板剪辑」选择模板开始。';
      $('btn-cancel').textContent = '返回';
    }
  }
})();
