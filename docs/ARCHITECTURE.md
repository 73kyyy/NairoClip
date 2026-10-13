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

## 7. 里程碑（截至 M2.29，测试 918 用例全绿）

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
  · 导出计划编译期展开（递归 + 循环引用阻断）：复合片段内容真实参与渲染
- M2.13 ✅ 智能重构图（文档第 7 部分「AI工具 → 智能重构图：横转竖/竖转横/跟踪灵敏度」）：
  · computeFrameSaliency：从 RGBA8 帧提取主体重心（中心/上部先验 + 运动能量）
  · planReframe：时间平滑 + 平移速度限制 + 源内边界钳制 → 逐帧裁剪窗口轨迹
  · reframeRenderFrameAt：逐帧裁剪 + 双线性缩放（复用 frame_fit 缩放器）
  · 接入导出计划/执行器：ExportPlan::reframe 非空时优先于 frameFit 走跟踪裁剪
  · 序列化持久化（默认未启用不写入文件；非法数据降级为未启用）
  · 端到端验证：16:9 源 → 9:16 竖屏真实导出 108x192（100% 有效像素）
- M2.14 ✅ 专业混音台（文档第 3 部分「专业混音台视图」）：
  · Track 增加 solo/gainDb（轨道级混音控制）；Sequence 增加 masterGainDb（主输出）
  · 混音器 prepare：静音排除 → 独奏过滤（solo 是「选听」聚合语义，多条 solo 全部保留）；
    片段级增益 × 轨道增益 × 闪避包络 × 主输出，逐块应用后限幅
  · 编辑器 setTrackSolo/setTrackGain/setMasterGain（参数校验、撤销重做、历史名）
  · trackAudible 查询（供 UI 混音台高亮联动）
  · 序列化（默认值不写入；越界值载入截断到合法范围）
  · C ABI：nc_set_track_solo / nc_set_track_gain / nc_get_track_mixer /
    nc_set_master_gain / nc_get_master_gain
  · 修复 SetTrackCommand::apply 的真实缺陷：SeqMasterGain 不属于轨道，
    原实现先查轨道导致主输出永远失败（revert 同步修复）
  · 端到端验证：真实 MP4 回读测量——音乐 -12dB → ×0.24；主输出 -6dB → 两轨各 ×0.5；
    独奏音乐 → 旁白降至频谱泄漏底（完全静音）
- M2.15 ✅ 会员权益与导出水印（文档第 6 部分「会员订阅体系」+ 8「安全体系」）：
  · `Entitlement`：权益矩阵单一事实来源（轨道 4/∞、队列 1/5、短边 1080/∞、
    水印 必需/无、字幕 30min/∞、AI 成片 5 次/∞、补帧与高精度抠像 Pro 专属）；
    `proActiveAt` 支持永久买断与到期判定（到期即降级，fail-closed）
  · `makeExportPolicy`：由权益派生导出策略——免费/过期 → 1080p 上限 + 水印
  · `watermark` 模块：逐项精确实现文档 6.3.9 规格——
    右下角 / 16px@1080p（按短边等比缩放）/ 宽度 = 画面宽 × 8% / 70% 不透明度 /
    白色主体 + 30% 黑色描边；Logo 标记为程序化光栅化真实像素
    （圆角方框描边 + 播放三角），说明文字输出渲染项描述符（平台层系统字体绘制）
  · `runExport` 门控：降级只缩不放（保持宽高比），输出尺寸以门控结果为准，
    逐帧水印经执行器统一应用（含转场/空档/解码失败全部帧路径），并产出提示警告
  · C ABI：nc_set_entitlement / nc_get_entitlement / nc_can_add_track /
    nc_would_clamp_export；nc_export_run 自动按当前权益门控（默认 Free）
  · 端到端验证：真实 MP4 导出——免费 4K 工程 → 1920×1080 且水印区 877 亮像素
    （对照区 0）；Pro 4K 工程 → 3840×2160 无水印；720p 免费 → 不降级但有水印

- M2.15 ✅ 会员权益与导出水印（文档 6.3.1 / 6.3.9 / 6.3.10）：
  · entitlement：权益矩阵单一事实来源（轨道 4/∞、队列 1/5、短边 1080/∞、水印 必需/无、
    字幕 30min/∞、AI 成片 5 次/∞、补帧与高精度抠像 Pro 专属）；proActiveAt 到期即失效；
    clampSizeToShortSide 只缩不放；makeExportPolicy 免费/过期 → 1080p + 水印
  · watermark：逐项实现 6.3.9（右下角、16px@1080p 边距按短边缩放、宽 8%、
    70% 不透明、白色 + 30% 黑描边）；Logo 程序化光栅化真实像素 + 文字渲染项描述符
  · runExport 门控：分辨率降级 + 水印统一 finalize（转场/空档/正常/失败四路径全覆盖）
  · C ABI：nc_set_entitlement / nc_get_entitlement / nc_can_add_track / nc_would_clamp_export
