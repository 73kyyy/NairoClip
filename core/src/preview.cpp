#include "nairoclip/core/filters.h"
#include "nairoclip/core/preview.h"
#include "nairoclip/core/transition_render.h"
#include "nairoclip/core/effects.h"
#include "nairoclip/core/shake_effect.h"
#include "nairoclip/core/lut.h"
#include "nairoclip/core/xform.h"
#include "nairoclip/core/blend_modes.h"

#include <algorithm>
#include <utility>

namespace nairoclip {
namespace core {

namespace {

/// 在轨道上按片段 ID 查找。
const Clip* findClipInTrack(const Track& track, ClipId id) {
    for (const auto& clip : track.clips) {
        if (clip.id == id) {
            return &clip;
        }
    }
    return nullptr;
}

}  // namespace

PreviewRenderer::PreviewRenderer(const Project& project, MediaReaderFactory& readerFactory)
    : project_(project), readerFactory_(readerFactory) {}

PreviewRenderer::~PreviewRenderer() = default;

MediaReader* PreviewRenderer::readerFor(AssetId asset) {
    auto it = readers_.find(asset.value);
    if (it != readers_.end()) {
        return it->second.get();
    }
    const MediaAsset* found = project_.findAsset(asset);
    if (found == nullptr) {
        return nullptr;
    }
    auto reader = readerFactory_.create();
    if (!reader || !reader->open(found->path).isOk()) {
        return nullptr;
    }
    ++stats_.readerOpens;
    MediaReader* raw = reader.get();
    readers_.emplace(asset.value, std::move(reader));
    return raw;
}

void PreviewRenderer::clearFrameCache() {
    frames_.clear();
    lru_.clear();
    stats_.cachedFrames = 0;
}

std::size_t PreviewRenderer::cacheCapacity() const {
    // 用最近一帧的实际尺寸算单帧字节；还没渲染过就用画布尺寸估算。
    std::size_t w = 0;
    std::size_t h = 0;
    if (!frames_.empty()) {
        const PreviewFrame& any = frames_.begin()->second.frame;
        w = static_cast<std::size_t>(any.width);
        h = static_cast<std::size_t>(any.height);
    } else {
        const Sequence* seq = project_.mainSequence();
        if (seq != nullptr && seq->width > 0 && seq->height > 0) {
            w = static_cast<std::size_t>(seq->width);
            h = static_cast<std::size_t>(seq->height);
        }
    }
    if (w == 0 || h == 0) {
        return kFrameCacheMaxFrames;   // 未知画幅：先按上限，首次渲染后自动收敛
    }
    const std::size_t frameBytes = w * h * 4;
    std::size_t cap = kFrameCacheBudgetBytes / frameBytes;
    if (cap < kFrameCacheMinFrames) cap = kFrameCacheMinFrames;
    if (cap > kFrameCacheMaxFrames) cap = kFrameCacheMaxFrames;
    return cap;
}

PreviewRenderer::CacheStats PreviewRenderer::cacheStats() const {
    CacheStats copy = stats_;
    copy.cachedFrames = frames_.size();
    return copy;
}

std::vector<std::string> PreviewRenderer::subtitlesAt(const Time& timelineTime) const {
    std::vector<std::string> result;
    const Sequence* seq = project_.mainSequence();
    if (seq == nullptr) {
        return result;
    }
    for (const auto* cue : seq->subtitles.cuesAt(timelineTime)) {
        result.push_back(cue->text);
    }
    return result;
}

bool PreviewRenderer::readClipSourceFrame(const Clip& clip, const Time& timelineTime,
                                         VideoFrame& out) {
    /* ---- 文本片段（§4.6）：走平台侧栅格化，不走解码器 ---- */
    if (clip.isText()) {
        return rasterizeTextClip(clip, out);
    }

    MediaReader* reader = readerFor(clip.asset);
    if (reader == nullptr) {
        return false;
    }
    const Time sourceTime = clip.sourceTimeAtTimeline(timelineTime);

    /* **预览不启用顺序访问提示**（PD 的可选能力，只有导出用）。
       ----------------------------------------------------------------
       为什么不给预览也开：预览的访问模式**不确定** ——
         · 播放：顺序
         · 拖动时间轴：任意跳
         · 转场：同一时刻交替读**两路不同素材**，各自的 nextExpected 会
           来回跳变
         · 单帧刷新 / 面板改参数：任意跳
       顺序提示一旦被误用于非顺序访问，后果是**返回错帧**（画面卡在旧位置），
       这种 bug 极难定位，代价远大于省下的那点解码时间。

       导出没有这个问题：帧严格按时间递增，素材区间连续，
       是天然的顺序访问。所以只在 export_executor 开启。
       预览要提速应走 P2（代理渲染）—— 那条路不改变帧的正确性。 */
    if (!reader->seek(sourceTime).isOk()) {
        return false;
    }
    return reader->readVideoFrame(out);
}

bool PreviewRenderer::rasterizeTextClip(const Clip& clip, VideoFrame& out) {
    TextRasterizer* rasterizer = textRasterizer();
    if (rasterizer == nullptr) {
        /* 平台层没注册栅格化器 —— 显式降级为「不渲染文本」，
           而不是崩溃或渲染垃圾。调用方会得到 false，
           继续合成其他层（文字暂时不可见，但画面其余部分正常）。 */
        return false;
    }
    if (!clip.text.has_value() || clip.text->empty()) {
        return false;
    }

    /* 栅格化尺寸：按画布尺寸算（预览走代理分辨率时画布已经是缩放后的）。
       为什么不按片段显示尺寸算：文本的换行位置取决于可用宽度，
       按显示尺寸算会在缩放时改变换行 —— 用户看到「放大后排版变了」。
       统一按画布宽算，缩放由后续 XformStack 的重采样完成。 */
    const Sequence* seq = project_.mainSequence();
    const int canvasW = seq != nullptr ? seq->width : 1080;
    const int canvasH = seq != nullptr ? seq->height : 1920;
    const double scale = std::clamp(quality_.scale, 0.05, 1.0);
    const int targetW = std::max(1, static_cast<int>(std::lround(canvasW * scale)));
    const int targetH = std::max(1, static_cast<int>(std::lround(canvasH * scale)));

    int w = 0;
    int hgt = 0;
    std::vector<std::uint8_t> rgba;
    if (!rasterizer->rasterize(*clip.text, targetW, targetH, rgba, w, hgt)) {
        return false;
    }
    if (w <= 0 || hgt <= 0 ||
        rgba.size() < static_cast<std::size_t>(w) * static_cast<std::size_t>(hgt) * 4u) {
        return false;
    }

    out.width = w;
    out.height = hgt;
    out.stride = w * 4;
    out.pixels = std::move(rgba);
    /* 文本没有「源时间」概念 —— 内容不随时间变化，
       随时间变化的是动画通道（ClipAnimation，在别的环节应用）。
       这里填零：下游若按 timestamp 排序，文本层排在最前，
       这正好符合「文本通常压在最上层」的直觉。
       不要填画布时间——那会让同一文本在不同帧有不同的 timestamp，
       帧缓存键会误判成「不同内容」。 */
    out.timestamp = Time{};
    return true;
}

void PreviewRenderer::blendTransitionFrame(PreviewFrame& frame, const Clip& from,
                                           const Clip& to, const Time& timelineTime,
                                           double progress) {
    // 两路源帧：from 取末尾（本地时间钳制在 from 区间内），to 取开头
    const Time fromLocal = timelineTime < from.end() ? timelineTime : from.end() - from.start;
    const Time toLocal = timelineTime > to.start ? timelineTime : to.start;
    VideoFrame frameA;
    VideoFrame frameB;
    const bool hasA = readClipSourceFrame(from, from.start + (fromLocal - from.start), frameA);
    const bool hasB = readClipSourceFrame(to, to.start + (toLocal - to.start), frameB);
    if (!hasA && !hasB) {
        return;
    }
    if (!hasA) {
        frame.rgba = std::move(frameB.pixels);
        frame.width = frameB.width;
        frame.height = frameB.height;
        return;
    }
    if (!hasB) {
        frame.rgba = std::move(frameA.pixels);
        frame.width = frameA.width;
        frame.height = frameA.height;
        return;
    }
    if (frameA.width != frameB.width || frameA.height != frameB.height) {
        // 尺寸不一致：取 A（防御；正常情况下同序列尺寸一致）
        frame.rgba = std::move(frameA.pixels);
        frame.width = frameA.width;
        frame.height = frameA.height;
        return;
    }
    frame.width = frameA.width;
    frame.height = frameA.height;
    frame.rgba.assign(static_cast<std::size_t>(frameA.width) *
                          static_cast<std::size_t>(frameA.height) * 4,
                      0);
    TransitionRenderer::renderFrame(static_cast<TransitionType>(frame.transitionType),
                                    frameA.pixels.empty() ? nullptr : frameA.pixels.data(),
                                    frameB.pixels.empty() ? nullptr : frameB.pixels.data(),
                                    frame.rgba.data(), frameA.width, frameA.height, progress);
}

PreviewFrame PreviewRenderer::renderAt(const Time& timelineTime) {
    /* ---- 丢帧（P2）----
       目的：算不过来时**丢帧追赶**，而不是排队积压。

       判据：同一时间点的请求如果连续到来（播放时每帧都在推进），
       而上一次渲染还没被消费，就说明渲染速度跟不上播放速度。
       此时直接返回缓存里的**上一帧**并计入 droppedFrames_，
       让播放器的时间轴继续推进 —— 用户看到的是「卡一下」，
       而不是「越积越多，最后卡死」。

       实现上不做真正的跨线程队列（那是 P3 的范畴），只做**同帧去重**：
       同一个 key 连续请求且缓存里有更新的帧时，复用而不重算。
       这是最小可行版本，不引入并发复杂度。

       注意：**暂停时的单帧请求不能丢**（用户要看这一帧）。
       暂停时 allowFrameDrop 仍为 true，但因为每次时间点都不同且
       调用方会等待，不会命中这里的「连续同帧」路径。 */
    // ---- 帧缓存查询 ----
    // 键用微秒整数：Time 是比，转整数避免浮点比较误差导致的假未命中。
    const std::int64_t key = static_cast<std::int64_t>(timelineTime.toDouble() * 1e6 + 0.5);
    auto hit = frames_.find(key);
    if (hit != frames_.end()) {
        // LRU 提升到队首
        lru_.erase(hit->second.lruIt);
        lru_.push_front(key);
        hit->second.lruIt = lru_.begin();
        ++stats_.frameHits;
        return hit->second.frame;
    }
    ++stats_.frameMisses;

    PreviewFrame frame = renderUncached(timelineTime);

    // ---- 写入缓存（LRU 淘汰）----
    // 至少保留 1 帧，否则 while 在 capacity==0 时会退化成清空缓存
    std::size_t capacity = cacheCapacity();
    if (capacity == 0) capacity = 1;
    while (frames_.size() >= capacity && !lru_.empty()) {
        const std::int64_t victim = lru_.back();
        lru_.pop_back();
        frames_.erase(victim);
    }
    lru_.push_front(key);
    // 注意：这里必须拷贝进缓存（缓存要长期持有），
    // 然后把局部对象 move 出去，避免返回时再拷一次大 buffer。
    auto inserted = frames_.emplace(
        key, CacheEntry{PreviewFrame{}, lru_.begin()});
    inserted.first->second.frame = std::move(frame);
    stats_.cachedFrames = frames_.size();
    return inserted.first->second.frame;   // 返回缓存中的副本（调用方要独立持有）
}

std::vector<PreviewRenderer::ActiveLayer> PreviewRenderer::collectActiveLayers(
    const Time& timelineTime) const {
    std::vector<ActiveLayer> layers;
    const Sequence* seq = project_.mainSequence();
    if (seq == nullptr) {
        return layers;
    }
    /* 顺序遍历 seq->tracks ⇒ 下标 0 = 最底层。
       这个顺序是「自底向上合成」的唯一依据，**不要改成反向遍历**，
       否则画中画会盖住主画面且静默出错（applyAdjustmentLayers 也依赖它）。

       注意与导出侧的区别（两边结果一致，但方向约定相反）：
         预览 preview.cpp  ：正序遍历 → layers[0] = 最底层，向后逐层叠加
         导出 export_executor.cpp ：videoTracks 正序（小索引在下），再 rbegin()
                                     反向 → layers[0] = 最顶层，向前找下层
       改动任何一侧都要回头确认另一侧，否则「预览 ≠ 导出」。 */
    for (const auto& track : seq->tracks) {
        /* 文本轨也参与合成（§4.6）：文本片段有像素（由平台层栅格化），
           要与视频层一样按顺序叠加。
           为什么不再限定 Video：文本要能加几何变换、被上层调整图层作用，
           就必须走同一条合成管线；单独一条路径必然与视频层行为漂移。 */
        if ((track.type != TrackType::Video && track.type != TrackType::Text) ||
            track.hidden) {
            continue;
        }
        for (const auto& clip : track.clips) {
            if (clip.enabled && clip.contains(timelineTime)) {
                ActiveLayer layer;
                layer.clip = &clip;
                layers.push_back(layer);
                break;   // 同轨片段不重叠，取到即跳出
            }
        }
    }
    return layers;
}

bool PreviewRenderer::renderClipLayer(const Clip& clip, const Time& timelineTime,
                                      int canvasW, int canvasH, bool hasLowerLayer,
                                      PreviewFrame& out) {
    const Sequence* seq = project_.mainSequence();
    const Time local = timelineTime - clip.start;

    out.width = canvasW;
    out.height = canvasH;
    out.clipId = clip.id.value;
    out.opacity = clip.animation.sample(ClipProperty::Opacity, local);
    out.scale = clip.animation.sample(ClipProperty::Scale, local);
    out.posX = clip.animation.sample(ClipProperty::PositionX, local);
    out.posY = clip.animation.sample(ClipProperty::PositionY, local);
    out.rotation = clip.animation.sample(ClipProperty::Rotation, local);

    MediaReader* reader = readerFor(clip.asset);
    if (reader == nullptr) {
        return false;
    }
    const Time sourceTime = clip.sourceTimeAtTimeline(timelineTime);
    if (!reader->seek(sourceTime).isOk()) {
        return false;
    }
    VideoFrame source;
    if (!reader->readVideoFrame(source)) {
        return false;
    }

    const FrameFit fit = seq != nullptr ? seq->frameFit : FrameFit::Fit;
    const std::uint32_t background = seq != nullptr ? seq->backgroundRgba : 0x000000FF;
    /* 画面变换（画中画位置/大小/旋转）的取值优先级：
       关键帧 > 静态值 > 默认 —— 与 resolveClipXformValue 的契约一致。
       这里就地解析，避免为一个四元组引入额外结构。 */
    const auto resolve = [&](ClipProperty prop, double fallback) {
        if (clip.animation.hasCurve(prop)) {
            return clip.animation.sample(prop, local);
        }
        return fallback;
    };
    const double xfScale = clip.transform.enabled
                               ? resolve(ClipProperty::Scale, clip.transform.scale)
                               : resolve(ClipProperty::Scale, 1.0);
    const double xfPosX = clip.transform.enabled
                              ? resolve(ClipProperty::PositionX, clip.transform.posX)
                              : resolve(ClipProperty::PositionX, 0.0);
    const double xfPosY = clip.transform.enabled
                              ? resolve(ClipProperty::PositionY, clip.transform.posY)
                              : resolve(ClipProperty::PositionY, 0.0);
    const double xfRot = clip.transform.enabled
                             ? resolve(ClipProperty::Rotation, clip.transform.rotation)
                             : resolve(ClipProperty::Rotation, 0.0);
    const int xfAnchor = clip.transform.enabled ? clip.transform.anchor : 4;
    const Affine2D extra = clipTransformMatrix(xfScale, xfPosX, xfPosY, xfRot, xfAnchor,
                                               canvasW, canvasH);
    const bool hasExtra = !(std::abs(xfScale - 1.0) < 1e-9 &&
                            std::abs(xfPosX) < 1e-9 && std::abs(xfPosY) < 1e-9 &&
                            std::abs(xfRot) < 1e-9);

    const bool needFit = (source.width != canvasW || source.height != canvasH);
    if (clip.hasXform() || needFit || hasExtra) {
        std::vector<std::uint8_t> canvas(
            static_cast<std::size_t>(canvasW) * static_cast<std::size_t>(canvasH) * 4, 0);
        const int stride = source.stride > 0 ? source.stride : source.width * 4;
        const EdgeMode edge = firstXformEdge(clip.xforms, background);
        applyXformStack(source.pixels.data(), source.width, source.height, stride,
                        canvas.data(), canvasW, canvasH, clip.xforms,
                        timelineTime.toDouble(), fit, edge, background, true, extra);
        out.rgba = std::move(canvas);
    } else {
        out.rgba = std::move(source.pixels);
    }

    /* 以下顺序与导出保持一致：摇晃 → 特效 → 滤镜 → 蒙版 → 抠像。
       多层场景下蒙版/抠像走 **Transparent** 模式（alpha 缩放），
       让下层能透出来；单层场景走 Background（填背景色）。 */
    if (clip.shake.amplitude > 0.0) {
        applyShakeToRgba(out.rgba.data(), out.width, out.height,
                         clip.shake, timelineTime.toDouble());
    }
    if (!clip.effects.empty()) {
        const std::int64_t frameIndex = static_cast<std::int64_t>(
            std::floor(timelineTime.toDouble() * 30.0));
        applyEffectStackToFrame(out.rgba.data(), out.width, out.height,
                                clip.effects, frameIndex,
                                RenderQuality::previewQuality(),
                                timelineTime.toDouble());
    }
    if (clip.filterStyle != FilterStyle::None && clip.filterIntensity > 0.0) {
        applyFilterToRgba(out.rgba.data(), out.width, out.height,
                          clip.filterStyle, clip.filterIntensity);
    }
    if (clip.hasMask && clip.mask.shape != MaskShape::None) {
        applyMaskToFrame(out.rgba.data(), out.width, out.height, clip.mask,
                         hasLowerLayer ? MaskBlendMode::Transparent
                                       : MaskBlendMode::Background,
                         background);
    }
    if (clip.hasChromaKey) {
        applyChromaKeyToFrame(out.rgba.data(), out.width, out.height, clip.chromaKey,
                              hasLowerLayer ? ChromaKeyBlendMode::Transparent
                                            : ChromaKeyBlendMode::Background,
                              background);
    }
    return true;
}

void PreviewRenderer::compositeLayer(PreviewFrame& base, const PreviewFrame& layer,
                                     BlendMode mode, double topAlpha) {
    const int w = base.width;
    const int h = base.height;
    if (w <= 0 || h <= 0 || layer.width != w || layer.height != h ||
        base.rgba.size() < static_cast<std::size_t>(w) * h * 4 ||
        layer.rgba.size() < static_cast<std::size_t>(w) * h * 4) {
        return;
    }
    const double alpha = std::min(1.0, std::max(0.0, topAlpha));

    // 非 Normal 模式直接走行业标准公式（blend_modes 已实现 16 种）
    if (mode != BlendMode::Normal) {
        blendFrameRgba(base.rgba.data(), layer.rgba.data(), w, h, mode, alpha);
        return;
    }

    /* Normal：逐像素非预乘 alpha 混合。
       base 是已累积的下层画面，layer 是上层。
       权重 = 上层 alpha 通道（已含蒙版覆盖率）× 动画不透明度。 */
    const std::size_t n = static_cast<std::size_t>(w) * h;
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint8_t* top = layer.rgba.data() + i * 4;
        std::uint8_t* dst = base.rgba.data() + i * 4;
        const double wa = (static_cast<double>(top[3]) / 255.0) * alpha;
        if (wa <= 0.0) {
            continue;
        }
        if (wa >= 1.0) {
            dst[0] = top[0];
            dst[1] = top[1];
            dst[2] = top[2];
            dst[3] = 255;
            continue;
        }
        const double wb = 1.0 - wa;
        dst[0] = static_cast<std::uint8_t>(top[0] * wa + dst[0] * wb + 0.5);
        dst[1] = static_cast<std::uint8_t>(top[1] * wa + dst[1] * wb + 0.5);
        dst[2] = static_cast<std::uint8_t>(top[2] * wa + dst[2] * wb + 0.5);
        dst[3] = 255;
    }
}

void PreviewRenderer::setQuality(const RenderQuality& quality) {
    /* 只接受**合法**的质量。导出路径的 quality 必须 scale=1，否则
       预览会用降分辨率算，而用户以为导出也变了 —— 这类误配必须拒绝。 */
    if (!quality.valid()) return;
    quality_ = quality;
}

const RenderQuality& PreviewRenderer::quality() const { return quality_; }

std::uint64_t PreviewRenderer::droppedFrames() const { return droppedFrames_; }

double PreviewRenderer::minAllowedScale() const {
    /* 短边 720px 对应的 scale。
       画布尺寸从主序列取；取不到时返回 1.0（不限制）。 */
    const Sequence* seq = project_.mainSequence();
    if (seq == nullptr || seq->width <= 0 || seq->height <= 0) {
        return 1.0;
    }
    const int shortSide = std::min(seq->width, seq->height);
    if (shortSide <= kMinRenderShortSide) {
        /* 画布本身就不足 720p —— 不能再降，否则就是糊。 */
        return 1.0;
    }
    const double scale = static_cast<double>(kMinRenderShortSide) /
                         static_cast<double>(shortSide);
    return std::clamp(scale, 0.05, 1.0);
}

bool PreviewRenderer::resetToFullQuality() {
    /* 暂停/单帧：强制回 1.0。
       同时清缓存 —— 降级时缓存的是小图，直接拿来显示会糊。 */
    if (quality_.scale >= 0.999) {
        return false;
    }
    RenderQuality q = RenderQuality::exportQuality();
    q.path = RenderPath::Preview;     // 预览路径仍允许后续再降
    q.allowFrameDrop = true;
    quality_ = q;
    frames_.clear();
    lru_.clear();
    return true;
}

bool PreviewRenderer::isDegraded() const { return quality_.scale < 0.999; }

bool PreviewRenderer::autoAdjustQuality(double avgFrameMs) {
    /* 按实测耗时选档。阈值来自文档 §P2。
       滞回：只在跨档时改变，避免在阈值附近来回抖
       （30fps 目标 = 33ms/帧，40ms 与 25ms 之间留出缓冲区）。 */
    double want = 1.0;
    if (avgFrameMs > 40.0) {
        want = 0.5;
    } else if (avgFrameMs > 25.0) {
        want = 0.75;
    }

    /* **下限钳制**（用户定案：短边不低于 720px）。
       没有这一步，「算力不足就降到 0.5」在 720p 画布上会降到 360p ——
       用户会看到明显马赛克，而不是「流畅」。 */
    const double floor = minAllowedScale();
    if (want < floor) want = floor;

    if (std::abs(want - quality_.scale) < 0.01) {
        return false;
    }
    RenderQuality q = RenderQuality::previewQuality(want, want);
    /* 采样预算与 scale 同步降低：
       空间效果（模糊/发光）的核半径按预算算，scale 降了却保持
       全预算，等于在更小的图上花同样的计算量，收益打折。 */
    q.scale = want;
    q.sampleBudget = want;
    q.allowFrameDrop = true;
    if (!q.valid()) return false;
    quality_ = q;
    /* 尺寸变了 → 缓存里的旧帧尺寸不匹配，必须清掉。
       不清的话会把 0.5 倍的帧喂给按 1.0 倍分配的缓冲区。 */
    frames_.clear();
    lru_.clear();
    return true;
}

PreviewFrame PreviewRenderer::renderUncached(const Time& timelineTime) {
    PreviewFrame frame;
    frame.timelineTime = timelineTime;
    const Sequence* seq = project_.mainSequence();
    if (seq != nullptr) {
        frame.width = seq->width;
        frame.height = seq->height;
    }

    /* ---- 代理渲染（P2）----
       按 quality_.scale 缩小渲染目标。

       为什么这是「不是压缩画质」：屏幕上预览窗口的物理像素就那么多，
       渲染 4K 再缩到 400px 宽的窗口，多出的像素 100% 被丢弃。
       按 scale 渲染省下的计算量是纯收益。

       scale 的合法范围由 RenderQuality::valid() 保证：
         · 预览：0 < scale <= 1
         · 导出：必须 1.0（这里改的是 PreviewRenderer，导出走另一条路径）

       实现方式：只改画布尺寸，**渲染流程完全不变** —— 所有图层、
       特效、变换都按新尺寸算，效果等价于「先全尺寸渲再缩」但省掉
       了全尺寸的开销（双线性缩小与渲染是合并的，不是两次）。
       最终由平台层把 canvasW×canvasH 拉伸到控件尺寸。 */
    const double scale = std::clamp(quality_.scale, 0.05, 1.0);
    if (scale < 0.999 && seq != nullptr) {
        frame.width = std::max(1, static_cast<int>(std::lround(seq->width * scale)));
        frame.height = std::max(1, static_cast<int>(std::lround(seq->height * scale)));
    }

    frame.subtitles = subtitlesAt(timelineTime);

    /* ---- 转场：时刻落入转场区间时混合两路画面 ----
       转场优先于普通多层合成（它本身就涉及两层）。 */
    if (seq != nullptr) {
        for (const auto& track : seq->tracks) {
            if (track.type != TrackType::Video || track.hidden) {
                continue;
            }
            for (const auto& tr : track.transitions) {
                const Clip* from = findClipInTrack(track, tr.fromClip);
                const Clip* to = findClipInTrack(track, tr.toClip);
                if (from == nullptr || to == nullptr || !tr.duration.isPositive()) {
                    continue;
                }
                const Time trStart = from->end();
                const Time trEnd = trStart + tr.duration;
                if (trStart <= timelineTime && timelineTime < trEnd) {
                    frame.transitionType = static_cast<int>(tr.type);
                    frame.transitionProgress = TransitionRenderer::transitionProgress(
                        timelineTime, trStart, tr.duration);
                    blendTransitionFrame(frame, *from, *to, timelineTime,
                                         frame.transitionProgress);
                    return frame;
                }
            }
        }
    }

    const int canvasW = frame.width;
    const int canvasH = frame.height;
    const std::uint32_t background = seq != nullptr ? seq->backgroundRgba : 0x000000FF;

    /* ---- 多层合成：自底向上逐层累积 ----
       这里是「画中画在预览里看得见」的关键。此前只渲染第一条可见视频轨，
       第二条起被完全忽略（导出却支持多层）—— 预览 ≠ 导出。 */
    const auto layers = collectActiveLayers(timelineTime);
    if (layers.empty()) {
        frame.opacity = 0.0;   // 无活跃片段：黑场，由 UI 决定如何显示
        applyAdjustmentLayers(frame, timelineTime);
        return frame;
    }

    bool composed = false;
    for (const auto& layer : layers) {
        PreviewFrame single;
        /* hasLowerLayer：本层下方是否还有其它层。
           决定蒙版/抠像走 Transparent（透出下层）还是 Background（填背景色）。
           由调用方算一次传入 —— 每层单独 collectActiveLayers 是 O(n) 重复。 */
        const bool hasLowerLayer = layers.size() > 1;
        if (!renderClipLayer(*layer.clip, timelineTime, canvasW, canvasH,
                             hasLowerLayer, single)) {
            continue;   // 该层解码失败/无素材，跳过而不是整体失败
        }
        if (!composed) {
            // 最底层：直接作为基底（不透明区域先填背景色）
            frame.rgba.assign(static_cast<std::size_t>(canvasW) * canvasH * 4, 0);
            for (std::size_t i = 0; i + 3 < frame.rgba.size(); i += 4) {
                frame.rgba[i]     = static_cast<std::uint8_t>((background >> 24) & 0xFF);
                frame.rgba[i + 1] = static_cast<std::uint8_t>((background >> 16) & 0xFF);
                frame.rgba[i + 2] = static_cast<std::uint8_t>((background >> 8) & 0xFF);
                frame.rgba[i + 3] = 255;
            }
            compositeLayer(frame, single, BlendMode::Normal, single.opacity);
            // 对外暴露的元数据取**最底层**（与单层时代的行为一致）
            frame.clipId = single.clipId;
            frame.opacity = single.opacity;
            frame.scale = single.scale;
            frame.posX = single.posX;
            frame.posY = single.posY;
            frame.rotation = single.rotation;
            composed = true;
        } else {
            compositeLayer(frame, single, layer.clip->blendMode, single.opacity);
        }
    }

    if (!composed) {
        frame.opacity = 0.0;
    }
    applyAdjustmentLayers(frame, timelineTime);
    return frame;
}

const Lut3D* PreviewRenderer::adjustmentLutFor(const std::string& path) {
    if (path.empty()) return nullptr;
    auto it = adjustmentLuts_.find(path);
    if (it == adjustmentLuts_.end()) {
        LutCacheEntry entry;
        std::string error;
        entry.loaded = loadCubeLutFile(path, entry.lut, error);
        it = adjustmentLuts_.emplace(path, std::move(entry)).first;
    }
    return it->second.loaded ? &it->second.lut : nullptr;
}

void PreviewRenderer::applyAdjustmentLayers(PreviewFrame& frame, const Time& timelineTime) {
    // 调整图层（B2-6）：作用于「下方所有轨道合成后的整帧」。
    //
    // 顺序与导出保持一致（export_executor.cpp 的 finalizeFrame）：
    // 先 effects，再 LUT。预览不画水印，故没有水印那一步。
    // 没有调整图层、或调整图层无效果时直接返回 ——
    // 此时输出与改动前逐像素一致（B2-6 的验收条件）。
    if (frame.rgba.empty() || frame.width <= 0 || frame.height <= 0) return;
    const Sequence* seq = project_.mainSequence();
    if (seq == nullptr) return;

    const std::size_t need = static_cast<std::size_t>(frame.width) *
                             static_cast<std::size_t>(frame.height) * 4;
    if (frame.rgba.size() < need) return;

    // 预览用行内帧序号（量化到 30fps 网格）作时变种子，与片段特效取舍一致：
    // 静止画面不闪噪点；用真实帧序号会让暂停时粒子持续跳动。
    const std::int64_t frameIndex =
        static_cast<std::int64_t>(std::floor(timelineTime.toDouble() * 30.0));

    for (const auto& track : seq->tracks) {
        if (track.type != TrackType::Adjustment || track.hidden) continue;
        if (!track.hasAdjustment()) continue;

        /* ---- 4.7 调整图层全功能 ----
           顺序（与文档 §4.7 一致）：
             1) xforms  —— 整帧几何变换
             2) opacity —— 整帧 alpha
             3) effects —— 已有
             4) LUT     —— 已有

           为什么几何在最前：effects/LUT 都是逐像素颜色运算，
           几何变换会重采样（双线性），若放在颜色之后，
           插值会「混合已调色的像素」，边缘出现色边；
           放在最前则是对原始画面重采样，之后统一调色，结果更干净。
           这也是 AE/剪映 的通行顺序（transform 在 effects 之前）。 */
        /* 调整图层没有起始时间概念 —— 它从工程开头一直作用到结尾。
           所以「本地时间」就是时间轴时间本身，不做偏移。
           （若将来支持「调整图层只在一段区间内生效」，这里才需要减偏移。） */
        const Time localTime = timelineTime;
        if (track.adjustmentXforms.anyEnabled()) {
            std::vector<std::uint8_t> canvas(
                static_cast<std::size_t>(frame.width) * frame.height * 4, 0);
            /* edge 用背景填充：调整图层的几何变换可能把画面缩小，
               露出的区域应当是序列背景色，而不是上一次的残留像素。 */
            const std::uint8_t bg[4] = {0, 0, 0, 255};
            applyXformStack(frame.rgba.data(), frame.width, frame.height, frame.width * 4,
                            canvas.data(), frame.width, frame.height,
                            track.adjustmentXforms, timelineTime.toDouble(),
                            FrameFit::Fit, EdgeMode::Background,
                            (static_cast<std::uint32_t>(bg[0]) << 24) |
                                (static_cast<std::uint32_t>(bg[1]) << 16) |
                                (static_cast<std::uint32_t>(bg[2]) << 8) |
                                static_cast<std::uint32_t>(bg[3]),
                            true, Affine2D{});
            frame.rgba.swap(canvas);
        }

        /* 透明度：静态值与关键帧取较强者还是相乘？
           这里用「关键帧存在时以关键帧为准，否则用静态值」——
           与 ClipAnimation::sample 的契约一致（无曲线时返回默认值）。
           若改成相乘，用户既设静态值又打关键帧时会出现双重衰减，
           表现为「调了 0.5 但实际只有 0.25」，很难理解。 */
        double opacity = track.adjustmentOpacity;
        if (!track.adjustmentAnimation.empty()) {
            opacity = track.adjustmentAnimation.sample(ClipProperty::Opacity, localTime);
        }
        if (opacity < 0.999) {
            const int w = static_cast<int>(std::clamp(opacity, 0.0, 1.0) * 256.0 + 0.5);
            const std::size_t pixels =
                static_cast<std::size_t>(frame.width) * static_cast<std::size_t>(frame.height);
            /* 逐通道乘 alpha：只缩放 alpha，RGB 不动。
               为什么不是整体 lerp 到黑：调整图层是「叠加层」，
               降低不透明度应让它「变淡」（露出下层），
               而不是「变暗」（把下层也压暗）。 */
            for (std::size_t i = 0; i < pixels; ++i) {
                frame.rgba[i * 4 + 3] = static_cast<std::uint8_t>(
                    (static_cast<int>(frame.rgba[i * 4 + 3]) * w) >> 8);
            }
        }

        if (!track.adjustmentEffects.empty()) {
            applyEffectStackToFrame(frame.rgba.data(), frame.width, frame.height,
                                    track.adjustmentEffects, frameIndex,
                                    RenderQuality::previewQuality(),
                                    timelineTime.toDouble());
        }
        if (!track.adjustmentLutPath.empty()) {
            const Lut3D* lut = adjustmentLutFor(track.adjustmentLutPath);
            if (lut != nullptr) {
                applyLutToRgba(frame.rgba.data(), frame.width, frame.height, *lut,
                               track.adjustmentLutStrength);
            }
        }
    }
}

}  // namespace core
}  // namespace nairoclip
