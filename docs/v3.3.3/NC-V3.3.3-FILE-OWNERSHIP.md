# NC-V3.3.3 文件与函数所有权矩阵（V3.3.3-FINAL-DEFECT-CLOSURE 修订）

> 按**函数/代码区域**判断冲突。本轮对 4 组冲突做了源码级核验并给出依据。

| 文件 | 函数/区域 | 批次 | 共享 | 允许并行 | 合并顺序 | **源码依据** |
|---|---|---|---|---|---|---|
| `mobile/native/android/nc_jni.cpp` | `DispatchEdit`(121 分支) | S1-2,S1-3,S1-7,S1-5c | ✅ | ❌ | 2→3→7→5c | 唯一入口 `:400` |
| `core/src/capi.cpp` | `nc_project_load:429` | S1-1,S1-3 | ✅ | ❌ | 1→3 | 同函数 |
| `core/src/capi.cpp` | `nc_import_asset:692` | S1-4A,S1-8A | ✅ | ❌（同批） | 同批 | 同函数 |
| `core/src/capi.cpp` | 其余 | S2-1,S2-2 | ✅ | ❌ | 串行 | — |
| `core/include/nairoclip/core/capi.h` | 声明 | S1-3,S1-7,S2-1 | ✅ | ❌ | 随 .cpp | ABI |
| `android/.../MainActivity.kt` | `:57`/`:181` | S1-1 | ✅ | ❌ | 1 | 崩溃处理器 |
| `android/.../MainActivity.kt` | 新增 `WebChromeClient` | S1-4A | ✅ | ❌ | 4A（S1-1 后） | — |
| `core/src/json.cpp` | `parseValue:155`（无 depth） | S1-8B | ❌ | ✅ | — | 无 depth 参数 |
| **`core/src/serialization.cpp`** | **`read*` 函数族（`readAsset:112`/`readClip:471` 等）** | **S1-3, S1-8C** | ✅ | ❌ | **S1-3 → S1-8C** | ★**同函数族，必须串行** |
| `core/src/recovery.cpp` | RecoveryStore | S1-3 | ❌ | ✅ | — | — |
| `core/src/contract/task.cpp` / `task.h` | TaskRegistry | S1-7,S2-4 | ✅ | ❌ | 7→2-4 | `task.h:76` 非线程安全 |
| `core/src/thread_pool.cpp` / `.h` | **`:62-63`（递减）/`:100`（初始化）/`:107`（等待）**；`thread_pool.h:17`（死锁自述） | **S1-7 前置** | ✅ | ❌ | 先修 | 空段不递减（递减在 `if(localJob && end>begin)` 守卫内） |
| `core/src/canvas.cpp` | `:198`/`:220` | S3-2 | ❌ | ✅ | — | — |
| **`core/src/preview.cpp`** | **`compositeLayer:830` / `renderAt:516`** | **S3-3, S4-5a/b/c** | ✅ | ❌ | **S3-3 → S4-5a** | ★**同文件，GPU 需 CPU 基线** |
| `core/src/export_executor.cpp` | 导出执行 | S1-5c,S4-3 | ✅ | ❌ | 串行 | — |
| **`core/src/media_ffmpeg.cpp`** | **解码路径（43 处 avcodec/swr）** | **S4-2a** | ❌ | ✅ | — | ★**解码专用** |
| **`core/src/media_ffmpeg_writer.cpp`** | **编码写入（32 处）** | **S4-1b** | ❌ | ✅ | — | ★**编码专用** |
| **`core/src/codec_policy.cpp`** | **`classifyEncoderLicense:185` / `encoderAllowed:334`（仅编码）** | **S4-1b** | ❌ | ✅ | — | ★**无解码逻辑** |
| `core/src/ai_*.cpp` | AI | S4-4A/B | ✅ | ⚠️ 分文件可并行 | — | — |
| **`android/app/src/main/AndroidManifest.xml`** | **`allowBackup:11` + `<application>` 属性** | **S1-8D** | ✅ | ❌ | **S1-8D → S5-1** | ★**属性节点** |
| **`android/app/src/main/AndroidManifest.xml`** | **`<activity>:19/31`** | **S5-1** | ✅ | ❌ | S5-1 | ★**组件节点** |
| `android/app/src/main/res/xml/backup_rules.xml` | 新建 | S1-8D | ❌ | ✅ | — | — |
| `android/app/src/main/res/xml/data_extraction_rules.xml` | 新建 | S1-8D | ❌ | ✅ | — | — |
| `android/app/build.gradle.kts` | 版本/ABI/签名 | S6-2,S6-4 | ✅ | ❌ | 串行 | — |
| `.github/workflows/ci.yml` | 全文件 | S1-5b,S1-6,S6-1 | ✅ | ❌ | 串行 | — |
| `.github/workflows/android-apk.yml` | 全文件 | S6-1,S6-4 | ✅ | ❌ | 串行 | — |
| `ffmpeg-build/build-android.sh` | configure/ldflags（`:138` decoder 列表、`:149` ldflags） | **S1-5a-C, S6-1, S4-2a** | ✅ | ❌ | **三方串行（顺序待按构建/交付内容确定）** | ★**M-1 补登 S4-2a** |
| `android/app/src/main/cpp/CMakeLists.txt` | `:71` | S6-1 | ❌ | ✅ | — | `target_link_options` |
| `mobile/assets/editor.js` | 导入/导出/UI | S1-4A,S1-5c,S5-* | ✅ | ❌ | 串行 | — |
| `mobile/pages/10-editor.html` | `:20` btn-export | S1-5c | ❌ | ✅ | — | — |
| `mobile/assets/tokens.css` | 26 变量 | S5-0A（只读） | ❌ | ✅ | — | — |
| `tests/*` | 各被测模块 | S1-2,S1-5b,S3-2 | ✅ | 按模块 | — | — |
| `tools/check_op_contract.py` | 新建 | S1-2 | ❌ | ✅ | — | — |
| `tools/test_inventory.json` | 新建 | S1-6 | ❌ | ✅ | — | — |

