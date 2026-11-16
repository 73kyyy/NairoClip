/* S1-2：ANativeWindow JNI 桥桩头（仅主机侧编译用）。 */
#ifndef NC_TEST_ANATIVE_WINDOW_JNI_STUB_H
#define NC_TEST_ANATIVE_WINDOW_JNI_STUB_H
#include <jni.h>
#include <android/native_window.h>
inline ANativeWindow* ANativeWindow_fromSurface(JNIEnv*, jobject) { return nullptr; }
#endif
