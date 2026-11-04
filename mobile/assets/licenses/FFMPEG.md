# FFmpeg 许可声明（LGPL-2.1-or-later）

## 使用方式

本应用以 **LGPL-2.1-or-later** 使用 FFmpeg，采用**动态链接**方式：

```
libnairoclip_core（本应用静态库）
        ↓ 动态链接
libavformat.so / libavcodec.so / libavutil.so / libswscale.so / libswresample.so
```

**没有静态链接**，因此本应用自身代码可以闭源 —— 这是 LGPL 与 GPL 的关键区别。

## 构建配置

```
--disable-gpl          # 不启用任何 GPL 组件
--disable-nonfree      # 不使用不可分发的组件
--enable-shared        # 动态库（LGPL 要求）
--disable-static       # 不产出 .a，避免误用成静态链接
--enable-jni
--enable-mediacodec    # Android 硬件解码
```

**未启用**的组件：libx264、libx265、libxvid、libfdk_aac（GPL 或不可分发许可）。

## 是否修改过 FFmpeg

**没有修改源代码。**

仅通过编译开关裁剪组件（禁用 GPL 部分）。构建脚本公开可见，
任何人都可以复现完全相同的产物。

## 获取 FFmpeg 源代码

- 版本：7.1
- 官方源码：https://ffmpeg.org/releases/ffmpeg-7.1.tar.xz
- 构建脚本：本仓库 `ffmpeg-build/` 目录

自行构建需要 Android NDK r27d，执行 `ffmpeg-build/build-android.sh`。

## 用户替换 FFmpeg 的权利

因为采用动态链接，您**可以替换** APK 中的 `libav*.so` 为您自己编译的版本。
这是 LGPL 赋予您的权利，本应用不会阻止。

（需要用相同 ABI 的版本，例如 arm64-v8a 对应 aarch64 构建。）

## 许可全文

GNU Lesser General Public License v2.1 全文：
https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt

Copyright (c) FFmpeg developers.