- M2.16 ✅ AI 检查（文档 7.4.5：问题可点击定位 + 全部自动修复）：
  · 字幕类：重叠（带实测重叠量）/ 过短 / 空字幕；音频类：峰值过高（真实解码）/ 近乎静音；
    视频轨空隙；每项带定位时间点供「跳转查看」
  · fixIssue / fixAllIssues 经编辑器命令执行（撤销栈可回退）；
    重叠修复=后条起点对齐前条终点；峰值修复=精确安全增益（用实测峰值计算）
  · 修复无上限解码缺陷：按素材被引用的最大源终点解码（非全量），并设绝对上限；
    窗口长度按素材实际采样率换算（原先硬编码 48kHz）
  · C ABI：nc_ai_check（JSON 报告）/ nc_ai_check_fix_all

- M2.17 ✅ 代理剪辑（文档第 3 部分「三、代理剪辑（Proxy Editing）」）：
  · proxy 模块（零文件系统依赖，逻辑全可测）：ProxyRegistry 登记表（随工程序列化）、
    ProxySettings（自动/手动/从不 + 1/2·1/4 质量 + 高分辨率阈值）、ProxyRecord 状态机
  · checkProxyOnImport：识别 4K 级素材并给出建议尺寸/估算耗时/估算体积/说明文案；
    已有可用代理或非高分辨率素材不重复建议；「从不」策略直接跳过
  · generateProxyFrames：真实逐帧转码驱动（读源 → 双线性缩放 → 写编码器），
    支持进度回调与取消；解码器/编码器由平台层注入（桌面=FFmpeg，移动=MediaCodec）
  · selectPreviewSource：预览用代理、原片兜底；导出路径不经此函数——ExportPlan 始终
    引用 asset->path（原片），对应「导出时自动使用原始画质」（有对应用例守护）
  · 序列化：登记表持久化（空表不写入；载入时「生成中」重置为未完成、
    悬空素材引用自动丢弃，保证工程可打开）
  · 「生成中」为运行时状态不持久化；占用统计（readyCount/totalSizeBytes）供设置页展示与清理
  · 修正估算模型缺陷：原按单一吞吐估算严重低估（实测 0.10s 估算 vs 3.58s 实测）；
    改为解码/缩放/编码三段成本模型（取偏保守吞吐），2.10s 估算对 3.58s 实测
  · C ABI：nc_proxy_check_import / nc_proxy_set_record / nc_proxy_get_record /
    nc_proxy_usage / nc_proxy_clear_records

- M2.18 ✅ 导出预设 / 资源预估 / 性能模式（文档 6.1 导出页 & 快速导出预设库、第 4 部分后台导出）：
  · 预设库单一事实来源（6 个）：⚙️自定义、📱抖音 9:16、📷Reels 9:16、🎵Shorts 9:16、
    💬视频号 9:16、▶️YouTube 16:9 4K（Pro 权益）；各含目标尺寸/帧率/推荐码率/
    推荐画幅适配（竖屏 Fill 铺满无黑边、YouTube Fit 保留完整画面）
  · 性能模式 → 编码参数真实生效：极速 ultrafast+全核+暂停编辑；平衡 veryfast+保留 1 核
    （可继续编辑）；省电 veryfast+单线程（降速静默）；WriterConfig.threads 接入 FFmpeg
    编码器 thread_count
  · 资源预估：文件大小（视频码率 + 192kbps 音频）+ 渲染耗时（输出像素/吞吐），
    支持设备基准二次校正；输出「约 245 MB / 约 1分15秒」友好文案
  · setupPresetExport：预设 + 权益组装完整写入配置；4K 对非 Pro 降级并给出警告
  · 修正门控缺陷：原用 entitlement.maxExportShortSide()（基于 tier 名义层级），
    在「已到期但服务器未下发更新」窗口内会放行 4K；改用 proActiveAt（含到期判定）
  · C ABI：nc_export_presets / nc_export_preset_info / nc_export_estimate /
    nc_export_setup_preset

- M2.19 ✅ 口播精剪（文档 7.2「AI成片 → 口播精剪」）：
  · speech_cut 模块（全本地，零网络）：
    · analyzeSpeechActivity：真实解码音频 → 逐帧 RMS 语音活动检测（VAD）→
      区间合并（minSilence 保留句内换气）→ padding 边界外扩（避免语音首尾被切）→
      minKeep 相邻合并 / maxKeep 超长切分；输出保留区间 + 有效语音/静音时长/保留比
    · planSpeechCut / planSpeechCutForTrack：保留区间按片段 sourceIn/speed 映射为
      时间轴新片段序列（含「剪掉 X 秒（缩短 Y%）」汇总文案）
    · applySpeechCut：一条撤销事务落盘（删旧片段 + 插入保留段）；事务外先算计划，
      失败零副作用；锁定轨道/缺失片段拒绝
    · analyzeAssetSpeech：便捷入口（解码器由平台层注入；空工厂给出说明性 note）
  · 与自动闪避（只降音量）/音乐卡点（按节拍切分）的区别已在头文件注明
  · C ABI：nc_speech_analyze（真实解码统计）/ nc_speech_cut_apply（默认首个音频轨）

