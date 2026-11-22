#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace desknx::ui {

enum class MotionMode : std::uint8_t {
    Full,
    Reduced,
    LowPerformance
};

enum class MotionCurve : std::uint8_t {
    Linear,
    EaseOutCubic,
    EaseInOutCubic
};

struct MotionTrack {
    float from{0.0f};
    float to{1.0f};
    std::uint32_t startedAt{0};
    std::uint32_t durationMs{0};
    MotionCurve curve{MotionCurve::EaseOutCubic};
};

inline const char* MotionModeSettingName(MotionMode mode) noexcept {
    switch(mode) {
        case MotionMode::Reduced:return "reduced";
        case MotionMode::LowPerformance:return "lowperformance";
        case MotionMode::Full:
        default:return "full";
    }
}

inline bool ParseMotionModeSetting(const char* value,MotionMode& result) noexcept {
    if(!value)return false;
    if(std::strcmp(value,"full")==0)result=MotionMode::Full;
    else if(std::strcmp(value,"reduced")==0)result=MotionMode::Reduced;
    else if(std::strcmp(value,"lowperformance")==0)result=MotionMode::LowPerformance;
    else return false;
    return true;
}

class MotionController {
public:
    void setMode(MotionMode mode) noexcept { mode_=mode; }
    MotionMode mode() const noexcept { return mode_; }

    std::uint32_t resolveDuration(std::uint32_t requestedMs) const noexcept {
        switch(mode_) {
            case MotionMode::Reduced:
                return 0;
            case MotionMode::LowPerformance:
                return std::min<std::uint32_t>(requestedMs,80);
            case MotionMode::Full:
            default:
                return requestedMs;
        }
    }

    MotionTrack begin(float from,float to,std::uint32_t now,
                      std::uint32_t durationMs,
                      MotionCurve curve=MotionCurve::EaseOutCubic) const noexcept {
        return {from,to,now,resolveDuration(durationMs),curve};
    }

    float sample(const MotionTrack& track,std::uint32_t now) const noexcept {
        if(track.durationMs==0)return track.to;
        const std::uint32_t elapsed=now-track.startedAt;
        if(elapsed>=track.durationMs)return track.to;
        const float linear=static_cast<float>(elapsed)/
            static_cast<float>(track.durationMs);
        float eased=linear;
        switch(track.curve) {
            case MotionCurve::Linear:
                break;
            case MotionCurve::EaseInOutCubic:
                eased=linear<0.5f
                    ?4.0f*linear*linear*linear
                    :1.0f-((-2.0f*linear+2.0f)*(-2.0f*linear+2.0f)*
                           (-2.0f*linear+2.0f))/2.0f;
                break;
            case MotionCurve::EaseOutCubic:
            default: {
                const float inverse=1.0f-linear;
                eased=1.0f-inverse*inverse*inverse;
                break;
            }
        }
        return track.from+(track.to-track.from)*eased;
    }

    bool active(const MotionTrack& track,std::uint32_t now) const noexcept {
        return track.durationMs>0&&
            static_cast<std::uint32_t>(now-track.startedAt)<track.durationMs;
    }

private:
    MotionMode mode_{MotionMode::Full};
};

} // namespace desknx::ui
