#include "test_framework.h"
#include "nairoclip/core/capi.h"

#include <cstring>
#include <string>

// =============================================================================
// B0-4 C ABI 所有权标注：验证「文档写的」与「实现做的」一致
// =============================================================================
//
// `tools/check_abi_ownership.py` 只能证明「注释里有字」。若注释写了
// 「需 nc_free_string」而实现返回静态/库持有的指针，调用方照样 double-free。
// 这里做**行为级**验证。

NC_TEST(CapiOwnership, ErrorStringIsHeapAllocated) {
    NcEditor* e = nc_editor_create();
    NC_CHECK(e != nullptr);

    char* err = nullptr;
    const int32_t rc = nc_superres_load(e, "/nonexistent/model.onnx", &err);
    NC_CHECK(rc != NC_OK);
    if (err != nullptr) {
        NC_CHECK(std::strlen(err) > 0);
        nc_free_string(err);  // 能安全释放 = 堆分配
    }
    nc_editor_destroy(e);
}

NC_TEST(CapiOwnership, NullOutParamIsAccepted) {
    NcEditor* e = nc_editor_create();
    NC_CHECK(e != nullptr);
    const int32_t rc = nc_superres_load(e, "/nonexistent/model.onnx", nullptr);
    NC_CHECK(rc != NC_OK);
    nc_editor_destroy(e);
}

NC_TEST(CapiOwnership, FailureLeavesOutParamUsable) {
    NcEditor* e = nc_editor_create();
    NC_CHECK(e != nullptr);
    char* sentinel = nullptr;
    const int32_t rc = nc_translate_load(e, nullptr, nullptr, nullptr, nullptr, &sentinel);
    NC_CHECK(rc != NC_OK);
    if (sentinel != nullptr) {
        nc_free_string(sentinel);  // 若写了未初始化指针，这里会崩
    }
    nc_editor_destroy(e);
}

NC_TEST(CapiOwnership, LastErrorNeedsNoFree) {
    // nc_last_error 标注「无需释放」且返回库持有存储：
    // 连续两次调用应返回同一地址。调用方若误用 nc_free_string 会炸堆。
    nc_set_project_name(nullptr, "x");
    const char* a = nc_last_error();
    const char* b = nc_last_error();
    NC_CHECK(a != nullptr);
    NC_CHECK(a == b);
}
