#include "test_framework.h"
#include "nairoclip/core/model_manager.h"

#include <cstring>
#include <string>
#include <vector>

using namespace nairoclip::core;

namespace {

Entitlement proEntitlement(std::int64_t expiresAt = 0) {
    Entitlement e;
    e.tier = MembershipTier::Pro;
    e.expiresAtUnix = expiresAt;
    return e;
}

std::vector<std::uint8_t> makePayload(std::size_t size, std::uint8_t seed = 7) {
    std::vector<std::uint8_t> data(size);
    for (std::size_t i = 0; i < size; ++i) {
        data[i] = static_cast<std::uint8_t>((i * 31 + seed) & 0xff);
    }
    return data;
}

std::uint8_t testKey[kAes256KeySize] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f};

}  // namespace

// =============================================================================
// 模型目录
// =============================================================================

NC_TEST(ModelManager, CatalogHasExpectedEntries) {
    const auto& catalog = builtinModelCatalog();
    // 文档第 5 部分列举：抠像（高精度）、Whisper、光流补帧；第 7 部分补充超分/TTS/人声分离/翻译。
    NC_CHECK(catalog.size() >= 8);
    NC_CHECK(findBuiltinModel("matting_hq") != nullptr);
    NC_CHECK(findBuiltinModel("whisper_zh") != nullptr);
    NC_CHECK(findBuiltinModel("flow_interp") != nullptr);
    NC_CHECK(findBuiltinModel("super_res_2x4x") != nullptr);
    NC_CHECK(findBuiltinModel("tts_zh") != nullptr);
    NC_CHECK(findBuiltinModel("stem_sep") != nullptr);
    NC_CHECK(findBuiltinModel("translate_zh_en") != nullptr);
    NC_CHECK(findBuiltinModel("not_exist") == nullptr);
}

NC_TEST(ModelManager, CatalogEntriesAreValid) {
    for (const auto& info : builtinModelCatalog()) {
        NC_CHECK(info.valid());
        NC_CHECK(!info.name.empty());
        NC_CHECK(info.sizeBytes > 0);
    }
}

NC_TEST(ModelManager, CatalogSizesMatchDocument) {
    // 文档第 5 部分展示：高精度抠像 120MB、Whisper 240MB、光流补帧 85MB。
    const ModelInfo* matting = findBuiltinModel("matting_hq");
    NC_CHECK(matting != nullptr);
    if (matting != nullptr) {
        NC_CHECK_EQ(matting->sizeBytes, std::int64_t(120) * 1024 * 1024);
    }
    const ModelInfo* whisper = findBuiltinModel("whisper_zh");
    NC_CHECK(whisper != nullptr);
    if (whisper != nullptr) {
        NC_CHECK_EQ(whisper->sizeBytes, std::int64_t(240) * 1024 * 1024);
    }
    const ModelInfo* flow = findBuiltinModel("flow_interp");
    NC_CHECK(flow != nullptr);
    if (flow != nullptr) {
        NC_CHECK_EQ(flow->sizeBytes, std::int64_t(85) * 1024 * 1024);
    }
}

NC_TEST(ModelManager, CapabilityNameRoundTrip) {
    const ModelCapability all[] = {
        ModelCapability::None,
        ModelCapability::MattingBasic,
        ModelCapability::MattingHighPrecision,
        ModelCapability::SpeechToText,
        ModelCapability::FrameInterpolation,
        ModelCapability::SuperResolution,
        ModelCapability::Tts,
        ModelCapability::StemSeparation,
        ModelCapability::Translation};
    for (ModelCapability capability : all) {
        ModelCapability parsed = ModelCapability::None;
        NC_CHECK(modelCapabilityFromString(modelCapabilityName(capability), parsed));
        NC_CHECK(parsed == capability);
    }
    ModelCapability parsed = ModelCapability::None;
    NC_CHECK(!modelCapabilityFromString("bogus", parsed));
}

// =============================================================================
// 权益门控（与 Entitlement 单一事实来源一致）
// =============================================================================

