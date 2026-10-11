#pragma once
// =============================================================================
// 隐形水印（Invisible Watermark）—— 文档第 8 部分「3.1 隐形水印（事后追溯）」
// =============================================================================
// 产品对应（原文）：
//   「对 Pro 版本生成的内容加一层不可见数字水印，编码账号信息，
//     用于后续发现"破解版生成内容被传播"时定位泄露源头，
//     作为技术封堵和法律维权依据。」
//
// 实现原理（真实、可测，零第三方依赖）：
//   · QIM（Quantization Index Modulation）块量化水印：
//     把画面按 blockSize 划分为小块，对每块的亮度均值做「奇偶量化」——
//     量化到 Q 的倍数后取商 q，用 q 的奇偶性编码 1 bit；
//     嵌入时把块内像素整体平移（shift）使均值落到目标量化级；
//   · 解码时对同一位置重新量化，取奇偶性得比特；
//   · 鲁棒性：每位重复 redundancy 个块（密钥置换选择块位置），
//     解码按多数表决 + CRC-16 校验（抗压缩/缩放导致的部分位错）；
//   · 位置置换由 key 派生（LCG），避免局部损坏连续摧毁载荷比特。
//
// 载荷（固定 64 位）：
//   · 32 位：账号哈希（SHA-256(accountId) 前 32 位；与 crypto 模块一致）；
//   · 16 位：导出序号（每次导出递增/随机；用于定位具体导出批次）；
//   · 16 位：CRC-16/CCITT（前 48 位校验；解码时验证完整性）。
//
// 分工（与全项目一致）：
//   · core 负责嵌入/检测算法（纯像素运算，可测试）；
//   · 账号 ID 由平台层提供（核心不联网、不存凭据）；
//   · 导出管线对 Pro 内容逐帧嵌入（见 export_executor 的 invisibleWatermark 选项）。
//
// 合规说明：仅编码「账号哈希」（不可逆），不采集任何个人信息；
// 用途限定为泄露溯源（文档隐私政策 3.2）。

#include <cstdint>
#include <string>

namespace nairoclip {
namespace core {

/// 隐形水印载荷（64 位）。
struct WatermarkPayload {
    /// 账号哈希（SHA-256(accountId) 前 32 位；0 = 匿名/未登录）。
    std::uint32_t accountHash = 0;
    /// 导出序号（每次导出唯一；用于定位导出批次）。
    std::uint16_t exportSequence = 0;
    /// CRC-16/CCITT（由前 48 位计算；构造时用 finalize() 填充）。
    std::uint16_t crc = 0;

    /// 计算并写入 CRC（修改本对象）。
    void finalize();
    /// 校验 CRC 是否匹配（不解码时预检）。
    bool crcValid() const;
    /// 是否有效载荷（CRC 通过）。
    bool valid() const { return crcValid(); }
};

/// 从账号 ID 计算账号哈希（SHA-256 前 32 位；空串 → 0）。
std::uint32_t accountHashOf(const std::string& accountId);

/// CRC-16/CCITT-FALSE（多项式 0x1021，初值 0xFFFF）。
std::uint16_t crc16Ccitt(const std::uint8_t* data, std::size_t length);

/// 隐形水印配置。
struct InvisibleWatermarkConfig {
    /// 位置置换种子（应用内固定；平台层从构建配置注入）。
    std::uint64_t key = 0x4E434C50ULL;  // "NCLP"
    /// 量化块边长（像素；8 或 16 推荐；越小容量越大、鲁棒性越低）。
    int blockSize = 8;
    /// QIM 量化步长（亮度级；越大越鲁棒但越可能可见；推荐 8-16）。
    /// 容差规格：对均匀亮度偏移的容忍度约为 ±(quantStep/2 - 1) 灰度
    /// （step=8 → ±3；step=16 → ±7）；超出容差后由冗余多数表决 + CRC 兜底。
    /// 取值由 adaptiveForResolution 按输出分辨率自动放大（小分辨率更脆弱）。
    double quantStep = 8.0;
    /// 每比特重复块数（多数表决；越大越鲁棒、需要更多容量）。
    /// 默认 16：实测 H.264 CRF18 在 ≥640x480 下可稳定检出（详见测试）。
    int redundancy = 16;

