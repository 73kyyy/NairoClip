#!/usr/bin/env bash
# =============================================================================
# 为 Android 构建 FFmpeg 共享库（LGPL 合规：必须 --enable-shared）
# =============================================================================
# 产物：out/<abi>/lib/libav{codec,format,util,swscale,swresample}.so
#
# 关键设计：
#   · --disable-static —— 只产出 .so。同时产出 .a 会让人误用静态链接，
#     而静态链接 LGPL 库要求整个应用按 LGPL 开源，与本项目闭源策略冲突。
#   · --disable-gpl / --disable-nonfree —— 法律边界，不可放开。
#   · --enable-jni + --enable-mediacodec —— 硬解路径（P7 的基础）。

#
# Android 媒体编解码策略：不打包任何软件编解码器。
#   · 视频/音频解码只启用 FFmpeg 的 *_mediacodec 系统 MediaCodec wrapper。
#   · 视频编码只启用 *_mediacodec 系统 MediaCodec wrapper。
#   · FFmpeg 仅保留容器 demux/mux、bitstream parser、像素/采样格式转换。
#   · MediaCodec 不支持的格式必须 fail-closed，禁止静默调用软件解码/编码。
#   · MediaCodec wrapper 只是系统 API 适配层，不是 APK 内置编解码实现。
#   · Android AAC 音频编码由独立系统 MediaCodec + MediaMuxer 接入负责；
#     当前 FFmpeg writer 不允许退回内置 AAC 软件编码器。
#   · H.264 仅允许通过设备系统 MediaCodec 使用；HEVC/H.265 全线禁用。
#   · 只保留需要的组件，控制 APK 体积。
# =============================================================================
set -euo pipefail

FFMPEG_VERSION="$(cat "$(dirname "$0")/VERSION")"
ROOT="$(cd "$(dirname "$0")" && pwd)"
OUT="$ROOT/out"
SRC="$ROOT/src/ffmpeg-$FFMPEG_VERSION"
: "${ANDROID_NDK:?请先 export ANDROID_NDK=/path/to/android-ndk-r27d}"
: "${ANDROID_API:=24}"

mkdir -p "$ROOT/src" "$OUT"

# ---- 取源码（幂等）----
if [ ! -d "$SRC" ]; then
    echo "==> 下载 FFmpeg $FFMPEG_VERSION"
    # 多镜像 + 强重试。
    # 起因：run #119 报 `curl: (35) Recv failure: Connection reset by peer`，
    # 是 ffmpeg.org 单点网络抖动。而且 actions/cache 只在 job 成功后才会保存，
    # 之前每次构建都失败 → 缓存从未落地 → 每次都要重新下载，撞上抖动的概率很高。
    TAR="$ROOT/src/ffmpeg.tar.xz"
    OK=0
    for URL in \
        "https://ffmpeg.org/releases/ffmpeg-$FFMPEG_VERSION.tar.xz" \
        "https://www.ffmpeg.org/releases/ffmpeg-$FFMPEG_VERSION.tar.xz" \
        "https://fossies.org/linux/misc/ffmpeg-$FFMPEG_VERSION.tar.xz"; do
        echo "==> 尝试 $URL"
        if curl -fL --retry 5 --retry-delay 5 --retry-all-errors \
                --connect-timeout 30 --max-time 600 -o "$TAR" "$URL"; then
            OK=1
            break
        fi
        echo "    失败，换下一个镜像"
    done
    if [ "$OK" != "1" ]; then
        echo "❌ 所有镜像都下载失败：ffmpeg-$FFMPEG_VERSION.tar.xz"
        exit 1
    fi
    tar -xJf "$ROOT/src/ffmpeg.tar.xz" -C "$ROOT/src"
fi

# ---- 应用补丁（当前为空）----
if compgen -G "$ROOT/patches/*.patch" > /dev/null; then
    for p in "$ROOT"/patches/*.patch; do
        echo "==> 应用补丁 $p"
        patch -d "$SRC" -p1 < "$p"
    done
fi

build_abi() {
    local ABI="$1" TRIPLE="$2" CPU="$3"
    local PREFIX="$OUT/$ABI"

    # 每个 ABI 的 API 级别要与 android-apk.yml 的 core 构建保持一致：
    #   arm64-v8a → 24，armeabi-v7a → 21，x86_64 → 21
    # 不一致会导致 FFmpeg 的 .so 要求更高 API，
    # 而 core 按低 API 编译，装到老设备上直接找不到符号。
    local API="$ANDROID_API"
    case "$ABI" in
        arm64-v8a)   API=24 ;;
        armeabi-v7a) API=21 ;;
        x86_64)      API=21 ;;
    esac

    echo "==> 构建 $ABI ($CPU)"
    rm -rf "$PREFIX"
    mkdir -p "$PREFIX"

    local EXTRA=""
    case "$ABI" in
        arm64-v8a)   EXTRA="--enable-neon" ;;
        armeabi-v7a) EXTRA="--enable-neon --cpu=armv7-a" ;;
        x86_64)      EXTRA="" ;;
    esac

    cd "$SRC"
    make distclean >/dev/null 2>&1 || true

    # 说明（放在命令外，避免 shell 把 /* */ 当参数）：
    #   --host-cc       configure 需要一个**能在构建机上跑**的编译器做探测，
    #                   不是目标编译器。不指定时它会猜，猜错就是
    #                   「C compiler test failed」。
    #   --cross-prefix 留空：cc/cxx 已给完整路径，再加前缀会拼出错误路径。
    #   --disable-doc  连同 html/man/pod/txt 一起关，
    #                   否则会 INSTALL 一堆 doc/examples 占时间。
    ./configure \
        --prefix="$PREFIX" \
        --target-os=android \
        --arch="$CPU" \
        --cc="$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/${TRIPLE}${API}-clang" \
        --cxx="$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/${TRIPLE}${API}-clang++" \
        --ar="$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-ar" \
        --ranlib="$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-ranlib" \
        --strip="$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" \
        --sysroot="$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/sysroot" \
        --enable-cross-compile \
        --host-cc="gcc" \
        --cross-prefix="" \
        --enable-shared \
        --disable-static \
        --disable-gpl \
        --disable-nonfree \
        --disable-version3 \
        --disable-programs \
        --disable-doc \
        --disable-htmlpages \
        --disable-manpages \
        --disable-podpages \
        --disable-txtpages \
        --disable-avdevice \
        --disable-postproc \
        --disable-swscale-alpha \
        --enable-jni \
        --enable-mediacodec \
        --disable-decoders \
        --disable-encoders \
        --enable-decoder=h264_mediacodec,mpeg2_mediacodec,mpeg4_mediacodec,vp8_mediacodec,vp9_mediacodec,av1_mediacodec,aac_mediacodec,amrnb_mediacodec,amrwb_mediacodec,mp3_mediacodec \
        --enable-encoder=h264_mediacodec,vp8_mediacodec,vp9_mediacodec,mpeg4_mediacodec,av1_mediacodec \
        --enable-demuxer=mov,matroska,avi,mp3,wav,flac,ogg,aac,mpegts \
        --enable-muxer=mp4,mov,matroska,wav,mp3 \
        --enable-parser=h264,vp9,av1,aac,mpegaudio,opus,flac \
        --enable-protocol=file,pipe \
        --enable-filter=aresample,scale,null,format \
        --disable-debug \
        --enable-small \
        $EXTRA \
        --extra-cflags="-fPIC -O2 -DANDROID" \
        --extra-ldflags="-Wl,--no-undefined"

    # 对 configure 的最终产物做硬门禁，防止未来误把软件编解码器带进 APK。
    local COMPONENTS="$SRC/libavcodec/config_components.h"
    if [ ! -f "$COMPONENTS" ]; then
        echo "❌ 找不到 FFmpeg 生成的 config_components.h：$COMPONENTS"
        exit 1
    fi
    for codec in H264 HEVC MPEG2VIDEO MPEG4 VP8 VP9 AV1 AAC MP3 OPUS FLAC VORBIS PCM_S16LE PCM_S16BE AMRNB AMRWB; do
        if grep -Eq "^#define CONFIG_${codec}_DECODER 1$" "$COMPONENTS"; then
            echo "❌ Android 禁止软件解码器进入构建：${codec}"
            exit 1
        fi
    done
    for codec in H264 HEVC MPEG4 VP8 VP9 AV1 AAC OPUS MP3 FLAC PCM_S16LE; do
        if grep -Eq "^#define CONFIG_${codec}_ENCODER 1$" "$COMPONENTS"; then
            echo "❌ Android 禁止软件编码器进入构建：${codec}"
            exit 1
        fi
    done
    for codec in H264 MPEG2 MPEG4 VP8 VP9 AV1 AAC AMRNB AMRWB MP3; do
        if ! grep -Eq "^#define CONFIG_${codec}_MEDIACODEC_DECODER 1$" "$COMPONENTS"; then
            echo "❌ 缺少系统 MediaCodec 解码 wrapper：${codec}"
            exit 1
        fi
    done
    if grep -E '^#define CONFIG_[A-Z0-9_]+_DECODER 1$' "$COMPONENTS" | grep -Ev '^#define CONFIG_[A-Z0-9]+_MEDIACODEC_DECODER 1$'; then
        echo "❌ Android FFmpeg 配置包含非 MediaCodec 软件解码器"
        exit 1
    fi
    for codec in H264 MPEG4 VP8 VP9 AV1; do
        if ! grep -Eq "^#define CONFIG_${codec}_MEDIACODEC_ENCODER 1$" "$COMPONENTS"; then
            echo "❌ 缺少系统 MediaCodec 视频编码 wrapper：${codec}"
            exit 1
        fi
    done
    if grep -E '^#define CONFIG_[A-Z0-9_]+_ENCODER 1$' "$COMPONENTS" | grep -Ev '^#define CONFIG_[A-Z0-9]+_MEDIACODEC_ENCODER 1$'; then
        echo "❌ Android FFmpeg 配置包含非 MediaCodec 软件编码器"
        exit 1
    fi
    # 编译失败时把真正的错误打出来。
    # 原来直接 make，CI 日志只留尾部若干行，而 FFmpeg 编译到 libavcodec
    # 时才出错 —— 尾部全是无关的 warning，真错误被截掉，等于没法排查。
    local JOBS="${JOBS:-$(nproc)}"
    if ! make -j"$JOBS" > "$ROOT/build-$ABI.log" 2>&1; then
        echo "❌ $ABI 编译失败（make -j$JOBS），最后 120 行："
        tail -n 120 "$ROOT/build-$ABI.log"
        exit 1
    fi
    if ! make install > "$ROOT/install-$ABI.log" 2>&1; then
        echo "❌ $ABI 安装失败，最后 80 行："
        tail -n 80 "$ROOT/install-$ABI.log"
        exit 1
    fi

    # 剥离调试符号（体积）；LGPL 不要求保留符号，源码可获取即可
    find "$PREFIX/lib" -name '*.so' -exec "$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" --strip-unneeded {} \;
    ls -lh "$PREFIX/lib"
}

