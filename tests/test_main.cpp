#include "test_framework.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>

namespace {

/// 判断某路径是否存在（用于素材就绪检查）。
bool pathExists(const char* path) {
    struct stat st;
    return ::stat(path, &st) == 0;
}

/// 测试素材自愈（B2-3 期间踩到的坑）。
///
/// 问题：FFmpeg 相关用例依赖 `/tmp/media_gen/*`，素材由
/// `tests/fixtures/generate_media.sh` 在 nairoclip_tests 的 **POST_BUILD**
/// 步骤生成。这意味着：
///   · 素材一旦被清理（/tmp 被系统回收），只要没重新编译，就不会自动恢复；
///   · 表现是一批 FFmpeg 用例突然大面积失败，看起来像"代码改坏了"，
///     实际与代码无关 —— 排查一次要花很久。
///
/// 处置：在 main() 里检查哨兵文件，缺失就在**运行时**补生成。
/// 脚本本身幂等（已存在则跳过），所以正常情况下这只是一次 stat 调用。
/// 生成失败（如机器没装 ffmpeg）不阻断——相关用例会自己失败并给出原因，
/// 比在这里直接退出更利于定位。
void ensureTestMedia() {
    if (pathExists("/tmp/media_gen/test_av.mp4")) {
        return;
    }
    std::printf("[media] /tmp/media_gen 缺失，尝试生成测试素材...\n");
    // 脚本路径要用绝对路径：测试可执行文件的工作目录不确定
    // （直接运行时是仓库根，但被 CI 或 IDE 启动时可能是别处）。
    // 因此先试几个常见相对位置，第一个存在的就用。
    const char* candidates[] = {
        "tests/fixtures/generate_media.sh",
        "../tests/fixtures/generate_media.sh",
        "../../tests/fixtures/generate_media.sh",
    };
    const char* script = nullptr;
    for (const char* c : candidates) {
        if (pathExists(c)) {
            script = c;
            break;
        }
    }
    if (script == nullptr) {
        std::printf("[media] 找不到 generate_media.sh（当前工作目录不对？）\n");
        std::printf("[media] FFmpeg 相关用例将失败。\n");
        return;
    }
    const std::string cmd = std::string("bash ") + script + " /tmp/media_gen";
    const int rc = std::system(cmd.c_str());
    if (rc != 0) {
        std::printf("[media] 生成失败（rc=%d）。FFmpeg 相关用例将失败。\n", rc);
        std::printf("[media] 手动执行：%s\n", cmd.c_str());
    } else {
        std::printf("[media] 完成\n");
    }
}

}  // namespace

int main() {
    ensureTestMedia();
    return nairoclip::test::runAll();
}