NC_TEST(ModelManager, HighPrecisionMattingRequiresPro) {
    const ModelInfo* info = findBuiltinModel("matting_hq");
    NC_CHECK(info != nullptr);
    if (info == nullptr) return;
    const std::int64_t now = 1000;
    NC_CHECK(!modelAccessAllowed(*info, Entitlement{}, now));            // 免费
    NC_CHECK(modelAccessAllowed(*info, proEntitlement(), now));          // Pro
    NC_CHECK(!modelAccessAllowed(*info, proEntitlement(500), now));      // 已过期
}

NC_TEST(ModelManager, FrameInterpolationRequiresPro) {
    const ModelInfo* info = findBuiltinModel("flow_interp");
    NC_CHECK(info != nullptr);
    if (info == nullptr) return;
    const std::int64_t now = 1000;
    NC_CHECK(!modelAccessAllowed(*info, Entitlement{}, now));
    NC_CHECK(modelAccessAllowed(*info, proEntitlement(), now));
}

NC_TEST(ModelManager, FreeCapabilitiesAllowedLocally) {
    // 免费能力本地放行（配额由服务端计数裁决）。
    const char* freeIds[] = {"matting_basic", "whisper_zh", "super_res_2x4x",
                             "tts_zh", "stem_sep", "translate_zh_en"};
    for (const char* id : freeIds) {
        const ModelInfo* info = findBuiltinModel(id);
        NC_CHECK(info != nullptr);
        if (info != nullptr) {
            NC_CHECK(modelAccessAllowed(*info, Entitlement{}, 1000));
        }
    }
}

// =============================================================================
// 下载状态机
// =============================================================================

NC_TEST(ModelManager, PhaseTransitionsValid) {
    // 合法边
    NC_CHECK(isDownloadTransitionValid(DownloadPhase::Idle, DownloadPhase::Downloading));
    NC_CHECK(isDownloadTransitionValid(DownloadPhase::Downloading, DownloadPhase::Paused));
    NC_CHECK(isDownloadTransitionValid(DownloadPhase::Paused, DownloadPhase::Downloading));
    NC_CHECK(isDownloadTransitionValid(DownloadPhase::Downloading, DownloadPhase::Verifying));
    NC_CHECK(isDownloadTransitionValid(DownloadPhase::Verifying, DownloadPhase::Completed));
    NC_CHECK(isDownloadTransitionValid(DownloadPhase::Downloading, DownloadPhase::Failed));
    NC_CHECK(isDownloadTransitionValid(DownloadPhase::Failed, DownloadPhase::Downloading));
    NC_CHECK(isDownloadTransitionValid(DownloadPhase::Downloading, DownloadPhase::Cancelled));
    NC_CHECK(isDownloadTransitionValid(DownloadPhase::Cancelled, DownloadPhase::Downloading));
    // 非法边（不得跳过校验直接完成；不得从未开始直接暂停）
    NC_CHECK(!isDownloadTransitionValid(DownloadPhase::Idle, DownloadPhase::Paused));
    NC_CHECK(!isDownloadTransitionValid(DownloadPhase::Idle, DownloadPhase::Verifying));
    NC_CHECK(!isDownloadTransitionValid(DownloadPhase::Idle, DownloadPhase::Completed));
    NC_CHECK(!isDownloadTransitionValid(DownloadPhase::Paused, DownloadPhase::Completed));
    NC_CHECK(!isDownloadTransitionValid(DownloadPhase::Verifying, DownloadPhase::Paused));
}

NC_TEST(ModelManager, PhaseNameRoundTrip) {
    const DownloadPhase all[] = {DownloadPhase::Idle,      DownloadPhase::Downloading,
                                 DownloadPhase::Paused,    DownloadPhase::Verifying,
                                 DownloadPhase::Completed, DownloadPhase::Failed,
                                 DownloadPhase::Cancelled};
    for (DownloadPhase phase : all) {
        DownloadPhase parsed = DownloadPhase::Idle;
        NC_CHECK(downloadPhaseFromString(downloadPhaseName(phase), parsed));
        NC_CHECK(parsed == phase);
    }
}

