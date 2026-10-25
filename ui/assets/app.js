/* ============================================================================
   NairoClip · 原型交互脚本（Part 1 范围）
   交互严格对应开发文档：
   - §3.3⑤ 存储空间 / ⑦ 关于：手风琴原地展开，两个区块互斥
   - §2.2 草稿管理：管理 = 多选删除模式
   - §5? 本阶段不涉及剪辑页
   - 首页快捷入口：剪辑=主流程，其余 3 项为占位（点击提示"功能开发中"）
   - 草稿长按：[重命名][删除][分享]（Part 5 §25.3）
   ========================================================================== */
(function () {
  "use strict";

  const $ = (sel, root) => (root || document).querySelector(sel);
  const $$ = (sel, root) => Array.from((root || document).querySelectorAll(sel));

  /* ── Toast ─────────────────────────────────────────────────────────── */
  let toastTimer = null;
  window.ncToast = function (text) {
    let el = $(".toast");
    if (!el) {
      el = document.createElement("div");
      el.className = "toast hidden";
      const host = $(".phone") || document.body;
      host.appendChild(el);
    }
    el.textContent = text;
    el.classList.remove("hidden");
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => el.classList.add("hidden"), 1900);
  };

  /* ── 对话框 ────────────────────────────────────────────────────────── */
  window.ncDialog = function (id, open) {
    const el = document.getElementById(id);
    if (!el) return;
    el.classList.toggle("hidden", !open);
  };
  window.ncConfirm = function (opts) {
    const el = document.getElementById("dlg-generic");
    if (!el) return;
    $("#dlg-generic .dg-title").textContent = opts.title || "确认操作";
    $("#dlg-generic .dg-desc").textContent = opts.desc || "";
    const ok = $("#dlg-generic .js-ok");
    const cancel = $("#dlg-generic .js-cancel");
    ok.textContent = opts.ok || "确认";
    ok.className = "btn " + (opts.danger ? "btn-danger" : "btn-primary");
    cancel.textContent = opts.cancel || "取消";
    el.classList.remove("hidden");
    const close = () => el.classList.add("hidden");
    ok.onclick = () => { close(); if (opts.onOk) opts.onOk(); };
    cancel.onclick = () => { close(); };
  };

  /* ── 开关 ─────────────────────────────────────────────────────────── */
  function bindSwitches() {
    $$(".switch").forEach((sw) => {
      sw.addEventListener("click", () => {
        if (sw.classList.contains("disabled")) {
          window.ncToast("该项在所示状态下不可切换");
          return;
        }
        if (sw.classList.contains("static")) {
          window.ncToast(sw.dataset.toast || "该项在当前版本不可切换");
          return;
        }
        sw.classList.toggle("on");
        const msg = sw.dataset.toast;
        if (msg) window.ncToast(sw.classList.contains("on") ? msg + "已开启" : msg + "已关闭");
      });
    });
  }

  /* ── 手风琴（互斥） ────────────────────────────────────────────────── */
  function bindAccordions() {
    const items = $$("[data-accordion]");
    const sync = () => items.forEach((a) => {
      const head = $(".acc-head", a);
      if (head) head.setAttribute("aria-expanded", a.classList.contains("open") ? "true" : "false");
    });
    items.forEach((acc) => {
      const head = $(".acc-head", acc);
      if (!head) return;
      const toggle = () => {
        const willOpen = !acc.classList.contains("open");
        items.forEach((other) => other.classList.remove("open")); // 互斥：同时只允许一个展开
        if (willOpen) acc.classList.add("open");
        sync();
      };
      head.addEventListener("click", toggle);
      head.addEventListener("keydown", (e) => {
        if (e.key === "Enter" || e.key === " ") { e.preventDefault(); toggle(); }
      });
    });
    sync();
  }

  /* ── 草稿：选择模式 / 长按菜单 / 重命名 ───────────────────────────── */
  function bindDrafts() {
    const grid = $("#draft-grid");
    if (!grid) return;
    const manageBtn = $("#draft-manage");
    const manageBar = $("#draft-manage-bar");
    const countEl = $("#draft-selected-count");
    let managing = false;

    const cards = () => $$(".draft-card", grid);
    const picked = () => cards().filter((c) => c.classList.contains("picked"));

    function refresh() {
      const n = picked().length;
      if (countEl) countEl.textContent = String(n);
      const del = $("#draft-delete");
      if (del) del.disabled = n === 0;
      if (del) del.style.opacity = n === 0 ? "0.42" : "1";
    }

    function setMode(on) {
      managing = on;
      grid.classList.toggle("managing", on);
      cards().forEach((c) => c.classList.remove("picked"));
      if (manageBtn) manageBtn.textContent = on ? "取消" : "管理";
      if (manageBar) manageBar.classList.toggle("hidden", !on);
      refresh();
    }

    if (manageBtn) manageBtn.addEventListener("click", () => setMode(!managing));

    cards().forEach((card) => {
      let pressTimer = null;

      const start = () => {
        pressTimer = setTimeout(() => {
          pressTimer = null;
          if (managing) return;
          openDraftMenu(card);
        }, 520);
      };
      const cancel = () => { if (pressTimer) { clearTimeout(pressTimer); pressTimer = null; } };

      card.addEventListener("pointerdown", start);
      card.addEventListener("pointerup", cancel);
      card.addEventListener("pointerleave", cancel);
      card.addEventListener("pointermove", cancel);
      card.addEventListener("contextmenu", (e) => {
        e.preventDefault();
        if (!managing) openDraftMenu(card);
      });

      card.addEventListener("click", () => {
        if (managing) {
          card.classList.toggle("picked");
          refresh();
        } else {
          window.ncToast("进入剪辑页：" + (card.dataset.title || "草稿"));
        }
      });
    });

    const delBtn = $("#draft-delete");
    if (delBtn) {
      delBtn.addEventListener("click", () => {
        const n = picked().length;
        if (!n) return;
        window.ncConfirm({
          title: "删除 " + n + " 个草稿",
          desc: "删除后不可恢复，确认继续吗？",
          ok: "删除",
          danger: true,
          onOk: () => {
            picked().forEach((c) => c.remove());
            setMode(false);
            window.ncToast("已删除 " + n + " 个草稿");
            updateDraftCount();
          }
        });
      });
    }
  }

  function updateDraftCount() {
    const grid = $("#draft-grid");
    const titleCount = $("#draft-count");
    if (grid && titleCount) titleCount.textContent = String($$(".draft-card", grid).length);
  }

  /* 草稿长按菜单 */
  function openDraftMenu(card) {
    const sheet = $("#sheet-draft");
    if (!sheet) return;
    const title = card.dataset.title || "草稿";
    const titleEl = $("#sheet-draft .as-title");
    if (titleEl) titleEl.textContent = title;
    sheet.classList.remove("hidden");

    const close = () => sheet.classList.add("hidden");
    sheet.onclick = (e) => { if (e.target === sheet) close(); };

    const btnRename = $("#sheet-rename");
    const btnDelete = $("#sheet-delete");
    const btnShare = $("#sheet-share");
    const btnCancel = $("#sheet-cancel");

    if (btnRename) btnRename.onclick = () => {
      close();
      const nameEl = $(".draft-title", card);
      if (!nameEl) return;
      const input = document.createElement("input");
      input.className = "draft-title-input";
      input.value = nameEl.textContent;
      input.maxLength = 30;
      nameEl.replaceWith(input);
      input.focus();
      input.select();
      const commit = () => {
        const v = input.value.trim() || "未命名项目";
        const span = document.createElement("div");
        span.className = "draft-title";
        span.textContent = v;
        input.replaceWith(span);
        card.dataset.title = v;
        window.ncToast("草稿已重命名");
      };
      input.addEventListener("blur", commit);
      input.addEventListener("keydown", (e) => { if (e.key === "Enter") input.blur(); });
    };

    if (btnDelete) btnDelete.onclick = () => {
      close();
      window.ncConfirm({
        title: "删除草稿",
        desc: "「" + title + "」删除后不可恢复，确认继续吗？",
        ok: "删除",
        danger: true,
        onOk: () => {
          card.remove();
          window.ncToast("草稿已删除");
          updateDraftCount();
        }
      });
    };

    if (btnShare) btnShare.onclick = () => { close(); window.ncToast("已调起系统分享（原型占位）"); };
    if (btnCancel) btnCancel.onclick = close;
  }

  /* ── 占位入口 ─────────────────────────────────────────────────────── */
  function bindPlaceholders() {
    $$("[data-placeholder]").forEach((el) => {
      el.addEventListener("click", () => window.ncToast(el.dataset.placeholder));
    });
  }

  /* ── 搜索过滤（草稿 > 8 时） ──────────────────────────────────────── */
  function bindDraftSearch() {
    const input = $("#draft-search-input");
    if (!input) return;
    input.addEventListener("input", () => {
      const q = input.value.trim().toLowerCase();
      $$(".draft-card").forEach((c) => {
        const t = (c.dataset.title || "").toLowerCase();
        c.style.display = !q || t.indexOf(q) >= 0 ? "" : "none";
      });
    });
  }

  /* ── 语言页占位 ───────────────────────────────────────────────────── */
  function bindLanguage() {
    const lang = $("#row-language");
    if (lang) lang.addEventListener("click", () => window.ncDialog("dlg-language", true));
    const close = $("#dlg-language .js-close");
    if (close) close.addEventListener("click", () => window.ncDialog("dlg-language", false));
  }

  /* ── 初始化 ───────────────────────────────────────────────────────── */
  function init() {
    bindSwitches();
    bindAccordions();
    bindDrafts();
    bindPlaceholders();
    bindDraftSearch();
    bindLanguage();

    // 深链：供评审/截图直接展开对应手风琴（也便于真机联调定位）
    const hash = (location.hash || "").replace("#", "");
    if (hash === "storage" || hash === "about") {
      const acc = document.getElementById(hash === "storage" ? "acc-storage" : "acc-about");
      if (acc) {
        $$("[data-accordion]").forEach((a) => a.classList.remove("open"));
        acc.classList.add("open");
      }
    }

    // 深链：评审用状态（首页多选模式 / 草稿长按菜单 / 二次确认弹窗）
    if (hash === "manage") {
      const btn = $("#draft-manage");
      if (btn) {
        btn.click();
        $$(".draft-card").slice(0, 2).forEach((c) => c.classList.add("picked"));
        if (window.ncDraftRefresh) window.ncDraftRefresh();
      }
    }
    if (hash === "sheet") {
      const card = $(".draft-card");
      if (card) openDraftMenu(card);
    }
    if (hash === "logout-confirm") {
      const b = $("#btn-logout");
      if (b) b.click();
    }
    if (hash === "language") {
      window.ncDialog("dlg-language", true);
    }

    // 设置页：账号注销 / 退出登录 二次确认
    const logout = $("#btn-logout");
    if (logout) logout.addEventListener("click", () => {
      window.ncConfirm({
        title: "退出登录",
        desc: "确认退出当前账号？退出后本机草稿仍会保留。",
        ok: "退出登录",
        danger: true,
        onOk: () => window.ncToast("已退出登录（原型占位页）")
      });
    });

    const del = $("#row-delete-account");
    if (del) del.addEventListener("click", () => {
      window.ncConfirm({
        title: "账号注销",
        desc: "注销后数据不可恢复，确认继续吗？",
        ok: "继续注销",
        danger: true,
        onOk: () => window.ncDialog("dlg-delete-account", true)
      });
    });

    // 存储空间"一键清理缓存"
    const clean = $("#btn-clean-cache");
    if (clean) clean.addEventListener("click", () => {
      window.ncConfirm({
        title: "清理缓存",
        desc: "将清理 1.1GB 缓存文件，草稿与已导出作品不受影响。",
        ok: "立即清理",
        onOk: () => window.ncToast("缓存已清理，释放 1.1GB")
      });
    });

    // 检查更新
    const upd = $("#row-update");
    if (upd) upd.addEventListener("click", () => {
      if (upd.dataset.busy) return;
      upd.dataset.busy = "1";
      const v = $(".row-value", upd);
      const old = v.textContent;
      v.textContent = "检查中…";
      setTimeout(() => {
        v.textContent = "已是最新";
        delete upd.dataset.busy;
        window.ncToast("当前已是最新版本");
        setTimeout(() => { v.textContent = old === "检查中…" ? "已是最新" : old; }, 1600);
      }, 1100);
    });
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", init);
  } else {
    init();
  }
})();
