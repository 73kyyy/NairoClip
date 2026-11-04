/* =============================================================================
   意见反馈（06-feedback.html）
   -----------------------------------------------------------------------------
   关于「接口」：现在**没有后端**。

   不伪造提交成功 —— 那是欺骗用户。给出两条真实可用的路径：
     ① 有平台桥（NCAndroid.hasFeedbackChannel）→ 走原生（邮件/系统分享）
     ② 没有 → 生成完整的反馈文本，一键复制，用户自己发到指定邮箱

   为什么不做「假装提交然后 toast 成功」：
   用户以为反馈送达了，实际石沉大海。这比不给入口更糟。

   将来接后端时，只需把 submitFeedback() 的实现换掉，UI 不用动。
   ============================================================================= */
(function () {
  'use strict';

  const $ = (id) => document.getElementById(id);

  /** 反馈接收邮箱（换成真实地址后即可用路径②）。 */
  const CONTACT_EMAIL = 'feedback@nairoclip.app';

  const state = { type: 'bug' };

  /* ---------- 类型切换 ---------- */
  document.querySelectorAll('.fb-type').forEach((b) => {
    b.addEventListener('click', () => {
      document.querySelectorAll('.fb-type').forEach((x) => x.classList.toggle('on', x === b));
      state.type = b.dataset.type;
    });
  });

  /* ---------- 设备信息（不采集素材/工程内容） ---------- */
  function deviceInfo() {
    const ua = navigator.userAgent || '';
    return [
      `类型: ${state.type}`,
      `系统: ${ua}`,
      `屏幕: ${window.screen.width}x${window.screen.height} @${window.devicePixelRatio || 1}x`,
      `语言: ${navigator.language || '-'}`,
      `时间: ${new Date().toISOString()}`
    ].join('\n');
  }

  function buildReport() {
    const text = ($('fb-text').value || '').trim();
    const contact = ($('fb-contact').value || '').trim();
    return [
      '=== NairoClip 意见反馈 ===',
      deviceInfo(),
      contact ? `联系方式: ${contact}` : '',
      '',
      '--- 描述 ---',
      text || '(未填写)'
    ].filter(Boolean).join('\n');
  }

  /* ---------- 复制 ---------- */
  async function copyText(t) {
    try {
      if (navigator.clipboard && navigator.clipboard.writeText) {
        await navigator.clipboard.writeText(t);
        return true;
      }
      const ta = document.createElement('textarea');
      ta.value = t;
      ta.style.position = 'fixed';
      ta.style.opacity = '0';
      document.body.appendChild(ta);
      ta.select();
      const ok = document.execCommand('copy');
      document.body.removeChild(ta);
      return ok;
    } catch (e) {
      return false;
    }
  }

  function toast(msg) {
    const d = document.createElement('div');
    d.className = 'toast';
    d.textContent = msg;
    document.body.appendChild(d);
    setTimeout(() => d.remove(), 2200);
  }

  /* ---------- 提交 ---------- */
  $('btn-submit').addEventListener('click', async () => {
    const text = ($('fb-text').value || '').trim();
    if (text.length < 5) {
      toast('请多写几个字，便于我们定位问题');
      $('fb-text').focus();
      return;
    }

    const report = buildReport();

    /* 路径①：平台层提供了反馈通道（原生邮件/分享） */
    if (window.NCAndroid && typeof window.NCAndroid.sendFeedback === 'function') {
      try {
        const ok = window.NCAndroid.sendFeedback(report, CONTACT_EMAIL);
        if (ok) { toast('已打开邮件应用，请确认发送'); return; }
      } catch (e) { /* 落到路径② */ }
    }

    /* 路径②：复制全文 + 告知真实去向 */
    const ok = await copyText(report);
    $('fb-note').textContent = ok
      ? `反馈内容已复制。请发送到 ${CONTACT_EMAIL}，我们会尽快回复。`
      : `请手动复制下方内容发送到 ${CONTACT_EMAIL}。`;
    if (ok) toast('已复制反馈内容');
  });

  $('btn-copy').addEventListener('click', async () => {
    const ok = await copyText(buildReport());
    toast(ok ? '已复制' : '复制失败，请长按选择');
  });

  $('btn-back').addEventListener('click', () => {
    if (history.length > 1) history.back();
    else location.href = '02-me.html';
  });
})();
