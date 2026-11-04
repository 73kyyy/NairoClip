#pragma once

#include "nairoclip/core/export_executor.h"
#include "nairoclip/core/export_plan.h"
#include "nairoclip/core/lut.h"
#include "nairoclip/core/media_io.h"
#include "nairoclip/core/render_engine.h"
#include "nairoclip/core/project.h"
#include "nairoclip/core/subtitle.h"

#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace nairoclip {
namespace core {

/// 预览渲染器：给定时间轴时刻，返回该帧应显示的画面 + 字幕文本。
///
/// 职责：
///  - 查找时刻处的活跃片段（视频轨）
///  - 时间映射（timeline → source），读取源帧
///  - 附加动画属性（透明度等，供 UI 层应用）
///  - 附加该时刻的字幕文本列表
///
/// 与导出执行器的区别：预览是「按需单帧」，导出是「顺序全帧」。
struct PreviewFrame {
    Time timelineTime;
    int width = 0;
    int height = 0;
    /// RGBA8 像素（空 = 该时刻无画面，UI 显示黑场）。
    std::vector<std::uint8_t> rgba;
    /// 该帧的活动字幕（按轨道顺序）。
    std::vector<std::string> subtitles;
    /// 片段动画快照（透明度等；-1 = 无片段）。
    double opacity = 1.0;
    double scale = 1.0;
    double posX = 0.0;
    double posY = 0.0;
    double rotation = 0.0;
    /// 片段 ID（无片段时 0）。
    std::uint64_t clipId = 0;
    /// 转场元数据（-1 = 无转场；否则 = TransitionType 枚举值）。
    int transitionType = -1;
    /// 转场进度 0..1（仅转场帧有效）。
    double transitionProgress = 0.0;
};

/// 预览渲染器。
///
/// 缓存设计（为什么这样分层）：
///  1) readers_ 按 assetId 复用解码器 —— 解码器 open 一次可反复 seek，
///     之前的实现把 reader 和渲染器绑在一起，渲染器一重建就要重新 open，
///     这是"每一帧都慢"的主要来源之一。
///  2) frames_ 是最近访问时刻的 RGBA 帧环形缓存 —— 拖动/播放时相邻帧
///     常重复命中（同一片段内 seek 到同一源帧），命中就直接返回，
///     不再走 decode + 尺寸适配 + 特效栈。
///  3) 缓存键是精确的 timelineTime（微秒）。不做"就近取帧"式的模糊合并：
///     那会让用户在静止画面上看到上一帧的画面，属于错误结果。
///     真正的流畅播放依赖帧率对齐，由上层按时间网格请求。
class PreviewRenderer {
public:
    /// 帧缓存**字节预算**（不是固定帧数）。
    ///
    /// 为什么按字节而不是按帧数：上一版固定 12 帧，
    /// 1080p（约 8MB/帧）是 96MB 还能接受，
    /// 但 4K（约 33MB/帧）就是 400MB —— 移动端会被系统直接杀掉。
    /// 用户可以选择任意分辨率与帧率，所以容量必须随画幅自适应。
    static constexpr std::size_t kFrameCacheBudgetBytes = 96u * 1024u * 1024u;
    /// 帧数下限（保证小画幅下也有基本的来回拖动命中）
    static constexpr std::size_t kFrameCacheMinFrames = 4;
    /// 帧数上限（避免 240p 之类极小画幅把缓存堆成几万帧）
    static constexpr std::size_t kFrameCacheMaxFrames = 48;

    PreviewRenderer(const Project& project, MediaReaderFactory& readerFactory);
    ~PreviewRenderer();

    PreviewRenderer(const PreviewRenderer&) = delete;
    PreviewRenderer& operator=(const PreviewRenderer&) = delete;

    /// 渲染指定时刻的一帧。
    /// - 视频：**所有**未隐藏视频轨在 t 时刻的片段，自底向上逐层合成
    ///   （修复：此前只取第一条可见视频轨，导致画中画/混合模式在预览里
    ///    完全看不到 —— 导出支持多层而预览不支持，属于功能不成立）
    /// - 字幕：取序列字幕轨中所有可见字幕
    /// - 动画：应用片段动画通道（透明度/缩放/位移/旋转）
    /// 命中帧缓存时直接返回缓存副本（不重新解码）。
    /// 设置渲染质量（P2 代理渲染 + 丢帧）。
    ///
    /// 默认 = 全质量（scale=1），与改动前行为完全一致。
    /// 平台层按实测单帧耗时调用 setQuality(RenderQuality::previewQuality(0.5)) 降级。
    ///
    /// **导出路径不走这里** —— 导出有自己的 ExportQuality，恒为 1.0。
    /// 若这里被设成 scale<1，只影响预览帧的尺寸，不影响导出。
    void setQuality(const RenderQuality& quality);
    const RenderQuality& quality() const;
    /// 丢帧统计（播放时上一帧未完成则跳过本帧）。
    std::uint64_t droppedFrames() const;

