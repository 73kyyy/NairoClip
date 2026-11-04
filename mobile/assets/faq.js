/* =============================================================================
   常见问题（07-faq.html）
   -----------------------------------------------------------------------------
   纯前端数据，不需要接口。内容必须**真实**：
   不写"功能开发中"当作 FAQ —— 那不是常见问题，是我们的待办。
   ============================================================================= */
(function () {
  'use strict';

  const $ = (id) => document.getElementById(id);

  const FAQ = [
    {
      q: '导入的视频没有画面 / 提示引擎未连接',
      a: '当前构建若不包含 FFmpeg 解码器，导入真实视频会失败。\n' +
         '这在开发构建里可能出现；正式安装包应完整支持。\n' +
         '如果正式版仍如此，请通过「意见反馈」告知设备型号。'
    },
    {
      q: '预览画面比我导出的成片模糊',
      a: '这是**预期行为**。预览会按屏幕实际像素降采样渲染（最低到 720p 短边），\n' +
         '以换取拖动和播放的流畅度。导出永远是全分辨率、全质量。\n' +
         '暂停时预览会自动回到最高画质。'
    },
    {
      q: '为什么拖动时间轴时画面变模糊了',
      a: '拖动期间会自动降低渲染分辨率（代理渲染），松手后恢复。\n' +
         '屏幕上能显示的像素就那么多，降采样不影响你最终看到的成片质量。'
    },
    {
      q: '工程数据会上传吗',
      a: '不会。剪辑、AI 推理、导出全部在本机完成。\n' +
         '只有你主动使用联网功能（如模型下载）时才会用到网络。'
    },
    {
      q: 'AI 功能显示「模型未下载」',
      a: 'AI 模型体积较大（几十 MB 到几百 MB），不随安装包分发。\n' +
         '首次使用某个 AI 功能时会提示下载，下载后可离线使用。'
    },
    {
      q: '删除的素材还能恢复吗',
      a: '素材删除不可撤销。但工程有自动快照（约每 30 秒一次，保留最近 5 个），\n' +
         '如果误删后还没做太多操作，可能可以从快照恢复。'
    },
    {
      q: '导出中断 / 切到后台导出失败',
      a: '请保持应用在前台直到导出完成。\n' +
         '长时间导出建议连接电源，避免系统因省电限制后台任务。'
    },
    {
      q: '素材在相册里有，但应用里找不到',
      a: '如果素材是导入后被移动或重命名，工程会提示「素材离线」。\n' +
         '应用内提供了「重新链接」功能，可以指回新位置。'
    }
  ];

  function render() {
    const box = $('faq-list');
    box.innerHTML = '';
    FAQ.forEach((item, i) => {
      const wrap = document.createElement('div');
      wrap.className = 'faq-item';

      const btn = document.createElement('button');
      btn.className = 'faq-q';
      btn.type = 'button';
      btn.innerHTML = `<span>${escapeHtml(item.q)}</span><i class="faq-arrow">›</i>`;

      const body = document.createElement('div');
      body.className = 'faq-a';
      body.hidden = true;
      body.textContent = item.a;

      btn.addEventListener('click', () => {
        const open = !body.hidden;
        body.hidden = open;
        wrap.classList.toggle('open', !open);
      });

      wrap.appendChild(btn);
      wrap.appendChild(body);
      box.appendChild(wrap);

      // 第一项默认展开，避免用户面对一屏折叠项
      if (i === 0) {
        body.hidden = false;
        wrap.classList.add('open');
      }
    });
  }

  /* 转义 HTML。
     不用正则字符类（/[&<>"']/）—— 仓库的前端检查器是简化的 JS 扫描器，
     不认正则字面量，会把里面的引号当成字符串开头，导致误报「未闭合」。
     用 replace 的字符映射表写法，既避开误报，也比正则更直观。 */
  const HTML_ESCAPES = {
    '&': '&amp;',
    '<': '&lt;',
    '>': '&gt;',
    '"': '&quot;',
    "'": '&#39;'
  };
  function escapeHtml(s) {
    return String(s).split('').map((ch) => HTML_ESCAPES[ch] || ch).join('');
  }

  $('btn-back').addEventListener('click', () => {
    if (history.length > 1) history.back();
    else location.href = '02-me.html';
  });

  render();
})();