NC_TEST(ModelManager, StatusNameRoundTrip) {
    const ModelStatus all[] = {
        ModelStatus::NotDownloaded, ModelStatus::Downloading,      ModelStatus::Installed,
        ModelStatus::Corrupt,       ModelStatus::UpdateAvailable,  ModelStatus::Verifying};
    for (ModelStatus status : all) {
        ModelStatus parsed = ModelStatus::NotDownloaded;
        NC_CHECK(modelStatusFromString(modelStatusName(status), parsed));
        NC_CHECK(parsed == status);
    }
}

NC_TEST(ModelManager, BeginRejectsInvalidArguments) {
    ModelDownloadManager manager;
    NC_CHECK(!manager.begin("", 100, 1).isOk());          // 空 ID
    NC_CHECK(!manager.begin("m1", 0, 1).isOk());          // 大小未知
    NC_CHECK(!manager.begin("m1", -5, 1).isOk());         // 负数
    NC_CHECK(manager.find("m1") == nullptr);              // 失败无副作用
    NC_CHECK(manager.activeCount() == 0);
}

NC_TEST(ModelManager, ProgressMonotonicAndClamped) {
    ModelDownloadManager manager;
    NC_CHECK(manager.begin("m1", 1000, 100).isOk());
    NC_CHECK(manager.markProgress("m1", 400, 101).isOk());
    const DownloadState* state = manager.find("m1");
    NC_CHECK(state != nullptr);
    if (state != nullptr) {
        NC_CHECK_EQ(state->downloadedBytes, std::int64_t(400));
        NC_CHECK_EQ(state->remainingBytes(), std::int64_t(600));
        // 进度回退被拒绝（断点已落盘，回退会破坏续传语义）
        NC_CHECK(!manager.markProgress("m1", 300, 102).isOk());
        NC_CHECK_EQ(manager.find("m1")->downloadedBytes, std::int64_t(400));
        // 超出总量被钳制
        NC_CHECK(manager.markProgress("m1", 9999, 103).isOk());
        NC_CHECK_EQ(manager.find("m1")->downloadedBytes, std::int64_t(1000));
        NC_CHECK(manager.find("m1")->progress() > 0.99);
    }
}

NC_TEST(ModelManager, ProgressRejectedOutsideDownloading) {
    ModelDownloadManager manager;
    NC_CHECK(manager.begin("m1", 1000, 100).isOk());
    NC_CHECK(manager.pause("m1", 101).isOk());
    NC_CHECK(!manager.markProgress("m1", 500, 102).isOk());  // 暂停中不接收进度
    NC_CHECK(manager.resume("m1", 103).isOk());
    NC_CHECK(manager.markProgress("m1", 500, 104).isOk());
}

NC_TEST(ModelManager, PauseResumeKeepsBreakpoint) {
    ModelDownloadManager manager;
    NC_CHECK(manager.begin("m1", 2000, 100).isOk());
    NC_CHECK(manager.markProgress("m1", 750, 101).isOk());
    NC_CHECK(manager.pause("m1", 102).isOk());
    NC_CHECK(manager.resume("m1", 103).isOk());
    const DownloadState* state = manager.find("m1");
    NC_CHECK(state != nullptr);
    if (state != nullptr) {
        NC_CHECK(state->phase == DownloadPhase::Downloading);
        NC_CHECK_EQ(state->downloadedBytes, std::int64_t(750));  // 断点保留
        NC_CHECK(state->canResume() == false);                   // 下载中不算「可恢复」
    }
}

NC_TEST(ModelManager, CancelKeepsResumeAbility) {
    // 文档 3.4：下载中断弹窗「[继续下载] [取消]」——取消后仍可继续。
    ModelDownloadManager manager;
    NC_CHECK(manager.begin("m1", 1000, 100).isOk());
    NC_CHECK(manager.markProgress("m1", 450, 101).isOk());
    NC_CHECK(manager.cancel("m1", 102).isOk());
    const DownloadState* state = manager.find("m1");
    NC_CHECK(state != nullptr);
    if (state != nullptr) {
        NC_CHECK(state->phase == DownloadPhase::Cancelled);
        NC_CHECK(state->downloadedBytes == 450);
        NC_CHECK(state->canResume());  // 有断点且未完成
        NC_CHECK(std::abs(state->progress() - 0.45) < 1e-9);
    }
    NC_CHECK(manager.resume("m1", 103).isOk());
}

