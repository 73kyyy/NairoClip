#pragma once

#include "nairoclip/core/animation.h"
#include "nairoclip/core/effects.h"
#include "nairoclip/core/mask.h"
#include "nairoclip/core/speed_curve.h"
#include "nairoclip/core/rational.h"
#include "nairoclip/core/time.h"
#include "nairoclip/core/types.h"

#include <string>

namespace nairoclip {
namespace core {

/// 时间轴上的一个片段。
struct Clip {
    ClipId id;
    AssetId asset;    // 引用的素材（可为空，如文字片段）
    /// 嵌套序列引用（复合片段：非空时本片段代表另一条序列的渲染结果，
    /// 文档第 3 部分「嵌套序列/复合片段」）。
    /// 语义：asset 与 nestedSequence 二选一（嵌套时 asset 应为空）；
    /// duration 由嵌套序列的内容时长决定（创建时写入，解除时还原）。
    SequenceId nestedSequence;
    /// 复合片段名称（可选；空则用嵌套序列名）。用于时间轴显示「📦复合片段A」。
    std::string compoundName;
    Time start;       // 时间轴起点
    Time duration;    // 时间轴时长
    Time sourceIn;    // 源素材入点
    Rational speed = Rational::make(1);  // 播放速度倍率（1 = 原速）
    bool enabled = true;
    /// 动画通道（透明度/缩放/位移/旋转/音量），采样用「片段本地时间」。
    ClipAnimation animation;
    /// 速度曲线（可选）。空曲线 = 恒速（使用 speed 字段）。
    /// 非空时：时间轴时长由曲线积分决定，speed 字段作为基准速度乘数。
    SpeedCurve speedCurve;
    /// 视频特效栈（按顺序应用；空间效果由渲染器处理）。
    EffectStack effects;
    /// 片段蒙版（文档「✂️剪辑 → 蒙版：线性/镜面/圆形/矩形（羽化/反转）」）。
    /// hasMask 为 true 时导出/预览按蒙版裁剪可见区域；false = 不使用蒙版。
    bool hasMask = false;
    Mask mask;
    /// 音频增益（dB，0 = 原始电平；合法范围 [-60, +12]）。
    double audioGainDb = 0.0;
    /// 音频淡入时长（从片段起点算起；0 = 无淡入）。
    Time audioFadeIn;
    /// 音频淡出时长（至片段终点；0 = 无淡出）。
    Time audioFadeOut;
    /// 音频属性是否合法：增益在范围内、淡入淡出不超过片段时长且非负、
    /// 淡入 + 淡出不超过片段时长。
    bool audioValid() const {
        if (audioGainDb < -60.0 || audioGainDb > 12.0) return false;
        if (audioFadeIn.isNegative() || audioFadeOut.isNegative()) return false;
        if (audioFadeIn + audioFadeOut > duration) return false;
        return true;
    }
    /// 采样本地时间处的淡入淡出系数（0..1），未考虑增益。
    double audioFadeFactor(const Time& localTime) const {
        double factor = 1.0;
        if (audioFadeIn.isPositive() && localTime < audioFadeIn) {
            factor *= localTime.toDouble() / audioFadeIn.toDouble();
        }
        if (audioFadeOut.isPositive()) {
            const Time fadeOutStart = duration - audioFadeOut;
            if (localTime > fadeOutStart) {
                const double remain = (duration - localTime).toDouble();
                factor *= remain / audioFadeOut.toDouble();
            }
        }
        if (factor < 0.0) return 0.0;
        if (factor > 1.0) return 1.0;
        return factor;
    }
    /// 时间轴时刻 → 片段本地时间。
    Time toLocalTime(const Time& timelineTime) const { return timelineTime - start; }

    /// 时间轴终点。
    Time end() const { return start + duration; }

    /// 是否为复合片段（引用嵌套序列）。
    bool isCompound() const { return nestedSequence.isValid(); }

    /// 消耗的源素材范围终点：sourceIn + duration * speed。
    Time sourceOut() const { return sourceIn + duration * speed; }

    /// [start, end) 是否包含时刻 t。
    bool contains(const Time& t) const { return start <= t && t < end(); }

    /// 片段参数是否合法：时长 > 0、速度 > 0、起点/入点不为负。
    bool valid() const;
};

}  // namespace core
}  // namespace nairoclip