# armeabi-v7a 的 triple 是 **armv7a**-linux-androideabi，不是 arm-linux-androideabi。
# 用错会得到「unable to create an executable file / C compiler test failed」——
# 因为 NDK 的 clang wrapper 按 triple 名找目标文件，名字不对就找不到。
# --arch 用 FFmpeg 的命名：arm / aarch64 / x86_64（不是 arm64）。
build_abi arm64-v8a   aarch64-linux-android  aarch64
# 只出 arm64-v8a（2026-11-07）：x86_64 只用于模拟器且需要 nasm，是最脆的一路；
# armeabi-v7a 在现代机上已极少。要恢复就把下面两行取消注释，并同步改
# android/app/build.gradle.kts 的 abiFilters 与 workflow 的三处 for ABI 循环。
# build_abi armeabi-v7a armv7a-linux-androideabi arm
# build_abi x86_64      x86_64-linux-android   x86_64

echo "==> 完成。产物在 $OUT/<abi>/lib"
 "$COMPONENTS" | grep -Ev '^#define CONFIG_[A-Z0-9]+_MEDIACODEC_DECODER 1
    for codec in H264 MPEG4 VP8 VP9 AV1; do
        if ! grep -Eq "^#define CONFIG_${codec}_MEDIACODEC_ENCODER 1$" "$COMPONENTS"; then
            echo "❌ 缺少系统 MediaCodec 视频编码 wrapper：${codec}"
            exit 1
        fi
    done

    # 编译失败时把真正的错误打出来。
    # 原来直接 make，CI 日志只留尾部若干行，而 FFmpeg 编译到 libavcodec
    # 时才出错 —— 尾部全是无关的 warning，真错误被截掉，等于没法排查。
    local JOBS="${JOBS:-$(nproc)}"
    if ! make -j"$JOBS" > "$ROOT/build-$ABI.log" 2>&1; then
        echo "❌ $ABI 编译失败（make -j$JOBS），最后 120 行："
        tail -n 120 "$ROOT/build-$ABI.log"
        exit 1
    fi
    if ! make install > "$ROOT/install-$ABI.log" 2>&1; then
        echo "❌ $ABI 安装失败，最后 80 行："
        tail -n 80 "$ROOT/install-$ABI.log"
        exit 1
    fi

    # 剥离调试符号（体积）；LGPL 不要求保留符号，源码可获取即可
    find "$PREFIX/lib" -name '*.so' -exec "$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" --strip-unneeded {} \;
    ls -lh "$PREFIX/lib"
}

# armeabi-v7a 的 triple 是 **armv7a**-linux-androideabi，不是 arm-linux-androideabi。
# 用错会得到「unable to create an executable file / C compiler test failed」——
# 因为 NDK 的 clang wrapper 按 triple 名找目标文件，名字不对就找不到。
# --arch 用 FFmpeg 的命名：arm / aarch64 / x86_64（不是 arm64）。
build_abi arm64-v8a   aarch64-linux-android  aarch64
# 只出 arm64-v8a（2026-11-07）：x86_64 只用于模拟器且需要 nasm，是最脆的一路；
# armeabi-v7a 在现代机上已极少。要恢复就把下面两行取消注释，并同步改
# android/app/build.gradle.kts 的 abiFilters 与 workflow 的三处 for ABI 循环。
# build_abi armeabi-v7a armv7a-linux-androideabi arm
# build_abi x86_64      x86_64-linux-android   x86_64

echo "==> 完成。产物在 $OUT/<abi>/lib"
; then
        echo "❌ Android FFmpeg 配置包含非 MediaCodec 软件解码器"
        exit 1
    fi
    if grep -E '^#define CONFIG_[A-Z0-9_]+_ENCODER 1
    for codec in H264 MPEG4 VP8 VP9 AV1; do
        if ! grep -Eq "^#define CONFIG_${codec}_MEDIACODEC_ENCODER 1$" "$COMPONENTS"; then
            echo "❌ 缺少系统 MediaCodec 视频编码 wrapper：${codec}"
            exit 1
        fi
    done

    # 编译失败时把真正的错误打出来。
    # 原来直接 make，CI 日志只留尾部若干行，而 FFmpeg 编译到 libavcodec
    # 时才出错 —— 尾部全是无关的 warning，真错误被截掉，等于没法排查。
    local JOBS="${JOBS:-$(nproc)}"
    if ! make -j"$JOBS" > "$ROOT/build-$ABI.log" 2>&1; then
        echo "❌ $ABI 编译失败（make -j$JOBS），最后 120 行："
        tail -n 120 "$ROOT/build-$ABI.log"
        exit 1
    fi
    if ! make install > "$ROOT/install-$ABI.log" 2>&1; then
        echo "❌ $ABI 安装失败，最后 80 行："
        tail -n 80 "$ROOT/install-$ABI.log"
        exit 1
    fi

    # 剥离调试符号（体积）；LGPL 不要求保留符号，源码可获取即可
    find "$PREFIX/lib" -name '*.so' -exec "$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" --strip-unneeded {} \;
    ls -lh "$PREFIX/lib"
}

# armeabi-v7a 的 triple 是 **armv7a**-linux-androideabi，不是 arm-linux-androideabi。
# 用错会得到「unable to create an executable file / C compiler test failed」——
# 因为 NDK 的 clang wrapper 按 triple 名找目标文件，名字不对就找不到。
# --arch 用 FFmpeg 的命名：arm / aarch64 / x86_64（不是 arm64）。
build_abi arm64-v8a   aarch64-linux-android  aarch64
# 只出 arm64-v8a（2026-11-07）：x86_64 只用于模拟器且需要 nasm，是最脆的一路；
# armeabi-v7a 在现代机上已极少。要恢复就把下面两行取消注释，并同步改
# android/app/build.gradle.kts 的 abiFilters 与 workflow 的三处 for ABI 循环。
# build_abi armeabi-v7a armv7a-linux-androideabi arm
# build_abi x86_64      x86_64-linux-android   x86_64

echo "==> 完成。产物在 $OUT/<abi>/lib"
 "$COMPONENTS" | grep -Ev '^#define CONFIG_[A-Z0-9]+_MEDIACODEC_ENCODER 1
    for codec in H264 MPEG4 VP8 VP9 AV1; do
        if ! grep -Eq "^#define CONFIG_${codec}_MEDIACODEC_ENCODER 1$" "$COMPONENTS"; then
            echo "❌ 缺少系统 MediaCodec 视频编码 wrapper：${codec}"
            exit 1
        fi
    done

    # 编译失败时把真正的错误打出来。
    # 原来直接 make，CI 日志只留尾部若干行，而 FFmpeg 编译到 libavcodec
    # 时才出错 —— 尾部全是无关的 warning，真错误被截掉，等于没法排查。
    local JOBS="${JOBS:-$(nproc)}"
    if ! make -j"$JOBS" > "$ROOT/build-$ABI.log" 2>&1; then
        echo "❌ $ABI 编译失败（make -j$JOBS），最后 120 行："
        tail -n 120 "$ROOT/build-$ABI.log"
        exit 1
    fi
    if ! make install > "$ROOT/install-$ABI.log" 2>&1; then
        echo "❌ $ABI 安装失败，最后 80 行："
        tail -n 80 "$ROOT/install-$ABI.log"
        exit 1
    fi

    # 剥离调试符号（体积）；LGPL 不要求保留符号，源码可获取即可
    find "$PREFIX/lib" -name '*.so' -exec "$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" --strip-unneeded {} \;
    ls -lh "$PREFIX/lib"
}

