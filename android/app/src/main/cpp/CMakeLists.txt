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

# 依赖的系统库
find_library(log-lib log)
find_library(android-lib android)
find_library(z-lib z)
target_link_libraries(nc_jni ${log-lib} ${android-lib} ${z-lib} m)

target_link_options(nc_jni PRIVATE "-Wl,--no-undefined")
