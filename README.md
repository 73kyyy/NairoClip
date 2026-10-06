# NairoClip

跨平台 AI 视频剪辑软件 —— 原生平台层 + C++ 共享核心。

## 当前进度

| 里程碑 | 内容 | 状态 |
|---|---|---|
| M0.1 | Core 地基：时间系统 / 数据模型 / 撤销栈 / 编辑器门面 / 单元测试 / CI | ✅ 已完成 |
| M0.2 | 关键帧缓动 / 速度曲线 / 转场模型 / 跨轨道移动 | ✅ 已完成 |
| M0.3 | .nclip 工程序列化 + 版本迁移 + 崩溃恢复挂钩 | ⬜ 计划中 |
| M1.0 | 媒体层：解码 / 预览 / 导出最小闭环（FFmpeg） | ⬜ 计划中 |
| M2.x | Android 平台壳（Kotlin + JNI），真机跑通时间轴编辑 | ⬜ 计划中 |

## 仓库结构

```
NairoClip/
├── core/                       # C++17 核心库（零第三方依赖）
│   ├── include/nairoclip/core/ # 公开头文件（rational/time/types/media/clip/track/sequence/project/command/commands/editor）
│   └── src/                    # 实现
├── tests/                      # 单元测试（零依赖自研框架）
├── docs/ARCHITECTURE.md        # 架构设计说明
└── .github/workflows/ci.yml    # CI：三平台构建 + 测试
```

## 构建与测试

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
cd build && ctest -C Release --output-on-failure
```

## 核心设计要点

- **精确时间模型**：所有时间以「秒」为单位的 64 位有理数表示（`Rational`），帧率亦是（如 29.97 = 30000/1001）。帧号换算零误差，无浮点漂移。
- **数据模型**：Project → Sequence → Track → Clip 四层；轨道内片段按起点排序且互不重叠（不变量由测试守护）。
- **命令模式编辑**：所有修改通过 Command（apply/revert）执行，由 UndoStack 统一驱动，天然支持撤销/重做与历史面板。
- **零第三方依赖**：核心库仅使用 C++17 标准库，可交叉编译至 Android / iOS / 桌面。

## 合规红线（全项目遵守）

- 剪辑运算全部本地完成；仅模型下载 / 会员校验 / 意见反馈联网；
- 严禁引入 GPL/AGPL 等传染性协议组件；
- 对外文案仅允许「参考风格生成」。
