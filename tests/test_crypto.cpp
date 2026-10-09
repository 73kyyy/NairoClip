#include "test_framework.h"
#include "nairoclip/core/crypto.h"

#include <cstring>
#include <string>
#include <vector>

using namespace nairoclip::core;

namespace {

std::vector<std::uint8_t> bytesFromHex(const std::string& hex) {
    std::vector<std::uint8_t> out;
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
        const int hi = nibble(hex[i]);
        const int lo = nibble(hex[i + 1]);
        if (hi < 0 || lo < 0) break;
        out.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
    }
    return out;
}

std::string bytesToHex(const std::uint8_t* data, std::size_t length) {
    static const char* kHex = "0123456789abcdef";
    std::string out;
    out.reserve(length * 2);
    for (std::size_t i = 0; i < length; ++i) {
        out.push_back(kHex[(data[i] >> 4) & 0x0f]);
        out.push_back(kHex[data[i] & 0x0f]);
    }
    return out;
}

std::string toHex(const std::vector<std::uint8_t>& data) {
    return bytesToHex(data.data(), data.size());
}

}  // namespace

// =============================================================================
// SHA-256（FIPS 180-4 / NIST 官方示例向量）
// =============================================================================

NC_TEST(Crypto, Sha256EmptyString) {
    // FIPS 180-4 标准空串摘要。
    const std::string digest = sha256Hex(std::string());
    NC_CHECK_EQ(digest, std::string("e3b0c44298fc1c149afbf4c8996fb924"
                                    "27ae41e4649b934ca495991b7852b855"));
}

NC_TEST(Crypto, Sha256Abc) {
    // FIPS 180-4 附录 B.1："abc"。
    const std::string digest = sha256Hex(std::string("abc"));
    NC_CHECK_EQ(digest, std::string("ba7816bf8f01cfea414140de5dae2223"
                                    "b00361a396177a9cb410ff61f20015ad"));
}

NC_TEST(Crypto, Sha256LongMessage448Bits) {
    // FIPS 180-4 附录 B.2：448 位消息。
    const std::string text =
        "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    const std::string digest = sha256Hex(text);
    NC_CHECK_EQ(digest, std::string("248d6a61d20638b8e5c026930c3e6039"
                                    "a33ce45964ff2167f6ecedd419db06c1"));
}

NC_TEST(Crypto, Sha256MillionA) {
    // FIPS 180-4 附录 B.3：一百万个 'a'（逐块喂入验证增量接口）。
    Sha256 hasher;
    std::string chunk(1000, 'a');
    for (int i = 0; i < 1000; ++i) {
        hasher.update(chunk.data(), chunk.size());
    }
    std::uint8_t digest[kSha256DigestSize];
    hasher.finalize(digest);
    NC_CHECK_EQ(bytesToHex(digest, kSha256DigestSize),
                std::string("cdc76e5c9914fb9281a1c7e284d73e67"
                            "f1809a48a497200e046d39ccc7112cd0"));
}

NC_TEST(Crypto, Sha256IncrementalMatchesOneShot) {
    const std::string text = "NairoClip model payload integrity check";
    std::uint8_t oneShot[kSha256DigestSize];
    sha256(text.data(), text.size(), oneShot);

    Sha256 hasher;
    for (char c : text) {
        hasher.update(&c, 1);
    }
    std::uint8_t incremental[kSha256DigestSize];
    hasher.finalize(incremental);
    NC_CHECK(std::memcmp(oneShot, incremental, kSha256DigestSize) == 0);
}

NC_TEST(Crypto, Sha256ResetReusable) {
    Sha256 hasher;
    hasher.update("abc", 3);
    std::uint8_t first[kSha256DigestSize];
    hasher.finalize(first);

    hasher.reset();
    hasher.update("abc", 3);
    std::uint8_t second[kSha256DigestSize];
    hasher.finalize(second);
    NC_CHECK(std::memcmp(first, second, kSha256DigestSize) == 0);
}

NC_TEST(Crypto, Sha256HexRoundTrip) {
    std::uint8_t digest[kSha256DigestSize];
    const std::string hex = sha256Hex("roundtrip");
    NC_CHECK(sha256FromHex(hex.c_str(), digest));
    NC_CHECK_EQ(bytesToHex(digest, kSha256DigestSize), hex);
}

NC_TEST(Crypto, Sha256FromHexRejectsInvalid) {
    std::uint8_t digest[kSha256DigestSize];
    NC_CHECK(!sha256FromHex("abcd", digest));   // 长度不足
    NC_CHECK(!sha256FromHex(nullptr, digest));  // 空指针
    std::string bad(64, 'z');                   // 非法字符
    NC_CHECK(!sha256FromHex(bad.c_str(), digest));
}

