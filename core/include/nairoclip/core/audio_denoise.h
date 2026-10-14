#pragma once
// =============================================================================
// 音频降噪（Audio Denoise）—— 文档第 2 部分「🎵音频 → 降噪 → 开关/强度(弱中强)」
// =============================================================================
// 产品对应（原文）：
//   · 「降噪 → 开关/强度(弱中强)」（音频工具栏）；
//   · 文档第 8 部分将「降噪」列为本地 AI 功能之一（模型化路线）。
//
// 实现路线（本模块为**真实可测的经典 DSP**实现，零第三方依赖、无需推理引擎）：
//   · 谱减法（Spectral Subtraction，Boll 1979；语音增强行业标准）：
//       STFT（Hann 窗 1024 点、50% 重叠 OLA）→ 幅度谱减去噪声估计
//       （过减因子 α 与谱底 β 防止过度相减与音乐噪声）→ 保留相位 ISTFT；
//   · 噪声估计：逐频点「快速下降 / 慢速上升」不对称平滑
//       （mag < noise 时快速跟随，mag > noise 时缓慢上浮）——
//       经典最小值追踪的确定性简化，不需 VAD、不依赖采样率；
//   · 三档强度映射（过减因子 α / 谱底 β）：
//       Weak   α=1.3 β=0.12（温和：保留更多细节与自然噪声底）
//       Medium α=1.8 β=0.06（平衡）
//       Strong α=2.4 β=0.02（激进：噪声抑制更强，可能牺牲少量细节）
//   · 已知特性（谱减法的固有属性，非缺陷）：持续稳态纯音有被当作噪声
//     压制的倾向；口播/人声（幅度起伏、音节间隙）不受影响——
//     这正是本功能的目标场景（旁白/口播降噪）。
//
// 流式语义（时轴零偏移；与 AudioMixer 块式驱动一致）：
//   · WOLA 分析需要「半窗（512 帧）的未来数据」才能完成一个 hop 的重叠相加；
//     为保持输出与输入**逐样本时轴对齐**，本类要求调用方按
//     feedFramesFor(outFrames) 读取输入（首块多读半窗前视），产出量
//     与调用方期望输出帧数对应（稳态无净延迟、无时间偏移）；
//   · 精确产出规则：produced = min(desired, 内部可产出)，其中
//       desired = 本次喂入帧数 − 首块前视（首块 512，此后 0）；
//     当 outFrames 为半窗（512）整数倍时（如混音块 1024/2048），
//     逐块产出一一对应；其他尺寸首块可能略少（内部队列在后续块补足，
//     总样本数守恒）；
//   · 片段结束：以 isFinal 标记（内部右零填充完成尾部 hop；最终一次
//     产出恰好返回剩余样本，使 输入总帧数 == 输出总帧数）；
//   · 流开始：首 hop 自动左零填充（std 库同款边界处理）；
//   · 多通道：每通道独立处理（不假设声道相关性）；
//   · 确定性：同一输入序列 → 同一输出（逐样本一致，可测）。
//
// 职责边界（与全项目一致）：
//   · core 负责算法（纯 DSP、可测试）；降噪是**片段级**音频属性
//     （Clip::denoise，与 audioGainDb 并列）；
//   · 导出混音在 AudioMixer 内按片段逐块应用；
//   · 模型化降噪（神经降噪）后续经推理接口注入替换，对外语义不变。

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nairoclip {
namespace core {

/// 降噪强度（文档「开关/强度(弱中强)」）。
enum class DenoiseStrength {
    Off = 0,     ///< 关闭
    Weak = 1,    ///< 弱
    Medium = 2,  ///< 中
    Strong = 3,  ///< 强
};

/// 强度名称（"off" / "weak" / "medium" / "strong"）。
const char* denoiseStrengthName(DenoiseStrength strength);
/// 从字符串解析（大小写不敏感；支持 "弱"/"中"/"强"；非法返回 false）。
bool denoiseStrengthFromString(const char* text, DenoiseStrength& out);
/// 从整数解析（0..3；非法返回 false）。
bool denoiseStrengthFromInt(int value, DenoiseStrength& out);

/// 强度对应的 DSP 参数（导出以便测试与 UI 说明）。
struct DenoiseTuning {
    /// 过减因子 α（噪声谱估计的倍数；>1 表示过度相减以抑制残留）。
    double overSubtraction = 1.8;
    /// 谱底 β（相减后幅度下限 = β × 原幅度；防止过度相减产生空洞/音乐噪声）。
    double spectralFloor = 0.06;
};

/// 强度 → 参数映射（见头文件顶部说明的取值表）。
/// 说明：Off 返回 α=1.0、β=1.0（数学等价于直通，供统一公式路径使用）。
DenoiseTuning denoiseTuningFor(DenoiseStrength strength);

/// 流式音频降噪器（状态型；单实例单线程使用）。
///
/// 用法（AudioMixer 集成模式）：
///   1. 构造：AudioDenoiser denoiser(strength, channels)；
///   2. 每块：feed = denoiser.feedFramesFor(outFrames)；
///      读取 feed 帧源样本（不足处以静音补齐并置 isFinal）；
///      produced = denoiser.pushAndPull(input, feed, isFinal, output)；
///      produced 在稳态应等于 outFrames（首块/尾块见下方规则）；
///   3. 片段跳转/重新开始：denoiser.reset()。
///
/// 说明：
///   · strength == Off 时 pushAndPull 为直通（输出 = 输入前 outFrames 帧）；
///   · 输出帧数与「已喂入帧数 − 半窗前视 + 已完成 hop」严格对应，
///     保证内容相对源的时间位置逐样本对齐（无 A/V 偏移）。
class AudioDenoiser {
public:
    /// channels：交错声道数（>= 1）；非法值构造为「直通」状态。
    AudioDenoiser(DenoiseStrength strength, int channels);

    /// 本块为产出 desiredOutputFrames 帧，调用方应喂入的帧数。
    /// - 首块 = desired + 半窗（建立前视）；稳态 = desired（前视已被内部持有）；
    /// - Off / 直通状态 = desired（无前视需求）。
    int feedFramesFor(int desiredOutputFrames) const;

    /// 喂入 inputFrames 帧（交错 float）→ 产出（追加写入 output，交错）。
    /// - inputFrames 一般应等于 feedFramesFor(desired)（源不足时可少）；
    /// - isFinal：源是否已结束（true 时内部用右零填充完成剩余 hop，
    ///   并把剩余样本全部产出，保证总样本守恒）；
    /// - 返回产出帧数；输出样本数 = 返回帧数 × channels。
    std::size_t pushAndPull(const float* input, int inputFrames, bool isFinal,
                            std::vector<float>& output);

    /// 清除全部状态（噪声估计/缓冲）；强度与通道数不变。
    void reset();

    /// 当前是否启用降噪（strength != Off 且参数合法）。
    bool active() const { return active_; }
    /// 采样声道数。
    int channels() const { return channels_; }
    /// 强度。
    DenoiseStrength strength() const { return strength_; }

    /// 分析窗长（样本；固定 1024，50% 重叠）。供文档与测试引用。
    static constexpr int analysisWindow() { return 1024; }
    /// hop（样本；固定 512）。
    static constexpr int hopSize() { return 512; }

private:
    /// 每通道 DSP 状态。
    struct ChannelState {
        std::vector<double> inFifo;    ///< 输入流缓冲（覆盖 [inFifoStart, inFifoStart+size)）
        std::vector<double> acc;       ///< OLA 累加缓冲（2H，覆盖 [anchor, anchor+2H)）
        std::vector<double> wsum;      ///< OLA 权重和（窗平方累加，同 acc 区间）
        std::vector<double> noiseMag;  ///< 噪声幅度谱估计（N/2+1 个频点）
        std::vector<double> fftRe;     ///< FFT 实部（N）
        std::vector<double> fftIm;     ///< FFT 虚部（N）
        /// 已处理帧数（帧 p 覆盖 [pH-H, pH+H)）。
        long long processedFrames = 0;
        /// inFifo 起点对应的绝对样本位置（随时间右移；始终 >= 0）。
        long long inFifoStart = 0;
        /// OLA 缓冲 anchor（绝对样本位置；= 已发射 hop 数 × H）。
        long long anchor = 0;
        /// 噪声估计是否已初始化。
        bool noiseInitialized = false;
    };

    /// 追加交错输入到各通道 inFifo（input 可为 null）。
    void appendInput(const float* input, int frames);
    /// 处理一帧（p = processedFrames）：从 inFifo 取窗、FFT、谱减、IFFT、OLA。
    void processFrame(ChannelState& state);
    /// 处理「已就绪」的帧（所有通道同步）并在帧完成时发射 hop：
    ///   · 非终块：帧 p 右边界 (p+1)H <= 已喂入量（半窗前视就绪）；
    ///   · 终块  ：帧左边界 (p-1)H <  已喂入量（右部以零填充完成尾部），
    ///             且完整 hop 只发射到 floor(F/H) - 1（其余走尾部残余）；
    ///   每帧处理后立即丢弃不再需要的输入（< pH），保证内存有界。
    void processFrames(bool isFinal);
    /// 发射一个完整 hop（所有通道同步入队 + 滑动缓冲；emittedFrames_ += H）。
    void emitHop();
    /// 终块残余（tail < H 且 > 0）：把 acc[0..tail) 归一化后入队（所有通道）。
    void emitTailPartial(long long tail);
    /// 从各通道产出队列取 desiredFrames 帧（不足时取到队列空），交错写入 output。
    std::size_t drainOutput(int desiredFrames, std::vector<float>& output);

    DenoiseStrength strength_ = DenoiseStrength::Off;
    int channels_ = 1;
    bool active_ = false;
    DenoiseTuning tuning_;
    std::vector<ChannelState> states_;             ///< 每通道一个状态
    std::vector<std::vector<double>> outChannels_; ///< 每通道已产出样本队列
    long long fedFrames_ = 0;         ///< 累计喂入帧数（全通道）
    long long emittedFrames_ = 0;     ///< 累计已发射样本数（完整 hop + 残余；全通道）
    long long deliveredFrames_ = 0;   ///< 累计交付调用方的帧数（drainOutput）
    bool firstPull_ = true;           ///< 是否尚未发生首次 pushAndPull（首块前视）
    bool finalReceived_ = false;      ///< 是否已收到 isFinal（此后幂等）
};

}  // namespace core
}  // namespace nairoclip