# armeabi-v7a 的 triple 是 **armv7a**-linux-androideabi，不是 arm-linux-androideabi。
# 用错会得到「unable to create an executable file / C compiler test failed」——
# 因为 NDK 的 clang wrapper 按 triple 名找目标文件，名字不对就找不到。
# --arch 用 FFmpeg 的命名：arm / aarch64 / x86_64（不是 arm64）。
build_abi arm64-v8a   aarch64-linux-android  aarch64
# 只出 arm64-v8a（2026-11-07）：x86_64 只用于模拟器且需要 nasm，是最脆的一路；
# armeabi-v7a 在现代机上已极少。要恢复就把下面两行取消注释，并同步改
# android/app/build.gradle.kts 的 abiFilters 与 workflow 的三处 for ABI 循环。
# build_abi armeabi-v7a armv7a-linux-androideabi arm
# build_abi x86_64      x86_64-linux-android   x86_64

echo "==> 完成。产物在 $OUT/<abi>/lib"
; then
        echo "❌ Android FFmpeg 配置包含非 MediaCodec 软件编码器"
        exit 1
    fi
    for codec in H264 MPEG4 VP8 VP9 AV1; do
        if ! grep -Eq "^#define CONFIG_${codec}_MEDIACODEC_ENCODER 1$" "$COMPONENTS"; then
            echo "❌ 缺少系统 MediaCodec 视频编码 wrapper：${codec}"
            exit 1
        fi
    done

    # 编译失败时把真正的错误打出来。
    # 原来直接 make，CI 日志只留尾部若干行，而 FFmpeg 编译到 libavcodec
    # 时才出错 —— 尾部全是无关的 warning，真错误被截掉，等于没法排查。
    local JOBS="${JOBS:-$(nproc)}"
    if ! make -j"$JOBS" > "$ROOT/build-$ABI.log" 2>&1; then
        echo "❌ $ABI 编译失败（make -j$JOBS），最后 120 行："
        tail -n 120 "$ROOT/build-$ABI.log"
        exit 1
    fi
    if ! make install > "$ROOT/install-$ABI.log" 2>&1; then
        echo "❌ $ABI 安装失败，最后 80 行："
        tail -n 80 "$ROOT/install-$ABI.log"
        exit 1
    fi

    # 剥离调试符号（体积）；LGPL 不要求保留符号，源码可获取即可
    find "$PREFIX/lib" -name '*.so' -exec "$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" --strip-unneeded {} \;
    ls -lh "$PREFIX/lib"
}

# armeabi-v7a 的 triple 是 **armv7a**-linux-androideabi，不是 arm-linux-androideabi。
# 用错会得到「unable to create an executable file / C compiler test failed」——
# 因为 NDK 的 clang wrapper 按 triple 名找目标文件，名字不对就找不到。
# --arch 用 FFmpeg 的命名：arm / aarch64 / x86_64（不是 arm64）。
build_abi arm64-v8a   aarch64-linux-android  aarch64
# 只出 arm64-v8a（2026-11-07）：x86_64 只用于模拟器且需要 nasm，是最脆的一路；
# armeabi-v7a 在现代机上已极少。要恢复就把下面两行取消注释，并同步改
# android/app/build.gradle.kts 的 abiFilters 与 workflow 的三处 for ABI 循环。
# build_abi armeabi-v7a armv7a-linux-androideabi arm
# build_abi x86_64      x86_64-linux-android   x86_64

echo "==> 完成。产物在 $OUT/<abi>/lib"
 "$COMPONENTS" | grep -Ev '^#define CONFIG_[A-Z0-9]+_MEDIACODEC_DECODER 1
        if ! grep -Eq "^#define CONFIG_${codec}_MEDIACODEC_ENCODER 1$" "$COMPONENTS"; then
            echo "❌ 缺少系统 MediaCodec 视频编码 wrapper：${codec}"
            exit 1
        fi
    done

    # 编译失败时把真正的错误打出来。
    # 原来直接 make，CI 日志只留尾部若干行，而 FFmpeg 编译到 libavcodec
    # 时才出错 —— 尾部全是无关的 warning，真错误被截掉，等于没法排查。
    local JOBS="${JOBS:-$(nproc)}"
    if ! make -j"$JOBS" > "$ROOT/build-$ABI.log" 2>&1; then
        echo "❌ $ABI 编译失败（make -j$JOBS），最后 120 行："
        tail -n 120 "$ROOT/build-$ABI.log"
        exit 1
    fi
    if ! make install > "$ROOT/install-$ABI.log" 2>&1; then
        echo "❌ $ABI 安装失败，最后 80 行："
        tail -n 80 "$ROOT/install-$ABI.log"
        exit 1
    fi

    # 剥离调试符号（体积）；LGPL 不要求保留符号，源码可获取即可
    find "$PREFIX/lib" -name '*.so' -exec "$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" --strip-unneeded {} \;
    ls -lh "$PREFIX/lib"
}

# armeabi-v7a 的 triple 是 **armv7a**-linux-androideabi，不是 arm-linux-androideabi。
# 用错会得到「unable to create an executable file / C compiler test failed」——
# 因为 NDK 的 clang wrapper 按 triple 名找目标文件，名字不对就找不到。
# --arch 用 FFmpeg 的命名：arm / aarch64 / x86_64（不是 arm64）。
build_abi arm64-v8a   aarch64-linux-android  aarch64
# 只出 arm64-v8a（2026-11-07）：x86_64 只用于模拟器且需要 nasm，是最脆的一路；
# armeabi-v7a 在现代机上已极少。要恢复就把下面两行取消注释，并同步改
# android/app/build.gradle.kts 的 abiFilters 与 workflow 的三处 for ABI 循环。
# build_abi armeabi-v7a armv7a-linux-androideabi arm
# build_abi x86_64      x86_64-linux-android   x86_64

echo "==> 完成。产物在 $OUT/<abi>/lib"
 "$COMPONENTS" | grep -Ev '^#define CONFIG_[A-Z0-9]+_MEDIACODEC_DECODER 1
    for codec in H264 MPEG4 VP8 VP9 AV1; do
        if ! grep -Eq "^#define CONFIG_${codec}_MEDIACODEC_ENCODER 1$" "$COMPONENTS"; then
            echo "❌ 缺少系统 MediaCodec 视频编码 wrapper：${codec}"
            exit 1
        fi
    done

    # 编译失败时把真正的错误打出来。
    # 原来直接 make，CI 日志只留尾部若干行，而 FFmpeg 编译到 libavcodec
    # 时才出错 —— 尾部全是无关的 warning，真错误被截掉，等于没法排查。
    local JOBS="${JOBS:-$(nproc)}"
    if ! make -j"$JOBS" > "$ROOT/build-$ABI.log" 2>&1; then
        echo "❌ $ABI 编译失败（make -j$JOBS），最后 120 行："
        tail -n 120 "$ROOT/build-$ABI.log"
        exit 1
    fi
    if ! make install > "$ROOT/install-$ABI.log" 2>&1; then
        echo "❌ $ABI 安装失败，最后 80 行："
        tail -n 80 "$ROOT/install-$ABI.log"
        exit 1
    fi

    # 剥离调试符号（体积）；LGPL 不要求保留符号，源码可获取即可
    find "$PREFIX/lib" -name '*.so' -exec "$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" --strip-unneeded {} \;
    ls -lh "$PREFIX/lib"
}

# armeabi-v7a 的 triple 是 **armv7a**-linux-androideabi，不是 arm-linux-androideabi。
# 用错会得到「unable to create an executable file / C compiler test failed」——
# 因为 NDK 的 clang wrapper 按 triple 名找目标文件，名字不对就找不到。
# --arch 用 FFmpeg 的命名：arm / aarch64 / x86_64（不是 arm64）。
build_abi arm64-v8a   aarch64-linux-android  aarch64
# 只出 arm64-v8a（2026-11-07）：x86_64 只用于模拟器且需要 nasm，是最脆的一路；
# armeabi-v7a 在现代机上已极少。要恢复就把下面两行取消注释，并同步改
# android/app/build.gradle.kts 的 abiFilters 与 workflow 的三处 for ABI 循环。
# build_abi armeabi-v7a armv7a-linux-androideabi arm
# build_abi x86_64      x86_64-linux-android   x86_64