- M2.20 ✅ 蒙版（文档第 2 部分「✂️剪辑 → 蒙版：线性/镜面/圆形/矩形（羽化/反转）」）：
  · mask 模块（确定性像素运算，零第三方依赖）：
    · 四种形状：线性（单侧半平面 + 法线旋转 + 偏移）/ 镜面（中心带状窗口）/
      圆形椭圆（各向异性半轴）/ 矩形（旋转 + 圆角 SDF）
    · 羽化：smoothstep 过渡带（相对画面短边；横竖屏视觉一致）
    · 反转：可见区与隐藏区互换
    · 两种混合模式：Transparent（蒙版外 alpha=0，多层合成透出下层）/
      Background（蒙版外填充背景色，单层导出不透明输出）
    · 覆盖率评估（maskCoverageAt 像素级 / maskCoverageRatio 采样估计，供 UI 展示）
  · Clip 增加 hasMask/mask 字段；RenderClip 携带蒙版快照
  · 导出执行器：解码帧后应用蒙版（Transparent 模式，与多层 alpha 合成衔接）
  · 预览器：同一实现（预览与导出一致）
  · SetClipMaskCommand（撤销重做）+ 编辑器 setClipMask（参数校验/仅视频轨）
  · 序列化（默认不写入；载入非法参数降级为未启用）
  · C ABI：nc_set_clip_mask / nc_get_clip_mask

- M2.21 ✅ 批量多选操作（文档第 3 部分「八、框选/多选批量操作」）：
  · batchDelete（可选波纹删除）/ batchApplyTransitions / batchSetAudio /
    batchSetSpeed / batchAlignStart；逐项回报成功/失败/消息，不阻断其余项
  · 全部经编辑器命令 + 事务（可整体撤销；历史名 = 用户操作名）
  · 修复两处真实缺陷：
    1) validateTransition 占用规则过严——禁止任何片段被两个转场引用，
       导致 A→B→C 转场链无法建立；改为「同一对重复」/「同起点」/「同终点」
       三种精确冲突判定（保留原冲突测试语义）
    2) 单命令事务丢失事务名（历史面板显示底层命令名如「添加转场」而非
       「批量应用转场」）；Command 增加 setDisplayName 覆盖机制

- M2.22 ✅ AI 模型管理与密码学基础（文档第 5 部分「二、AI模型管理」+ 第 8 部分「2.2 模型分发架构」）：
  · crypto 模块（零第三方依赖，标准算法可逐位对标）：
    · SHA-256（FIPS 180-4）：增量摘要器（流式处理大文件）+ 一次性接口 +
      hex 互转 + 恒定时间比较（防时序侧信道）
    · AES-256（FIPS 197）：真实 S-box / 密钥扩展 / 轮函数；ECB 单分组原语
    · AES-256-CTR（NIST SP 800-38A）：16 字节大端计数器、支持任意分片、
      密钥流偏移定位（aesCtrCounterAtOffset / aesCtrIntraBlockOffset）——
      断点续传时从「已下载字节数」处精确续解密，不重算前缀
    · CSPRNG 注入点（setSecureRandomProvider）：core 不直连系统随机源，
      未注入时 fail-closed（不降级到弱随机）
  · model_manager 模块（文档「智能抠像 120MB / Whisper 240MB / 光流补帧 85MB」）：
    · 内置模型目录（id/展示名/描述/体积/版本/能力分类/Pro 门控），
      与 Entitlement 单一事实来源联动（过期 Pro 走 proActiveAt）
    · 下载状态机：7 阶段（idle/downloading/paused/verifying/completed/failed/
      cancelled）+ 合法迁移约束 + 进度单调钳制 + 断点保留（暂停/取消后可续传）
    · 断点续传坐标 DownloadResumePlan：续传起点（HTTP Range）+ 起始计数器 +
      块内丢弃字节数；计数器由「模型 ID + 版本」确定性派生（重复安装坐标一致）
    · 完整性校验：大小 + SHA-256（expectedDigest 为空 → fail-closed）
    · 加密存储：明文 ↔ 密文（AES-256-CTR，密钥由服务器下发不落盘）
    · 存储统计：可用/总计容量 + 模型占用 + 安全系数容纳判定 + 友好文案
    · 记录表持久化：JSON 序列化（严格校验/非法记录跳过/运行时状态降级）
  · C ABI：nc_model_catalog / nc_model_info / nc_model_status /
    nc_model_access_allowed / nc_model_download_begin/progress/pause/resume/
    cancel/finish/fail / nc_model_download_state / nc_model_resume_plan /
    nc_model_install / nc_model_remove / nc_model_storage_stats / nc_model_decrypt
    （+ nc_free_buffer / nc_free_string_array 释放辅助）
  · 测试：crypto 19 例（FIPS 180-4 / FIPS 197 C.3 / NIST SP 800-38A F.5.1 官方向量）
    + model_manager 40 例 + C ABI 5 例（共新增 64 例，总计 686 例）
  · 端到端交叉验证（独立于单元测试期望值）：
    · SHA-256 vs OpenSSL sha256sum：100000 字节 / 1000003 字节（非块对齐）逐位一致
    · AES-256-CTR vs OpenSSL enc：100000 字节密文逐字节一致
    · 全链路：8MB 载荷 加密 → 校验 → 两次暂停续传 → 断点分片解密 → 整体解密一致