NC_TEST(ModelManager, FullLifecycleToCompleted) {
    ModelDownloadManager manager;
    NC_CHECK(manager.begin("whisper_zh", 1000, 100).isOk());
    NC_CHECK(manager.markProgress("whisper_zh", 1000, 101).isOk());
    NC_CHECK(manager.finishDownload("whisper_zh", 102).isOk());
    const DownloadState* verifying = manager.find("whisper_zh");
    NC_CHECK(verifying != nullptr);
    if (verifying != nullptr) {
        NC_CHECK(verifying->phase == DownloadPhase::Verifying);
        NC_CHECK_EQ(manager.activeCount(), std::size_t(1));
    }
    NC_CHECK(manager.complete("whisper_zh", 103).isOk());
    const DownloadState* done = manager.find("whisper_zh");
    NC_CHECK(done != nullptr);
    if (done != nullptr) {
        NC_CHECK(done->phase == DownloadPhase::Completed);
        NC_CHECK_EQ(done->downloadedBytes, done->totalBytes);
        NC_CHECK_EQ(manager.activeCount(), std::size_t(0));
    }
}

NC_TEST(ModelManager, FinishWithoutDownloadingRejected) {
    ModelDownloadManager manager;
    NC_CHECK(manager.begin("m1", 1000, 100).isOk());
    NC_CHECK(manager.pause("m1", 101).isOk());
    // 暂停中不能直接进入校验（必须先恢复下载）
    NC_CHECK(!manager.finishDownload("m1", 102).isOk());
    NC_CHECK(manager.find("m1")->phase == DownloadPhase::Paused);
}

NC_TEST(ModelManager, FailRecordsReasonAndAllowsRetry) {
    ModelDownloadManager manager;
    NC_CHECK(manager.begin("m1", 1000, 100).isOk());
    NC_CHECK(manager.markProgress("m1", 200, 101).isOk());
    NC_CHECK(manager.fail("m1", "网络连接丢失", 102).isOk());
    const DownloadState* state = manager.find("m1");
    NC_CHECK(state != nullptr);
    if (state != nullptr) {
        NC_CHECK(state->phase == DownloadPhase::Failed);
        NC_CHECK_EQ(state->message, std::string("网络连接丢失"));
        NC_CHECK(state->canResume());  // 失败保留断点，可重试
    }
    NC_CHECK(manager.resume("m1", 103).isOk());
}

NC_TEST(ModelManager, MissingTaskOperationsFail) {
    ModelDownloadManager manager;
    NC_CHECK(!manager.pause("ghost", 1).isOk());
    NC_CHECK(!manager.resume("ghost", 1).isOk());
    NC_CHECK(!manager.cancel("ghost", 1).isOk());
    NC_CHECK(!manager.complete("ghost", 1).isOk());
    NC_CHECK(!manager.fail("ghost", "x", 1).isOk());
    NC_CHECK(!manager.markProgress("ghost", 1, 1).isOk());
    NC_CHECK(!manager.finishDownload("ghost", 1).isOk());
}

NC_TEST(ModelManager, RemoveAndClear) {
    ModelDownloadManager manager;
    NC_CHECK(manager.begin("m1", 100, 1).isOk());
    NC_CHECK(manager.begin("m2", 100, 1).isOk());
    NC_CHECK(manager.remove("m1"));
    NC_CHECK(!manager.remove("m1"));
    NC_CHECK(manager.find("m1") == nullptr);
    NC_CHECK_EQ(manager.activeCount(), std::size_t(1));
    manager.clear();
    NC_CHECK_EQ(manager.activeCount(), std::size_t(0));
}

// =============================================================================
// 断点续传坐标（真实 AES-CTR 偏移）
// =============================================================================

NC_TEST(ModelManager, DeriveCounterDeterministic) {
    std::uint8_t a[kAesCtrCounterSize];
    std::uint8_t b[kAesCtrCounterSize];
    deriveModelCounter("whisper_zh", "2.0.0", a);
    deriveModelCounter("whisper_zh", "2.0.0", b);
    NC_CHECK(std::memcmp(a, b, kAesCtrCounterSize) == 0);

    std::uint8_t c[kAesCtrCounterSize];
    deriveModelCounter("matting_hq", "2.0.0", c);
    NC_CHECK(std::memcmp(a, c, kAesCtrCounterSize) != 0);  // 不同模型不同计数器

    std::uint8_t d[kAesCtrCounterSize];
    deriveModelCounter("whisper_zh", "2.0.1", d);
    NC_CHECK(std::memcmp(a, d, kAesCtrCounterSize) != 0);  // 不同版本不同计数器
}