    /// 按实测单帧耗时自动选档（P2 的「自动档」）。
    ///
    /// 规则（与文档一致）：
    ///   单帧 > 40ms  → scale = 0.5
    ///   单帧 > 25ms  → scale = 0.75
    ///   否则         → scale = 1.0
    ///
    /// 为什么让调用方传入耗时而不是内部计时：耗时统计需要**跨调用**
    /// 平滑（每 30 帧评估一次），放在调用方（播放循环/平台层）更自然，
    /// 也避免 PreviewRenderer 变成有状态的自适应黑盒。
    /// 返回是否改变了档位。
    bool autoAdjustQuality(double avgFrameMs);

    PreviewFrame renderAt(const Time& timelineTime);

    /// 便捷：只取字幕。
    std::vector<std::string> subtitlesAt(const Time& timelineTime) const;

    /// 缓存统计（供诊断与测试断言；不影响渲染结果）。
    struct CacheStats {
        std::size_t frameHits = 0;      ///< 命中帧缓存的次数
        std::size_t frameMisses = 0;    ///< 未命中（走完整渲染）的次数
        std::size_t readerOpens = 0;    ///< 真正 open 解码器的次数
        std::size_t cachedFrames = 0;   ///< 当前缓存中的帧数
    };
    CacheStats cacheStats() const;

    /// 清空帧缓存（保留已打开的解码器）。
    /// 用于「解码器仍在、但画面变了」的场景（如仅叠加层变化）。
    void clearFrameCache();

    /// 当前画幅下的有效帧缓存容量（由字节预算 ÷ 单帧字节算出）。
    /// 画幅未知（还没渲染过）时返回上限值，首次渲染后自动收敛。
    std::size_t cacheCapacity() const;

private:
    /// 不做缓存查询的实际渲染（renderAt 命中缓存时不会走到这里）。
    PreviewFrame renderUncached(const Time& timelineTime);
    /// 渲染**单层**（一个片段）到画布尺寸，含该层自己的变换/特效/滤镜/蒙版。
    /// 返回 false 表示该层无可用像素（解码失败/无素材），调用方跳过即可。
    bool renderClipLayer(const Clip& clip, const Time& timelineTime,
                         int canvasW, int canvasH, bool hasLowerLayer,
                         PreviewFrame& out);
    /// 收集 t 时刻所有活动视频层。**下标 0 = 最底层**（与 seq->tracks 同序）。
    struct ActiveLayer {
        const Clip* clip = nullptr;
    };
    std::vector<ActiveLayer> collectActiveLayers(const Time& timelineTime) const;
    /// 把一层按 alpha 合成到已经累积的画布上（base 是累积结果）。
    void compositeLayer(PreviewFrame& base, const PreviewFrame& layer, BlendMode mode,
                        double topAlpha);
    /// 读取片段在时间轴时刻对应的源帧（自动打开并缓存 reader）。
    bool readClipSourceFrame(const Clip& clip, const Time& timelineTime, VideoFrame& out);
    /// 转场混合：读取两路源帧，混合写回 frame（含尺寸初始化）。
    void blendTransitionFrame(PreviewFrame& frame, const Clip& from, const Clip& to,
                              const Time& timelineTime, double progress);
    /// 取（必要时创建）某素材的解码器。
    MediaReader* readerFor(AssetId asset);
    /// 应用调整图层（B2-6）：对已合成的整帧依次应用 effects 与 LUT。
    void applyAdjustmentLayers(PreviewFrame& frame, const Time& timelineTime);

    RenderQuality quality_{RenderQuality::exportQuality()};
    std::uint64_t droppedFrames_ = 0;

    const Project& project_;
    MediaReaderFactory& readerFactory_;
    std::map<std::uint64_t, std::unique_ptr<MediaReader>> readers_;

    /// 帧缓存：键 = 时间轴微秒。用 list 维护 LRU 顺序，
    /// map 做 O(log n) 查找。容量固定，满时淘汰最久未用的。
    struct CacheEntry {
        PreviewFrame frame;
        std::list<std::int64_t>::iterator lruIt;
    };
    std::map<std::int64_t, CacheEntry> frames_;
    std::list<std::int64_t> lru_;   ///< front = 最近使用
    mutable CacheStats stats_;

    /// 调整图层 LUT 缓存（键 = 路径）。
    ///
    /// 为什么必须缓存：预览每帧都会走 renderUncached，一个 33³ .cube 文件
    /// 是几十万行的文本，每帧重读会让拖动直接卡死。这里按路径保留解析结果，
    /// 路径不变就永远只读一次盘（与导出端「LUT 只加载一次」的取舍一致）。
    struct LutCacheEntry {
        bool loaded = false;
        Lut3D lut;
    };
    std::map<std::string, LutCacheEntry> adjustmentLuts_;
    /// 取（必要时加载）调整图层 LUT；失败返回 nullptr（调用方跳过该图层）。
    const Lut3D* adjustmentLutFor(const std::string& path);
};

}  // namespace core
}  // namespace nairoclip
