#pragma once
// =============================================================================
// 密码学基础（Crypto Primitives）—— 文档第 8 部分「二、模型分发架构」
// =============================================================================
// 产品对应（第 8 部分 2.2 具体架构）：
//   「模型文件本地 AES 加密存储，推理时用服务器下发的密钥在内存解密」
//
// 本模块提供两件标准算法，零第三方依赖（仅 C++17 标准库）：
//   1. SHA-256（FIPS 180-4）—— 模型文件完整性校验；
//   2. AES-256-CTR（FIPS 197 + NIST SP 800-38A）—— 模型文件加解密
//      （CTR 模式支持流式处理，无需填充；密钥由服务器下发）。
//
// 实现说明（为什么自研而不用 OpenSSL）：
//   · 项目红线「core 零第三方依赖」，可交叉编译到移动端；
//   · 算法为标准公开算法，可用 NIST/FIPS 官方向量逐位验证（见测试）。
//
// 安全边界（务必阅读）：
//   · 本模块只提供「原语」。密钥管理、签名 URL 校验、安全存储由上层负责；
//   · 服务端下发密钥经内存解密——密钥不得写入磁盘/日志（平台层职责）；
//   · 不实现 RSA/签名——客户端只做「验证 + 解密」，不做密钥协商。

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nairoclip {
namespace core {

// =============================================================================
// SHA-256（FIPS 180-4）
// =============================================================================

/// SHA-256 摘要长度（字节）。
constexpr std::size_t kSha256DigestSize = 32;
/// SHA-256 分组长度（字节）。
constexpr std::size_t kSha256BlockSize = 64;

/// SHA-256 增量摘要器（支持流式处理大文件，无需整块载入内存）。
class Sha256 {
public:
    Sha256();

    /// 追加数据（可多次调用）。
    void update(const void* data, std::size_t length);
    /// 结束并写入 32 字节摘要；之后不可再 update（除非重新构造）。
    void finalize(std::uint8_t outDigest[kSha256DigestSize]);

    /// 重置为初始状态（可复用）。
    void reset();

private:
    void processBlock(const std::uint8_t block[kSha256BlockSize]);

    std::uint32_t state_[8];
    std::uint64_t totalBytes_;
    std::uint8_t buffer_[kSha256BlockSize];
    std::size_t bufferLength_;
};

/// 一次性计算 SHA-256 摘要。
void sha256(const void* data, std::size_t length, std::uint8_t outDigest[kSha256DigestSize]);

/// 便捷封装：返回小写十六进制字符串（64 字符）。
std::string sha256Hex(const void* data, std::size_t length);
/// 便捷封装：字符串输入。
std::string sha256Hex(const std::string& text);
/// 摘要转小写十六进制。
std::string sha256Hex(const std::uint8_t digest[kSha256DigestSize]);
/// 十六进制字符串解析（大小写不敏感；长度/字符非法返回 false）。
bool sha256FromHex(const char* text, std::uint8_t outDigest[kSha256DigestSize]);

/// 恒定时间比较（防时序侧信道；长度不等直接 false）。
/// 用于摘要/密钥校验，避免提前返回泄露匹配前缀长度。
bool constantTimeEquals(const void* a, const void* b, std::size_t length);

// =============================================================================
// AES-256（FIPS 197）+ CTR 模式（NIST SP 800-38A）
// =============================================================================

/// AES-256 密钥长度（字节）。
constexpr std::size_t kAes256KeySize = 32;
/// AES 分组长度（字节）。
constexpr std::size_t kAesBlockSize = 16;
/// CTR 模式计数器长度（字节；使用完整 16 字节计数器，纯计数，不做 nonce 拆分）。
constexpr std::size_t kAesCtrCounterSize = 16;

/// AES-256 分组加密原语（ECB 单分组；仅供内部与标准向量测试使用）。
/// - key：32 字节密钥；in/out：各 16 字节。
void aes256EncryptBlock(const std::uint8_t key[kAes256KeySize],
                        const std::uint8_t in[kAesBlockSize], std::uint8_t out[kAesBlockSize]);

/// AES-256-CTR 流式加解密器（加密 = 解密，异或对称）。
/// - 计数器按大端整数递增（NIST SP 800-38A F.5.1 约定）；
/// - 支持任意长度数据（含跨块分片）；
/// - 同一 key + 初始计数器下，加密后解密还原原文。
class Aes256Ctr {
public:
    /// 构造：key（32 字节）、初始计数器（16 字节，大端递增）。
    /// 非法指针直接置为无效状态（valid() == false）。
    Aes256Ctr(const std::uint8_t key[kAes256KeySize],
              const std::uint8_t initialCounter[kAesCtrCounterSize]);

    /// 是否可用（构造参数有效）。
    bool valid() const { return valid_; }

    /// 处理数据（原地加密/解密）；支持任意分片。
    /// 无效状态直接返回（不修改数据）。
    void process(std::uint8_t* data, std::size_t length);

    /// 便捷：处理并返回新缓冲。
    std::vector<std::uint8_t> process(const std::vector<std::uint8_t>& data);

    /// 重置到初始计数器（可对同一密钥流位置重新开始）。
    void reset();

    /// 当前计数器值（用于断点续传时定位密钥流偏移）。
    const std::uint8_t* counter() const { return counter_; }

private:
    void generateKeystreamBlock();

    std::uint8_t expandedKey_[16 * 15];  ///< 预展开轮密钥（构造时一次；避免逐块重复扩展）
    int rounds_ = 14;
    std::uint8_t initialCounter_[kAesCtrCounterSize];
    std::uint8_t counter_[kAesCtrCounterSize];
    std::uint8_t keystream_[kAesBlockSize];
    std::size_t keystreamOffset_;
    bool valid_;
};

/// 一次性 AES-256-CTR 加解密（等价于构造 + process）。
/// 返回与输入等长的密文/明文；key 非法时返回空向量。
std::vector<std::uint8_t> aes256CtrCrypt(const std::uint8_t key[kAes256KeySize],
                                         const std::uint8_t counter[kAesCtrCounterSize],
                                         const std::vector<std::uint8_t>& data);

/// 按指定密钥流偏移加解密（断点续传场景：续传时从「已下载字节数」处继续）。
/// - offset 必须为 16 的倍数？——不必：内部按块推进计数器后丢弃前 offset%16 字节。
/// 返回等长结果；key/counter 非法返回空向量。
std::vector<std::uint8_t> aes256CtrCryptAtOffset(const std::uint8_t key[kAes256KeySize],
                                                 const std::uint8_t counter[kAesCtrCounterSize],
                                                 std::uint64_t offset,
                                                 const std::vector<std::uint8_t>& data);

// =============================================================================
// 密钥流偏移工具（供断点续传精确定位）
// =============================================================================

/// 由「字节偏移」推导 CTR 起始计数器（大端整数加法）。
/// - offset：从初始计数器的字节偏移；按块数推进计数器低 64 位（高 64 位不动，
///   与 AES-256-CTR 的 128 位计数器实际使用量相比余量极大）；
/// - outCounter：写入推进后的计数器（16 字节）。
void aesCtrCounterAtOffset(const std::uint8_t initialCounter[kAesCtrCounterSize],
                           std::uint64_t offset, std::uint8_t outCounter[kAesCtrCounterSize]);

/// 计算「密钥流偏移」对应的块内剩余字节数（offset % 16）。
/// 断点续传时：用 aesCtrCounterAtOffset 定位块，再丢弃 intraBlock 字节。
std::size_t aesCtrIntraBlockOffset(std::uint64_t offset);

// =============================================================================
// 随机数（密钥/盐生成；非密码学强度时明确标注）
// =============================================================================

/// 密码学安全随机字节（平台层提供 CSPRNG；core 无系统依赖时返回 false）。
/// - 说明：本函数为平台层注入点——core 不直接调用系统随机源，
///   由平台层在 app 启动时通过 setSecureRandomProvider 注入
///   （Android：SecureRandom / iOS：SecRandomCopyBytes / 桌面：urandom）。
using SecureRandomProvider = bool (*)(std::uint8_t* out, std::size_t length);

/// 注入安全随机源（平台层调用；传 nullptr 表示清除）。
void setSecureRandomProvider(SecureRandomProvider provider);

/// 生成随机字节；未注入 provider 时返回 false（fail-closed，不降级到弱随机）。
bool secureRandomBytes(std::uint8_t* out, std::size_t length);

/// 生成随机十六进制字符串（length 字节 → 2×length 字符）。
/// 未注入 provider 时返回空串。
std::string secureRandomHex(std::size_t length);

}  // namespace core
}  // namespace nairoclip
