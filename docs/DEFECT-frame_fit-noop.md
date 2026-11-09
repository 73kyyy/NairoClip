# 缺陷单：`frame_fit` JNI 分支假成功

| 项目 | 内容 |
|---|---|
| 编号 | NC-DEFECT-FRAME-FIT-NOOP |
| 发现于 | T2.2 审计（`docs/NC-P2-T22-AUDIT.md` §5） |
| 严重级 | P1（用户操作无效果却显示成功） |
| 状态 | **待修**（T2.2 决策：本次不改） |
| 基线 | `main` @ `3031e8db` |

---

## 1. 现象

前端调用 `frame_fit` 时，JNI 直接返回成功，但**不执行任何操作**：

```cpp
// mobile/native/android/nc_jni.cpp
if (op == "frame_fit") {
    *out = Ok("");      // 不调用任何 C ABI
    return true;
}
```

## 2. 影响

- 用户在「画面适配」里切换 适应 / 拉伸 / 填充，画面**不会变化**。
- 桥返回 `{"ok":true}`，前端无法察觉失败，也不会提示。
- 属于任务书第十六章第 5 条：「只实现 UI，没有连接真实引擎」。

## 3. 已确认的事实（证据）

| 事实 | 证据 |
|---|---|
| 真实接口存在且完整 | `nc_set_frame_fit(NcEditor*, int32_t mode)` / `nc_get_frame_fit(const NcEditor*)`，见 `capi.h` |
| JNI 未调用它们 | 分支体内无任何 `nc_*` 调用（静态解析已确认） |
| 前端可达 | `op` 派发链可路由到该分支，JS 调用即命中 |
| 语义定义存在 | `FrameFit` 枚举（Fit / Fill / Crop）定义于 `frame_fit.h`，Sequence 上有 `frameFit` 字段 |

## 4. 未确认（不得推断）

- 该分支是**历史遗留占位**，还是曾经由前端纯 CSS 实现、后来迁移到引擎时漏接。
- 产品对「适应/拉伸/填充」三档的最终交互定义（是否需要同时影响导出）。

## 5. 修复要求（待单独授权）

1. 先确认产品语义，再改行为。
2. 接 `nc_set_frame_fit` 实现真实设置，并让 `nc_get_frame_fit` 可读回。
3. 补充「设置后 `Sequence::frameFit` 变化」与「预览帧随之改变」的测试。
4. 若短期无法实现，必须返回明确错误码（如 `NC_RENDER_UNSUPPORTED`），
   **禁止继续返回 `Ok`**。
5. 不得删除该 op（可能仍有调用方），只修正行为。

## 6. 关联

- T2.2 审计报告 §5
- 任务书第十六章第 5、8 条
