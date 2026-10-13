# 第三方开源组件许可声明（THIRD PARTY NOTICES）

> 本文件对应 App「关于 → 开源软件许可」页面内容，须随依赖变更同步更新。
> 依据：《NairoClip 所有模型协议和其它要求》第五章第三节。
> 维护规则：新增/替换依赖时更新本文件；协议全文必须完整保留（MIT/BSD 较短，
> 必须全文）；建议在 CI 中加入依赖协议扫描自动比对，避免人工漏记间接依赖。
>
> **当前状态（M2.29）**：core 库自身为零第三方依赖（仅 C++17 标准库）。
> 以下是**可选集成**与**平台层/运行期**组件的许可声明。

---

## 1. 可选构建依赖（libnairoclip_core）

### 1.1 FFmpeg（可选；构建时探测）

- 组件：libavformat / libavcodec / libavutil / libswscale / libswresample
- 协议：**LGPL 2.1 or later**（本项目仅链接上述核心组件；**不启用**
  `--enable-gpl` / `--enable-nonfree`；动态链接；不静态链接进最终二进制）
- 版权：FFmpeg developers（https://ffmpeg.org）
- 义务：保留版权声明；提供 LGPL 源码获取渠道（发行时提供链接或书面 offer）
- 注意：`libpostproc`（GPL）、`libx264`/`libx265`（GPL）等模块**不在使用范围**；
  本项目代码级禁止选择 GPL 编码器（见 `core/src/codec_policy.cpp` 与
  `docs/COMPLIANCE.md` §1）

### 1.2 ONNX Runtime（可选；AI 推理，规划中）

- 协议：**MIT License**
- 版权：Copyright (c) Microsoft Corporation. All rights reserved.
- 义务：保留版权与许可声明

### 1.3 OpenH264（可选；H.264 软件编码兜底）

- 协议：**Simplified BSD License**（Cisco 另提供二进制版本并承担对应专利费覆盖）
- 版权：Copyright (c) Cisco Systems, Inc.
- 义务：保留版权与许可声明

### 1.4 dav1d（可选；AV1 软件解码兜底，规划中）

- 协议：**BSD 2-Clause License**
- 版权：Copyright (c) 2018-2024, VideoLAN and dav1d authors

### 1.5 bgfx（可选；GPU 渲染后端，规划中）

- 协议：**BSD 2-Clause License**
- 版权：Copyright (c) 2010-2024 Branimir Karadžić

---

## 2. AI 模型权重许可（下载分发，非编译依赖）

| 模型 | 协议 | 义务 |
|---|---|---|
| MediaPipe Selfie Segmentation | Apache-2.0 | 保留 NOTICE 与许可声明 |
| RIFE | MIT | 保留版权与许可声明 |
| Real-ESRGAN | BSD-3-Clause | 保留版权、许可与免责声明 |
| UVR-MDX-NET-Voc_FT | MIT | 保留版权与许可声明 |
| whisper.cpp（含模型） | MIT | 保留版权与许可声明 |
| MADLAD-400 | Apache-2.0 | 保留 NOTICE 与许可声明 |
| VITS（官方 LJSpeech/VCTK 权重） | MIT（代码）/ 数据集各自协议（LJSpeech 公版权、VCTK CC-BY-4.0） | 注明权重来源与数据集 |
| MiDaS | MIT | 保留版权与许可声明 |
| Silero VAD | MIT | 保留版权与许可声明 |

> 禁用清单（协议风险，不得引入）：RVM（GPL-3.0）、GFPGAN/CodeFormer（非商用）、
> Demucs 官方权重/Spleeter（数据协议灰色）、NLLB-200（CC-BY-NC-4.0）、
> Depth-Anything-V2 Base/Large（CC-BY-NC-4.0）。详见 `docs/COMPLIANCE.md` §2。

---

## 3. 许可全文（按需展开）

### MIT License（适用于 ONNX Runtime / RIFE / whisper.cpp / MiDaS / Silero VAD / VITS 代码）

```
MIT License

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

> 说明：上为 MIT 标准全文。正式发行时，每个组件的 `copyright notice` 行须替换为
> 各项目实际版权行（如 `Copyright (c) Microsoft Corporation`），并在本文件中
> 逐组件列出。

### BSD 2-Clause License（适用于 bgfx / dav1d）

```
BSD 2-Clause License

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
```

### Apache License 2.0（适用于 MediaPipe / MADLAD-400 / OpenCV）

```
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
```

> Apache-2.0 要求附 NOTICE 文件（若上游提供了 NOTICE，须一并保留）。

### LGPL 2.1（适用于 FFmpeg 核心组件）

```
This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
```

---

## 4. CI/CD 自动审计建议（落地动作）

1. 在 CI 中添加依赖协议扫描步骤（示例工具：`licensecheck` / `scancode-toolkit`
   / `oss-review-toolkit`），输出清单与本文件比对；
2. 新增依赖 PR 必须更新本文件与 `docs/COMPLIANCE.md` 校验清单；
3. 发布构建执行 `cmake -DNAIROCLIP_DEVELOPMENT_GPL_OK=OFF`（编解码合规
   fail-closed，见 COMPLIANCE §1.3）。
