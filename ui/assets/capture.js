/* ============================================================================
   截图/评审模式（仅影响设计稿承载页，不影响 App 组件本身）
   用法：page.html?capture=viewport   固定 390×844 可视区
         page.html?capture=full       输出整页长图（内部滚动区展开）
   ========================================================================== */
(function () {
  "use strict";
  var mode = (location.search.match(/[?&]capture=([a-z]+)/) || [])[1];
  if (!mode) return;
  document.documentElement.classList.add("nc-capture");
  document.body.classList.add("capture");
  if (mode === "full") document.body.classList.add("capture-full");
})();