- M2.23 ✅ 章节自动生成（文档第 7 部分「🧪AI工具 → 章节自动生成：自动检测/手动编辑标题」）：
  · chapter 模块（纯本地算法，零第三方依赖；复用既有 scene_detect + VAD）：
    · 边界来源：场景切换（直方图差异）/ 长静音（VAD 语音区间补集，取中点为边界）/
      时长硬切（超长章节按 maxChapterSeconds 等分，防「整场一个章节」）
    · 候选聚类：近邻边界合并（场景 + 静音双重命中标记为 Both；窗口 = max(1s, 最短章节/4)）
    · 时长约束：过短间隔丢弃；maxChapters 超限时按边界强度丢弃最弱（并入相邻章）
    · 首章恒从 0 起、末章覆盖到内容结束、章节无缝连续覆盖
    · 自动标题：本地规则化（「第 N 章」+ 真实测量线索：人声清晰/近乎静音/画面明亮/
      动作丰富），不调用云端、不做内容识别（合规：隐私政策 3.1/3.2）
    · 手动编辑：renameChapter（结果对象）/ nc_chapter_rename（已落盘标记）；
      重新生成不覆盖手动标题（isAutoGeneratedTitle 判定）
    · 真实采样 sampleSequenceForChapters：逐片段 seek 解码帧特征（4×4 亮度网格 +
      均值距离，与参考仿剪同一约定）+ 逐块解码音频映射到时间轴坐标；
      自适应采样步长（0.25~1s，约 240 点覆盖全片）；上限保护 + 复合片段/缺失素材跳过
  · 落盘（标记为单一事实来源，不引入第二份持久化状态）：
    · 章节 = 序列标记（固定章节色 #00D084 + 标题为标记名）
    · applyChapters：一条撤销事务（「生成章节」）；幂等（按 markerId 或「章节色 +
      相同起点」匹配既有标记；ID 预分配保证 redo 确定性）
    · readChaptersFromSequence：由标记重建章节（末章终点 = 内容时长）
  · C ABI：nc_chapter_detect / nc_chapter_apply / nc_chapter_count / nc_chapter_get /
    nc_chapter_rename / nc_chapter_clear
  · CLI：nclip_tool chapters <file.nclip> [--apply <out.nclip>] [--min-s N] [--max-s N]
  · 测试：chapter 31 例 + C ABI 4 例（含真实素材端到端）；共 721 用例全绿（新增 35 例）
  · 独立端到端验证（真实 FFmpeg 解码，非单元测试期望值）：
    · two_shots.mp4（4s 两镜头）：场景边界检出 2.00s（与拼接点一致）
    · chapter_av.mp4（12s，0-4/7-12s 有声）：静音边界检出 5.49s（静音段 4-7s 中点）
    · CLI --apply 落盘 + 二次 --apply 幂等（新增 0 / 更新 2）+ info 显示标记=2

