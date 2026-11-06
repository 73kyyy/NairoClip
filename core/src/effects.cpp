#include "nairoclip/core/light_effects.h"
#include "nairoclip/core/effects.h"
#include "nairoclip/core/thread_pool.h"

#include "nairoclip/core/atmosphere_effects.h"
#include "nairoclip/core/motion_effects.h"
#include "nairoclip/core/person_effects.h"
#include "nairoclip/core/screen_effects.h"
#include "nairoclip/core/spatial_effects.h"

#include <algorithm>

namespace nairoclip {
namespace core {

EffectSpec effectSpec(EffectType type) {
    switch (type) {
        case EffectType::None: return EffectSpec{0.0, 0.0, 0.0, "无", "", ""};
        case EffectType::Brightness: return EffectSpec{-1.0, 1.0, 0.0, "亮度", "", "整体提亮或压暗；+1 约两档曝光，-1 约压两档"};
        case EffectType::Contrast: return EffectSpec{0.0, 3.0, 1.0, "对比度", "", "1 = 原始；>1 明暗反差更强，<1 更平"};
        case EffectType::Saturation: return EffectSpec{0.0, 3.0, 1.0, "饱和度", "", "1 = 原始；0 = 黑白；>1 颜色更艳"};
        case EffectType::Temperature: return EffectSpec{-1.0, 1.0, 0.0, "色温", "", "负数偏冷（蓝），正数偏暖（橙）"};
        case EffectType::Tint: return EffectSpec{-1.0, 1.0, 0.0, "色调", "", "负数偏绿，正数偏品红"};
        case EffectType::Grayscale: return EffectSpec{0.0, 1.0, 0.0, "灰度", "", "1 = 完全黑白"};
        case EffectType::Sepia: return EffectSpec{0.0, 1.0, 0.0, "复古", "", "1 = 完全复古棕调"};
        case EffectType::Vignette: return EffectSpec{0.0, 1.0, 0.0, "暗角", "", "四角压暗的程度；1 最重"};
        case EffectType::GaussianBlur: return EffectSpec{0.0, 100.0, 0.0, "高斯模糊", "px", "模糊半径（像素）：数值越大越糊"};
        case EffectType::Sharpen: return EffectSpec{0.0, 5.0, 0.0, "锐化", "", "锐化强度；过大出现白边"};
        case EffectType::Grain: return EffectSpec{0.0, 1.0, 0.0, "噪点", "", "胶片颗粒感；1 最明显"};
        case EffectType::Fade: return EffectSpec{0.0, 1.0, 0.0, "褪色", "", "提黑位、降对比，胶片褪色感"};
        // ---- 画面特效（文档第二段 5.1）----
        case EffectType::Glitch: return EffectSpec{0.0, 1.0, 0.0, "故障", "", "RGB 通道偏移 + 色块替换，故障艺术风"};
        case EffectType::SplitScreen: return EffectSpec{0.0, 1.0, 0.0, "分屏", "", "2×2 宫格，每格一份完整画面"};
        case EffectType::Mirror: return EffectSpec{0.0, 1.0, 0.0, "镜像", "", "左右翻转"};
        case EffectType::RadialBlur: return EffectSpec{0.0, 1.0, 0.0, "旋转模糊", "", "绕画面中心做角度偏移采样"};
        case EffectType::Scanlines: return EffectSpec{0.0, 1.0, 0.0, "扫描线", "", "周期性横向暗纹，CRT 显示器感"};
        case EffectType::RgbSplit: return EffectSpec{0.0, 1.0, 0.0, "RGB 分离", "", ""};
        // ---- 氛围特效（文档第二段 5.3）----
        case EffectType::Snow: return EffectSpec{0.0, 1.0, 0.0, "雪花", "", "粒子引擎：下落速度与密度随强度增加"};
        case EffectType::Rain: return EffectSpec{0.0, 1.0, 0.0, "下雨", "", "粒子引擎：雨丝密度随强度增加"};
        case EffectType::LightLeak: return EffectSpec{0.0, 1.0, 0.0, "光斑", "", "胶片漏光：光斑扫过并闪烁"};
        case EffectType::OldFilm: return EffectSpec{0.0, 1.0, 0.0, "老电影", "", "划痕 + 画面抖动 + 颗粒"};
        case EffectType::Starburst: return EffectSpec{0.0, 1.0, 0.0, "星光", "", "高光位置的十字星芒"};
        // ---- 动感特效（文档第二段 5.4）----
        case EffectType::ZoomPunch: return EffectSpec{0.0, 1.0, 0.0, "缩放冲击", "", "按节拍做脉冲式缩放，冲击感"};
        case EffectType::WhipPan: return EffectSpec{0.0, 1.0, 0.0, "甩镜", "", "方向性模糊，模拟快速转镜头"};
        case EffectType::BeatFlicker: return EffectSpec{0.0, 1.0, 0.0, "变速闪烁", "", "随节拍闪一下亮度"};
        case EffectType::BeatRhythm: return EffectSpec{0.0, 1.0, 0.0, "节奏律动", "", "整体随节拍做律动"};
        case EffectType::MotionBlur: return EffectSpec{0.0, 1.0, 0.0, "运动模糊", "", "多帧叠加；在帧序列层处理，导出时生效"};
        // ---- 画面特效补充（文档 Part 4-B 5.2）----
        case EffectType::Flash: return EffectSpec{0.0, 1.0, 0.0, "闪光", "", "整帧瞬时提亮"};
        case EffectType::Mosaic: return EffectSpec{0.0, 1.0, 0.0, "马赛克", "", "像素化块替换；数值越大块越大"};
        // ---- 人物特效（文档 Part 4-B 5.2「人物特效」）----
        case EffectType::Jitter: return EffectSpec{0.0, 1.0, 0.0, "抖动", "", "整帧随机小幅位移"};
        case EffectType::ZoomIn: return EffectSpec{0.0, 1.0, 0.0, "放大", "", "中心区域局部放大采样"};
        case EffectType::EdgeDetect: return EffectSpec{0.0, 1.0, 0.0, "边缘检测", "", "Sobel 边缘提取，线稿感"};
        case EffectType::Slim: return EffectSpec{0.0, 1.0, 0.0, "瘦身", "", "人像区域横向压缩（需先抠像得到 Alpha）"};
        case EffectType::GlowOutline: return EffectSpec{0.0, 1.0, 0.0, "发光轮廓", "", "抠像边缘高斯模糊后叠加发光（需 Alpha）"};
        case EffectType::Shake: return EffectSpec{0.0, 1.0, 0.0, "震动", "", "震动；完整参数在震动面板里调"};
        case EffectType::Bloom: return EffectSpec{0.0, 1.0, 0.0, "泛光", "", "高光外溢：灯、反光、白字会发光"};
        case EffectType::LensFlare: return EffectSpec{0.0, 1.0, 0.0, "镜头光晕", "", "光源圆盘 + 沿光线方向的鬼影串"};
    }
    return EffectSpec{};
}

const char* effectName(EffectType type) {
    return effectSpec(type).name;
}

Result EffectStack::add(Effect effect) {
    if (effect.type == EffectType::None) {
        return Result::fail(ErrorCode::InvalidArgument, "特效类型不能为空");
    }
    if (!effect.valid()) {
        return Result::fail(ErrorCode::InvalidArgument, "特效参数超出合法范围");
    }
    for (auto& e : effects_) {
        if (e.id == effect.id) {
            e = effect;
            return Result::ok();
        }
    }
    effects_.push_back(effect);
    return Result::ok();
}

bool EffectStack::remove(EffectId id) {
    const auto it = std::find_if(effects_.begin(), effects_.end(),
                                 [id](const Effect& e) { return e.id == id; });
    if (it == effects_.end()) {
        return false;
    }
    effects_.erase(it);
    return true;
}

bool EffectStack::moveUp(EffectId id) {
    for (std::size_t i = 0; i < effects_.size(); ++i) {
        if (effects_[i].id == id && i > 0) {
            std::swap(effects_[i], effects_[i - 1]);
            return true;
        }
    }
    return false;
}

bool EffectStack::moveDown(EffectId id) {
    for (std::size_t i = 0; i < effects_.size(); ++i) {
        if (effects_[i].id == id && i + 1 < effects_.size()) {
            std::swap(effects_[i], effects_[i + 1]);
            return true;
        }
    }
    return false;
}

bool EffectStack::indexOf(EffectId id, std::size_t* outIndex) const {
    for (std::size_t i = 0; i < effects_.size(); ++i) {
        if (effects_[i].id == id) {
            if (outIndex != nullptr) {
                *outIndex = i;
            }
            return true;
        }
    }
    return false;
}

bool EffectStack::moveTo(EffectId id, std::size_t toIndex) {
    std::size_t from = 0;
    if (!indexOf(id, &from)) {
        return false;
    }
    if (effects_.empty()) {
        return false;
    }
    const std::size_t last = effects_.size() - 1;
    const std::size_t to = toIndex > last ? last : toIndex;
    if (from == to) {
        return false;
    }
    // 取出后插入：等价于「整段位移」，中间元素相对顺序保持不变。
    Effect moved = effects_[from];
    effects_.erase(effects_.begin() + static_cast<std::ptrdiff_t>(from));
    effects_.insert(effects_.begin() + static_cast<std::ptrdiff_t>(to), moved);
    return true;
}

void EffectStack::clear() { effects_.clear(); }

Effect* EffectStack::find(EffectId id) {
    for (auto& e : effects_) {
        if (e.id == id) return &e;
    }
    return nullptr;
}

const Effect* EffectStack::find(EffectId id) const {
    for (const auto& e : effects_) {
        if (e.id == id) return &e;
    }
    return nullptr;
}

bool EffectStack::invariantsHold() const {
    for (const auto& e : effects_) {
        if (!e.valid()) {
            return false;
        }
    }
    return true;
}

void ColorTransform::apply(double& r, double& g, double& b) const {
    // 1) 亮度：加法
    r += brightness;
    g += brightness;
    b += brightness;
    // 2) 对比度：围绕 0.5 缩放
    r = (r - 0.5) * contrast + 0.5;
    g = (g - 0.5) * contrast + 0.5;
    b = (b - 0.5) * contrast + 0.5;
    // 3) 色温：R/B 通道偏移
    r += temperature * 0.1;
    b -= temperature * 0.1;
    // 4) 色调：G/M 轴
    g += tint * 0.1;
    // 5) 饱和度：向灰度插值
    const double luma = 0.299 * r + 0.587 * g + 0.114 * b;
    r = luma + (r - luma) * saturation;
    g = luma + (g - luma) * saturation;
    b = luma + (b - luma) * saturation;
    // 6) 灰度：向 luma 插值
    if (grayscale > 0.0) {
        const double gray = 0.299 * r + 0.587 * g + 0.114 * b;
        r += (gray - r) * grayscale;
        g += (gray - g) * grayscale;
        b += (gray - b) * grayscale;
    }
    // 7) 复古
    if (sepia > 0.0) {
        const double sr = 0.393 * r + 0.769 * g + 0.189 * b;
        const double sg = 0.349 * r + 0.686 * g + 0.168 * b;
        const double sb = 0.272 * r + 0.534 * g + 0.131 * b;
        r += (sr - r) * sepia;
        g += (sg - g) * sepia;
        b += (sb - b) * sepia;
    }
    // 8) 褪色：向 0.5 灰雾提升
    if (fade > 0.0) {
        constexpr double kFadeLift = 0.05;
        r += (0.5 - r) * fade * kFadeLift * 2.0;
        g += (0.5 - g) * fade * kFadeLift * 2.0;
        b += (0.5 - b) * fade * kFadeLift * 2.0;
    }
    // 钳制
    r = r < 0.0 ? 0.0 : (r > 1.0 ? 1.0 : r);
    g = g < 0.0 ? 0.0 : (g > 1.0 ? 1.0 : g);
    b = b < 0.0 ? 0.0 : (b > 1.0 ? 1.0 : b);
}

void applyColorTransformToRgba(std::uint8_t* pixels, std::size_t pixelCount,
                               const ColorTransform& transform) {
    /* 逐像素调色（P3 并行化）。
       -----------------------------------------------------------------
       为什么能安全并行：每个像素只读写自己的 4 字节，**没有任何跨像素依赖**
       （对比：模糊/锐化需要读取邻居，不能用这个切法）。
       按**像素区间**切分而不是按行：本函数拿不到高度，也不需要 ——
       区间边界对齐到 4 字节（RGBA），各段互不重叠。

       为什么结果 bit-exact：每个像素的计算与并行与否完全无关
       （同一组输入 → 同一组浮点运算 → 同一结果）。不存在「浮点累加顺序
       变化导致末位不同」的问题，因为根本没有跨像素的累加。

       小图（< 64K 像素）线程池会退化为同步执行，调度开销不反超。 */
    if (pixels == nullptr || pixelCount == 0) return;

    ThreadPool::instance().parallelFor(
        pixelCount, ThreadPool::kMinPixelsForParallel / 4,
        [&](std::size_t begin, std::size_t end) {
            for (std::size_t i = begin; i < end; ++i) {
                double r = static_cast<double>(pixels[i * 4]) / 255.0;
                double g = static_cast<double>(pixels[i * 4 + 1]) / 255.0;
                double b = static_cast<double>(pixels[i * 4 + 2]) / 255.0;
                transform.apply(r, g, b);
                pixels[i * 4] = static_cast<std::uint8_t>(r * 255.0 + 0.5);
                pixels[i * 4 + 1] = static_cast<std::uint8_t>(g * 255.0 + 0.5);
                pixels[i * 4 + 2] = static_cast<std::uint8_t>(b * 255.0 + 0.5);
                // alpha 保持不变
            }
        });
}

ColorTransform evaluateColorTransform(const EffectStack& stack) {
    ColorTransform t;
    for (const auto& e : stack.effects()) {
        if (!e.enabled) {
            continue;
        }
        switch (e.type) {
            case EffectType::Brightness: t.brightness += e.value; break;
            case EffectType::Contrast: t.contrast *= e.value; break;
            case EffectType::Saturation: t.saturation *= e.value; break;
            case EffectType::Temperature: t.temperature += e.value; break;
            case EffectType::Tint: t.tint += e.value; break;
            case EffectType::Grayscale:
                t.grayscale = std::max(t.grayscale, e.value);
                break;
            case EffectType::Sepia: t.sepia = std::max(t.sepia, e.value); break;
            case EffectType::Fade: t.fade = std::max(t.fade, e.value); break;
            case EffectType::Vignette:
            case EffectType::GaussianBlur:
            case EffectType::Sharpen:
            case EffectType::Grain:
            case EffectType::Bloom:       // 光效由 applyEffectStackToFrame 处理
            case EffectType::LensFlare:
            case EffectType::None:
                break;  // 空间效果由 applyEffectStackToFrame 统一处理
        }
    }
    return t;
}

bool applyEffectStackToFrame(std::uint8_t* pixels, int width, int height,
                             const EffectStack& stack, std::int64_t frameIndex,
                             const RenderQuality& quality, double timeSeconds,
                             const std::vector<Time>& beats) {
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    const std::size_t pixelCount =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    // 时间：调用方未传（< 0）时按 30fps 由帧序号推算（向后兼容旧调用方）
    const double time = timeSeconds >= 0.0
                            ? timeSeconds
                            : static_cast<double>(frameIndex) / 30.0;
    // 1) 颜色变换（逐像素，无邻域；多特效折叠求值）
    const ColorTransform transform = evaluateColorTransform(stack);
    if (!transform.isIdentity()) {
        applyColorTransformToRgba(pixels, pixelCount, transform);
    }
    // 2) 按栈序应用邻域/时变效果：
    //    - 空间效果（8..11）：暗角/模糊/锐化/噪点；
    //    - 画面特效（13..18 + 29..30）：故障/分屏/镜像/旋转模糊/扫描线/RGB 分离/闪光/马赛克；
    //    - 氛围特效（19..23）：雪花/下雨/光斑/老电影/星光；
    //    - 动感特效（24..27）：缩放冲击/甩镜/变速闪烁/节奏律动；
    //    - 人物特效（31..35）：抖动/放大/边缘检测/瘦身/发光轮廓。
    //    颜色类（1..7、12）已在上面处理；运动模糊（28）在帧序列层处理。
    for (const auto& e : stack.effects()) {
        if (!e.enabled) {
            continue;
        }
        const int typeValue = static_cast<int>(e.type);
        if (typeValue >= 8 && typeValue <= 11) {
            if (!applySpatialEffectToRgba(pixels, width, height, typeValue, e.value,
                                          frameIndex)) {
                return false;
            }
        } else if (typeValue >= 13 && typeValue <= 18) {
            if (!applyScreenEffectToRgba(pixels, width, height, typeValue, e.value,
                                         frameIndex, quality)) {
                return false;
            }
        } else if (typeValue >= 19 && typeValue <= 23) {
            if (!applyAtmosphereEffectToRgba(pixels, width, height, typeValue, e.value,
                                             time, quality)) {
                return false;
            }
        } else if (typeValue >= 24 && typeValue <= 28) {
            if (!applyMotionEffectToRgba(pixels, width, height, typeValue, e.value, time,
                                         beats, quality)) {
                return false;
            }
        } else if (typeValue >= 29 && typeValue <= 30) {
            // 画面特效补充：闪光/马赛克（文档 Part 4-B 5.2 预设清单）
            if (!applyScreenEffectToRgba(pixels, width, height, typeValue, e.value,
                                         frameIndex, quality)) {
                return false;
            }
        } else if (typeValue == 37 || typeValue == 38) {
            /* 光效（B1-3 收尾）：泛光 / 镜头光晕。
               ------------------------------------------------------------------
               实现（applyBloomToRgba / applyLensFlareToRgba）早就写好并有测试，
               一直缺的只是「枚举 + 这条分发」—— 于是那两个函数在产品里
               从来没有被调用过（有代码、没接线）。

               参数映射：Effect 只有单个 double value，所以 value 只控强度，
               其余参数用固定默认值（阈值 0.7 / 半径 12px / 中性色调；
               光源偏右上 / 中性偏暖）。这样它与其它 30 多个特效的交互一致
               （单滑杆）。要细调三个参数时走 applyBloomToRgba 的直接调用。

               value <= 0 视为关闭（与其它特效一致）。 */
            if (e.value <= 0.0) {
                continue;
            }
            if (typeValue == 37) {
                BloomParams params;             // 默认：阈值 0.7 / 半径 12 / 中性
                params.intensity = e.value;
                if (!applyBloomToRgba(pixels, width, height, params)) {
                    return false;
                }
            } else {
                LensFlareParams params;         // 默认：光源 (0.7,0.3) / 偏暖
                params.intensity = e.value;
                if (!applyLensFlareToRgba(pixels, width, height, params)) {
                    return false;
                }
            }
        } else if (typeValue >= 31 && typeValue <= 35) {
            // 人物特效：抖动/放大/边缘检测/瘦身/发光轮廓（文档 Part 4-B 5.2）
            // 说明：Alpha 参数暂由调用方传入——特效栈路径暂无抠像 Alpha 通道，
            //       传 nullptr（瘦身/发光轮廓零改动）；接入智能抠像后由管线贯通。
            if (!applyPersonEffectToRgba(pixels, width, height, typeValue, e.value, time,
                                         nullptr, quality)) {
                return false;
            }
        }
    }
    return true;
}

}  // namespace core
}  // namespace nairoclip