echo "==> 完成。产物在 $OUT/<abi>/lib"
; then
        echo "❌ Android FFmpeg 配置包含非 MediaCodec 软件解码器"
        exit 1
    fi
    if grep -E '^#define CONFIG_[A-Z0-9_]+_ENCODER 1
    for codec in H264 MPEG4 VP8 VP9 AV1; do
        if ! grep -Eq "^#define CONFIG_${codec}_MEDIACODEC_ENCODER 1$" "$COMPONENTS"; then
            echo "❌ 缺少系统 MediaCodec 视频编码 wrapper：${codec}"
            exit 1
        fi
    done

    # 编译失败时把真正的错误打出来。
    # 原来直接 make，CI 日志只留尾部若干行，而 FFmpeg 编译到 libavcodec
    # 时才出错 —— 尾部全是无关的 warning，真错误被截掉，等于没法排查。
    local JOBS="${JOBS:-$(nproc)}"
    if ! make -j"$JOBS" > "$ROOT/build-$ABI.log" 2>&1; then
        echo "❌ $ABI 编译失败（make -j$JOBS），最后 120 行："
        tail -n 120 "$ROOT/build-$ABI.log"
        exit 1
    fi
    if ! make install > "$ROOT/install-$ABI.log" 2>&1; then
        echo "❌ $ABI 安装失败，最后 80 行："
        tail -n 80 "$ROOT/install-$ABI.log"
        exit 1
    fi

    # 剥离调试符号（体积）；LGPL 不要求保留符号，源码可获取即可
    find "$PREFIX/lib" -name '*.so' -exec "$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" --strip-unneeded {} \;
    ls -lh "$PREFIX/lib"
}

# armeabi-v7a 的 triple 是 **armv7a**-linux-androideabi，不是 arm-linux-androideabi。
# 用错会得到「unable to create an executable file / C compiler test failed」——
# 因为 NDK 的 clang wrapper 按 triple 名找目标文件，名字不对就找不到。
# --arch 用 FFmpeg 的命名：arm / aarch64 / x86_64（不是 arm64）。
build_abi arm64-v8a   aarch64-linux-android  aarch64
# 只出 arm64-v8a（2026-11-07）：x86_64 只用于模拟器且需要 nasm，是最脆的一路；
# armeabi-v7a 在现代机上已极少。要恢复就把下面两行取消注释，并同步改
# android/app/build.gradle.kts 的 abiFilters 与 workflow 的三处 for ABI 循环。
# build_abi armeabi-v7a armv7a-linux-androideabi arm
# build_abi x86_64      x86_64-linux-android   x86_64

echo "==> 完成。产物在 $OUT/<abi>/lib"
 "$COMPONENTS" | grep -Ev '^#define CONFIG_[A-Z0-9]+_MEDIACODEC_ENCODER 1
    for codec in H264 MPEG4 VP8 VP9 AV1; do
        if ! grep -Eq "^#define CONFIG_${codec}_MEDIACODEC_ENCODER 1$" "$COMPONENTS"; then
            echo "❌ 缺少系统 MediaCodec 视频编码 wrapper：${codec}"
            exit 1
        fi
    done

    # 编译失败时把真正的错误打出来。
    # 原来直接 make，CI 日志只留尾部若干行，而 FFmpeg 编译到 libavcodec
    # 时才出错 —— 尾部全是无关的 warning，真错误被截掉，等于没法排查。
    local JOBS="${JOBS:-$(nproc)}"
    if ! make -j"$JOBS" > "$ROOT/build-$ABI.log" 2>&1; then
        echo "❌ $ABI 编译失败（make -j$JOBS），最后 120 行："
        tail -n 120 "$ROOT/build-$ABI.log"
        exit 1
    fi
    if ! make install > "$ROOT/install-$ABI.log" 2>&1; then
        echo "❌ $ABI 安装失败，最后 80 行："
        tail -n 80 "$ROOT/install-$ABI.log"
        exit 1
    fi

    # 剥离调试符号（体积）；LGPL 不要求保留符号，源码可获取即可
    find "$PREFIX/lib" -name '*.so' -exec "$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" --strip-unneeded {} \;
    ls -lh "$PREFIX/lib"
}

# armeabi-v7a 的 triple 是 **armv7a**-linux-androideabi，不是 arm-linux-androideabi。
# 用错会得到「unable to create an executable file / C compiler test failed」——
# 因为 NDK 的 clang wrapper 按 triple 名找目标文件，名字不对就找不到。
# --arch 用 FFmpeg 的命名：arm / aarch64 / x86_64（不是 arm64）。
build_abi arm64-v8a   aarch64-linux-android  aarch64
# 只出 arm64-v8a（2026-11-07）：x86_64 只用于模拟器且需要 nasm，是最脆的一路；
# armeabi-v7a 在现代机上已极少。要恢复就把下面两行取消注释，并同步改
# android/app/build.gradle.kts 的 abiFilters 与 workflow 的三处 for ABI 循环。
# build_abi armeabi-v7a armv7a-linux-androideabi arm
# build_abi x86_64      x86_64-linux-android   x86_64

echo "==> 完成。产物在 $OUT/<abi>/lib"
; then
        echo "❌ Android FFmpeg 配置包含非 MediaCodec 软件编码器"
        exit 1
    fi
    for codec in H264 MPEG4 VP8 VP9 AV1; do
        if ! grep -Eq "^#define CONFIG_${codec}_MEDIACODEC_ENCODER 1$" "$COMPONENTS"; then
            echo "❌ 缺少系统 MediaCodec 视频编码 wrapper：${codec}"
            exit 1
        fi
    done

    # 编译失败时把真正的错误打出来。
    # 原来直接 make，CI 日志只留尾部若干行，而 FFmpeg 编译到 libavcodec
    # 时才出错 —— 尾部全是无关的 warning，真错误被截掉，等于没法排查。
    local JOBS="${JOBS:-$(nproc)}"
    if ! make -j"$JOBS" > "$ROOT/build-$ABI.log" 2>&1; then
        echo "❌ $ABI 编译失败（make -j$JOBS），最后 120 行："
        tail -n 120 "$ROOT/build-$ABI.log"
        exit 1
    fi
    if ! make install > "$ROOT/install-$ABI.log" 2>&1; then
        echo "❌ $ABI 安装失败，最后 80 行："
        tail -n 80 "$ROOT/install-$ABI.log"
        exit 1
    fi

    # 剥离调试符号（体积）；LGPL 不要求保留符号，源码可获取即可
    find "$PREFIX/lib" -name '*.so' -exec "$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" --strip-unneeded {} \;
    ls -lh "$PREFIX/lib"
}

# armeabi-v7a 的 triple 是 **armv7a**-linux-androideabi，不是 arm-linux-androideabi。
# 用错会得到「unable to create an executable file / C compiler test failed」——
# 因为 NDK 的 clang wrapper 按 triple 名找目标文件，名字不对就找不到。
# --arch 用 FFmpeg 的命名：arm / aarch64 / x86_64（不是 arm64）。
build_abi arm64-v8a   aarch64-linux-android  aarch64
# 只出 arm64-v8a（2026-11-07）：x86_64 只用于模拟器且需要 nasm，是最脆的一路；
# armeabi-v7a 在现代机上已极少。要恢复就把下面两行取消注释，并同步改
# android/app/build.gradle.kts 的 abiFilters 与 workflow 的三处 for ABI 循环。
# build_abi armeabi-v7a armv7a-linux-androideabi arm
# build_abi x86_64      x86_64-linux-android   x86_64

echo "==> 完成。产物在 $OUT/<abi>/lib"
; then
        echo "❌ Android FFmpeg 配置包含非 MediaCodec 软件解码器"
        exit 1
    fi
    if grep -E '^#define CONFIG_[A-Z0-9_]+_ENCODER 1
        if ! grep -Eq "^#define CONFIG_${codec}_MEDIACODEC_ENCODER 1$" "$COMPONENTS"; then
            echo "❌ 缺少系统 MediaCodec 视频编码 wrapper：${codec}"
            exit 1
        fi
    done

    # 编译失败时把真正的错误打出来。
    # 原来直接 make，CI 日志只留尾部若干行，而 FFmpeg 编译到 libavcodec
    # 时才出错 —— 尾部全是无关的 warning，真错误被截掉，等于没法排查。
    local JOBS="${JOBS:-$(nproc)}"
    if ! make -j"$JOBS" > "$ROOT/build-$ABI.log" 2>&1; then
        echo "❌ $ABI 编译失败（make -j$JOBS），最后 120 行："
        tail -n 120 "$ROOT/build-$ABI.log"
        exit 1
    fi
    if ! make install > "$ROOT/install-$ABI.log" 2>&1; then
        echo "❌ $ABI 安装失败，最后 80 行："
        tail -n 80 "$ROOT/install-$ABI.log"
        exit 1
    fi

    # 剥离调试符号（体积）；LGPL 不要求保留符号，源码可获取即可
    find "$PREFIX/lib" -name '*.so' -exec "$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" --strip-unneeded {} \;
    ls -lh "$PREFIX/lib"
}