- M2.24 ✅ 导出队列管理 + 媒体离线检测与重新链接：
  【导出队列（文档第 6 部分「二、导出队列管理」+ 第 4 部分「后台导出」）】
  · export_queue 模块（纯状态机 + 调度决策，零线程/文件系统依赖）：
    · 任务结构（名称/输出路径/预设/性能模式/合规标识/状态/进度/帧数/备注）
    · 状态机：等待 → 运行 → 完成/失败/已暂停/已取消（合法迁移约束 + 终态不可迁移）
    · 调度：pollNext 按入队顺序取首个等待任务；并发上限由性能模式派生
      （Turbo 1 独占 + 暂停编辑 / Balanced min(2,并行度) / Eco 1 降速静默）
    · 权益限额：Free 1 / Pro 5（entitlement.maxExportQueue 单一事实来源；
      活动任务计入额度，终态释放——可继续入队）
    · 操作：暂停（等待中/运行中，诚实说明「继续将从头重新导出」——H.264
      流不支持断点续写）/ 继续 / 取消 / 移除（活动态先转 Cancelled 供平台层停线程）
      / cancelAll（应用退出）/ 进度单调钳制
  · C ABI：nc_export_enqueue / nc_export_poll_next / nc_export_job_state /
    nc_export_job_progress / nc_export_job_update（6 种动作）/ nc_export_queue_summary /
    nc_export_queue_cancel_all
  【媒体离线检测与重新链接（文档第 3 部分「四」）】
  · media_offline 模块（零文件系统依赖；探测经 OfflineProbe 抽象注入）：
    · 扫描：路径存在性 + SHA-256 摘要校验（统一用 crypto 模块；摘要不符 →
      DigestMismatch fail-closed；无期望摘要 → 只做存在性检查）；未接入探测器时
      全部 Unverified 并给出说明（不阻塞项目打开）
    · 标记写回：applyOfflineFlags 同步 asset.offline（状态同步语义，不进撤销栈）
    · 自动扫描：findRelinkCandidates 按文件名（大小写不敏感）在候选目录查找，
      摘要一致者优先排序；容忍目录不存在
    · 重新链接：单素材（摘要校验、路径占用拒绝、fail-closed）/ 批量（全量预校验 +
      一条事务 + 部分失败不阻断）→ 经 RelinkAssetCommand 进撤销栈（完整还原
      路径/摘要/离线标记）
    · asset 摘要持久化：.nclip 新增 "sha256" 字段（空不写入保持 diff 稳定；
      载入非法值降级为未记录，不阻止打开；旧工程无该字段完全兼容）
  · C ABI：nc_media_scan_offline / nc_media_apply_offline_flags / nc_media_relink /
    nc_media_find_relink_candidates / nc_media_relink_batch / nc_media_set_digest
    （探测用 FFmpeg 真实文件系统实现；非 FFmpeg 构建 fail-closed）
  · CLI：nclip_tool offline <file.nclip> [--scan-dir D]... [--apply <out.nclip>]
  · 测试：export_queue 25 例 + media_offline 22 例 + C ABI 10 例；
    共 785 用例全绿（新增 64 例；无 FFmpeg 配置 760 例全绿）
  · 独立端到端验证（真实文件 + 真实 FFmpeg 编码）：
    · 队列驱动 2 个真实导出（90 帧/任务）→ 两个 MP4 均被 FFmpeg 回读解码成功
    · 离线链路：记录真实 SHA-256 → 删除文件检出 Missing → 篡改内容重链被拒
      → 恢复内容重链成功 → 撤销还原离线状态 → 批量重链 1 成功/1 失败不阻断
    · 自动扫描：同名垃圾文件与真实文件共存时，摘要匹配者正确排首位
    · CLI 四场景：在线（exit 0）/ 丢失（exit 1）/ 扫描命中 / --apply 写回 offline=true

- M2.25 ✅ 多版本智能成片 + 重新生成影响分析 + 参考风格权益门控
  （文档第 7 部分 4.1「智能成片」/ 4.4「重新生成」/ 六、七「权益矩阵与边界」）：
  · smart_movie 模块（纯本地算法 + 事务化落盘，零网络）：
    · 权益门控：参考风格提取（Pro 专属，fail-closed + 付费墙文案含 🔒）；
      生成版本数（Free 固定 1；Pro 1-5，超限降级并提示）
    · 多版本计划 planSmartMovie：5 种变体（忠实参考/快切/舒缓/动感/沉静），
      真实差异 = 镜头时长（median 基线 × 变体系数 × 节奏滑块 p25↔p75 插值）
      + 转场密度（确定性规则，可测）；目标时长约束镜头数；素材容量裁剪保护
    · 落盘 applySmartMovieVersion：转场需间隙的项目约束下，真实预留间隙并添加
      转场；参考色调映射为调色特效；一条事务可整体回退
    · 批量落盘 applySmartMovieVersions：逐版本独立，某版本失败不阻断其余
      （文档 4.1 失败态「已成功生成的版本不受影响」）
    · 影响分析 analyzeRegenerationImpact：真实扫描工程状态，统计将丢失
      （转场/特效/关键帧/蒙版/音频调整/速度曲线/复合片段/标记含章节/字幕）
      与将保留（锁定轨道/其它序列）；detailLines 生成弹窗清单文案
    · 清理 clearForRegeneration：按报告执行（锁定轨道跳过）；标记/字幕按范围清理；
      两阶段设计（清理与重建分别可撤销）
  · C ABI：nc_reference_style_access / nc_movie_version_access /
    nc_smart_movie_plan（扁平 JSON 风格画像）/ nc_smart_movie_apply（部分失败明细）/
    nc_regeneration_impact / nc_regeneration_clear
  · 测试：test_smart_movie 24 例 + C ABI 4 例；共 813 用例全绿
    （新增 28 例；无 FFmpeg 配置 788 例全绿）
  · 独立端到端验证（真实素材 + 真实 FFmpeg 编码）：
    · 3 版本真实时长差异：快切 2.10s < 忠实 3.00s（舒缓版受素材容量约束正确裁剪至 3.00s）
    · 批量落盘 3 版本（各 1 转场，间隙真实预留）→ 真实导出 102 帧 → 回读解码成功
    · 影响分析实测 6 类痕迹各 1（转场/特效/蒙版/音频/标记/字幕）+ 锁定轨保留
    · 清理保锁定轨；重建 3 片段；双撤销完整还原（视频 2 + 音频 1 + 标记 1 + 字幕 1）

