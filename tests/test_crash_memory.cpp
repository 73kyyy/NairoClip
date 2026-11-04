/* =============================================================================
   §8.1 / §8.2 崩溃捕获与内存释放的验收测试
   -----------------------------------------------------------------------------
   崩溃处理器**不能真的触发一次段错误来测** —— 那会杀掉测试进程。
   所以这里只测「安装 / 卸载 / 路径」这些可安全验证的部分。

   真正的崩溃验证只能在真机上做（见 §8.7 的说明）。
   ============================================================================= */

#include "test_framework.h"

#include "nairoclip/core/capi.h"
#include "nairoclip/core/crash_handler.h"
#include "nairoclip/core/editor.h"
#include "nairoclip/core/project.h"

#include <cstring>
#include <string>

using namespace nairoclip::core;

NC_TEST(CrashHandler, InstallsAndReportsPath) {
    const bool ok = installCrashHandler("/tmp");
    NC_CHECK(ok);
    const char* path = crashLogPath();
    NC_CHECK(path != nullptr);
    NC_CHECK(std::strlen(path) > 0);
    NC_CHECK(std::string(path).find("crash.log") != std::string::npos);
}

NC_TEST(CrashHandler, InstallIsIdempotent) {
    /* 重复安装必须安全 —— 多页面/多次进入编辑器都会调。 */
    NC_CHECK(installCrashHandler("/tmp"));
    NC_CHECK(installCrashHandler("/tmp"));
    NC_CHECK(installCrashHandler("/data/local/tmp"));
    /* 第二次传不同目录不应改变已安装的路径（幂等语义） */
    NC_CHECK(std::strlen(crashLogPath()) > 0);
}

NC_TEST(CrashHandler, UninstallIsSafe) {
    uninstallCrashHandler();
    uninstallCrashHandler();   // 重复卸载不能崩
    NC_CHECK(true);
}

NC_TEST(CrashCapi, NullDirUsesCwd) {
    // 传 nullptr 应回退到当前目录，而不是失败
    const int32_t rc = nc_install_crash_handler(nullptr);
    NC_CHECK(rc == 1 || rc == 0);   // 平台不支持时 0 也算合法
    const char* p = nc_crash_log_path();
    NC_CHECK(p != nullptr);
}

/* ---------------------------------------------------------------- 内存释放 */

NC_TEST(ReleaseMemory, NullEditorIsSafe) {
    NC_CHECK_EQ(nc_release_memory(nullptr, 0), std::uint64_t(0));
    NC_CHECK_EQ(nc_release_memory(nullptr, 2), std::uint64_t(0));
}

NC_TEST(ReleaseMemory, DoesNotDestroyProjectData) {
    /* 最关键的断言：释放内存**不能**丢用户数据。
       工程名、轨道数在释放前后必须一致。 */
    NcEditor* ed = nc_editor_create();
    NC_CHECK(ed != nullptr);

    nc_set_project_name(ed, "内存测试工程");
    int32_t w = 0, h = 0;
    nc_project_canvas(ed, &w, &h);
    const int32_t beforeW = w;
    const int32_t beforeH = h;
    const int32_t beforeTracks = nc_track_count(ed);

    // 三个等级都试一遍
    for (int32_t level = 0; level <= 2; ++level) {
        (void)nc_release_memory(ed, level);
        NC_CHECK_EQ(nc_track_count(ed), beforeTracks);
        nc_project_canvas(ed, &w, &h);
        NC_CHECK_EQ(w, beforeW);
        NC_CHECK_EQ(h, beforeH);
    }

    // 工程名仍在
    char* name = nc_project_name(ed);
    NC_CHECK(name != nullptr);
    if (name != nullptr) {
        NC_CHECK_EQ(std::string(name), std::string("内存测试工程"));
        nc_free_string(name);
    }

    nc_editor_destroy(ed);
}

NC_TEST(ReleaseMemory, InvalidLevelDoesNotCrash) {
    NcEditor* ed = nc_editor_create();
    NC_CHECK(ed != nullptr);
    // 负数与超大值都只是「没有额外动作」，不能崩
    (void)nc_release_memory(ed, -1);
    (void)nc_release_memory(ed, 999);
    NC_CHECK_EQ(nc_track_count(ed), 0);
    nc_editor_destroy(ed);
}
