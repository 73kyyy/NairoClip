#include "test_framework.h"

#ifdef NAIROCLIP_HAS_FFMPEG

#include "nairoclip/core/auto_edit.h"
#include "nairoclip/core/editor.h"
#include "nairoclip/core/export_runner.h"
#include "nairoclip/core/media_ffmpeg.h"
#include "nairoclip/core/media_ffmpeg_writer.h"
#include "nairoclip/core/scene_detect.h"
#include <cmath>
#include <fstream>
using namespace nairoclip::core;
// =============================================================================
// 参考仿剪端到端（全真实视频）：
//   参考素材（two_shots.mp4）→ 真实解码抽帧 → 场景检测 → 节奏画像
//   → 用户素材自动剪辑 → 真实导出 MP4 → 回读验证
// =============================================================================

namespace {
bool mediaExists(const char* path) {
    std::ifstream f(path);
    return f.good();
}
/// 从真实视频以固定步长采样帧特征（简化场景检测前端）。
std::vector<FrameFeature> sampleFeatures(const char* path, double stepSeconds) {
    std::vector<FrameFeature> features;
    FFmpegMediaReader reader;
    if (!reader.open(path).isOk()) {
        return features;
    }
    const double duration = reader.probe().duration.toDouble();
    std::vector<std::uint8_t> prevLuma;
    double prevR = 0.0, prevG = 0.0, prevB = 0.0;
    for (double t = 0.0; t < duration; t += stepSeconds) {
        if (!reader.seek(Rational::make(static_cast<std::int64_t>(t * 1000), 1000)).isOk()) {
            break;
        }
        VideoFrame frame;
        if (!reader.readVideoFrame(frame)) {
            break;
        }
        // 计算均值与 8x8 缩略亮度直方图差异
        double sumR = 0.0, sumG = 0.0, sumB = 0.0;
        std::size_t count = 0;
        std::vector<int> hist(8, 0);
        const int stride = frame.stride > 0 ? frame.stride : frame.width * 4;
        for (int y = 0; y < frame.height; y += 4) {
            const std::uint8_t* row = frame.pixels.data() +
                                     static_cast<std::size_t>(y) * static_cast<std::size_t>(stride);
            for (int x = 0; x < frame.width; x += 4) {
                const std::uint8_t* px = row + static_cast<std::size_t>(x) * 4;
                sumR += px[0];
                sumG += px[1];
                sumB += px[2];
                const int luma = (px[0] * 299 + px[1] * 587 + px[2] * 114) / 1000;
                hist[luma / 32]++;
                ++count;
            }
        }
        const double avgR = sumR / std::max<std::size_t>(1, count) / 255.0;
        const double avgG = sumG / std::max<std::size_t>(1, count) / 255.0;
        const double avgB = sumB / std::max<std::size_t>(1, count) / 255.0;
        // 与上一采样的直方图差异
        double diff = 0.0;
        if (!prevLuma.empty()) {
            std::vector<int> prevHist(8, 0);
            for (std::size_t i = 0; i < prevLuma.size(); ++i) {
                prevHist[prevLuma[i] / 32]++;
            }
            int sumAbs = 0;
            int total = 0;
            for (int i = 0; i < 8; ++i) {
                sumAbs += std::abs(hist[i] - prevHist[i]);
                total += std::max(hist[i], prevHist[i]);
            }
            diff = total > 0 ? static_cast<double>(sumAbs) / (2.0 * total) : 0.0;
        }
        // 也考虑颜色剧变（均值距离）
        const double colorDelta =
            (std::abs(avgR - prevR) + std::abs(avgG - prevG) + std::abs(avgB - prevB)) / 3.0;
        diff = std::max(diff, colorDelta * 3.0);  // 颜色剧变放大
        prevR = avgR;
        prevG = avgG;
        prevB = avgB;
        // 保存缩略 luma 采样（4x4 网格）
        prevLuma.clear();
        for (int gy = 0; gy < 4; ++gy) {
            for (int gx = 0; gx < 4; ++gx) {
                const int sx = gx * frame.width / 4;
                const int sy = gy * frame.height / 4;
                const std::uint8_t* px = frame.pixels.data() +
                                         static_cast<std::size_t>(sy) * static_cast<std::size_t>(stride) +
                                         static_cast<std::size_t>(sx) * 4;
                const int luma = (px[0] * 299 + px[1] * 587 + px[2] * 114) / 1000;
                prevLuma.push_back(static_cast<std::uint8_t>(std::min(255, luma)));
            }
        }
        FrameFeature ff;
        ff.timestamp = Rational::make(static_cast<std::int64_t>(t * 1000), 1000);
        ff.avgR = avgR;
        ff.avgG = avgG;
        ff.avgB = avgB;
        ff.histogramDiff = std::min(1.0, diff);
        features.push_back(ff);
    }
    reader.close();
    return features;
}
}  // namespace