- M2.26 ✅ 隐形水印 + 会员心跳校验（文档第 8 部分 3.1「隐形水印（事后追溯）」
  + 五「会员心跳校验具体流程」）：
  · invisible_watermark 模块（QIM 块量化水印；纯本地像素运算，零网络）：
    · 载荷 64 位：账号哈希（SHA-256 前 32 位，不可逆）+ 导出序号（定位批次）
      + CRC-16/CCITT 校验（标准向量 0x29B1 守护）
    · 嵌入：画布分块（默认 8px）→ 块均值 QIM 量化（奇偶性编码 1 bit）→
      块内像素整体平移；解码：重新量化取奇偶 + 冗余多数表决 + CRC 校验
    · 位置置换由 key 派生（LCG Fisher-Yates），局部损坏不连续摧毁载荷
    · 自适应配置：≥640x480 用默认（实测 H.264 CRF18 agreement 0.988）；
      小分辨率换 4px 块 + 放大步长（160x120 实测 0.98~1.00）
    · 容量预检 / 嵌入 / 检测共用 effectiveWatermarkConfig（单一事实来源）
  · 真实缺陷修复（本次端到端验证发现）：饱和亮度区域（信箱黑边）QIM 不可达——
    纯黑块编码 bit=1 时原实现选 -step 且被 0 钳制无法移动，解码回读 bit=0；
    1080x1920 信箱导出（65% 饱和块）曾致水印 0/10 帧可检出。修复：
    候选量化级「钳制模拟 + 回读校验」（embedBitInBlock），黑块正确抬升至
    +step（视觉不可见）；修复后端到端 10/10 帧检出（agreement 0.96）
  · heartbeat 模块（客户端状态机；平台层负责真实 HTTP 与签名校验）：
    · 触发调度：启动 / AI 入口 / 30 分钟周期 / 手动 / 后台恢复（周期到期判定）
    · 分级处理：✅ Ok（更新权益 + 清零异常计数）→ ⚠️ Warning（首次异常提示 +
      纠正机会，不封号）→ 🚫 Locked（连续 3 次异常升级，或服务端明确锁定）
    · 校验项：到期 / 时间偏差（±300s 防改时间绕过）/ 设备数超限 / 签名一致性
    · 网络失败（NetworkError）：保持权益不变、不累计异常（fail-safe）
    · 解除锁定 / 重置（切换账号）；锁定原因文案可展示
  · 导出管线接入：ExportExecutorOptions::invisibleWatermark 逐帧嵌入
    （容量不足记 warning 并跳过，不中断导出）
  · C ABI：nc_watermark_account_hash / nc_watermark_set_export /
    nc_watermark_detect（1 检出 / 0 未检出 / -1 参数错）/
    nc_heartbeat_handle / nc_heartbeat_network_error / nc_heartbeat_status /
    nc_heartbeat_periodic_due / nc_heartbeat_clear_lock
  · 测试：test_invisible_watermark 15 例 + test_heartbeat 13 例 + C ABI 3 例；
    共 847 用例全绿（新增 34 例；无 FFmpeg 配置 820 例全绿）
  · 独立端到端验证（C ABI → 真实 H.264 导出 → 解码回读 → 检测）：
    · 横屏素材进竖屏画布（1080x1920 信箱，65% 黑边饱和块）导出 30 帧
      → 前 5 帧全部检出，账号哈希与导出序号完整还原（修复前 0/10）
    · H.264 CRF18 单元往返：detected=1，agreement=0.988
  · 合规说明：仅编码账号哈希（不可逆），不采集个人信息；用途限定泄露溯源

