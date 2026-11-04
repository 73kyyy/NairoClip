# nairoclip-ffmpeg-build

本仓库**只包含 FFmpeg 的构建脚本、补丁与许可声明**，不含 NairoClip App 的任何源代码。

## 为什么有这个仓库

NairoClip 以 **LGPL-2.1-or-later** 使用 FFmpeg，采用**动态链接**（`libavcodec.so` /
`libavformat.so` / `libavutil.so` / `libswscale.so` / `libswresample.so`）。

LGPL 允许应用自身闭源，但要求：

1. **动态链接**（已完成 —— 见 `build-android.sh` 的 `--enable-shared --disable-static`）
2. **用户可以替换 FFmpeg 库**（因为动态链接，替换 `.so` 即可）
3. **提供 FFmpeg 源代码的获取途径**
4. **若修改过 FFmpeg，需说明修改内容**

本仓库承担第 3、4 项。

## 我们改了什么

**默认情况下：没有修改 FFmpeg 源代码。**

若某个 ABI 需要兼容性补丁，补丁放在 `patches/` 下，并在 `build-android.sh` 里自动应用。
当前 `patches/` 为空。

## 构建配置

见 `build-android.sh`。关键开关：

```
--disable-gpl          # 不启用任何 GPL 组件（libx264/libx265 等）
--disable-nonfree      # 不使用不可分发的组件
--enable-shared        # 动态库（LGPL 要求）
--disable-static       # 不产出静态库（避免误用成静态链接）
--enable-jni           # MediaCodec 硬解支持
--enable-mediacodec    # 同上
```

## 获取 FFmpeg 源代码

对应版本：见 `VERSION` 文件（当前 `7.1`）。

- 官方源码：https://ffmpeg.org/releases/ffmpeg-7.1.tar.xz
- 镜像：https://github.com/FFmpeg/FFmpeg/archive/refs/tags/n7.1.tar.gz

自行构建（需 Android NDK r27d）：

```bash
export ANDROID_NDK=/path/to/android-ndk-r27d
./build-android.sh
```

产出：`out/<abi>/lib/libav*.so`

## 许可

FFmpeg 以 LGPL-2.1-or-later 发布，全文见 `COPYING.LGPLv2.1`。
本仓库的构建脚本以 MIT 发布。

## 联系

若上述途径均不可用，可来信索取源码与构建脚本：<在此填写联系邮箱>
