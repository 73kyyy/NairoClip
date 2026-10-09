#include "nairoclip/core/crypto.h"

#include <cstring>

namespace nairoclip {
namespace core {

// =============================================================================
// SHA-256（FIPS 180-4）
// =============================================================================

namespace {

constexpr std::uint32_t kSha256K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u,
    0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu,
    0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu,
    0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u,
    0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u,
    0xc67178f2u};

inline std::uint32_t rotr32(std::uint32_t x, int n) {
    return (x >> n) | (x << (32 - n));
}

}  // namespace

Sha256::Sha256() { reset(); }

void Sha256::reset() {
    state_[0] = 0x6a09e667u;
    state_[1] = 0xbb67ae85u;
    state_[2] = 0x3c6ef372u;
    state_[3] = 0xa54ff53au;
    state_[4] = 0x510e527fu;
    state_[5] = 0x9b05688cu;
    state_[6] = 0x1f83d9abu;
    state_[7] = 0x5be0cd19u;
    totalBytes_ = 0;
    bufferLength_ = 0;
    std::memset(buffer_, 0, sizeof(buffer_));
}

void Sha256::processBlock(const std::uint8_t block[kSha256BlockSize]) {
    std::uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24) |
               (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
               (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
               static_cast<std::uint32_t>(block[i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i) {
        const std::uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const std::uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    std::uint32_t a = state_[0];
    std::uint32_t b = state_[1];
    std::uint32_t c = state_[2];
    std::uint32_t d = state_[3];
    std::uint32_t e = state_[4];
    std::uint32_t f = state_[5];
    std::uint32_t g = state_[6];
    std::uint32_t h = state_[7];

    for (int i = 0; i < 64; ++i) {
        const std::uint32_t s1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        const std::uint32_t ch = (e & f) ^ ((~e) & g);
        const std::uint32_t temp1 = h + s1 + ch + kSha256K[i] + w[i];
        const std::uint32_t s0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t temp2 = s0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

void Sha256::update(const void* data, std::size_t length) {
    if (data == nullptr || length == 0) {
        return;
    }
    const std::uint8_t* bytes = static_cast<const std::uint8_t*>(data);
    totalBytes_ += length;

    // 先填满已有缓冲。
    if (bufferLength_ > 0) {
        const std::size_t need = kSha256BlockSize - bufferLength_;
        const std::size_t take = length < need ? length : need;
        std::memcpy(buffer_ + bufferLength_, bytes, take);
        bufferLength_ += take;
        bytes += take;
        length -= take;
        if (bufferLength_ == kSha256BlockSize) {
            processBlock(buffer_);
            bufferLength_ = 0;
        }
    }

    // 整块处理。
    while (length >= kSha256BlockSize) {
        processBlock(bytes);
        bytes += kSha256BlockSize;
        length -= kSha256BlockSize;
    }

    // 尾部余量。
    if (length > 0) {
        std::memcpy(buffer_, bytes, length);
        bufferLength_ = length;
    }
}

void Sha256::finalize(std::uint8_t outDigest[kSha256DigestSize]) {
    if (outDigest == nullptr) {
        return;
    }
    const std::uint64_t bitLength = totalBytes_ * 8u;

    // 追加 0x80 + 补零 + 64 位大端长度。
    const std::uint8_t pad = 0x80;
    update(&pad, 1);
    const std::uint8_t zero = 0x00;
    while (bufferLength_ != 56) {
        update(&zero, 1);
    }
    std::uint8_t lengthBytes[8];
    for (int i = 0; i < 8; ++i) {
        lengthBytes[i] = static_cast<std::uint8_t>((bitLength >> (56 - i * 8)) & 0xffu);
    }
    update(lengthBytes, 8);

    for (int i = 0; i < 8; ++i) {
        const std::uint32_t v = state_[i];
        outDigest[i * 4] = static_cast<std::uint8_t>((v >> 24) & 0xffu);
        outDigest[i * 4 + 1] = static_cast<std::uint8_t>((v >> 16) & 0xffu);
        outDigest[i * 4 + 2] = static_cast<std::uint8_t>((v >> 8) & 0xffu);
        outDigest[i * 4 + 3] = static_cast<std::uint8_t>(v & 0xffu);
    }
}

void sha256(const void* data, std::size_t length, std::uint8_t outDigest[kSha256DigestSize]) {
    Sha256 hasher;
    hasher.update(data, length);
    hasher.finalize(outDigest);
}

std::string sha256Hex(const std::uint8_t digest[kSha256DigestSize]) {
    static const char* kHex = "0123456789abcdef";
    std::string out;
    out.reserve(kSha256DigestSize * 2);
    for (std::size_t i = 0; i < kSha256DigestSize; ++i) {
        out.push_back(kHex[(digest[i] >> 4) & 0x0f]);
        out.push_back(kHex[digest[i] & 0x0f]);
    }
    return out;
}

std::string sha256Hex(const void* data, std::size_t length) {
    std::uint8_t digest[kSha256DigestSize];
    sha256(data, length, digest);
    return sha256Hex(digest);
}

std::string sha256Hex(const std::string& text) {
    return sha256Hex(text.data(), text.size());
}

bool sha256FromHex(const char* text, std::uint8_t outDigest[kSha256DigestSize]) {
    if (text == nullptr || outDigest == nullptr) {
        return false;
    }
    std::size_t len = 0;
    while (text[len] != '\0') {
        ++len;
    }
    if (len != kSha256DigestSize * 2) {
        return false;
    }
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (std::size_t i = 0; i < kSha256DigestSize; ++i) {
        const int hi = nibble(text[i * 2]);
        const int lo = nibble(text[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        outDigest[i] = static_cast<std::uint8_t>((hi << 4) | lo);
    }
    return true;
}

bool constantTimeEquals(const void* a, const void* b, std::size_t length) {
    if (a == nullptr || b == nullptr) {
        return a == b;
    }
    const std::uint8_t* pa = static_cast<const std::uint8_t*>(a);
    const std::uint8_t* pb = static_cast<const std::uint8_t*>(b);
    std::uint8_t diff = 0;
    for (std::size_t i = 0; i < length; ++i) {
        diff |= static_cast<std::uint8_t>(pa[i] ^ pb[i]);
    }
    return diff == 0;
}

// =============================================================================
// AES-256（FIPS 197）
// =============================================================================

namespace {

/// AES S-Box（FIPS 197 图 7）。
constexpr std::uint8_t kSbox[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab,
    0x76, 0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4,
    0x72, 0xc0, 0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71,
    0xd8, 0x31, 0x15, 0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2,
    0xeb, 0x27, 0xb2, 0x75, 0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6,
    0xb3, 0x29, 0xe3, 0x2f, 0x84, 0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb,
    0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf, 0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45,
    0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8, 0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5,
    0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2, 0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44,
    0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73, 0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a,
    0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb, 0xe0, 0x32, 0x3a, 0x0a, 0x49,
    0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79, 0xe7, 0xc8, 0x37, 0x6d,
    0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08, 0xba, 0x78, 0x25,
    0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a, 0x70, 0x3e,
    0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e, 0xe1,
    0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb,
    0x16};

/// GF(2^8) 乘 2（xtime）。
inline std::uint8_t xtime(std::uint8_t x) {
    return static_cast<std::uint8_t>((x << 1) ^ ((x & 0x80u) ? 0x1bu : 0x00u));
}

/// GF(2^8) 乘法（俄罗斯农民乘法）。
inline std::uint8_t gmul(std::uint8_t a, std::uint8_t b) {
    std::uint8_t result = 0;
    for (int i = 0; i < 8; ++i) {
        if (b & 1u) {
            result ^= a;
        }
        b = static_cast<std::uint8_t>(b >> 1);
        a = xtime(a);
    }
    return result;
}

/// AES 密钥扩展（支持 128/192/256 位；Nk = 4/6/8）。
/// 返回轮数（10/12/14）；outExpanded 需 ≥ (rounds+1)*16 字节。
int expandKey(const std::uint8_t* key, std::size_t keySize, std::uint8_t* outExpanded) {
    const int nk = static_cast<int>(keySize / 4);
    const int rounds = nk + 6;
    const int totalWords = 4 * (rounds + 1);

    for (int i = 0; i < nk; ++i) {
        outExpanded[i * 4] = key[i * 4];
        outExpanded[i * 4 + 1] = key[i * 4 + 1];
        outExpanded[i * 4 + 2] = key[i * 4 + 2];
        outExpanded[i * 4 + 3] = key[i * 4 + 3];
    }

    std::uint8_t rcon = 0x01;
    for (int i = nk; i < totalWords; ++i) {
        std::uint8_t temp[4];
        std::memcpy(temp, outExpanded + (i - 1) * 4, 4);
        if (i % nk == 0) {
            // RotWord + SubWord + Rcon。
            const std::uint8_t t0 = temp[0];
            temp[0] = static_cast<std::uint8_t>(kSbox[temp[1]] ^ rcon);
            temp[1] = kSbox[temp[2]];
            temp[2] = kSbox[temp[3]];
            temp[3] = kSbox[t0];
            rcon = xtime(rcon);
        } else if (nk > 6 && i % nk == 4) {
            temp[0] = kSbox[temp[0]];
            temp[1] = kSbox[temp[1]];
            temp[2] = kSbox[temp[2]];
            temp[3] = kSbox[temp[3]];
        }
        for (int j = 0; j < 4; ++j) {
            outExpanded[i * 4 + j] =
                static_cast<std::uint8_t>(outExpanded[(i - nk) * 4 + j] ^ temp[j]);
        }
    }
    return rounds;
}

/// 单分组 AES 加密（ECB；输入输出 16 字节）。
void aesEncryptBlockInternal(const std::uint8_t* expanded, int rounds,
                             const std::uint8_t in[16], std::uint8_t out[16]) {
    std::uint8_t state[16];
    for (int i = 0; i < 16; ++i) {
        state[i] = static_cast<std::uint8_t>(in[i] ^ expanded[i]);
    }

    for (int round = 1; round <= rounds; ++round) {
        // SubBytes。
        for (int i = 0; i < 16; ++i) {
            state[i] = kSbox[state[i]];
        }
        // ShiftRows（列主序状态；行 r 左移 r 字节）。
        std::uint8_t tmp[16];
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) {
                tmp[c * 4 + r] = state[((c + r) % 4) * 4 + r];
            }
        }
        std::memcpy(state, tmp, 16);
        // MixColumns（最后一轮跳过）。
        if (round != rounds) {
            for (int c = 0; c < 4; ++c) {
                const std::uint8_t a0 = state[c * 4];
                const std::uint8_t a1 = state[c * 4 + 1];
                const std::uint8_t a2 = state[c * 4 + 2];
                const std::uint8_t a3 = state[c * 4 + 3];
                state[c * 4] = static_cast<std::uint8_t>(
                    gmul(a0, 2) ^ gmul(a1, 3) ^ a2 ^ a3);
                state[c * 4 + 1] = static_cast<std::uint8_t>(
                    a0 ^ gmul(a1, 2) ^ gmul(a2, 3) ^ a3);
                state[c * 4 + 2] = static_cast<std::uint8_t>(
                    a0 ^ a1 ^ gmul(a2, 2) ^ gmul(a3, 3));
                state[c * 4 + 3] = static_cast<std::uint8_t>(
                    gmul(a0, 3) ^ a1 ^ a2 ^ gmul(a3, 2));
            }
        }
        // AddRoundKey。
        const std::uint8_t* roundKey = expanded + round * 16;
        for (int i = 0; i < 16; ++i) {
            state[i] = static_cast<std::uint8_t>(state[i] ^ roundKey[i]);
        }
    }

    std::memcpy(out, state, 16);
}

}  // namespace

void aes256EncryptBlock(const std::uint8_t key[kAes256KeySize],
                        const std::uint8_t in[kAesBlockSize], std::uint8_t out[kAesBlockSize]) {
    if (key == nullptr || in == nullptr || out == nullptr) {
        return;
    }
    std::uint8_t expanded[16 * 15];
    const int rounds = expandKey(key, kAes256KeySize, expanded);
    aesEncryptBlockInternal(expanded, rounds, in, out);
}

// ---- AES-256-CTR ----

Aes256Ctr::Aes256Ctr(const std::uint8_t key[kAes256KeySize],
                     const std::uint8_t initialCounter[kAesCtrCounterSize])
    : keystreamOffset_(kAesBlockSize), valid_(key != nullptr && initialCounter != nullptr) {
    std::memset(expandedKey_, 0, sizeof(expandedKey_));
    std::memset(initialCounter_, 0, sizeof(initialCounter_));
    std::memset(counter_, 0, sizeof(counter_));
    std::memset(keystream_, 0, sizeof(keystream_));
    if (!valid_) {
        return;
    }
    rounds_ = expandKey(key, kAes256KeySize, expandedKey_);
    std::memcpy(initialCounter_, initialCounter, kAesCtrCounterSize);
    std::memcpy(counter_, initialCounter, kAesCtrCounterSize);
}

void Aes256Ctr::reset() {
    if (!valid_) {
        return;
    }
    std::memcpy(counter_, initialCounter_, kAesCtrCounterSize);
    keystreamOffset_ = kAesBlockSize;
    std::memset(keystream_, 0, sizeof(keystream_));
}

void Aes256Ctr::generateKeystreamBlock() {
    aesEncryptBlockInternal(expandedKey_, rounds_, counter_, keystream_);

    // 计数器大端递增（128 位）。
    for (int i = kAesCtrCounterSize - 1; i >= 0; --i) {
        if (++counter_[i] != 0) {
            break;
        }
    }
    keystreamOffset_ = 0;
}

void Aes256Ctr::process(std::uint8_t* data, std::size_t length) {
    if (!valid_ || data == nullptr || length == 0) {
        return;
    }
    for (std::size_t i = 0; i < length; ++i) {
        if (keystreamOffset_ == kAesBlockSize) {
            generateKeystreamBlock();
        }
        data[i] = static_cast<std::uint8_t>(data[i] ^ keystream_[keystreamOffset_]);
        ++keystreamOffset_;
    }
}

std::vector<std::uint8_t> Aes256Ctr::process(const std::vector<std::uint8_t>& data) {
    std::vector<std::uint8_t> out = data;
    process(out.data(), out.size());
    return out;
}

void aesCtrCounterAtOffset(const std::uint8_t initialCounter[kAesCtrCounterSize],
                           std::uint64_t offset, std::uint8_t outCounter[kAesCtrCounterSize]) {
    if (initialCounter == nullptr || outCounter == nullptr) {
        return;
    }
    std::uint8_t counter[kAesCtrCounterSize];
    std::memcpy(counter, initialCounter, kAesCtrCounterSize);

    // 按 16 字节块推进（offset 可能极大，但块数在 64 位内）。
    const std::uint64_t blocks = offset / kAesBlockSize;
    // 低 64 位加上块数（高 64 位不动，实务中不可能触及）。
    std::uint64_t low = 0;
    for (int i = 8; i < 16; ++i) {
        low = (low << 8) | counter[i];
    }
    low += blocks;
    for (int i = 15; i >= 8; --i) {
        counter[i] = static_cast<std::uint8_t>(low & 0xffu);
        low >>= 8;
    }
    std::memcpy(outCounter, counter, kAesCtrCounterSize);
}

std::size_t aesCtrIntraBlockOffset(std::uint64_t offset) {
    return static_cast<std::size_t>(offset % kAesBlockSize);
}

std::vector<std::uint8_t> aes256CtrCrypt(const std::uint8_t key[kAes256KeySize],
                                         const std::uint8_t counter[kAesCtrCounterSize],
                                         const std::vector<std::uint8_t>& data) {
    Aes256Ctr cipher(key, counter);
    if (!cipher.valid()) {
        return {};
    }
    return cipher.process(data);
}

std::vector<std::uint8_t> aes256CtrCryptAtOffset(const std::uint8_t key[kAes256KeySize],
                                                 const std::uint8_t counter[kAesCtrCounterSize],
                                                 std::uint64_t offset,
                                                 const std::vector<std::uint8_t>& data) {
    if (key == nullptr || counter == nullptr) {
        return {};
    }
    std::uint8_t startCounter[kAesCtrCounterSize];
    aesCtrCounterAtOffset(counter, offset, startCounter);

    Aes256Ctr cipher(key, startCounter);
    if (!cipher.valid()) {
        return {};
    }
    // 丢弃块内前导密钥流字节（保证与从头处理的结果一致）。
    const std::size_t intra = aesCtrIntraBlockOffset(offset);
    if (intra > 0) {
        std::vector<std::uint8_t> skip(intra, 0);
        cipher.process(skip.data(), skip.size());
    }
    return cipher.process(data);
}

// =============================================================================
// 随机数注入点
// =============================================================================

namespace {
SecureRandomProvider g_secureRandomProvider = nullptr;
}

void setSecureRandomProvider(SecureRandomProvider provider) { g_secureRandomProvider = provider; }

bool secureRandomBytes(std::uint8_t* out, std::size_t length) {
    if (out == nullptr || length == 0) {
        return false;
    }
    if (g_secureRandomProvider == nullptr) {
        // fail-closed：不降级到弱随机（否则会生成可预测密钥）。
        return false;
    }
    return g_secureRandomProvider(out, length);
}

std::string secureRandomHex(std::size_t length) {
    if (length == 0) {
        return std::string();
    }
    std::vector<std::uint8_t> bytes(length, 0);
    if (!secureRandomBytes(bytes.data(), bytes.size())) {
        return std::string();
    }
    static const char* kHex = "0123456789abcdef";
    std::string out;
    out.reserve(length * 2);
    for (std::size_t i = 0; i < length; ++i) {
        out.push_back(kHex[(bytes[i] >> 4) & 0x0f]);
        out.push_back(kHex[bytes[i] & 0x0f]);
    }
    return out;
}

}  // namespace core
}  // namespace nairoclip