NC_TEST(ModelManager, ResumePlanCoordinates) {
    const DownloadResumePlan plan = planDownloadResume("whisper_zh", "2.0.0", 33);
    NC_CHECK(plan.valid);
    NC_CHECK_EQ(plan.offsetBytes, std::int64_t(33));
    NC_CHECK_EQ(plan.intraBlockSkip, std::size_t(1));  // 33 % 16

    std::uint8_t base[kAesCtrCounterSize];
    deriveModelCounter("whisper_zh", "2.0.0", base);
    std::uint8_t expected[kAesCtrCounterSize];
    aesCtrCounterAtOffset(base, 33, expected);
    NC_CHECK(std::memcmp(plan.counter, expected, kAesCtrCounterSize) == 0);

    const DownloadResumePlan invalid = planDownloadResume("", "2.0.0", 10);
    NC_CHECK(!invalid.valid);
}

NC_TEST(ModelManager, ResumeDecryptMatchesFullDecrypt) {
    // 断点续传的核心正确性：从断点解出的尾部必须与整体解密一致。
    const std::vector<std::uint8_t> plaintext = makePayload(4096);
    const std::string version = "1.0.0";
    const auto ciphertext =
        encryptModelPayload(testKey, "flow_interp", version, plaintext);
    NC_CHECK_EQ(ciphertext.size(), plaintext.size());

    const auto full = decryptModelPayload(testKey, "flow_interp", version, ciphertext);
    NC_CHECK_EQ(full.size(), plaintext.size());
    NC_CHECK(std::memcmp(full.data(), plaintext.data(), plaintext.size()) == 0);

    // 模拟：已下载 1000 字节（非块对齐），续传仅拿尾部。
    const std::uint64_t offset = 1000;
    std::vector<std::uint8_t> tail(ciphertext.begin() + offset, ciphertext.end());
    const auto decryptedTail =
        decryptModelPayloadAt(testKey, "flow_interp", version, offset, tail);
    NC_CHECK_EQ(decryptedTail.size(), tail.size());
    bool matched = true;
    for (std::size_t i = 0; i < decryptedTail.size(); ++i) {
        if (decryptedTail[i] != plaintext[static_cast<std::size_t>(offset) + i]) {
            matched = false;
            break;
        }
    }
    NC_CHECK(matched);
}

NC_TEST(ModelManager, EncryptDecryptRoundTripMultipleModels) {
    for (const auto& info : builtinModelCatalog()) {
        const std::vector<std::uint8_t> plaintext = makePayload(300, 3);
        const auto cipher = encryptModelPayload(testKey, info.id, info.version, plaintext);
        NC_CHECK_EQ(cipher.size(), plaintext.size());
        NC_CHECK(std::memcmp(cipher.data(), plaintext.data(), plaintext.size()) != 0);
        const auto round = decryptModelPayload(testKey, info.id, info.version, cipher);
        NC_CHECK(std::memcmp(round.data(), plaintext.data(), plaintext.size()) == 0);
    }
}

NC_TEST(ModelManager, EncryptRejectsInvalidArguments) {
    const std::vector<std::uint8_t> data(32, 1);
    NC_CHECK(encryptModelPayload(nullptr, "m", "1", data).empty());
    NC_CHECK(encryptModelPayload(testKey, "", "1", data).empty());
    NC_CHECK(encryptModelPayload(testKey, "m", "", data).empty());
}

// =============================================================================
// 完整性校验
// =============================================================================

