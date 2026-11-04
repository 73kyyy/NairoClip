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
#   · 只保留需要的组件（decoder/demuxer/parser），其余 --disable 掉，
#     否则 .so 加起来 40MB+，APK 装不下三个 ABI。
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
    curl -fL --retry 3 -o "$ROOT/src/ffmpeg.tar.xz" \
        "https://ffmpeg.org/releases/ffmpeg-$FFMPEG_VERSION.tar.xz"
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
    local TOOLCHAIN="$ANDROID_NDK/build/cmake/android.toolchain.cmake"

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

    ./configure \
        --prefix="$PREFIX" \
        --target-os=android \
        --arch="$CPU" \
        --cc="$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/${TRIPLE}${ANDROID_API}-clang" \
        --cxx="$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/${TRIPLE}${ANDROID_API}-clang++" \
        --ar="$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-ar" \
        --ranlib="$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-ranlib" \
        --strip="$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" \
        --sysroot="$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/sysroot" \
        --enable-cross-compile \
        --enable-shared \
        --disable-static \
        --disable-gpl \
        --disable-nonfree \
        --disable-version3 \
        --disable-programs \
        --disable-doc \
        --disable-avdevice \
        --disable-postproc \
        --disable-swscale-alpha \
        --enable-jni \
        --enable-mediacodec \
        --enable-decoder=h264,hevc,mpeg4,vp8,vp9,av1,aac,mp3,opus,flac,vorbis,pcm_s16le,pcm_s16be \
        --enable-encoder=aac,pcm_s16le \
        --enable-demuxer=mov,matroska,avi,mp3,wav,flac,ogg,aac,mpegts \
        --enable-muxer=mp4,mov,matroska,wav,mp3 \
        --enable-parser=h264,hevc,vp9,av1,aac,mpegaudio,opus,flac \
        --enable-protocol=file,pipe \
        --enable-filter=aresample,scale,null,format \
        --disable-debug \
        --enable-small \
        $EXTRA \
        --extra-cflags="-fPIC -O2 -DANDROID" \
        --extra-ldflags="-Wl,--no-undefined"

    make -j"$(nproc)"
    make install

    # 剥离调试符号（体积）；LGPL 不要求保留符号，源码可获取即可
    find "$PREFIX/lib" -name '*.so' -exec "$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" --strip-unneeded {} \;
    ls -lh "$PREFIX/lib"
}

build_abi arm64-v8a   aarch64-linux-android arm64
build_abi armeabi-v7a arm-linux-androideabi arm
build_abi x86_64      x86_64-linux-android  x86_64

echo "==> 完成。产物在 $OUT/<abi>/lib"