# armeabi-v7a 的 triple 是 **armv7a**-linux-androideabi，不是 arm-linux-androideabi。
# 用错会得到「unable to create an executable file / C compiler test failed」——
# 因为 NDK 的 clang wrapper 按 triple 名找目标文件，名字不对就找不到。
# --arch 用 FFmpeg 的命名：arm / aarch64 / x86_64（不是 arm64）。
build_abi arm64-v8a   aarch64-linux-android  aarch64
# 只出 arm64-v8a（2026-11-07）：x86_64 只用于模拟器且需要 nasm，是最脆的一路；
# armeabi-v7a 在现代机上已极少。要恢复就把下面两行取消注释，并同步改
# android/app/build.gradle.kts 的 abiFilters 与 workflow 的三处 for ABI 循环。
# build_abi armeabi-v7a armv7a-linux-androideabi arm
# build_abi x86_64      x86_64-linux-android   x86_64

echo "==> 完成。产物在 $OUT/<abi>/lib"
 "$COMPONENTS" | grep -Ev '^#define CONFIG_[A-Z0-9]+_MEDIACODEC_DECODER 1
    for codec in H264 MPEG4 VP8 VP9 AV1; do
        if ! grep -Eq "^#define CONFIG_${codec}_MEDIACODEC_ENCODER 1$" "$COMPONENTS"; then
            echo "❌ 缺少系统 MediaCodec 视频编码 wrapper：${codec}"
            exit 1
        fi
    done

    # 编译失败时把真正的错误打出来。
    # 原来直接 make，CI 日志只留尾部若干行，而 FFmpeg 编译到 libavcodec
    # 时才出错 —— 尾部全是无关的 warning，真错误被截掉，等于没法排查。
    local JOBS="${JOBS:-$(nproc)}"
    if ! make -j"$JOBS" > "$ROOT/build-$ABI.log" 2>&1; then
        echo "❌ $ABI 编译失败（make -j$JOBS），最后 120 行："
        tail -n 120 "$ROOT/build-$ABI.log"
        exit 1
    fi
    if ! make install > "$ROOT/install-$ABI.log" 2>&1; then
        echo "❌ $ABI 安装失败，最后 80 行："
        tail -n 80 "$ROOT/install-$ABI.log"
        exit 1
    fi

    # 剥离调试符号（体积）；LGPL 不要求保留符号，源码可获取即可
    find "$PREFIX/lib" -name '*.so' -exec "$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" --strip-unneeded {} \;
    ls -lh "$PREFIX/lib"
}

# armeabi-v7a 的 triple 是 **armv7a**-linux-androideabi，不是 arm-linux-androideabi。
# 用错会得到「unable to create an executable file / C compiler test failed」——
# 因为 NDK 的 clang wrapper 按 triple 名找目标文件，名字不对就找不到。
# --arch 用 FFmpeg 的命名：arm / aarch64 / x86_64（不是 arm64）。
build_abi arm64-v8a   aarch64-linux-android  aarch64
# 只出 arm64-v8a（2026-11-07）：x86_64 只用于模拟器且需要 nasm，是最脆的一路；
# armeabi-v7a 在现代机上已极少。要恢复就把下面两行取消注释，并同步改
# android/app/build.gradle.kts 的 abiFilters 与 workflow 的三处 for ABI 循环。
# build_abi armeabi-v7a armv7a-linux-androideabi arm
# build_abi x86_64      x86_64-linux-android   x86_64

echo "==> 完成。产物在 $OUT/<abi>/lib"
; then
        echo "❌ Android FFmpeg 配置包含非 MediaCodec 软件解码器"
        exit 1
    fi
    if grep -E '^#define CONFIG_[A-Z0-9_]+_ENCODER 1
    for codec in H264 MPEG4 VP8 VP9 AV1; do
        if ! grep -Eq "^#define CONFIG_${codec}_MEDIACODEC_ENCODER 1$" "$COMPONENTS"; then
            echo "❌ 缺少系统 MediaCodec 视频编码 wrapper：${codec}"
            exit 1
        fi
    done

    # 编译失败时把真正的错误打出来。
    # 原来直接 make，CI 日志只留尾部若干行，而 FFmpeg 编译到 libavcodec
    # 时才出错 —— 尾部全是无关的 warning，真错误被截掉，等于没法排查。
    local JOBS="${JOBS:-$(nproc)}"
    if ! make -j"$JOBS" > "$ROOT/build-$ABI.log" 2>&1; then
        echo "❌ $ABI 编译失败（make -j$JOBS），最后 120 行："
        tail -n 120 "$ROOT/build-$ABI.log"
        exit 1
    fi
    if ! make install > "$ROOT/install-$ABI.log" 2>&1; then
        echo "❌ $ABI 安装失败，最后 80 行："
        tail -n 80 "$ROOT/install-$ABI.log"
        exit 1
    fi

    # 剥离调试符号（体积）；LGPL 不要求保留符号，源码可获取即可
    find "$PREFIX/lib" -name '*.so' -exec "$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" --strip-unneeded {} \;
    ls -lh "$PREFIX/lib"
}

# armeabi-v7a 的 triple 是 **armv7a**-linux-androideabi，不是 arm-linux-androideabi。
# 用错会得到「unable to create an executable file / C compiler test failed」——
# 因为 NDK 的 clang wrapper 按 triple 名找目标文件，名字不对就找不到。
# --arch 用 FFmpeg 的命名：arm / aarch64 / x86_64（不是 arm64）。
build_abi arm64-v8a   aarch64-linux-android  aarch64
# 只出 arm64-v8a（2026-11-07）：x86_64 只用于模拟器且需要 nasm，是最脆的一路；
# armeabi-v7a 在现代机上已极少。要恢复就把下面两行取消注释，并同步改
# android/app/build.gradle.kts 的 abiFilters 与 workflow 的三处 for ABI 循环。
# build_abi armeabi-v7a armv7a-linux-androideabi arm
# build_abi x86_64      x86_64-linux-android   x86_64

echo "==> 完成。产物在 $OUT/<abi>/lib"
 "$COMPONENTS" | grep -Ev '^#define CONFIG_[A-Z0-9]+_MEDIACODEC_ENCODER 1
    for codec in H264 MPEG4 VP8 VP9 AV1; do
        if ! grep -Eq "^#define CONFIG_${codec}_MEDIACODEC_ENCODER 1$" "$COMPONENTS"; then
            echo "❌ 缺少系统 MediaCodec 视频编码 wrapper：${codec}"
            exit 1
        fi
    done

    # 编译失败时把真正的错误打出来。
    # 原来直接 make，CI 日志只留尾部若干行，而 FFmpeg 编译到 libavcodec
    # 时才出错 —— 尾部全是无关的 warning，真错误被截掉，等于没法排查。
    local JOBS="${JOBS:-$(nproc)}"
    if ! make -j"$JOBS" > "$ROOT/build-$ABI.log" 2>&1; then
        echo "❌ $ABI 编译失败（make -j$JOBS），最后 120 行："
        tail -n 120 "$ROOT/build-$ABI.log"
        exit 1
    fi
    if ! make install > "$ROOT/install-$ABI.log" 2>&1; then
        echo "❌ $ABI 安装失败，最后 80 行："
        tail -n 80 "$ROOT/install-$ABI.log"
        exit 1
    fi

    # 剥离调试符号（体积）；LGPL 不要求保留符号，源码可获取即可
    find "$PREFIX/lib" -name '*.so' -exec "$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" --strip-unneeded {} \;
    ls -lh "$PREFIX/lib"
}

# armeabi-v7a 的 triple 是 **armv7a**-linux-androideabi，不是 arm-linux-androideabi。
# 用错会得到「unable to create an executable file / C compiler test failed」——
# 因为 NDK 的 clang wrapper 按 triple 名找目标文件，名字不对就找不到。
# --arch 用 FFmpeg 的命名：arm / aarch64 / x86_64（不是 arm64）。
build_abi arm64-v8a   aarch64-linux-android  aarch64
# 只出 arm64-v8a（2026-11-07）：x86_64 只用于模拟器且需要 nasm，是最脆的一路；
# armeabi-v7a 在现代机上已极少。要恢复就把下面两行取消注释，并同步改
# android/app/build.gradle.kts 的 abiFilters 与 workflow 的三处 for ABI 循环。
# build_abi armeabi-v7a armv7a-linux-androideabi arm
# build_abi x86_64      x86_64-linux-android   x86_64

