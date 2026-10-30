# 开发环境约定：本地零执行，一切验证走 CI

> 基准：2026-10-30。本文是硬约束，不是建议。

## 一、规则

**开发容器（本地）只允许做四件事：**

1. 读代码（`read` / `grep` / `sed` / `ls` / `git log` / `git diff`）
2. 改文件（编辑源码、文档、配置）
3. `git add` / `commit` / `push`
4. 查 CI 结果（`gh run list` / `gh run view`）

**其余一切执行，一律在 GitHub Actions 上跑：**

| 类别 | 例子 | 本地 | CI |
|---|---|---|---|
| 编译 | `cmake --build`、NDK 交叉编译 | 不许 | 必须 |
| 测试 | `nairoclip_tests`、ASAN、UBSan、valgrind | 不许 | 必须 |
| 素材生成 | `gen_grid_previews.py`、`gen_*`、`bake_*` | 不许 | 必须 |
| 静态版打包 | `make_static.py`、`make_pages_static.py` | 不许 | 必须 |
| 截图 / 像素验证 | `shotwait.py`、WebKit 截图对比 | 不许 | 必须（headless） |
| 变异测试 | 批量改源码 → 重编 → 重跑 | 不许 | 必须 |

**原因**：开发容器是 aarch64 手机环境。一次全量测试约 9 分钟满载，
批量图像生成会长时占满 CPU，都会导致设备持续发热；
同时执行过程会占住交互会话。CI runner 是 x64、免费、不限量、可并行。

## 二、为什么这也是**质量**要求，不只是省事

本地不跑，意味着「本地绿」这个借口消失了。
唯一的事实来源是 CI 输出，任何人都能复看同一次 run。
这反过来强制了两件事：

1. 验证必须脚本化（不能让「我在本地手动点过」成为证据）
2. 验证必须自动化（新功能必须同时加 CI 步骤，否则等于没有验证）

## 三、现有 CI 覆盖面

| 工作流 | 触发 | 覆盖 |
|---|---|---|
| `ci.yml` → `build-and-test` | push / PR / 手动 | FFmpeg+ORT 全量构建 + 全量测试 + 用例数下限对账（防漏编） |
| `ci.yml` → `asan-diagnose` | 同上 | ASAN 全量（堆破坏定位，诊断通道，不阻塞） |
| `ci.yml` → `valgrind` | 同上 | valgrind memcheck 跑到指定用例 |
| `ci.yml` → `ubsan` | 同上 | UBSan + auto-var-init 跑崩溃用例 |
| `android-apk.yml` | push（core/android/mobile/static 变动） | NDK 交叉编译 → APK → 产物自检（必须是 live-engine 页面版，不得混入静态快照） |
| `android-build.yml` | push | Android 构建 |
| `gen-assets.yml` | push（生成器/底图/规格表变动）/ 手动 | 宫格缩略图生成 + 与 `l3-spec.js` 的 id 对齐校验 + 自动回提交 |
| `fetch-assets.yml` | 手动 | 外部大文件中继（NDK 等） |

### 关键：`gen-assets.yml` 的「对齐校验」为什么必须有

生成器把清单写死在 Python 里，UI 的清单写在 `l3-spec.js` 里。
两边一旦漂移，宫格图就会**张冠李戴**（点「电影」显示「黑白」的图），
而这种错误在界面上完全看不出来 —— 属于「看起来有功能、实际是错的」。
所以工作流在生成**之前**先用机器比对四类清单的 id 集合，
不一致直接失败，不产出任何产物。

## 四、新增需要执行的脚本时，怎么做

不要在本机跑。按这个顺序：

1. 脚本写进仓库（`mobile/tools/`、`tools/`、`tests/`）
2. 加一个 `workflow_dispatch` 可手动触发的 workflow（或并进已有的）
3. push 到 main
4. 在 Actions 页面 / `gh run watch` 看结果
5. 需要产物回仓库的，让 workflow 自己 `git commit && git push`（提交信息带 `[skip ci]`）

## 五、例外（仅限「不消耗算力」的操作）

允许本地执行的只有纯读取类的辅助命令，例如：

```bash
python3 -c "import json; print(len(json.load(open('x.json'))))"   # 读文件统计
grep -rn 'pattern' src/                                           # 检索
git log --oneline -20                                             # 历史
```

判定标准：**单次执行是否可能超过数秒、是否触发编译/解码/图像处理批次**。
超过就不许在本地跑。