NC_TEST(ModelManager, VerifyAcceptsValidPayload) {
    ModelInfo info;
    info.id = "m1";
    info.version = "1.0.0";
    info.sizeBytes = 512;
    const std::vector<std::uint8_t> payload = makePayload(512);
    const std::string digest = sha256Hex(payload.data(), payload.size());

    const ModelVerifyResult result = verifyModelPayload(info, payload, digest);
    NC_CHECK(result.ok);
    NC_CHECK(result.digestMatch);
    NC_CHECK(result.sizeMatch);
    NC_CHECK_EQ(result.sizeBytes, std::int64_t(512));
}

NC_TEST(ModelManager, VerifyRejectsTamperedPayload) {
    ModelInfo info;
    info.id = "m1";
    info.version = "1.0.0";
    info.sizeBytes = 512;
    std::vector<std::uint8_t> payload = makePayload(512);
    const std::string digest = sha256Hex(payload.data(), payload.size());

    payload[100] ^= 0x01;  // 篡改一个字节
    const ModelVerifyResult result = verifyModelPayload(info, payload, digest);
    NC_CHECK(!result.ok);
    NC_CHECK(!result.digestMatch);
    NC_CHECK(result.message.find("损坏") != std::string::npos);
}

NC_TEST(ModelManager, VerifyRejectsWrongSize) {
    ModelInfo info;
    info.id = "m1";
    info.version = "1.0.0";
    info.sizeBytes = 1024;
    const std::vector<std::uint8_t> payload = makePayload(512);
    const std::string digest = sha256Hex(payload.data(), payload.size());

    const ModelVerifyResult result = verifyModelPayload(info, payload, digest);
    NC_CHECK(!result.ok);
    NC_CHECK(result.digestMatch);  // 摘要对但大小不符
    NC_CHECK(!result.sizeMatch);
    NC_CHECK(result.message.find("大小") != std::string::npos);
}

NC_TEST(ModelManager, VerifyFailsClosedWithoutExpectedDigest) {
    // 缺少校验值时必须拒绝（不得默认放行被篡改文件）。
    ModelInfo info;
    info.id = "m1";
    info.version = "1.0.0";
    info.sizeBytes = 0;
    const std::vector<std::uint8_t> payload = makePayload(64);

    const ModelVerifyResult empty = verifyModelPayload(info, payload, "");
    NC_CHECK(!empty.ok);
    NC_CHECK(!empty.digestMatch);

    const ModelVerifyResult bogus = verifyModelPayload(info, payload, "zzzz");
    NC_CHECK(!bogus.ok);
    NC_CHECK(bogus.message.find("校验值") != std::string::npos);
}

// =============================================================================
// 模型记录表 / 存储统计
// =============================================================================

NC_TEST(ModelManager, StoreUpsertAndRemove) {
    ModelStore store;
    ModelRecord record;
    record.modelId = "whisper_zh";
    record.version = "2.0.0";
    record.path = "/data/models/whisper_zh.bin";
    record.sizeBytes = 1000;
    record.sha256 = std::string(64, 'a');
    store.upsert(record);
    NC_CHECK_EQ(store.installedCount(), 1);
    NC_CHECK_EQ(store.totalSizeBytes(), std::int64_t(1000));

    // 同 ID 覆盖
    record.sizeBytes = 2000;
    store.upsert(record);
    NC_CHECK_EQ(store.records.size(), std::size_t(1));
    NC_CHECK_EQ(store.totalSizeBytes(), std::int64_t(2000));

    NC_CHECK(store.remove("whisper_zh"));
    NC_CHECK(!store.remove("whisper_zh"));
    NC_CHECK_EQ(store.installedCount(), 0);
}

NC_TEST(ModelManager, StoreStatusOfIncludesVersionUpdate) {
    ModelStore store;
    ModelRecord record;
    record.modelId = "whisper_zh";
    record.version = "1.0.0";  // 低于清单版本 2.0.0
    record.sizeBytes = 100;
    record.status = ModelStatus::Installed;
    store.upsert(record);

    NC_CHECK(store.statusOf("whisper_zh") == ModelStatus::UpdateAvailable);
    NC_CHECK(store.statusOf("ghost") == ModelStatus::NotDownloaded);

    // 版本一致 → Installed
    store.find("whisper_zh")->version = "2.0.0";
    NC_CHECK(store.statusOf("whisper_zh") == ModelStatus::Installed);
}