- M2.27 ✅ 色度抠像（绿幕）（文档第 2 部分「🤖AI实验室 → 智能抠像 → 人像/物品/
  色度抠像(绿幕)」+ 权益矩阵「AI智能抠像：基础版85% | 高精度版98%+物品/色度抠像」）：
  · chroma_key 模块（纯本地像素运算，零第三方依赖、不需要推理引擎）：
    · 色彩空间：RGB → YCbCr（BT.601）；键控相似度 =
      方向余弦 × sqrt(min(|v_px|/|v_key|, 1))（亮度自适应）：
      纯绿幕/打光不完美/白光冲淡浅绿 → 完全抠除；
      同类阴影绿 → 部分保留（保留立体感，阈值可调）；
      反向色（肤色/红/蓝/白/黑/灰）→ 完整保留；
    · 双阈值软判决（inner/outer + smoothstep 过渡带）→ 边缘自然无锯齿；
    · 溢色抑制（通道法，行业标准）：绿/蓝/洋红/红键控的对应通道
      超出其余通道的溢色部分按强度收拢（消除边缘反光）；
    · 设计声明：不引入无实际效果的旋钮（如暗部保护/肤色保护参数在该度量下
      数学上冗余，已被移除而非保留为装饰参数）；
  · 数据模型与命令：Clip::hasChromaKey/chromaKey；SetClipChromaKeyCommand /
    ClearClipChromaKeyCommand（事务化、可撤销、redo 确定性）；
  · 管线接入：导出（多层 Transparent 透出下层 / 单层 Background 合成背景色）、
    预览（Background 显示最终效果）、序列化（默认值不写、非法降级）、
    重新生成影响分析（「智能抠像（色度/绿幕）」计入将丢失清单）；
  · 权益门控：mattingAccessFor（fail-closed；免费版拒绝 + 🔒 付费墙文案）；
    C ABI 层 nc_set_clip_chroma_key 在非 Pro 时返回 NC_ERROR_UNSUPPORTED；
  · C ABI：nc_matting_access / nc_set_clip_chroma_key / nc_get_clip_chroma_key /
    nc_chroma_key_estimate（前景覆盖率估计，供 UI 预览）
  · 测试：test_chroma_key 19 例 + 管线集成 10 例；共 876 用例全绿
    （新增 29 例；无 FFmpeg 配置 848 例全绿）
  · 独立端到端验证（真实素材 + 真实 FFmpeg 编码 + 像素级检查）：
    · FFmpeg 生成真实绿幕素材（绿底 + 肤色色块）+ 红色背景素材
    · 双层轨道（上层绿幕素材抠像 + 下层红底）→ 真实导出 30 帧 1080x1920
      → 解码像素检查：绿幕区透出红底（R=252,G=0,B=0）、肤色区保留（217,166,138）
    · 门控实测：Free 拒绝（rc=6）+ 🔒 文案；Pro 允许（quality=1）
    · 变异测试：临时禁用抠像后多层测试真实失败（证明测试非假绿）

- M2.28 ✅ 空间特效真实实现（文档第 2 部分「✨特效 → 画面特效」中需要邻域像素的
  效果 + 第 4.4 节质量分级预留）：
  · 背景（真实缺陷修复）：此前 Vignette / GaussianBlur / Sharpen / Grain 四种特效
    仅有枚举与参数声明，evaluateColorTransform 全部跳过（注释称"由渲染器单独处理"
    但渲染器无对应代码）→ 用户添加后画面毫无变化。本次修复为真实实现并全链路接入。
  · 新增 spatial_effects 模块（纯像素运算；CPU 参考实现 = 正确性基准）：
    · 暗角：径向平滑衰减（smoothstep，最多暗 85%）+ 相对短边归一化（画幅无关）；
    · 高斯模糊：可分离两趟卷积（sigma = r/3，半径 = ceil(3σ)），边缘钳制，
      double 中间缓冲保精度（实测均值保持误差 < 2/255）；
    · 锐化：非锐化掩模「orig + s×(orig − blur)」（blur 半径 1.5px 可分离），
      避免一次性拉普拉斯核的过冲饱和；
    · 噪点：确定性伪随机（boost::hash_combine + murmur3 finalizer 雪崩混合；
      修复——FNV 直接取高位曾导致相邻帧哈希差异仅 256，帧间噪声完全相同）；
      三通道独立（彩色颗粒感）；帧序号驱动时变（导出 = 输出帧序号）。
  · 统一入口 applyEffectStackToFrame：颜色变换先行、空间效果按栈序随后，
    导出与预览共用（消除两处各自解释栈语义的风险）；
  · 缺陷修复（同批发现）：
    · 转场帧此前完全不吃片段特效（与单层路径不一致）→ 修复为对 from/to
      两路源帧分别应用各自特效栈；
    · 序列化特效类型边界硬编码 `> 12` 拒绝（新增特效后新值会被拒）→
      改为按枚举最大值判定；
  · 测试：test_spatial_effects 22 例（单效果行为/单调性/零改动/确定性/时变/
    栈顺序/参数防御/管线集成/序列化往返/真实 FFmpeg 端到端）；
    共 895 用例全绿（新增 22 例；无 FFmpeg 配置 866 例全绿）
  · 独立端到端验证（高频细节素材 + 真实 FFmpeg 编解码 + 梯度能量测量）：
    · FFmpeg 生成 detail_src.mp4（testsrc2 + 8px 网格，高频细节）
    · 模糊：内容区水平梯度能量 263.9 → 34.8（降低 86.8%）
    · 锐化：梯度能量 263.9 → 393.6（增强 49.2%）
    · 暗角：角落亮度 122.3 → 32.8（衰减 73%）、中心 241.0 → 239.5（不变）
    · 噪点：38.1% 像素变化超过 H.264 压缩噪声阈值（±6 灰度）
    · 变异测试：临时跳过空间效果应用 → 4 个测试 6 个断言真实失败
  · 测试素材脚本补齐 detail_src.mp4（test_av.mp4 为大面积均匀色块，
    模糊对均匀区域天然无变化，不适合梯度能量验证——已实测确认）