echo "==> 完成。产物在 $OUT/<abi>/lib"
; then
        echo "❌ Android FFmpeg 配置包含非 MediaCodec 软件编码器"
        exit 1
    fi
    for codec in H264 MPEG4 VP8 VP9 AV1; do
        if ! grep -Eq "^#define CONFIG_${codec}_MEDIACODEC_ENCODER 1$" "$COMPONENTS"; then
            echo "❌ 缺少系统 MediaCodec 视频编码 wrapper：${codec}"
            exit 1
        fi
    done

    # 编译失败时把真正的错误打出来。
    # 原来直接 make，CI 日志只留尾部若干行，而 FFmpeg 编译到 libavcodec
    # 时才出错 —— 尾部全是无关的 warning，真错误被截掉，等于没法排查。
    local JOBS="${JOBS:-$(nproc)}"
    if ! make -j"$JOBS" > "$ROOT/build-$ABI.log" 2>&1; then
        echo "❌ $ABI 编译失败（make -j$JOBS），最后 120 行："
        tail -n 120 "$ROOT/build-$ABI.log"
        exit 1
    fi
    if ! make install > "$ROOT/install-$ABI.log" 2>&1; then
        echo "❌ $ABI 安装失败，最后 80 行："
        tail -n 80 "$ROOT/install-$ABI.log"
        exit 1
    fi

    # 剥离调试符号（体积）；LGPL 不要求保留符号，源码可获取即可
    find "$PREFIX/lib" -name '*.so' -exec "$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" --strip-unneeded {} \;
    ls -lh "$PREFIX/lib"
}

# armeabi-v7a 的 triple 是 **armv7a**-linux-androideabi，不是 arm-linux-androideabi。
# 用错会得到「unable to create an executable file / C compiler test failed」——
# 因为 NDK 的 clang wrapper 按 triple 名找目标文件，名字不对就找不到。
# --arch 用 FFmpeg 的命名：arm / aarch64 / x86_64（不是 arm64）。
build_abi arm64-v8a   aarch64-linux-android  aarch64
# 只出 arm64-v8a（2026-11-07）：x86_64 只用于模拟器且需要 nasm，是最脆的一路；
# armeabi-v7a 在现代机上已极少。要恢复就把下面两行取消注释，并同步改
# android/app/build.gradle.kts 的 abiFilters 与 workflow 的三处 for ABI 循环。
# build_abi armeabi-v7a armv7a-linux-androideabi arm
# build_abi x86_64      x86_64-linux-android   x86_64

echo "==> 完成。产物在 $OUT/<abi>/lib"
 "$COMPONENTS" | grep -Ev '^#define CONFIG_[A-Z0-9]+_MEDIACODEC_ENCODER 1
        if ! grep -Eq "^#define CONFIG_${codec}_MEDIACODEC_ENCODER 1$" "$COMPONENTS"; then
            echo "❌ 缺少系统 MediaCodec 视频编码 wrapper：${codec}"
            exit 1
        fi
    done

    # 编译失败时把真正的错误打出来。
    # 原来直接 make，CI 日志只留尾部若干行，而 FFmpeg 编译到 libavcodec
    # 时才出错 —— 尾部全是无关的 warning，真错误被截掉，等于没法排查。
    local JOBS="${JOBS:-$(nproc)}"
    if ! make -j"$JOBS" > "$ROOT/build-$ABI.log" 2>&1; then
        echo "❌ $ABI 编译失败（make -j$JOBS），最后 120 行："
        tail -n 120 "$ROOT/build-$ABI.log"
        exit 1
    fi
    if ! make install > "$ROOT/install-$ABI.log" 2>&1; then
        echo "❌ $ABI 安装失败，最后 80 行："
        tail -n 80 "$ROOT/install-$ABI.log"
        exit 1
    fi

    # 剥离调试符号（体积）；LGPL 不要求保留符号，源码可获取即可
    find "$PREFIX/lib" -name '*.so' -exec "$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" --strip-unneeded {} \;
    ls -lh "$PREFIX/lib"
}

# armeabi-v7a 的 triple 是 **armv7a**-linux-androideabi，不是 arm-linux-androideabi。
# 用错会得到「unable to create an executable file / C compiler test failed」——
# 因为 NDK 的 clang wrapper 按 triple 名找目标文件，名字不对就找不到。
# --arch 用 FFmpeg 的命名：arm / aarch64 / x86_64（不是 arm64）。
build_abi arm64-v8a   aarch64-linux-android  aarch64
# 只出 arm64-v8a（2026-11-07）：x86_64 只用于模拟器且需要 nasm，是最脆的一路；
# armeabi-v7a 在现代机上已极少。要恢复就把下面两行取消注释，并同步改
# android/app/build.gradle.kts 的 abiFilters 与 workflow 的三处 for ABI 循环。
# build_abi armeabi-v7a armv7a-linux-androideabi arm
# build_abi x86_64      x86_64-linux-android   x86_64

echo "==> 完成。产物在 $OUT/<abi>/lib"
 "$COMPONENTS" | grep -Ev '^#define CONFIG_[A-Z0-9]+_MEDIACODEC_DECODER 1
    for codec in H264 MPEG4 VP8 VP9 AV1; do
        if ! grep -Eq "^#define CONFIG_${codec}_MEDIACODEC_ENCODER 1$" "$COMPONENTS"; then
            echo "❌ 缺少系统 MediaCodec 视频编码 wrapper：${codec}"
            exit 1
        fi
    done

    # 编译失败时把真正的错误打出来。
    # 原来直接 make，CI 日志只留尾部若干行，而 FFmpeg 编译到 libavcodec
    # 时才出错 —— 尾部全是无关的 warning，真错误被截掉，等于没法排查。
    local JOBS="${JOBS:-$(nproc)}"
    if ! make -j"$JOBS" > "$ROOT/build-$ABI.log" 2>&1; then
        echo "❌ $ABI 编译失败（make -j$JOBS），最后 120 行："
        tail -n 120 "$ROOT/build-$ABI.log"
        exit 1
    fi
    if ! make install > "$ROOT/install-$ABI.log" 2>&1; then
        echo "❌ $ABI 安装失败，最后 80 行："
        tail -n 80 "$ROOT/install-$ABI.log"
        exit 1
    fi

    # 剥离调试符号（体积）；LGPL 不要求保留符号，源码可获取即可
    find "$PREFIX/lib" -name '*.so' -exec "$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" --strip-unneeded {} \;
    ls -lh "$PREFIX/lib"
}

# armeabi-v7a 的 triple 是 **armv7a**-linux-androideabi，不是 arm-linux-androideabi。
# 用错会得到「unable to create an executable file / C compiler test failed」——
# 因为 NDK 的 clang wrapper 按 triple 名找目标文件，名字不对就找不到。
# --arch 用 FFmpeg 的命名：arm / aarch64 / x86_64（不是 arm64）。
build_abi arm64-v8a   aarch64-linux-android  aarch64
# 只出 arm64-v8a（2026-11-07）：x86_64 只用于模拟器且需要 nasm，是最脆的一路；
# armeabi-v7a 在现代机上已极少。要恢复就把下面两行取消注释，并同步改
# android/app/build.gradle.kts 的 abiFilters 与 workflow 的三处 for ABI 循环。
# build_abi armeabi-v7a armv7a-linux-androideabi arm
# build_abi x86_64      x86_64-linux-android   x86_64

echo "==> 完成。产物在 $OUT/<abi>/lib"
; then
        echo "❌ Android FFmpeg 配置包含非 MediaCodec 软件解码器"
        exit 1
    fi
    if grep -E '^#define CONFIG_[A-Z0-9_]+_ENCODER 1
    for codec in H264 MPEG4 VP8 VP9 AV1; do
        if ! grep -Eq "^#define CONFIG_${codec}_MEDIACODEC_ENCODER 1$" "$COMPONENTS"; then
            echo "❌ 缺少系统 MediaCodec 视频编码 wrapper：${codec}"
            exit 1
        fi
    done

    # 编译失败时把真正的错误打出来。
    # 原来直接 make，CI 日志只留尾部若干行，而 FFmpeg 编译到 libavcodec
    # 时才出错 —— 尾部全是无关的 warning，真错误被截掉，等于没法排查。
    local JOBS="${JOBS:-$(nproc)}"
    if ! make -j"$JOBS" > "$ROOT/build-$ABI.log" 2>&1; then
        echo "❌ $ABI 编译失败（make -j$JOBS），最后 120 行："
        tail -n 120 "$ROOT/build-$ABI.log"
        exit 1
    fi
    if ! make install > "$ROOT/install-$ABI.log" 2>&1; then
        echo "❌ $ABI 安装失败，最后 80 行："
        tail -n 80 "$ROOT/install-$ABI.log"
        exit 1
    fi

    # 剥离调试符号（体积）；LGPL 不要求保留符号，源码可获取即可
    find "$PREFIX/lib" -name '*.so' -exec "$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" --strip-unneeded {} \;
    ls -lh "$PREFIX/lib"
}

