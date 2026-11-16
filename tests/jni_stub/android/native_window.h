/* S1-2：ANativeWindow 桩头（仅主机侧编译 nc_jni.cpp 用，不参与产品构建）。 */
#ifndef NC_TEST_ANATIVE_WINDOW_STUB_H
#define NC_TEST_ANATIVE_WINDOW_STUB_H
#include <cstdint>

struct ANativeWindow;

/* 真实值见 NDK android/native_window.h */
#define WINDOW_FORMAT_RGBA_8888 1
#define WINDOW_FORMAT_RGBX_8888 2
#define WINDOW_FORMAT_RGB_565   4
struct ANativeWindow_Buffer {
    int32_t width;
    int32_t height;
    int32_t stride;
    int32_t format;
    void*   bits;
    uint32_t reserved[6];
};
/* 真实返回码：0 = OK，负值 = 错误。桩只保证能链接/编译。 */
inline int32_t ANativeWindow_setBuffersGeometry(ANativeWindow*, int32_t, int32_t, int32_t) { return 0; }
inline int32_t ANativeWindow_lock(ANativeWindow*, ANativeWindow_Buffer*, void*) { return -1; }
inline int32_t ANativeWindow_unlockAndPost(ANativeWindow*) { return 0; }
inline void    ANativeWindow_release(ANativeWindow*) {}
#endif