- M2.29 ✅ 合规整改（《NairoClip 所有模型协议和其它要求》第七章/第九章 +
  《人工智能生成合成内容标识办法》+ 三端部署规格）：
  · codec_policy 模块（编解码器合规策略；纯逻辑、零 FFmpeg 依赖）：
    · 许可分级：硬件（MediaCodec/VideoToolbox/MF/NVENC/QSV/VAAPI/V4L2M2M/AMF）/
      宽松软件（OpenH264/libaom/libsvtav1/librav1e/libvpx/libopus/内置 AAC）/
      GPL 传染（libx264/libx265/libxvid/libfdk_aac）/ 未知（保守拒绝）；
    · 选择序：硬件优先 → 宽松软件兜底 → （仅开发构建）GPL 例外 → fail-closed；
    · 缺陷修复：FFmpegMediaWriter 此前用 avcodec_find_encoder(AV_CODEC_ID_H264)
      自动选择 → 沙箱解析到 libx264（GPL-2.0，文档明令禁止）→ 改为按策略选择 +
      候选序列逐个尝试打开（无 GPU 时 h264_nvenc 失败自动回退）；
    · HEVC/H.265 全线禁止（任何模式下均拒绝）；
    · 构建开关 NAIROCLIP_DEVELOPMENT_GPL_OK（发布构建必须 OFF；沙箱/CI 默认 ON
      并在日志显著标记「开发例外」）；
  · ai_marking 模块（AI 生成合成内容标识：显式 + 隐式）：
    · 隐式标识（元数据）：buildAiMetadata → 容器元数据（comment 含压缩机器可读
      字段「ai_generated=true；ai_generated_feature=…」，兼容 MP4 标准 tag 限制；
      MKV/WebM 保留独立字段）；parseAiMetadata 供导入检测；真实导出后 ffprobe
      可读（实测通过）；
    · 显式标识（音频）：applyAiAudioMarks（首尾 880Hz 提示音，含淡入淡出防爆音）
      —— 法规「音频起始/末尾/中间适当位置」要求；真实导出后解码验证
      （起始段 RMS 0.124 vs 无标识 0.000，中段主内容不受影响）；
    · 与隐形水印职责分离（水印 = 账号溯源；本模块 = AI 属性告知，两者共存）；
  · 设备性能分级（三端部署规格第五章）：
    · classifyDeviceTier（桌面恒 High 不自动分级；移动按 RAM/加速单元/基准耗时）；
    · recommendedQuantization（桌面 FP32 / 移动 High·Mid FP16 / Low INT8）；
    · buildModelDownloadRequest（功能 ID + 分级 + 量化版本；加密机制不因量化而变）；
    · 强制原则：量化只影响体积/速度，**不影响功能集合与输出质量上限**（三端对等）；
  · 合规文档产出：docs/COMPLIANCE.md（19 项强制校验清单，含平台层职责标注）+
    docs/THIRD_PARTY_NOTICES.md（协议全文 + CI 审计建议 + 禁用清单）；
  · 测试：test_codec_policy 12 例 + test_ai_marking 7 例 + DeviceTier 4 例；
    共 918 用例全绿（新增 23 例；无 FFmpeg 配置 887 例全绿）
  · 端到端验证（真实 FFmpeg）：
    · 发布构建（GPL denied）实测：沙箱硬件编码器全失败 → 拒绝编码（fail-closed）
    · 开发构建实测：nvenc 失败 → libx264 开发例外兜底（导出成功）
    · AI 元数据：真实 MP4 导出后 ffprobe 读到
      「本内容由人工智能生成合成（NairoClip）；ai_generated=true；
      ai_generated_feature=TTS 智能配音」
    · 音频标识：有/无提示音两份真实导出对比（起始段 RMS 差 0.124，样本数
      差值 57344 ≈ 2 段提示音）

### 待办
- ⬜ 超分辨率（2x/4x，需模型推理接口）
- ⬜ 光流补帧（60/120fps，需模型推理接口）
- ⬜ 模型分发服务器对接（core 仅实现客户端逻辑：签名 URL 有效期校验/加密存储/
  断点续传；服务器地址与密钥由用户提供，不硬编码）
- M3.0 ⬜ Android 平台层（JNI + MediaCodec 封装，需 NDK 环境）
- 后续 ⬜ 本地 AI 推理（模型分发走国内云）+ 平台壳（Kotlin/Qt）

## 8. 兼容性预留
- 绑定层将在 core 之上提供稳定 C ABI，JNI / Swift / Qt 均经该层接入；
- 核心不依赖任何平台 API、不依赖第三方库，可交叉编译至移动端与桌面端。
