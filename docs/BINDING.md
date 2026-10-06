# NairoClip 平台绑定层（C ABI）说明

## 1. 分层关系

```
┌────────────────────────────────────────────────┐
│ Android (Kotlin/JNI) │ iOS (Swift/C) │ 桌面 (Qt/C++) │
├────────────────────────────────────────────────┤
│            NairoClip C ABI（capi.h）             │
│   纯 C 接口、不透明句柄、UTF-8、稳定错误码          │
├────────────────────────────────────────────────┤
│              core/（C++17 静态库）               │
└────────────────────────────────────────────────┘
```

## 2. 为什么用 C ABI 而不是直接暴露 C++

- C++ ABI 在不同编译器/标准库版本间**不稳定**，JNI/Swift 直接调用 C++ 会踩版本坑；
- C ABI 是事实标准（JNI、Swift 互操作、Dart FFI、Python ctypes 均可无胶水调用）；
- 不透明句柄（NcEditor*）把实现细节完全封在库内，升级库不会破坏调用方。

## 3. 内存契约（必须遵守）

| 情形 | 规则 |
|---|---|
| 库返回的 `char*`（如 nc_project_save） | 调用方必须用 `nc_free_string` 释放 |
| 传入的 `const char*` 参数 | 调用返回后即可释放，库不长期持有 |
| 句柄 | `nc_editor_create` / `nc_editor_destroy` 严格配对 |
| 错误详情出参 | 同样用 `nc_free_string` 释放 |

## 4. 线程模型

- 单个 `NcEditor*` 句柄**不支持**多线程并发调用；
- 平台层把编辑操作串行化到单一工作线程（Android 建议 HandlerThread）；
- 只读查询（duration/track_count）可与写操作交错，但同一句柄上仍建议同一线程。

## 5. 版本策略

- `NC_ABI_VERSION` 只在不兼容变更时 +1；
- 平台层启动时用 `nc_abi_version()` 校验与预期一致，不一致时降级提示更新 App；
- 追加新函数不递增版本（向后兼容）。

## 6. 错误码映射

| C 错误码 | core::ErrorCode | 场景示例 |
|---|---|---|
| NC_ERROR_INVALID_ARGUMENT | InvalidArgument | 负起点、非法轨道下标 |
| NC_ERROR_NOT_FOUND | NotFound | 引用的实体不存在 |
| NC_ERROR_OVERLAP | Overlap | 片段重叠 |
| NC_ERROR_OUT_OF_RANGE | OutOfRange | 超出素材范围 |
| NC_ERROR_LOCKED | Locked | 轨道已锁定 |
| NC_ERROR_UNSUPPORTED | Unsupported | .nclip 版本过高 |

## 7. Android JNI 接入示例（规划）

```kotlin
// Kotlin 侧（示意，M2.x 实现）
object NairoClipNative {
    init { System.loadLibrary("nairoclip") }
    external fun abiVersion(): Int
    external fun editorCreate(): Long
    external fun editorDestroy(handle: Long)
    external fun projectSave(handle: Long): String
    // ...
}
```

JNI 层（C++）只做两件事：句柄转换（long ↔ NcEditor*）、字符串转换（jstring ↔ UTF-8 char*）。
业务逻辑全部留在 core/ 中，JNI 层代码量越小越安全。