NC_TEST(ModelManager, StoreCorruptNotCountedInBytes) {
    ModelStore store;
    ModelRecord installed;
    installed.modelId = "a";
    installed.sizeBytes = 500;
    installed.status = ModelStatus::Installed;
    store.upsert(installed);
    ModelRecord corrupt;
    corrupt.modelId = "b";
    corrupt.sizeBytes = 700;
    corrupt.status = ModelStatus::Corrupt;
    store.upsert(corrupt);

    NC_CHECK_EQ(store.installedCount(), 1);
    NC_CHECK_EQ(store.corruptCount(), 1);
    NC_CHECK_EQ(store.totalSizeBytes(), std::int64_t(500));  // 损坏项不计占用
}

NC_TEST(ModelManager, StorageStatsRoomCheck) {
    ModelStore store;
    ModelRecord record;
    record.modelId = "sup";
    record.sizeBytes = 150 * 1024 * 1024;
    store.upsert(record);

    const ModelStorageStats stats =
        computeModelStorage(store, 128LL * 1024 * 1024 * 1024, 45LL * 1024 * 1024 * 1024);
    NC_CHECK_EQ(stats.modelBytes, std::int64_t(150) * 1024 * 1024);
    NC_CHECK_EQ(stats.installedCount, 1);
    NC_CHECK(stats.usageText().find("45.0 GB") != std::string::npos);
    NC_CHECK(stats.usageText().find("128.0 GB") != std::string::npos);
    NC_CHECK(stats.modelText().find("150 MB") != std::string::npos);

    // 文档 3.3：可用空间 < 预估大小 × 1.2 时拦截
    NC_CHECK(stats.hasRoomFor(30LL * 1024 * 1024 * 1024));    // 30×1.2=36 < 45
    NC_CHECK(!stats.hasRoomFor(40LL * 1024 * 1024 * 1024));   // 40×1.2=48 > 45
    NC_CHECK(stats.hasRoomFor(0));
}

NC_TEST(ModelManager, FormatBytesTextUnits) {
    NC_CHECK_EQ(formatBytesText(0), std::string("0 B"));
    NC_CHECK_EQ(formatBytesText(512), std::string("512 B"));
    NC_CHECK_EQ(formatBytesText(2048), std::string("2 KB"));
    NC_CHECK_EQ(formatBytesText(150 * 1024 * 1024), std::string("150 MB"));
    NC_CHECK_EQ(formatBytesText(3LL * 1024 * 1024 * 1024), std::string("3.0 GB"));
    NC_CHECK_EQ(formatBytesText(-5), std::string("0 B"));
}

// =============================================================================
// 签名 URL 校验（文档 8.2）
// =============================================================================

NC_TEST(ModelManager, SignedUrlValidatesExpiry) {
    SignedUrl url;
    url.url = "https://cdn.example.com/models/whisper_zh.bin?sig=abc";
    url.expiresAtUnix = 2000;

    std::string reason;
    NC_CHECK(url.validAt(1000, &reason));
    NC_CHECK(reason.empty());
    NC_CHECK_EQ(signedUrlRemainingSeconds(url, 1000), std::int64_t(1000));

    // 到期瞬间即失效
    NC_CHECK(!url.validAt(2000, &reason));
    NC_CHECK(reason.find("过期") != std::string::npos);
    NC_CHECK(!url.validAt(3000));

    // 设备时间被手动调早 → 仍按传入的服务端时间判定（调用方传服务端时间）
    NC_CHECK(url.validAt(1500));
}

NC_TEST(ModelManager, SignedUrlFailClosed) {
    SignedUrl url;
    std::string reason;

    // 空地址
    url.url.clear();
    url.expiresAtUnix = 5000;
    NC_CHECK(!url.validAt(1, &reason));
    NC_CHECK(reason.find("为空") != std::string::npos);

    // 非法协议
    url.url = "ftp://example.com/model.bin";
    NC_CHECK(!url.validAt(1, &reason));
    NC_CHECK(reason.find("协议") != std::string::npos);

    // 无有效期 → 拒绝（fail-closed）
    url.url = "https://cdn.example.com/model.bin";
    url.expiresAtUnix = 0;
    NC_CHECK(!url.validAt(1, &reason));
    NC_CHECK(reason.find("有效期") != std::string::npos);
    NC_CHECK_EQ(signedUrlRemainingSeconds(url, 1), std::int64_t(0));
}

