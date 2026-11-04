/* =============================================================================
   崩溃日志查看（08-crashlog.html）
   -----------------------------------------------------------------------------
   数据来源：C++ 崩溃处理器写在 filesDir/crash.log 的文件。
   从平台桥读取（NCAndroid.readCrashLog）。

   日志里是**地址**不是函数名。要还原成源码行号需要未 strip 的 .so：
     llvm-symbolizer --obj=libnairoclip.so 0x1a2f3c
   所以构建配置里设了 debugSymbolLevel = "FULL"（见 build.gradle.kts）。

   为什么不做「自动上传」：
   崩溃日志可能含路径等信息。用户明确点「导出」才发出去，
   比默认上传更尊重隐私，也更容易过审核。
   ============================================================================= */
(function () {
  'use strict';

  const $ = (id) => document.getElementById(id);

  function toast(msg) {
    const d = document.createElement('div');
    d.className = 'toast';
    d.textContent = msg;
    document.body.appendChild(d);
    setTimeout(() => d.remove(), 2200);
  }

  /** 读日志：优先原生桥，回退到「没有」。 */
  function readLog() {
    try {
      if (window.NCAndroid && typeof window.NCAndroid.readCrashLog === 'function') {
        return window.NCAndroid.readCrashLog() || '';
      }
    } catch (e) { /* 落到空 */ }
    return '';
  }

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

  function paint() {
    const text = readLog();
    const box = $('cl-content');

    if (!text.trim()) {
      box.innerHTML = '<div class="cl-empty">' +
        '没有崩溃记录。<br>应用最近没有异常退出。</div>';
      return;
    }

    const meta = document.createElement('div');
    meta.className = 'cl-meta';
    meta.textContent = `共 ${text.length} 字符 · 每行一个栈帧`;

    const pre = document.createElement('div');
    pre.className = 'cl-body';
    pre.textContent = text;

    const acts = document.createElement('div');
    acts.className = 'cl-actions';

    const copyBtn = document.createElement('button');
    copyBtn.textContent = '复制全文';
    copyBtn.addEventListener('click', async () => {
      const ok = await copyText(text);
      toast(ok ? '已复制' : '复制失败，请长按选择');
    });

    const clearBtn = document.createElement('button');
    clearBtn.textContent = '清除日志';
    clearBtn.addEventListener('click', () => {
      try {
        if (window.NCAndroid && typeof window.NCAndroid.clearCrashLog === 'function') {
          window.NCAndroid.clearCrashLog();
          toast('已清除');
          paint();
          return;
        }
      } catch (e) { /* 忽略 */ }
      toast('无法清除');
    });

    acts.appendChild(copyBtn);
    acts.appendChild(clearBtn);

    const hint = document.createElement('p');
    hint.className = 'cl-hint';
    hint.textContent =
      '日志里的地址需要配合构建时的符号表才能还原成函数名。' +
      '如果你的设备上有未 strip 的 libnairoclip.so，可用 llvm-symbolizer 还原。';

    box.appendChild(meta);
    box.appendChild(pre);
    box.appendChild(acts);
    box.appendChild(hint);
  }

  $('btn-back').addEventListener('click', () => {
    if (history.length > 1) history.back();
    else location.href = '02-me.html';
  });

  paint();
})();