## 本轮 4 组冲突的核验结论

| 冲突 | 源码事实 | 结论 |
|---|---|---|
| **S1-3 vs S1-8C** | 同 `serialization.cpp`（1,467 行），均涉 `read*` 函数族 | **方案 A 串行**：S1-3 → S1-8C |
| **S4-2a vs S4-1b** | `media_ffmpeg.cpp`（解码）vs `media_ffmpeg_writer.cpp`+`codec_policy.cpp`（仅编码） | **方案 B 并行**：文件级隔离已验证 |
| **S3-3 vs S4-5** | 同 `preview.cpp`（`compositeLayer:830`） | **串行**：S3-3 → S4-5a |
| **S1-8D vs S5-1** | 同 Manifest，`allowBackup:11`（属性）vs `<activity>:19/31`（组件） | **串行**：S1-8D → S5-1（同文件合并冲突） |

### M-1 · `build-android.sh` 三方共享说明（★本轮补登）

| 项 | 内容 |
|---|---|
| 登记任务 | **S1-5a-C**（真实帧编码探针）、**S6-1**（16KB/构建）、**S4-2a**（HEVC 解码交付） |
| S4-2a 权限性质 | **条件性写入权** —— 仅在需要启用/调整 **`hevc_mediacodec` 包装器**时拥有该文件写入权 |
| **红线** | S4-2a **不得启用 FFmpeg 自有 `hevc` 软件解码器**（`--enable-decoder=hevc` 禁止） |
| 冲突性质 | **共享文件写入冲突**，**不是技术依赖** |
| 处理方式 | 同一文件修改**串行**或由**单一负责人整合** |
| 顺序 | **不得无证据固定任意顺序**；应结合各任务的构建与交付内容确定 |
| 禁止 | **不得把共享文件冲突自动转换为技术依赖边** |

**中央文件单一所有者 + 强制合并顺序**：`nc_jni.cpp`、`capi.cpp`、`MainActivity.kt`、`serialization.cpp`、`preview.cpp`、`ci.yml`、`build-android.sh`。