NC_TEST(ReferenceWorkflowFFmpeg, FullRealPipeline) {
    const char* reference = "/tmp/media_gen/two_shots.mp4";
    const char* material = "/tmp/media_gen/test_av.mp4";
    if (!mediaExists(reference) || !mediaExists(material)) {
        return;  // 素材未生成：跳过
    }
    // 1) 采样参考素材帧特征（0.1s 间隔）
    const auto features = sampleFeatures(reference, 0.1);
    NC_CHECK(features.size() > 20);  // 4s / 0.1 = 40 帧
    // 2) 场景检测
    SceneDetectConfig detectConfig;
    detectConfig.threshold = 0.3;
    detectConfig.minShotDurationSeconds = 0.5;
    const auto shots = detectShots(features, Rational::make(4), detectConfig);
    NC_CHECK(shots.size() >= std::size_t(2));  // 两段素材至少切出 2 镜
    // 3) 节奏画像
    const RhythmProfile rhythm = analyzeRhythm(shots);
    NC_CHECK(rhythm.valid());
    NC_CHECK(rhythm.shotCount >= std::size_t(2));
    // 4) 用户素材自动剪辑
    Project project;
    TimelineEditor editor(project);
    Sequence seq;
    seq.id = SequenceId{project.ids.next()};
    seq.width = 160;
    seq.height = 120;
    project.sequences.push_back(seq);
    const SequenceId seqId = project.sequences.front().id;
    MediaAsset asset;
    asset.id = AssetId{project.ids.next()};
    asset.path = material;
    asset.type = MediaType::Video;
    asset.duration = Rational::make(3);
    project.assets.push_back(asset);
    const AssetId assetId = project.assets.front().id;
    TrackId trackId;
    NC_CHECK(editor.addTrack(seqId, TrackType::Video, "V1", &trackId).isOk());
    StyleProfile profile;
    profile.rhythm = rhythm;
    profile.color = analyzeColor(0.6, 0.5, 0.4, 0.6, 0.5);
    const AutoEditResult editResult = applyAutoEdit(
        editor, project, seqId, trackId, profile,
        {{assetId, Rational::make(3)}}, AutoEditOptions{});
    NC_CHECK(editResult.result.isOk());
    NC_CHECK(editResult.clipsInserted > 0);
    // 5) 真实导出
    class FFWriterF : public MediaWriterFactory {
    public:
        std::unique_ptr<MediaWriter> create() override {
            return std::make_unique<FFmpegMediaWriter>();
        }
    };
    FFmpegReaderFactory readerFactory;
    FFWriterF writerFactory;
    const ExportPlan plan = compileExportPlan(project, *project.mainSequence());
    ExportRunConfig config;
    config.outputPath = "/tmp/media_gen/reference_workflow_out.mp4";
    config.writer.width = 160;
    config.writer.height = 120;
    config.writer.crf = 30;
    config.writer.includeAudio = false;
    const ExportRunResult result =
        runExport(plan, readerFactory, writerFactory, config, nullptr, &project);
    NC_CHECK(result.result.isOk());
    NC_CHECK(result.fileWritten);
    // 6) 回读验证：输出可解码且帧数正确
    std::ifstream f(config.outputPath, std::ios::binary | std::ios::ate);
    NC_CHECK(f.good());
    NC_CHECK(f.tellg() > 1000);
    FFmpegMediaReader verify;
    NC_CHECK(verify.open(config.outputPath).isOk());
    int decoded = 0;
    VideoFrame frame;
    while (verify.readVideoFrame(frame) && decoded < 1000) {
        ++decoded;
    }
    NC_CHECK_EQ(decoded, static_cast<int>(result.stats.framesWritten));
    verify.close();
}
#endif
