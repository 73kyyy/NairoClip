# NC-V3.3.3 验收与回退矩阵（V3.3.3-FINAL-DEFECT-CLOSURE 修订）

## 0. 状态定义（本轮统一，禁止混用「要求」与「结果」）
| 状态 | 含义 |
|---|---|
| 未开始 | 尚未执行 |
| 待执行 | 已明确要求，尚未执行 |
| 通过 | **有真实执行证据** |
| 失败 | 有真实失败证据 |
| N/A | 有明确理由不适用 |
| 不可验证 | 当前环境或证据不足 |
| 阻塞 | 依赖或审批未满足 |

> **铁律**：验收要求 ≠ 实际验收结果。规划要求真机测试 ≠ 真机已通过；要求 ASAN ≠ 已执行；要求 zipalign ≠ 已完成；要求 GPU 验证 ≠ 已核验。

## 1. 验收矩阵（**全部为「待执行」或「不可验证」，无一项可标「通过」**）

| 批次 | 静态 | 编译 | 单元 | 集成 | 压力 | ASAN/UBSAN | 设备 | 真机 | 失败条件 | 回退 |
|---|---|---|---|---|---|---|---|---|---|---|
| S1-1 | 待执行 | 待执行 | 待执行 | 待执行 | N/A | 待执行 | N/A | N/A | UAF 仍现 | revert |
| S1-2 | 待执行 | 待执行 | 待执行 | 待执行 | N/A | 待执行 | N/A | N/A | 51 项有缺失 | revert（表驱动可开关） |
| S1-3 | 待执行 | 待执行 | 待执行 | 待执行 | N/A | 待执行 | N/A | N/A | 重开丢字段 | revert |
| S1-4A | 待执行 | 待执行 | 待执行 | 待执行 | N/A | 待执行 | N/A | **不可验证** | 导入不可预览 | revert |
| S1-8A | 待执行 | 待执行 | 待执行 | 待执行 | N/A | 待执行 | N/A | N/A | 假路径通过 | revert |
| S1-8B | 待执行 | 待执行 | 待执行 | 待执行 | 待执行 | 待执行 | N/A | N/A | 栈溢出 | revert |
| S1-8C | 待执行 | 待执行 | 待执行 | 待执行 | 待执行 | 待执行 | N/A | N/A | 半加载污染 | revert |
| S1-8D | 待执行 | 待执行 | N/A | N/A | N/A | N/A | **不可验证** | **不可验证** | 备份含密钥 | revert |
| S1-5a-A | 待执行 | N/A | N/A | N/A | N/A | N/A | N/A | N/A | 无法出清单 | N/A |
| S1-5a-B | N/A | N/A | N/A | N/A | N/A | N/A | **不可验证** | N/A | 无设备能力 | N/A |
| S1-5a-C | 待执行 | 待执行 | N/A | N/A | N/A | N/A | **不可验证** | N/A | 无输出文件 | N/A |
| S1-5b | 待执行 | 待执行 | 待执行 | N/A | 待执行 | 待执行 | N/A | N/A | 仍有崩溃 | 恢复排除+记录 |
| S1-5c | 待执行 | 待执行 | 待执行 | 待执行 | N/A | 待执行 | N/A | **不可验证** | 输出不可播放 | revert |
| S1-6 | 待执行 | 待执行 | 待执行 | N/A | N/A | N/A | N/A | N/A | 用例静默减少 | revert |
| S1-7 | 待执行 | 待执行 | 待执行 | 待执行 | 待执行 | 待执行（TSan **不可验证**） | N/A | N/A | 死锁/竞态 | revert |
| S2-1…4 | 待执行 | 待执行 | 待执行 | 待执行 | N/A | 待执行 | N/A | N/A | 绕过残留 | 逐条 revert |
| S3-1…3 | 待执行 | 待执行 | 待执行 | 待执行 | N/A | 待执行 | N/A | N/A | Golden 不符 | revert（Golden 晚于修复） |
| S4-* | 待执行 | 待执行 | 待执行 | 待执行 | 待执行 | 待执行 | **不可验证** | **不可验证** | 无真实输出 | revert |
| S5-* | 待执行 | 待执行 | 待执行 | 待执行 | 待执行 | N/A | **不可验证** | **不可验证** | 视觉回归失败 | 保留旧页面 |
| S6-* | 待执行 | 待执行 | N/A | 待执行 | 待执行 | N/A | **不可验证** | **不可验证** | 对齐/签名失败 | 保留旧构建分支 |

