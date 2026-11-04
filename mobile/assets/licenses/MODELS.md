# 第三方 AI 模型许可声明

本应用使用以下 AI 模型。模型权重不随 APK 分发（运行时从官方源下载），
但按各许可证要求，在此保留版权声明与许可说明。

---

## MODNet（人像抠像）

- 用途：智能抠像 / 人像分割
- 来源：https://github.com/ZHKKKe/MODNet
- 许可：**Apache License 2.0**
- 版权：Copyright 2020 ZHKKKe
- 说明：本应用使用其 ONNX 导出（Xenova/modnet），未修改模型权重。

---

## Whisper（语音转字幕）

- 用途：语音识别、自动字幕
- 来源：https://github.com/openai/whisper
- 许可：**MIT License**
- 版权：Copyright (c) 2022 OpenAI
- 说明：本应用使用 whisper.cpp 同源 ONNX 导出（onnx-community/whisper-small）。

---

## MiDaS（深度估计）

- 用途：深度图、景深虚化、3D 视差
- 来源：https://github.com/isl-org/MiDaS
- 许可：**MIT License**
- 版权：Copyright (c) 2019 Intel Corporation
- 说明：本应用使用 dpt-hybrid-midas 的 ONNX 导出（Xenova/dpt-hybrid-midas）。

---

## UVR-MDX-NET（人声分离）

- 用途：人声/伴奏分离
- 来源：https://github.com/kuielab/mdx-net
- 许可：**MIT License**
- 版权：Copyright (c) 2021 kuielab & woosungchoi
- 说明：本应用使用 ONNX 导出（musetric/uvr-mdxnet-kara2-onnx）。

---

## OPUS-MT（字幕翻译）

- 用途：中英字幕翻译（全本地运行，不接云端）
- 来源：https://github.com/Helsinki-NLP/Opus-MT
- 许可：**Apache License 2.0**
- 版权：Copyright 2020 The Helsinki NLP Team
- 说明：本应用使用 opus-mt-zh-en 的分体 ONNX 导出。

---

## Super Resolution（超分辨率）

- 用途：视频/图片放大
- 许可：**BSD 3-Clause License**
- 说明：本应用使用 Real-ESRGAN 系架构的轻量导出（super_res_2x4x）。
- 版权：Copyright (c) 2021, Xintao Wang

---

## RIFE（光流补帧）

- 用途：视频补帧、慢动作
- 来源：https://github.com/hzwer/ECCV2022-RIFE
- 许可：**MIT License**
- 版权：Copyright (c) 2021 hzwer
- 说明：本应用使用 flow_interp 的 ONNX 导出。

---

## VITS（语音合成）

- 用途：文本转语音
- 来源：https://github.com/jaywalnut310/vits
- 许可：**MIT License**（代码）
- 版权：Copyright (c) 2021 Jaehyeon Kim
- **数据集署名（CC BY 4.0 要求）**：
  - LJSpeech：公有领域（LibriVox 录音）
  - VCTK：**VCTK Corpus, University of Edinburgh, CSTR**
    licensed under CC BY 4.0

---

## 未使用的模型（明确排除）

以下模型因许可限制，**不得引入本项目**：

- RVM（GPL-3.0，传染性）
- GFPGAN / CodeFormer（非商用许可）
- NLLB-200（CC-BY-NC-4.0，禁止商用）
- Depth-Anything-V2 Base/Large（CC-BY-NC-4.0）
- RMBG-2.0（BRIA，非商用）
- YOLOv8-seg（AGPL-3.0）
