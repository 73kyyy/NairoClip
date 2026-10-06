#include "nairoclip/core/animation.h"

namespace nairoclip {
namespace core {

PropertySpec propertySpec(ClipProperty property) {
    switch (property) {
        case ClipProperty::Opacity:
            return PropertySpec{1.0, 0.0, 1.0};
        case ClipProperty::Scale:
            return PropertySpec{1.0, 0.01, 100.0};
        case ClipProperty::PositionX:
            return PropertySpec{0.0, -10.0, 10.0};
        case ClipProperty::PositionY:
            return PropertySpec{0.0, -10.0, 10.0};
        case ClipProperty::Rotation:
            return PropertySpec{0.0, -3600.0, 3600.0};
        case ClipProperty::Volume:
            return PropertySpec{1.0, 0.0, 4.0};
    }
    return PropertySpec{};
}

const char* propertyName(ClipProperty property) {
    switch (property) {
        case ClipProperty::Opacity:
            return "透明度";
        case ClipProperty::Scale:
            return "缩放";
        case ClipProperty::PositionX:
            return "水平位置";
        case ClipProperty::PositionY:
            return "垂直位置";
        case ClipProperty::Rotation:
            return "旋转";
        case ClipProperty::Volume:
            return "音量";
    }
    return "未知属性";
}

namespace {

double clampToSpec(double value, const PropertySpec& spec) {
    if (value < spec.minValue) return spec.minValue;
    if (value > spec.maxValue) return spec.maxValue;
    return value;
}

}  // namespace

bool ClipAnimation::hasCurve(ClipProperty property) const {
    const KeyframeTrack* track = findCurve(property);
    return track != nullptr && !track->empty();
}

KeyframeTrack& ClipAnimation::curve(ClipProperty property) {
    switch (property) {
        case ClipProperty::Opacity: return opacity_;
        case ClipProperty::Scale: return scale_;
        case ClipProperty::PositionX: return positionX_;
        case ClipProperty::PositionY: return positionY_;
        case ClipProperty::Rotation: return rotation_;
        case ClipProperty::Volume: return volume_;
    }
    return opacity_;
}

const KeyframeTrack* ClipAnimation::findCurve(ClipProperty property) const {
    switch (property) {
        case ClipProperty::Opacity: return &opacity_;
        case ClipProperty::Scale: return &scale_;
        case ClipProperty::PositionX: return &positionX_;
        case ClipProperty::PositionY: return &positionY_;
        case ClipProperty::Rotation: return &rotation_;
        case ClipProperty::Volume: return &volume_;
    }
    return nullptr;
}

bool ClipAnimation::removeCurve(ClipProperty property) {
    const bool had = hasCurve(property);
    curve(property).clear();
    return had;
}

void ClipAnimation::clear() {
    opacity_.clear();
    scale_.clear();
    positionX_.clear();
    positionY_.clear();
    rotation_.clear();
    volume_.clear();
}

bool ClipAnimation::empty() const { return curveCount() == 0; }

std::size_t ClipAnimation::curveCount() const {
    std::size_t count = 0;
    if (!opacity_.empty()) ++count;
    if (!scale_.empty()) ++count;
    if (!positionX_.empty()) ++count;
    if (!positionY_.empty()) ++count;
    if (!rotation_.empty()) ++count;
    if (!volume_.empty()) ++count;
    return count;
}

double ClipAnimation::sample(ClipProperty property, const Time& localTime) const {
    const PropertySpec spec = propertySpec(property);
    const KeyframeTrack* track = findCurve(property);
    if (track == nullptr || track->empty()) {
        return spec.defaultValue;
    }
    return clampToSpec(track->sample(localTime, spec.defaultValue), spec);
}

bool ClipAnimation::invariantsHold() const {
    const KeyframeTrack* tracks[] = {&opacity_, &scale_, &positionX_,
                                     &positionY_, &rotation_, &volume_};
    const ClipProperty properties[] = {ClipProperty::Opacity, ClipProperty::Scale,
                                       ClipProperty::PositionX, ClipProperty::PositionY,
                                       ClipProperty::Rotation, ClipProperty::Volume};
    for (std::size_t i = 0; i < 6; ++i) {
        const KeyframeTrack* track = tracks[i];
        if (!track->invariantsHold()) {
            return false;
        }
        const PropertySpec spec = propertySpec(properties[i]);
        for (const auto& k : track->keyframes()) {
            if (k.value < spec.minValue || k.value > spec.maxValue) {
                return false;
            }
        }
    }
    return true;
}

}  // namespace core
}  // namespace nairoclip