NC_TEST(Crypto, ConstantTimeEquals) {
    const std::uint8_t a[4] = {1, 2, 3, 4};
    const std::uint8_t b[4] = {1, 2, 3, 4};
    const std::uint8_t c[4] = {1, 2, 3, 5};
    NC_CHECK(constantTimeEquals(a, b, 4));
    NC_CHECK(!constantTimeEquals(a, c, 4));
    NC_CHECK(!constantTimeEquals(nullptr, b, 4));
    NC_CHECK(constantTimeEquals(nullptr, nullptr, 4));
}

// =============================================================================
// AES-256（FIPS 197 附录 C.3 官方向量）
// =============================================================================

NC_TEST(Crypto, Aes256EncryptBlockFipsVector) {
    // FIPS 197 附录 C.3：AES-256 单分组加密官方测试向量。
    const auto key = bytesFromHex("000102030405060708090a0b0c0d0e0f"
                                  "101112131415161718191a1b1c1d1e1f");
    const auto plain = bytesFromHex("00112233445566778899aabbccddeeff");
    const auto expected = bytesFromHex("8ea2b7ca516745bfeafc49904b496089");
    NC_CHECK_EQ(key.size(), std::size_t(32));
    NC_CHECK_EQ(plain.size(), std::size_t(16));

    std::uint8_t out[kAesBlockSize];
    aes256EncryptBlock(key.data(), plain.data(), out);
    NC_CHECK_EQ(bytesToHex(out, kAesBlockSize), toHex(expected));
}

NC_TEST(Crypto, Aes256KnownAnswerFromSpec) {
    // 公开已知答案：AES-256 全零密钥/全零明文。
    std::uint8_t key[kAes256KeySize] = {};
    std::uint8_t plain[kAesBlockSize] = {};
    std::uint8_t out[kAesBlockSize];
    aes256EncryptBlock(key, plain, out);
    NC_CHECK_EQ(bytesToHex(out, kAesBlockSize),
                std::string("dc95c078a2408989ad48a21492842087"));
}

// =============================================================================
// AES-256-CTR（NIST SP 800-38A F.5.5 官方向量）
// =============================================================================

NC_TEST(Crypto, Aes256CtrNistSp800_38a) {
    // NIST SP 800-38A F.5.5（CTR-AES256.Encrypt）。
    const auto key = bytesFromHex("603deb1015ca71be2b73aef0857d7781"
                                  "1f352c073b6108d72d9810a30914dff4");
    const auto counter = bytesFromHex("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff");
    const auto plain = bytesFromHex("6bc1bee22e409f96e93d7e117393172a"
                                    "ae2d8a571e03ac9c9eb76fac45af8e51"
                                    "30c81c46a35ce411e5fbc1191a0a52ef"
                                    "f69f2445df4f9b17ad2b417be66c3710");
    const auto expected = bytesFromHex("601ec313775789a5b7a7f504bbf3d228"
                                       "f443e3ca4d62b59aca84e990cacaf5c5"
                                       "2b0930daa23de94ce87017ba2d84988d"
                                       "dfc9c58db67aada613c2dd08457941a6");

    const auto cipher = aes256CtrCrypt(key.data(), counter.data(), plain);
    NC_CHECK_EQ(cipher.size(), plain.size());
    NC_CHECK_EQ(toHex(cipher), toHex(expected));

    // 解密还原。
    const auto round = aes256CtrCrypt(key.data(), counter.data(), cipher);
    NC_CHECK_EQ(toHex(round), toHex(plain));
}

NC_TEST(Crypto, Aes256CtrStreamingAcrossBlocks) {
    // 跨块分片必须与一次性处理结果一致（同一密钥流位置）。
    const auto key = bytesFromHex("603deb1015ca71be2b73aef0857d7781"
                                  "1f352c073b6108d72d9810a30914dff4");
    const auto counter = bytesFromHex("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff");
    std::vector<std::uint8_t> data(100);
    for (std::size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<std::uint8_t>(i * 7 + 3);
    }

    const auto oneShot = aes256CtrCrypt(key.data(), counter.data(), data);

    Aes256Ctr cipher(key.data(), counter.data());
    std::vector<std::uint8_t> streamed = data;
    cipher.process(streamed.data(), 7);  // 非块对齐分片
    cipher.process(streamed.data() + 7, 41);
    cipher.process(streamed.data() + 48, 52);
    NC_CHECK_EQ(streamed.size(), oneShot.size());
    NC_CHECK(std::memcmp(streamed.data(), oneShot.data(), streamed.size()) == 0);
}