# armeabi-v7a 的 triple 是 **armv7a**-linux-androideabi，不是 arm-linux-androideabi。
# 用错会得到「unable to create an executable file / C compiler test failed」——
# 因为 NDK 的 clang wrapper 按 triple 名找目标文件，名字不对就找不到。
# --arch 用 FFmpeg 的命名：arm / aarch64 / x86_64（不是 arm64）。
build_abi arm64-v8a   aarch64-linux-android  aarch64
# 只出 arm64-v8a（2026-11-07）：x86_64 只用于模拟器且需要 nasm，是最脆的一路；
# armeabi-v7a 在现代机上已极少。要恢复就把下面两行取消注释，并同步改
# android/app/build.gradle.kts 的 abiFilters 与 workflow 的三处 for ABI 循环。
# build_abi armeabi-v7a armv7a-linux-androideabi arm
# build_abi x86_64      x86_64-linux-android   x86_64

echo "==> 完成。产物在 $OUT/<abi>/lib"
 "$COMPONENTS" | grep -Ev '^#define CONFIG_[A-Z0-9]+_MEDIACODEC_ENCODER 1
    for codec in H264 MPEG4 VP8 VP9 AV1; do
        if ! grep -Eq "^#define CONFIG_${codec}_MEDIACODEC_ENCODER 1$" "$COMPONENTS"; then
            echo "❌ 缺少系统 MediaCodec 视频编码 wrapper：${codec}"
            exit 1
        fi
    done

    # 编译失败时把真正的错误打出来。
    # 原来直接 make，CI 日志只留尾部若干行，而 FFmpeg 编译到 libavcodec
    # 时才出错 —— 尾部全是无关的 warning，真错误被截掉，等于没法排查。
    local JOBS="${JOBS:-$(nproc)}"
    if ! make -j"$JOBS" > "$ROOT/build-$ABI.log" 2>&1; then
        echo "❌ $ABI 编译失败（make -j$JOBS），最后 120 行："
        tail -n 120 "$ROOT/build-$ABI.log"
        exit 1
    fi
    if ! make install > "$ROOT/install-$ABI.log" 2>&1; then
        echo "❌ $ABI 安装失败，最后 80 行："
        tail -n 80 "$ROOT/install-$ABI.log"
        exit 1
    fi

    # 剥离调试符号（体积）；LGPL 不要求保留符号，源码可获取即可
    find "$PREFIX/lib" -name '*.so' -exec "$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" --strip-unneeded {} \;
    ls -lh "$PREFIX/lib"
}

# armeabi-v7a 的 triple 是 **armv7a**-linux-androideabi，不是 arm-linux-androideabi。
# 用错会得到「unable to create an executable file / C compiler test failed」——
# 因为 NDK 的 clang wrapper 按 triple 名找目标文件，名字不对就找不到。
# --arch 用 FFmpeg 的命名：arm / aarch64 / x86_64（不是 arm64）。
build_abi arm64-v8a   aarch64-linux-android  aarch64
# 只出 arm64-v8a（2026-11-07）：x86_64 只用于模拟器且需要 nasm，是最脆的一路；
# armeabi-v7a 在现代机上已极少。要恢复就把下面两行取消注释，并同步改
# android/app/build.gradle.kts 的 abiFilters 与 workflow 的三处 for ABI 循环。
# build_abi armeabi-v7a armv7a-linux-androideabi arm
# build_abi x86_64      x86_64-linux-android   x86_64

echo "==> 完成。产物在 $OUT/<abi>/lib"
; then
        echo "❌ Android FFmpeg 配置包含非 MediaCodec 软件编码器"
        exit 1
    fi
    for codec in H264 MPEG4 VP8 VP9 AV1; do
        if ! grep -Eq "^#define CONFIG_${codec}_MEDIACODEC_ENCODER 1$" "$COMPONENTS"; then
            echo "❌ 缺少系统 MediaCodec 视频编码 wrapper：${codec}"
            exit 1
        fi
    done

    # 编译失败时把真正的错误打出来。
    # 原来直接 make，CI 日志只留尾部若干行，而 FFmpeg 编译到 libavcodec
    # 时才出错 —— 尾部全是无关的 warning，真错误被截掉，等于没法排查。
    local JOBS="${JOBS:-$(nproc)}"
    if ! make -j"$JOBS" > "$ROOT/build-$ABI.log" 2>&1; then
        echo "❌ $ABI 编译失败（make -j$JOBS），最后 120 行："
        tail -n 120 "$ROOT/build-$ABI.log"
        exit 1
    fi
    if ! make install > "$ROOT/install-$ABI.log" 2>&1; then
        echo "❌ $ABI 安装失败，最后 80 行："
        tail -n 80 "$ROOT/install-$ABI.log"
        exit 1
    fi

    # 剥离调试符号（体积）；LGPL 不要求保留符号，源码可获取即可
    find "$PREFIX/lib" -name '*.so' -exec "$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" --strip-unneeded {} \;
    ls -lh "$PREFIX/lib"
}

# armeabi-v7a 的 triple 是 **armv7a**-linux-androideabi，不是 arm-linux-androideabi。
# 用错会得到「unable to create an executable file / C compiler test failed」——
# 因为 NDK 的 clang wrapper 按 triple 名找目标文件，名字不对就找不到。
# --arch 用 FFmpeg 的命名：arm / aarch64 / x86_64（不是 arm64）。
build_abi arm64-v8a   aarch64-linux-android  aarch64
# 只出 arm64-v8a（2026-11-07）：x86_64 只用于模拟器且需要 nasm，是最脆的一路；
# armeabi-v7a 在现代机上已极少。要恢复就把下面两行取消注释，并同步改
# android/app/build.gradle.kts 的 abiFilters 与 workflow 的三处 for ABI 循环。
# build_abi armeabi-v7a armv7a-linux-androideabi arm
# build_abi x86_64      x86_64-linux-android   x86_64

echo "==> 完成。产物在 $OUT/<abi>/lib"
; then
        echo "❌ Android FFmpeg 配置包含非 MediaCodec 软件编码器"
        exit 1
    fi
    for codec in H264 MPEG4 VP8 VP9 AV1; do
        if ! grep -Eq "^#define CONFIG_${codec}_MEDIACODEC_ENCODER 1$" "$COMPONENTS"; then
            echo "❌ 缺少系统 MediaCodec 视频编码 wrapper：${codec}"
            exit 1
        fi
    done

    # 编译失败时把真正的错误打出来。
    # 原来直接 make，CI 日志只留尾部若干行，而 FFmpeg 编译到 libavcodec
    # 时才出错 —— 尾部全是无关的 warning，真错误被截掉，等于没法排查。
    local JOBS="${JOBS:-$(nproc)}"
    if ! make -j"$JOBS" > "$ROOT/build-$ABI.log" 2>&1; then
        echo "❌ $ABI 编译失败（make -j$JOBS），最后 120 行："
        tail -n 120 "$ROOT/build-$ABI.log"
        exit 1
    fi
    if ! make install > "$ROOT/install-$ABI.log" 2>&1; then
        echo "❌ $ABI 安装失败，最后 80 行："
        tail -n 80 "$ROOT/install-$ABI.log"
        exit 1
    fi

    # 剥离调试符号（体积）；LGPL 不要求保留符号，源码可获取即可
    find "$PREFIX/lib" -name '*.so' -exec "$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" --strip-unneeded {} \;
    ls -lh "$PREFIX/lib"
}

# armeabi-v7a 的 triple 是 **armv7a**-linux-androideabi，不是 arm-linux-androideabi。
# 用错会得到「unable to create an executable file / C compiler test failed」——
# 因为 NDK 的 clang wrapper 按 triple 名找目标文件，名字不对就找不到。
# --arch 用 FFmpeg 的命名：arm / aarch64 / x86_64（不是 arm64）。
build_abi arm64-v8a   aarch64-linux-android  aarch64
# 只出 arm64-v8a（2026-11-07）：x86_64 只用于模拟器且需要 nasm，是最脆的一路；
# armeabi-v7a 在现代机上已极少。要恢复就把下面两行取消注释，并同步改
# android/app/build.gradle.kts 的 abiFilters 与 workflow 的三处 for ABI 循环。
# build_abi armeabi-v7a armv7a-linux-androideabi arm
# build_abi x86_64      x86_64-linux-android   x86_64

echo "==> 完成。产物在 $OUT/<abi>/lib"
 "$COMPONENTS" | grep -Ev '^#define CONFIG_[A-Z0-9]+_MEDIACODEC_DECODER 1
    for codec in H264 MPEG4 VP8 VP9 AV1; do
        if ! grep -Eq "^#define CONFIG_${codec}_MEDIACODEC_ENCODER 1$" "$COMPONENTS"; then
            echo "❌ 缺少系统 MediaCodec 视频编码 wrapper：${codec}"
            exit 1
        fi
    done

    # 编译失败时把真正的错误打出来。
    # 原来直接 make，CI 日志只留尾部若干行，而 FFmpeg 编译到 libavcodec
    # 时才出错 —— 尾部全是无关的 warning，真错误被截掉，等于没法排查。
    local JOBS="${JOBS:-$(nproc)}"
    if ! make -j"$JOBS" > "$ROOT/build-$ABI.log" 2>&1; then
        echo "❌ $ABI 编译失败（make -j$JOBS），最后 120 行："
        tail -n 120 "$ROOT/build-$ABI.log"
        exit 1
    fi
    if ! make install > "$ROOT/install-$ABI.log" 2>&1; then
        echo "❌ $ABI 安装失败，最后 80 行："
        tail -n 80 "$ROOT/install-$ABI.log"
        exit 1
    fi

    # 剥离调试符号（体积）；LGPL 不要求保留符号，源码可获取即可
    find "$PREFIX/lib" -name '*.so' -exec "$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" --strip-unneeded {} \;
    ls -lh "$PREFIX/lib"
}

