#pragma once
// =============================================================================
// 3D LUT 调色（B1-2）—— .cube 文件解析与应用
// =============================================================================
// 与 `filters.h` 的关系（两者不重叠）：
//   · filters：**内置** 14 种程序化色调映射，用户选风格即可，不需要外部文件；
//   · lut（本模块）：加载**外部** .cube 文件（调色师用 DaVinci/Premiere 做的），
//     把任意三维查找表应用到画面上。
//
// 为什么必须支持 .cube：它是行业事实标准（DaVinci Resolve / Premiere / FCP
// 都能导出）。用户拿到一个 `.cube` 就能在本产品里复现同一套风格，不必等我们
// 把每种风格都内置一遍。
//
// 文件格式（Adobe Cube LUT Specification 1.0 要点）：
//   TITLE "name"          可选，任意字符串
//   LUT_3D_SIZE 33        必需（本实现支持 2..64）；也接受 LUT_1D_SIZE，但
//                         本模块只做 3D（1D 会明确报错，不静默错处理）
//   DOMAIN_MIN 0 0 0      可选，默认 0 0 0
//   DOMAIN_MAX 1 1 1      可选，默认 1 1 1
//   # comment             以 # 开头
//   r g b                 数据行，共 size³ 行
//
// 数据顺序（易错点）：红色变化**最快**，蓝色最慢。
//   索引 = ((b * size + g) * size + r)
//   写成「r 内层循环、b 外层循环」才对。
//
// **格式校验从严**：畸形文件必须拒绝并给出原因，不能读一半就应用——
// 半个 LUT 会产出比不加 LUT 更糟的画面，且用户无从判断哪里错了。

#include <cstdint>
#include <string>
#include <vector>

namespace nairoclip {
namespace core {

/// 3D LUT（立方体查找表）。
struct Lut3D {
    /// 每边的采样点数（17 / 33 最常见；范围 2..64）。
    int size = 0;
    /// 通道数据，size³ × 3，按「r 最快、b 最慢」排列，取值范围 [0,1]。
    std::vector<double> data;
    /// DOMAIN_MIN / DOMAIN_MAX（默认 0 / 1）。部分 LUT 用 0..255 或 log 域。
    double domainMin[3] = {0.0, 0.0, 0.0};
    double domainMax[3] = {1.0, 1.0, 1.0};
    /// 标题（TITLE 行，可为空）。
    std::string title;

    /// 数据是否自洽（size 与 data 长度匹配、域范围有效）。
    bool valid() const;
};

/// 解析 .cube 文本。
/// - 成功返回 true 并填充 out；
/// - 失败返回 false，error 给出**可操作**的原因（行号 + 具体问题）。
/// - 已存在的 out 内容在失败时不被修改（调用方不会拿到半个 LUT）。
bool parseCubeLut(const std::string& text, Lut3D& out, std::string& error);

/// 从文件加载（读取失败也走 error）。
bool loadCubeLutFile(const std::string& path, Lut3D& out, std::string& error);

/// 采一个点：三线性插值（输入/输出 0..1）。
/// 超出 [0,1] 的输入先钳制；LUT 未初始化时原样返回输入。
void lutSample(const Lut3D& lut, double r, double g, double b,
               double& outR, double& outG, double& outB);

/// 对整帧应用 LUT（原地修改 RGB；alpha 不变）。
/// - strength：0 = 原图，1 = 完整 LUT，中间线性混合（专业软件必备）；
/// - 参数非法（空指针/尺寸非法/LUT 无效）返回 false 且不改动缓冲。
bool applyLutToRgba(std::uint8_t* pixels, int width, int height, const Lut3D& lut,
                    double strength = 1.0);

/// 说明文案（供 UI/日志）。
const char* lutPurpose();

}  // namespace core
}  // namespace nairoclip