// =============================================================================
// 持久化
// =============================================================================

NC_TEST(ModelManager, StoreJsonRoundTrip) {
    ModelStore store;
    ModelRecord a;
    a.modelId = "matting_hq";
    a.version = "1.1.0";
    a.path = "/data/models/matting_hq.bin";
    a.sizeBytes = 120 * 1024 * 1024;
    a.sha256 = std::string(64, 'b');
    a.installedAtUnix = 1700000000;
    a.status = ModelStatus::Installed;
    store.upsert(a);
    ModelRecord b;
    b.modelId = "flow_interp";
    b.version = "1.0.0";
    b.path = "/data/models/flow_interp.bin";
    b.sizeBytes = 85 * 1024 * 1024;
    b.sha256 = std::string(64, 'c');
    b.status = ModelStatus::Corrupt;
    b.note = "摘要校验失败";
    store.upsert(b);

    const std::string json = saveModelStoreToJson(store);
    NC_CHECK(!json.empty());

    ModelStore loaded;
    std::string error;
    NC_CHECK(loadModelStoreFromJson(json, loaded, &error));
    NC_CHECK_EQ(loaded.records.size(), std::size_t(2));
    const ModelRecord* ra = loaded.find("matting_hq");
    NC_CHECK(ra != nullptr);
    if (ra != nullptr) {
        NC_CHECK_EQ(ra->version, std::string("1.1.0"));
        NC_CHECK_EQ(ra->sizeBytes, a.sizeBytes);
        NC_CHECK_EQ(ra->sha256, a.sha256);
        NC_CHECK_EQ(ra->installedAtUnix, std::int64_t(1700000000));
        NC_CHECK(ra->status == ModelStatus::Installed);
    }
    const ModelRecord* rb = loaded.find("flow_interp");
    NC_CHECK(rb != nullptr);
    if (rb != nullptr) {
        NC_CHECK(rb->status == ModelStatus::Corrupt);
        NC_CHECK_EQ(rb->note, std::string("摘要校验失败"));
    }
}

NC_TEST(ModelManager, StoreJsonRejectsMalformed) {
    ModelStore store;
    std::string error;
    NC_CHECK(!loadModelStoreFromJson("{not json", store, &error));
    NC_CHECK(!error.empty());
    NC_CHECK(!loadModelStoreFromJson("[1,2,3]", store, &error));  // 根节点必须为对象

    // 空表合法
    ModelStore empty;
    NC_CHECK(loadModelStoreFromJson("{\"schema\":1}", empty, &error));
    NC_CHECK_EQ(empty.records.size(), std::size_t(0));
}

NC_TEST(ModelManager, StoreJsonSkipsInvalidRecords) {
    // 非法记录（缺 id）跳过，其余保留。
    const std::string json =
        "{\"records\":[{\"size\":100,\"status\":\"installed\"},"
        "{\"id\":\"whisper_zh\",\"version\":\"2.0.0\",\"size\":240,\"status\":\"installed\"}]}";
    ModelStore store;
    std::string error;
    NC_CHECK(loadModelStoreFromJson(json, store, &error));
    NC_CHECK_EQ(store.records.size(), std::size_t(1));
    NC_CHECK(store.find("whisper_zh") != nullptr);
    NC_CHECK(error.find("跳过") != std::string::npos);
}

NC_TEST(ModelManager, StoreJsonDowngradesRuntimeStates) {
    // 运行态（downloading/verifying）不是持久态 → 载入时降级为损坏（需重新下载）。
    const std::string json =
        "{\"records\":[{\"id\":\"a\",\"size\":10,\"status\":\"downloading\"},"
        "{\"id\":\"b\",\"size\":10,\"status\":\"verifying\"}]}";
    ModelStore store;
    std::string error;
    NC_CHECK(loadModelStoreFromJson(json, store, &error));
    NC_CHECK(store.statusOf("a") == ModelStatus::Corrupt);
    NC_CHECK(store.statusOf("b") == ModelStatus::Corrupt);
}