    bool valid() const {
        return blockSize >= 4 && blockSize <= 64 && quantStep >= 1.0 && quantStep <= 64.0 &&
               redundancy >= 1 && redundancy <= 64;
    }
    /// 载荷位数。
    static constexpr int payloadBits() { return 64; }
};

/// 按输出分辨率自适应调整配置（小分辨率放大步长与冗余，保证可检出）。
/// - 实测依据（H.264 CRF18，30 帧）：≥640x480 用默认配置可检出（agreement ≥0.98）；
///   小画面（<640x480）块数少、编码块效应占比高 → 放大步长并提高冗余；
/// - 容量不足时返回原配置（嵌入侧会拒绝并给警告，不中断导出）。
InvisibleWatermarkConfig adaptiveWatermarkConfig(int width, int height);

/// 计算「实际生效配置」：自适应配置与调用方配置取更鲁棒者（单一事实来源）。
/// - 嵌入/检测/导出容量预检必须使用同一函数，避免「预检说不足、嵌入其实可行」
///   或反向的不一致（这是一个已修复的真实缺陷：小分辨率输出曾因预检用原始配置
///   而误判容量不足，实际嵌入本可成功）；
/// - 规则：自适应块更小（小分辨率）时以自适应块与步长为基（更抗编码块效应），
///   quantStep 取两者较大值；key 与 redundancy 保持调用方值（容量不足时由
///   嵌入侧拒绝并给警告，语义直白）。
InvisibleWatermarkConfig effectiveWatermarkConfig(int width, int height,
                                                  const InvisibleWatermarkConfig& config);

/// 某帧尺寸能否承载隐形水印（按嵌入实际生效配置判定；供导出管线预检）。
/// 与 embedInvisibleWatermark / detectInvisibleWatermark 使用同一配置推导，
/// 保证「预检通过 ⇔ 嵌入可执行」。
bool invisibleWatermarkFits(int width, int height, const InvisibleWatermarkConfig& config);

/// 计算某帧尺寸下的实际容量（可承载的载荷比特数 = 块数 / 重复数）。
int invisibleCapacityBits(int width, int height, const InvisibleWatermarkConfig& config);

/// 容量是否足够（>= 64 位）。
bool invisibleCapacitySufficient(int width, int height, const InvisibleWatermarkConfig& config);

/// 嵌入隐形水印（原地修改亮度通道；alpha 不动）。
/// - rgba：交错 RGBA8；width/height 为像素尺寸；
/// - 返回 false：参数非法 / 载荷 CRC 无效 / 容量不足（不改动像素）。
/// - 说明：内部按分辨率自适应配置（adaptiveWatermarkConfig 与调用方配置取更鲁棒者）。
bool embedInvisibleWatermark(std::uint8_t* rgba, int width, int height,
                             const WatermarkPayload& payload,
                             const InvisibleWatermarkConfig& config = {});

/// 检测结果。
struct InvisibleDetectResult {
    /// 是否检出有效水印（多数表决通过 + CRC 校验通过）。
    bool detected = false;
    /// 解码出的载荷（detected=false 时内容不可信）。
    WatermarkPayload payload;
    /// 各比特的多数表决占比均值（0..1；越接近 1 越可靠）。
    double bitAgreement = 0.0;
    /// 容量（比特）。
    int capacityBits = 0;
    /// 说明（可直接展示，如「未检出（容量不足）」）。
    std::string note;

    bool valid() const { return detected; }
};

/// 检测隐形水印（只读；不修改像素）。
InvisibleDetectResult detectInvisibleWatermark(const std::uint8_t* rgba, int width, int height,
                                               const InvisibleWatermarkConfig& config = {});

/// 说明文案（供 UI/日志；描述水印用途）。
const char* invisibleWatermarkPurpose();

/// 导出管线用的隐形水印规格（enabled + 载荷 + 配置）。
/// 由平台层按权益组装（Pro 内容嵌入），放入 ExportExecutorOptions。
struct InvisibleWatermarkSpec {
    /// 是否启用（enabled 时逐帧嵌入；Free 内容不嵌，见文档 8.3.1「对 Pro 版本生成的内容」）。
    bool enabled = false;
    /// 载荷（accountHash 由 accountHashOf(accountId) 得到；exportSequence 每次导出唯一）。
    WatermarkPayload payload = {};
    /// 嵌入配置（容量不足时导出器记 warning 并跳过，不中断导出）。
    InvisibleWatermarkConfig config = {};

    /// 是否可用（启用 + 载荷 CRC 有效 + 配置合法）。
    bool usable() const { return enabled && payload.valid() && config.valid(); }
};

}  // namespace core
}  // namespace nairoclip