> **本轮修正**：上一版用 ✅ 标记，**实际均未执行** → 全部改为「待执行 / 不可验证 / N/A」。**无一项有真实执行证据**。

## 2. 测试命令对照表（★本轮新增，§4.2 要求）

> 依据仓库实际存在的脚本与任务名。**未存在的标「待建立」，不伪造**。

| 批次 | 测试类型 | 实际命令 | 执行目录 | 前置 | 预期 | 失败处理 |
|---|---|---|---|---|---|---|
| 全部 C++ | 静态检查 | `python3 tools/check_*.py`（9 个检查器） | 仓库根 | 无 | 0 退出 | 阻断 |
| S1-* | C++ 编译 | `cmake -S . -B build && cmake --build build -j` | 根 | cmake/ninja | 0 退出 | 阻断 |
| S5-* | Kotlin 编译 | `./gradlew :app:compileDebugKotlin` | `android/` | JDK17 | 0 退出 | 阻断 |
| S1-5c/S6-* | Android 构建 | `./gradlew :app:assembleRelease` | `android/` | NDK r27d | APK 产出 | 阻断 |
| 全部 | 单元+集成 | `./build/tests/nairoclip_tests` | 根 | 已编译 | 1,806 用例通过 | 阻断 |
| 全部 | 分片 | `NC_TEST_SHARDS=16 NC_TEST_SHARD=<n> ./build/tests/nairoclip_tests` | 根 | 已编译 | 分片全绿 | 阻断 |
| S1-5b | 堆破坏复现 | `MALLOC_PERTURB_=165 MALLOC_CHECK_=3 NC_TEST_CHECK_HEAP=1 NC_TEST_SKIP=267 NC_TEST_MAX=268 ./build/tests/nairoclip_tests` | 根 | 已编译 | 0 崩溃 | 记录 |
| S1-7 | 并发/死锁 | **待建立**（需新增并发测试入口） | — | TSan | — | — |
| 全部 | ASAN | CI `asan-diagnose` job（`ci.yml:495`，当前 `continue-on-error`） | CI | — | 0 报错 | **当前不阻断** |
| 全部 | UBSAN | CI `ub-diagnose` job（`ci.yml:603`，同上） | CI | — | 0 报错 | **当前不阻断** |
| S6-1 | ELF 检查 | `readelf -lW <so> | grep LOAD`（逐 6 个 .so） | 主机 | readelf | `p_align≥16384` | 阻断 |
| S6-1 | PAGE_SIZE | `getconf PAGE_SIZE`（真机） | 设备 | 设备 | 输出值 | — |
| S6-1 | zipalign 执行 | `zipalign -P 16 -v 4 in.apk out.apk` | 主机 | Build-Tools | 0 退出 | 阻断 |
| S6-1 | 签名 | `apksigner sign --ks <ks> out.apk` | 主机 | Build-Tools | 0 退出 | 阻断 |
| S6-1 | 签名验证 | `apksigner verify --verbose out.apk` | 主机 | Build-Tools | Verified | 阻断 |
| S6-1 | zipalign 复验 | `zipalign -c -P 16 -v 4 out.apk` | 主机 | Build-Tools | 校验通过 | 阻断 |
| S6-5 | 真机安装 | `adb install -r app-release.apk` | 设备 | 设备 | Success | 记录 |
| S6-5 | 真机启动 | `adb shell am start -W -n com.nairoclip.app/.MainActivity` | 设备 | 设备 | 无 FATAL | 记录 |
| S6-5 | 性能 | `adb shell dumpsys gfxinfo com.nairoclip.app` | 设备 | 设备 | 阈值待标定 | 记录 |
| S5-* | UI 视觉回归 | **待建立**（需新增截图比对入口） | — | — | — | — |

## 3. 回退专项
- **工程格式**：向后兼容为硬指标；迁移失败**不破坏原工程**；回退后须验证旧版可读。
- **签名/版本**：变更后须验证「可覆盖升级」。
- **原生化**：每单元保留旧页面切换开关，验收通过后才可删。

## 4. N/A 理由
静态检查不适用设备；探针类不适用单元测试；构建/CI 类不适用真机；S1-5a-A 为只读不适用编译。