# armeabi-v7a 的 triple 是 **armv7a**-linux-androideabi，不是 arm-linux-androideabi。
# 用错会得到「unable to create an executable file / C compiler test failed」——
# 因为 NDK 的 clang wrapper 按 triple 名找目标文件，名字不对就找不到。
# --arch 用 FFmpeg 的命名：arm / aarch64 / x86_64（不是 arm64）。
build_abi arm64-v8a   aarch64-linux-android  aarch64
# 只出 arm64-v8a（2026-11-07）：x86_64 只用于模拟器且需要 nasm，是最脆的一路；
# armeabi-v7a 在现代机上已极少。要恢复就把下面两行取消注释，并同步改
# android/app/build.gradle.kts 的 abiFilters 与 workflow 的三处 for ABI 循环。
# build_abi armeabi-v7a armv7a-linux-androideabi arm
# build_abi x86_64      x86_64-linux-android   x86_64

echo "==> 完成。产物在 $OUT/<abi>/lib"
; then
        echo "❌ Android FFmpeg 配置包含非 MediaCodec 软件解码器"
        exit 1
    fi
    if grep -E '^#define CONFIG_[A-Z0-9_]+_ENCODER 1
    for codec in H264 MPEG4 VP8 VP9 AV1; do
        if ! grep -Eq "^#define CONFIG_${codec}_MEDIACODEC_ENCODER 1$" "$COMPONENTS"; then
            echo "❌ 缺少系统 MediaCodec 视频编码 wrapper：${codec}"
            exit 1
        fi
    done

    # 编译失败时把真正的错误打出来。
    # 原来直接 make，CI 日志只留尾部若干行，而 FFmpeg 编译到 libavcodec
    # 时才出错 —— 尾部全是无关的 warning，真错误被截掉，等于没法排查。
    local JOBS="${JOBS:-$(nproc)}"
    if ! make -j"$JOBS" > "$ROOT/build-$ABI.log" 2>&1; then
        echo "❌ $ABI 编译失败（make -j$JOBS），最后 120 行："
        tail -n 120 "$ROOT/build-$ABI.log"
        exit 1
    fi
    if ! make install > "$ROOT/install-$ABI.log" 2>&1; then
        echo "❌ $ABI 安装失败，最后 80 行："
        tail -n 80 "$ROOT/install-$ABI.log"
        exit 1
    fi

    # 剥离调试符号（体积）；LGPL 不要求保留符号，源码可获取即可
    find "$PREFIX/lib" -name '*.so' -exec "$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" --strip-unneeded {} \;
    ls -lh "$PREFIX/lib"
}

# armeabi-v7a 的 triple 是 **armv7a**-linux-androideabi，不是 arm-linux-androideabi。
# 用错会得到「unable to create an executable file / C compiler test failed」——
# 因为 NDK 的 clang wrapper 按 triple 名找目标文件，名字不对就找不到。
# --arch 用 FFmpeg 的命名：arm / aarch64 / x86_64（不是 arm64）。
build_abi arm64-v8a   aarch64-linux-android  aarch64
# 只出 arm64-v8a（2026-11-07）：x86_64 只用于模拟器且需要 nasm，是最脆的一路；
# armeabi-v7a 在现代机上已极少。要恢复就把下面两行取消注释，并同步改
# android/app/build.gradle.kts 的 abiFilters 与 workflow 的三处 for ABI 循环。
# build_abi armeabi-v7a armv7a-linux-androideabi arm
# build_abi x86_64      x86_64-linux-android   x86_64

echo "==> 完成。产物在 $OUT/<abi>/lib"
 "$COMPONENTS" | grep -Ev '^#define CONFIG_[A-Z0-9]+_MEDIACODEC_ENCODER 1
    for codec in H264 MPEG4 VP8 VP9 AV1; do
        if ! grep -Eq "^#define CONFIG_${codec}_MEDIACODEC_ENCODER 1$" "$COMPONENTS"; then
            echo "❌ 缺少系统 MediaCodec 视频编码 wrapper：${codec}"
            exit 1
        fi
    done

    # 编译失败时把真正的错误打出来。
    # 原来直接 make，CI 日志只留尾部若干行，而 FFmpeg 编译到 libavcodec
    # 时才出错 —— 尾部全是无关的 warning，真错误被截掉，等于没法排查。
    local JOBS="${JOBS:-$(nproc)}"
    if ! make -j"$JOBS" > "$ROOT/build-$ABI.log" 2>&1; then
        echo "❌ $ABI 编译失败（make -j$JOBS），最后 120 行："
        tail -n 120 "$ROOT/build-$ABI.log"
        exit 1
    fi
    if ! make install > "$ROOT/install-$ABI.log" 2>&1; then
        echo "❌ $ABI 安装失败，最后 80 行："
        tail -n 80 "$ROOT/install-$ABI.log"
        exit 1
    fi

    # 剥离调试符号（体积）；LGPL 不要求保留符号，源码可获取即可
    find "$PREFIX/lib" -name '*.so' -exec "$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" --strip-unneeded {} \;
    ls -lh "$PREFIX/lib"
}

# armeabi-v7a 的 triple 是 **armv7a**-linux-androideabi，不是 arm-linux-androideabi。
# 用错会得到「unable to create an executable file / C compiler test failed」——
# 因为 NDK 的 clang wrapper 按 triple 名找目标文件，名字不对就找不到。
# --arch 用 FFmpeg 的命名：arm / aarch64 / x86_64（不是 arm64）。
build_abi arm64-v8a   aarch64-linux-android  aarch64
# 只出 arm64-v8a（2026-11-07）：x86_64 只用于模拟器且需要 nasm，是最脆的一路；
# armeabi-v7a 在现代机上已极少。要恢复就把下面两行取消注释，并同步改
# android/app/build.gradle.kts 的 abiFilters 与 workflow 的三处 for ABI 循环。
# build_abi armeabi-v7a armv7a-linux-androideabi arm
# build_abi x86_64      x86_64-linux-android   x86_64

echo "==> 完成。产物在 $OUT/<abi>/lib"
; then
        echo "❌ Android FFmpeg 配置包含非 MediaCodec 软件编码器"
        exit 1
    fi
    for codec in H264 MPEG4 VP8 VP9 AV1; do
        if ! grep -Eq "^#define CONFIG_${codec}_MEDIACODEC_ENCODER 1$" "$COMPONENTS"; then
            echo "❌ 缺少系统 MediaCodec 视频编码 wrapper：${codec}"
            exit 1
        fi
    done

    # 编译失败时把真正的错误打出来。
    # 原来直接 make，CI 日志只留尾部若干行，而 FFmpeg 编译到 libavcodec
    # 时才出错 —— 尾部全是无关的 warning，真错误被截掉，等于没法排查。
    local JOBS="${JOBS:-$(nproc)}"
    if ! make -j"$JOBS" > "$ROOT/build-$ABI.log" 2>&1; then
        echo "❌ $ABI 编译失败（make -j$JOBS），最后 120 行："
        tail -n 120 "$ROOT/build-$ABI.log"
        exit 1
    fi
    if ! make install > "$ROOT/install-$ABI.log" 2>&1; then
        echo "❌ $ABI 安装失败，最后 80 行："
        tail -n 80 "$ROOT/install-$ABI.log"
        exit 1
    fi

    # 剥离调试符号（体积）；LGPL 不要求保留符号，源码可获取即可
    find "$PREFIX/lib" -name '*.so' -exec "$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" --strip-unneeded {} \;
    ls -lh "$PREFIX/lib"
}

# armeabi-v7a 的 triple 是 **armv7a**-linux-androideabi，不是 arm-linux-androideabi。
# 用错会得到「unable to create an executable file / C compiler test failed」——
# 因为 NDK 的 clang wrapper 按 triple 名找目标文件，名字不对就找不到。
# --arch 用 FFmpeg 的命名：arm / aarch64 / x86_64（不是 arm64）。
build_abi arm64-v8a   aarch64-linux-android  aarch64
# 只出 arm64-v8a（2026-11-07）：x86_64 只用于模拟器且需要 nasm，是最脆的一路；
# armeabi-v7a 在现代机上已极少。要恢复就把下面两行取消注释，并同步改
# android/app/build.gradle.kts 的 abiFilters 与 workflow 的三处 for ABI 循环。
# build_abi armeabi-v7a armv7a-linux-androideabi arm
# build_abi x86_64      x86_64-linux-android   x86_64

echo "==> 完成。产物在 $OUT/<abi>/lib"
