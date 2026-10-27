#include "nairoclip/core/lut.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <sstream>

namespace nairoclip {
namespace core {

namespace {

constexpr int kMinLutSize = 2;
constexpr int kMaxLutSize = 64;

std::string trim(const std::string& s) {
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) --e;
    return s.substr(b, e - b);
}

/// 按空白切分。
std::vector<std::string> splitWhitespace(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream iss(s);
    std::string tok;
    while (iss >> tok) {
        out.push_back(tok);
    }
    return out;
}

bool parseDouble(const std::string& tok, double& out) {
    try {
        std::size_t used = 0;
        const double v = std::stod(tok, &used);
        if (used != tok.size()) {
            return false;  // 「1.0abc」这类部分匹配视为非法
        }
        if (!std::isfinite(v)) {
            return false;
        }
        out = v;
        return true;
    } catch (...) {
        return false;
    }
}

bool parseSize(const std::vector<std::string>& toks, int& out) {
    if (toks.size() != 2) {
        return false;
    }
    try {
        std::size_t used = 0;
        const long v = std::stol(toks[1], &used);
        if (used != toks[1].size()) {
            return false;
        }
        out = static_cast<int>(v);
        return true;
    } catch (...) {
        return false;
    }
}

}  // namespace

bool Lut3D::valid() const {
    if (size < kMinLutSize || size > kMaxLutSize) {
        return false;
    }
    const std::size_t expected =
        static_cast<std::size_t>(size) * static_cast<std::size_t>(size) *
        static_cast<std::size_t>(size) * 3u;
    if (data.size() != expected) {
        return false;
    }
    for (int c = 0; c < 3; ++c) {
        if (!(domainMax[c] > domainMin[c])) {
            return false;
        }
        if (!std::isfinite(domainMin[c]) || !std::isfinite(domainMax[c])) {
            return false;
        }
    }
    return true;
}

bool parseCubeLut(const std::string& text, Lut3D& out, std::string& error) {
    error.clear();
    Lut3D lut;
    bool seenSize = false;
    bool seen1D = false;
    bool hasTitle = false;
    std::vector<double> values;
    std::size_t lineNo = 0;

    std::istringstream iss(text);
    std::string rawLine;
    while (std::getline(iss, rawLine)) {
        ++lineNo;
        const std::string line = trim(rawLine);
        if (line.empty()) {
            continue;
        }
        if (line[0] == '#') {
            continue;
        }
        const std::vector<std::string> toks = splitWhitespace(line);
        if (toks.empty()) {
            continue;
        }
        const std::string& head = toks[0];

        // TITLE 后面可以带引号，也可以不带；内容不参与校验
        if (head == "TITLE") {
            hasTitle = true;
            const std::size_t pos = line.find("TITLE");
            std::string t = trim(line.substr(pos + 5));
            if (t.size() >= 2 && t.front() == '"' && t.back() == '"') {
                t = t.substr(1, t.size() - 2);
            }
            lut.title = t;
            continue;
        }
        if (head == "LUT_3D_SIZE") {
            int sz = 0;
            if (!parseSize(toks, sz)) {
                error = "第 " + std::to_string(lineNo) + " 行：LUT_3D_SIZE 需要一个整数";
                return false;
            }
            if (sz < kMinLutSize || sz > kMaxLutSize) {
                error = "第 " + std::to_string(lineNo) + " 行：LUT_3D_SIZE 超出支持范围（" +
                        std::to_string(kMinLutSize) + "~" + std::to_string(kMaxLutSize) +
                        "），实得 " + std::to_string(sz);
                return false;
            }
            if (seenSize) {
                error = "第 " + std::to_string(lineNo) + " 行：LUT_3D_SIZE 重复声明";
                return false;
            }
            lut.size = sz;
            seenSize = true;
            continue;
        }
        if (head == "LUT_1D_SIZE") {
            // 明确拒绝而不是忽略：1D LUT 与 3D 的采样语义不同，
            // 静默当成 3D 处理会产出完全错误的颜色。
            seen1D = true;
            error = "第 " + std::to_string(lineNo) +
                    " 行：暂不支持 1D LUT（仅支持 LUT_3D_SIZE）";
            return false;
        }
        if (head == "DOMAIN_MIN" || head == "DOMAIN_MAX") {
            if (toks.size() != 4) {
                error = "第 " + std::to_string(lineNo) + " 行：" + head + " 需要三个数值";
                return false;
            }
            double vals[3] = {0.0, 0.0, 0.0};
            for (int c = 0; c < 3; ++c) {
                if (!parseDouble(toks[static_cast<std::size_t>(c) + 1], vals[c])) {
                    error = "第 " + std::to_string(lineNo) + " 行：" + head + " 含非数值";
                    return false;
                }
            }
            for (int c = 0; c < 3; ++c) {
                if (head == "DOMAIN_MIN") {
                    lut.domainMin[c] = vals[c];
                } else {
                    lut.domainMax[c] = vals[c];
                }
            }
            continue;
        }
        // 其余行必须是三个数值（数据行）
        if (toks.size() != 3) {
            error = "第 " + std::to_string(lineNo) + " 行：期望三个数值，实得 " +
                    std::to_string(toks.size()) + " 个字段";
            return false;
        }
        for (const auto& tok : toks) {
            double v = 0.0;
            if (!parseDouble(tok, v)) {
                error = "第 " + std::to_string(lineNo) + " 行：\"" + tok + "\" 不是数值";
                return false;
            }
            values.push_back(v);
        }
    }

    (void)hasTitle;
    (void)seen1D;
    if (!seenSize) {
        error = "缺少 LUT_3D_SIZE 声明";
        return false;
    }
    const std::size_t expected =
        static_cast<std::size_t>(lut.size) * static_cast<std::size_t>(lut.size) *
        static_cast<std::size_t>(lut.size) * 3u;
    if (values.size() != expected) {
        error = "数据行数量不符：LUT_3D_SIZE=" + std::to_string(lut.size) + " 需要 " +
                std::to_string(expected / 3) + " 行（" + std::to_string(expected) +
                " 个数值），实得 " + std::to_string(values.size() / 3) + " 行（" +
                std::to_string(values.size()) + " 个数值）";
        return false;
    }
    lut.data = std::move(values);
    for (double v : lut.data) {
        if (!std::isfinite(v)) {
            error = "数据中含量非有限值";
            return false;
        }
    }
    if (!lut.valid()) {
        error = "LUT 数据不自洽（检查 DOMAIN_MIN/MAX）";
        return false;
    }
    out = std::move(lut);
    return true;
}

bool loadCubeLutFile(const std::string& path, Lut3D& out, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "无法打开 LUT 文件: " + path;
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return parseCubeLut(ss.str(), out, error);
}

void lutSample(const Lut3D& lut, double r, double g, double b,
               double& outR, double& outG, double& outB) {
    outR = r;
    outG = g;
    outB = b;
    if (!lut.valid()) {
        return;
    }
    const double in[3] = {r, g, b};
    double pos[3] = {0.0, 0.0, 0.0};
    for (int c = 0; c < 3; ++c) {
        // 输入约定为**归一化 0..1**（与 applyLutToRgba 从 8bit 像素换算一致），
        // 再线性映射到整个 [DOMAIN_MIN, DOMAIN_MAX] 区间。
        //
        // 为什么这样定义：.cube 的 DOMAIN 描述「该 LUT 期望的输入刻度」。
        // 我们的管线始终产出 0..1 的 sRGB 像素，所以 0 对应 domainMin、
        // 1 对应 domainMax。对标准 0..1 域这是恒等映射；对 0..255 域的 LUT
        // 会把 1.0 正确送到立方体末端（此前按域单位解释，1.0 被当成
        // 1/255 落在起点，整张图会被压成黑）。
        //
        // 注意：线性映射后 DOMAIN 对索引的影响在数学上抵消
        // （t → domainMin + t·range → 再除以 range），这是归一化输入的必然结果。
        // 保留 DOMAIN 的解析与校验是为了：① 拒绝 domainMax <= domainMin 的坏文件；
        // ② 将来接入对数域 LUT 时，需要在这里插入域变换而非改接口。
        const double t = std::clamp(in[c], 0.0, 1.0);
        pos[c] = t * (lut.size - 1);
    }
    const int x0 = static_cast<int>(std::floor(pos[0]));
    const int y0 = static_cast<int>(std::floor(pos[1]));
    const int z0 = static_cast<int>(std::floor(pos[2]));
    const int x1 = std::min(x0 + 1, lut.size - 1);
    const int y1 = std::min(y0 + 1, lut.size - 1);
    const int z1 = std::min(z0 + 1, lut.size - 1);
    const double tx = pos[0] - x0;
    const double ty = pos[1] - y0;
    const double tz = pos[2] - z0;

    // 索引：r 变化最快，b 最慢 → ((b * size + g) * size + r)
    auto at = [&lut](int xi, int yi, int zi, int c) {
        const std::size_t idx =
            ((static_cast<std::size_t>(zi) * lut.size + static_cast<std::size_t>(yi)) *
                 lut.size +
             static_cast<std::size_t>(xi)) *
                3u +
            static_cast<std::size_t>(c);
        return lut.data[idx];
    };

    double out[3] = {0.0, 0.0, 0.0};
    for (int c = 0; c < 3; ++c) {
        // 三线性插值：8 个角点按距离加权
        const double c000 = at(x0, y0, z0, c);
        const double c100 = at(x1, y0, z0, c);
        const double c010 = at(x0, y1, z0, c);
        const double c110 = at(x1, y1, z0, c);
        const double c001 = at(x0, y0, z1, c);
        const double c101 = at(x1, y0, z1, c);
        const double c011 = at(x0, y1, z1, c);
        const double c111 = at(x1, y1, z1, c);
        const double c00 = c000 + (c100 - c000) * tx;
        const double c10 = c010 + (c110 - c010) * tx;
        const double c01 = c001 + (c101 - c001) * tx;
        const double c11 = c011 + (c111 - c011) * tx;
        const double c0 = c00 + (c10 - c00) * ty;
        const double c1 = c01 + (c11 - c01) * ty;
        out[c] = c0 + (c1 - c0) * tz;
    }
    outR = out[0];
    outG = out[1];
    outB = out[2];
}

bool applyLutToRgba(std::uint8_t* pixels, int width, int height, const Lut3D& lut,
                    double strength) {
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    if (!lut.valid()) {
        return false;
    }
    if (strength <= 0.0) {
        return true;  // 强度 0：零改动
    }
    const double s = std::min(1.0, strength);
    const std::size_t count =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t idx = i * 4;
        const double r = static_cast<double>(pixels[idx + 0]) / 255.0;
        const double g = static_cast<double>(pixels[idx + 1]) / 255.0;
        const double b = static_cast<double>(pixels[idx + 2]) / 255.0;
        double mr = 0.0, mg = 0.0, mb = 0.0;
        lutSample(lut, r, g, b, mr, mg, mb);
        // 强度混合：0 = 原图，1 = 完整 LUT
        const double fr = r + (mr - r) * s;
        const double fg = g + (mg - g) * s;
        const double fb = b + (mb - b) * s;
        pixels[idx + 0] = static_cast<std::uint8_t>(std::clamp(fr * 255.0 + 0.5, 0.0, 255.0));
        pixels[idx + 1] = static_cast<std::uint8_t>(std::clamp(fg * 255.0 + 0.5, 0.0, 255.0));
        pixels[idx + 2] = static_cast<std::uint8_t>(std::clamp(fb * 255.0 + 0.5, 0.0, 255.0));
        // alpha 不变
    }
    return true;
}

const char* lutPurpose() {
    return "3D LUT 调色（.cube 格式）：加载外部调色表，支持 17³/33³ 与强度混合";
}

}  // namespace core
}  // namespace nairoclip
