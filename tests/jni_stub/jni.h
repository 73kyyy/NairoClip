/* S1-2：JNI 桩头（仅用于 Linux 主机侧测试 nc_jni.cpp 的 DispatchEdit）。
 *
 * 目的：DispatchEdit 是纯 C++ 函数（std::string 入参 + NcEditor*），
 * 但所在文件包含 <jni.h> 与 <android/native_window.h>，主机侧无法编译。
 * 本桩只提供**编译期所需的类型与成员方法**，不做任何真实 JNI 工作。
 * 测试只调用 DispatchEdit，不触碰任何 JNI 入口，因此桩的行为无影响。
 *
 * 与真实 jni.h 一致：JNIEnv 在 C++ 下是结构体，方法用 env->Xxx() 调用。
 * 桩放在测试目录，不进入产品构建（产品用 NDK 真实 jni.h）。
 */
#ifndef NC_TEST_JNI_STUB_H
#define NC_TEST_JNI_STUB_H

#include <cstdint>
#include <cstddef>

typedef int8_t   jbyte;
typedef int32_t  jint;
typedef int64_t  jlong;
typedef float    jfloat;
typedef double   jdouble;
typedef uint8_t  jboolean;
typedef int32_t  jsize;

#define JNI_FALSE 0
#define JNI_TRUE  1

struct _jobject;
typedef struct _jobject* jobject;
typedef jobject jstring;
typedef jobject jclass;
typedef jobject jbyteArray;
typedef jobject jlongArray;
typedef jobject jobjectArray;
typedef jobject jthrowable;
typedef void*   jmethodID;
typedef void*   jfieldID;

/* JNI 常量（真实值，供编译期使用）。 */
#define JNI_VERSION_1_6  6
#define JNI_EDETACHED   (-2)
#define JNI_OK          0
#define JNI_ERR         (-1)

union jvalue {
    jboolean z; jbyte b; char c; short s; int i;
    jlong j; float f; double d; jobject l;
};

struct JNIEnv_;
struct JavaVM_;
typedef struct JavaVM_ JavaVM;

/* 真实 JNI 用这两个宏标注入口；桩里置空即可（只编译不导出）。 */
#ifndef JNIEXPORT
#define JNIEXPORT
#endif
#ifndef JNICALL
#define JNICALL
#endif

struct JNIEnv_ {
    /* 只覆盖 nc_jni.cpp 实际用到的 16 个方法。全部返回安全占位值。 */
    const char* GetStringUTFChars(jstring, jboolean*) { return ""; }
    void        ReleaseStringUTFChars(jstring, const char*) {}
    jstring     NewStringUTF(const char*) { return nullptr; }
    jclass      GetObjectClass(jobject) { return nullptr; }
    void        DeleteLocalRef(jobject) {}
    jobject     NewGlobalRef(jobject) { return nullptr; }
    void        DeleteGlobalRef(jobject) {}
    jsize       GetArrayLength(jbyteArray) { return 0; }
    void        GetByteArrayRegion(jbyteArray, jsize, jsize, jbyte*) {}
    void        SetByteArrayRegion(jbyteArray, jsize, jsize, const jbyte*) {}
    jbyteArray  NewByteArray(jsize) { return nullptr; }
    jobject     GetObjectArrayElement(jobjectArray, jsize) { return nullptr; }
    jlongArray  NewLongArray(jsize) { return nullptr; }
    void        GetLongArrayRegion(jlongArray, jsize, jsize, jlong*) {}
    void        SetLongArrayRegion(jlongArray, jsize, jsize, const jlong*) {}
    void*       GetDirectBufferAddress(jobject) { return nullptr; }
    jlong       GetDirectBufferCapacity(jobject) { return 0; }
    jint        GetJavaVM(JavaVM**) { return 0; }
    jobject     CallObjectMethod(jobject, void*, ...) { return nullptr; }
    void*       GetMethodID(jclass, const char*, const char*) { return nullptr; }
    jboolean    ExceptionCheck() { return JNI_FALSE; }
    void        ExceptionClear() {}
};

typedef struct JNIEnv_ JNIEnv;

struct JavaVM_ {
    jint GetEnv(void**, jint) { return -1; }
    jint AttachCurrentThread(JNIEnv**, void*) { return -1; }
    jint DetachCurrentThread() { return 0; }
};

#endif  /* NC_TEST_JNI_STUB_H */
