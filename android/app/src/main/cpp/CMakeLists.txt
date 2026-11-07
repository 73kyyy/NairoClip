cmake_minimum_required(VERSION 3.22.1)
project(nc_jni CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# core 源码根目录（仓库内 core/）
set(NC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../..")

# libnc_jni.so：JNI 入口，直调 C ABI（不经过 HTTP）
add_library(nc_jni SHARED
    ${NC_ROOT}/mobile/native/android/nc_jni.cpp
)

target_include_directories(nc_jni PRIVATE
    ${NC_ROOT}/core/include
)

# core 静态库（CI 已按 ABI 交叉编译；见 .github/workflows/android-build.yml）
# 目录约定：android/app/src/main/cpp/prebuilt/<abi>/libnairoclip_core.a
set(NC_CORE_LIB "${CMAKE_CURRENT_SOURCE_DIR}/prebuilt/${ANDROID_ABI}/libnairoclip_core.a")
if(EXISTS "${NC_CORE_LIB}")
    target_link_libraries(nc_jni "${NC_CORE_LIB}")
else()
    message(FATAL_ERROR
        "缺少 core 静态库：${NC_CORE_LIB}\n"
        "请先运行 .github/workflows/android-build.yml 产出，"
        "或用 tools/fetch_android_core.sh 下载。")
endif()

# ---- FFmpeg 共享库（LGPL 动态链接）----
# 目录约定：android/app/src/main/jniLibs/<abi>/libav*.so
# CI 会把 ffmpeg-build 的产物拷到这里（见 android-apk.yml）。
#
# 为什么必须动态链接：
#   LGPL 允许应用闭源，但要求「用户可以替换被链接的库」。
#   静态链接（.a）无法替换，等于要求整个 App 按 LGPL 开源 ——
#   与闭源策略冲突。所以这里只接受 .so，找不到就报错，
#   **不静默退回 Mock**（那会让 APK 装上去能开但导不了视频）。
# 注意路径层级：本文件在 android/app/src/main/cpp/，
# 上一层是 main/，所以 jniLibs 是 ../jniLibs。
# 之前写成 ../../jniLibs（= android/app/src/jniLibs，不存在），
# 导致 EXISTS 恒为假 → 静默跳过链接 → 链接期一堆 undefined symbol。
set(NC_FFMPEG_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../jniLibs/${ANDROID_ABI}")
set(NC_FFMPEG_LIBS
    "${NC_FFMPEG_LIB_DIR}/libavformat.so"
    "${NC_FFMPEG_LIB_DIR}/libavcodec.so"
    "${NC_FFMPEG_LIB_DIR}/libswscale.so"
    "${NC_FFMPEG_LIB_DIR}/libswresample.so"
    "${NC_FFMPEG_LIB_DIR}/libavutil.so")
if(EXISTS "${NC_FFMPEG_LIB_DIR}/libavcodec.so")
    target_link_libraries(nc_jni ${NC_FFMPEG_LIBS})
    message(STATUS "nc_jni: FFmpeg 动态库已链接 ${NC_FFMPEG_LIB_DIR}")
else()
    # 必须是 FATAL_ERROR：上面的注释写着「找不到就报错，不静默退回 Mock」，
    # 但原代码只打了 WARNING，结果是链接期一堆 undefined symbol ——
    # 报错信息离真正原因很远，很难查。这里直接失败并说清楚。
    message(FATAL_ERROR
        "nc_jni: 未找到 FFmpeg 动态库：${NC_FFMPEG_LIB_DIR}/libavcodec.so\n"
        "APK 会退化为 Mock 解码器（能开但导不了真实视频）。\n"
        "请运行 ffmpeg-build/build-android.sh，并把 out/<abi>/lib/*.so\n"
        "拷到 android/app/src/main/jniLibs/<abi>/。")
endif()

# 依赖的系统库
find_library(log-lib log)
find_library(android-lib android)
find_library(z-lib z)
target_link_libraries(nc_jni ${log-lib} ${android-lib} ${z-lib} m)

target_link_options(nc_jni PRIVATE "-Wl,--no-undefined")