NC_TEST(Crypto, Aes256CtrAtOffsetMatchesPrefix) {
    // 从偏移解密必须与「整体解密后切片」一致（断点续传的正确性基础）。
    const auto key = bytesFromHex("603deb1015ca71be2b73aef0857d7781"
                                  "1f352c073b6108d72d9810a30914dff4");
    const auto counter = bytesFromHex("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff");
    std::vector<std::uint8_t> data(256);
    for (std::size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<std::uint8_t>((i * 13) & 0xff);
    }
    const auto full = aes256CtrCrypt(key.data(), counter.data(), data);

    // 覆盖块对齐与非块对齐两种偏移。
    const std::uint64_t offsets[] = {16, 17, 31, 32, 64, 100, 255};
    for (std::uint64_t offset : offsets) {
        std::vector<std::uint8_t> tail(full.begin() + offset, full.end());
        const auto partial = aes256CtrCryptAtOffset(key.data(), counter.data(), offset, tail);
        NC_CHECK_EQ(partial.size(), tail.size());
        bool matched = true;
        for (std::size_t i = 0; i < partial.size(); ++i) {
            if (partial[i] != data[static_cast<std::size_t>(offset) + i]) {
                matched = false;
                break;
            }
        }
        NC_CHECK(matched);
    }
}

NC_TEST(Crypto, AesCtrCounterAdvancesByOffset) {
    std::uint8_t counter[kAesCtrCounterSize] = {};
    std::uint8_t out[kAesCtrCounterSize];

    aesCtrCounterAtOffset(counter, 0, out);
    NC_CHECK_EQ(static_cast<int>(out[15]), 0);

    aesCtrCounterAtOffset(counter, 16, out);  // 一块 = +1
    NC_CHECK_EQ(static_cast<int>(out[15]), 1);

    aesCtrCounterAtOffset(counter, 15, out);  // 未满一块 = +0
    NC_CHECK_EQ(static_cast<int>(out[15]), 0);

    // 进位：计数器低字节 0xff + 1 块。
    std::uint8_t counter2[kAesCtrCounterSize] = {};
    counter2[15] = 0xff;
    aesCtrCounterAtOffset(counter2, 16, out);
    NC_CHECK_EQ(static_cast<int>(out[15]), 0x00);
    NC_CHECK_EQ(static_cast<int>(out[14]), 0x01);

    NC_CHECK_EQ(aesCtrIntraBlockOffset(0), std::size_t(0));
    NC_CHECK_EQ(aesCtrIntraBlockOffset(16), std::size_t(0));
    NC_CHECK_EQ(aesCtrIntraBlockOffset(17), std::size_t(1));
    NC_CHECK_EQ(aesCtrIntraBlockOffset(31), std::size_t(15));
}

NC_TEST(Crypto, Aes256CtrRejectsNullKey) {
    const auto data = bytesFromHex("00112233");
    const auto out = aes256CtrCrypt(nullptr, nullptr, data);
    NC_CHECK(out.empty());

    Aes256Ctr cipher(nullptr, nullptr);
    NC_CHECK(!cipher.valid());
}

NC_TEST(Crypto, Aes256CtrResetRepeatsKeystream) {
    const auto key = bytesFromHex("603deb1015ca71be2b73aef0857d7781"
                                  "1f352c073b6108d72d9810a30914dff4");
    const auto counter = bytesFromHex("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff");
    Aes256Ctr cipher(key.data(), counter.data());
    std::vector<std::uint8_t> a(40, 0xAA);
    cipher.process(a.data(), a.size());
    cipher.reset();
    std::vector<std::uint8_t> b(40, 0xAA);
    cipher.process(b.data(), b.size());
    NC_CHECK(std::memcmp(a.data(), b.data(), a.size()) == 0);
}

// =============================================================================
// 随机源注入（fail-closed）
// =============================================================================

namespace {

bool deterministicRandom(std::uint8_t* out, std::size_t length) {
    for (std::size_t i = 0; i < length; ++i) {
        out[i] = static_cast<std::uint8_t>(0x5a + i);
    }
    return true;
}

}  // namespace

NC_TEST(Crypto, SecureRandomFailsClosedWithoutProvider) {
    setSecureRandomProvider(nullptr);
    std::uint8_t buffer[16] = {};
    NC_CHECK(!secureRandomBytes(buffer, sizeof(buffer)));
    NC_CHECK(secureRandomHex(8).empty());
}

NC_TEST(Crypto, SecureRandomUsesInjectedProvider) {
    setSecureRandomProvider(&deterministicRandom);
    std::uint8_t buffer[4] = {};
    NC_CHECK(secureRandomBytes(buffer, sizeof(buffer)));
    NC_CHECK_EQ(static_cast<int>(buffer[0]), 0x5a);
    NC_CHECK_EQ(static_cast<int>(buffer[3]), 0x5d);
    NC_CHECK_EQ(secureRandomHex(2), std::string("5a5b"));
    setSecureRandomProvider(nullptr);  // 恢复，避免影响其他用例
}
