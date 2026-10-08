# NairoClip 架构（Core / M0.1）

## 1. 分层总览

```
┌─────────────────────────────────────────────┐
│ 平台层  Android(Kotlin) / iOS(Swift) / 桌面(Qt) │
├─────────────────────────────────────────────┤
│ 绑定层  JNI / C++ 互操作 / C ABI（后续里程碑）    │
├─────────────────────────────────────────────┤
│ 核心层  core/（本仓库当前内容）                  │
│   rational → time → model → command → editor   │
├─────────────────────────────────────────────┤
│ 媒体层  FFmpeg 解码 / 预览 / 导出（M1.0）        │
│ AI 层   本地推理（后续里程碑）                    │
└─────────────────────────────────────────────┘
```

## 2. 时间模型（为什么不用浮点秒）

- 29.97 / 23.976 等非整数帧率在浮点下会累计误差；
- 所有时间以 `Rational`（int64 分子/分母，秒）精确表示，约分到最简；
- 帧号换算：`timeAtFrame = frame / fps`、`frameAtTime = floor(t × fps)`；
- 关键乘法使用 128 位中间量（GCC/Clang `__int128`；其他平台回退 64 位）。

## 3. 数据模型与不变量

- `Project { assets, sequences, ids }` → `Sequence { tracks, markers }` → `Track { clips }` → `Clip`；
- `Track.clips` 不变量：按 `start` 升序、互不重叠、单片合法；
- 片段派生量：`end()`、`sourceOut() = sourceIn + duration × speed`；
- 变速语义：保持源区间不变，时间轴时长 = 源时长 / 新速度。

## 4. 编辑命令与撤销栈（Command 模式）

- 所有修改通过 `Command::apply()/revert()`；由 `UndoStack` 驱动；
- 游标模型 `[0, cursor)` 已应用；新命令截断 redo 分支；
- 命令自带快照（删除类命令持有被删对象完整拷贝），支持无限次 undo/redo；
- 命令名称用于历史面板展示（「添加轨道」「分割片段」等）。

## 5. 编辑器门面（TimelineEditor）

- 素材导入 / 轨道增删 / 片段增删改 / 分割 / 裁剪 / 变速 / 撤销重做；
- 所有写操作「先校验、后提交」，失败不产生副作用、不入撤销栈；
- 校验规则：片段合法、不与其他片段重叠、不超出素材源范围、轨道未锁定。

## 6. 测试与 CI

- 自研零依赖测试框架（tests/test_framework.h），用例自动注册；
- 覆盖：有理数运算、帧号/时间换算、模型不变量、编辑器全部操作、撤销/重做、边界拒绝；
- GitHub Actions：ubuntu / macos / windows 三平台 Release 构建 + ctest。

## 7. 里程碑（截至 M2.12，测试 401 用例全绿）

### Core 阶段（M0.x）
- M0.1 ✅ Core 地基：时间系统 / 数据模型 / 撤销栈 / 编辑器门面 / 测试 / CI
- M0.2 ✅ 关键帧缓动 / 速度曲线 / 转场模型（8 种）/ 跨轨道移动
- M0.3 ✅ .nclip 工程序列化 + 崩溃恢复（自动保存/环形快照）
- M0.4 ✅ 片段动画（关键帧接入 Clip）+ 导出计划编译器（IR）
- M0.5 ✅ C ABI 绑定层 + 随机编辑序列往返测试
- M0.6 ✅ 字幕模型（SubtitleTrack）+ 样式 + 序列化集成
- M0.7 ✅ 转场全链路（存储/命令/门面/序列化）
- M0.8 ✅ C ABI 扩展（字幕/转场/片段查询）
- M0.9–M0.17 ✅ 关键帧编辑命令 / 速度曲线接入 / 压力测试 / nclip_tool / 媒体 I/O 抽象 / 导出执行器 / 波纹编辑 / 撤销事务 / 轨道属性
- M0.38–M0.43 ✅ 边界测试 / C ABI 自动剪辑 / 场景检测 / 手势合并 / 端到端工作流

### 媒体层（M1.x）
- M1.0 ✅ FFmpeg 真实解码读取器（探测/解码/seek/重采样）
- M1.1 ✅ FFmpeg 媒体写入器（H.264+AAC 真实编码 + RGBA→YUV + FIFO 整帧）
- M1.2 ✅ 帧率探测策略修复（r_frame_rate 优先，防容器量化漂移）
- M1.3 ✅ 导出运行器 + 解码器 EOF 冲刷修复（尾帧丢失）
- M1.4 ✅ 导出管线接入真实解码（端到端像素验证）
- M1.5 ✅ 音频混音接入导出（块式混音重写 + AAC 修复）+ 增益/淡入淡出验证
- M1.7 ✅ 字幕渲染项接入导出帧
- M1.8 ✅ 预览器真实解码验证
- M1.9 ✅ 缩略图生成器
- M1.10–M1.12 ✅ 动画透明度 / 速度曲线 / 序列化往返
- M1.13–M1.15 ✅ 多视频轨合成（遮挡式+alpha 混合）/ nclip_tool export CLI

### 合规与集成（M2.x）
- M2.1–M2.2 ✅ AI 内容标识合规组件（深度合成规定第十七条）+ 接入导出管线
- M2.3 ✅ 音频波形生成器
- M2.4 ✅ 参考仿剪全真实链路端到端验证（核心卖点）
- M2.5 ✅ C ABI 导出接口 nc_export_run
- M2.6 ✅ 导出压力测试（性能基线）
- M2.7 ✅ SRT/WebVTT 字幕交换（解析容错 + 导出排序 + 序号行误判修复）
- M2.8 ✅ nclip_tool 字幕命令（SRT 导出/导入工程）
- M2.9 ✅ 导出画幅适配模式（fit 信箱 / fill 铺满裁切 / crop 居中1:1；含序列化、预览、CLI `--fit`、C ABI）
- M2.10 ✅ 节拍检测与音乐卡点（多频带 onset + 精细周期扫描 + 梳状对齐抗倍频歧义；节拍网格/吸附/卡片段规划；applyBeatSync 事务化落盘；CLI `beats`）
- M2.11 ✅ 音频自动闪避（本地 RMS VAD + hold 合并 + attack/release 包络；轨道级设置命令与序列化；混音器真实应用压低增益；duckRegions 供时间轴绘制；C ABI + CLI 展示）
- M2.12 ✅ 复合片段/嵌套序列（文档第 3 部分「嵌套序列/复合片段」）：
  · Clip 增加 nestedSequence/compoundName 与 isCompound()（asset 与嵌套二选一）
  · CreateCompoundClipCommand：连续片段打组为嵌套序列 + 原位置留引用片段
  · UnwrapCompoundClipCommand：还原内部片段并删除嵌套序列
  · 编辑器门面（预分配 ID 保证 redo 确定性）+ 序列化 + 引用完整性校验（悬空嵌套序列拒绝载入）
  · C ABI：nc_create_compound_clip / nc_unwrap_compound_clip / nc_clip_is_compound

### 待办
- M3.0 ⬜ Android 平台层（JNI + MediaCodec 封装，需 NDK 环境）
- 后续 ⬜ 本地 AI 推理（模型分发走国内云）+ 平台壳（Kotlin/Qt）

## 8. 兼容性预留
- 绑定层将在 core 之上提供稳定 C ABI，JNI / Swift / Qt 均经该层接入；
- 核心不依赖任何平台 API、不依赖第三方库，可交叉编译至移动端与桌面端。
