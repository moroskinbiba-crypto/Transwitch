#include "../include/desknx/MotionController.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>

int main() {
    using namespace desknx::ui;

    MotionController motion;
    const auto full=motion.begin(0.0f,1.0f,1000,160,MotionCurve::EaseOutCubic);
    assert(full.durationMs==160);
    assert(std::fabs(motion.sample(full,1000)-0.0f)<0.001f);
    assert(std::fabs(motion.sample(full,1080)-0.875f)<0.001f);
    assert(std::fabs(motion.sample(full,1160)-1.0f)<0.001f);
    assert(motion.active(full,1159));
    assert(!motion.active(full,1160));

    motion.setMode(MotionMode::Reduced);
    const auto reduced=motion.begin(0.0f,1.0f,2000,160);
    assert(reduced.durationMs==0);
    assert(motion.sample(reduced,2000)==1.0f);
    assert(!motion.active(reduced,2000));

    motion.setMode(MotionMode::LowPerformance);
    const auto low=motion.begin(0.0f,1.0f,3000,160,MotionCurve::Linear);
    assert(low.durationMs==80);
    assert(std::fabs(motion.sample(low,3040)-0.5f)<0.001f);
    assert(motion.sample(low,3080)==1.0f);

    // Reset the previous LowPerformance policy: the midpoint assertion below
    // deliberately checks the full 100 ms transition.
    motion.setMode(MotionMode::Full);
    const auto inOut=motion.begin(0.0f,1.0f,4000,100,MotionCurve::EaseInOutCubic);
    assert(std::fabs(motion.sample(inOut,4050)-0.5f)<0.001f);

    // Unsigned elapsed time remains correct when the platform tick counter wraps.
    const auto wrapped=motion.begin(0.0f,1.0f,0xFFFFFFF0u,64,MotionCurve::Linear);
    assert(std::fabs(motion.sample(wrapped,0x00000010u)-0.5f)<0.001f);

    MotionMode persisted=MotionMode::Full;
    assert(ParseMotionModeSetting("reduced",persisted));
    assert(persisted==MotionMode::Reduced);
    assert(std::strcmp(MotionModeSettingName(persisted),"reduced")==0);
    assert(ParseMotionModeSetting("lowperformance",persisted));
    assert(persisted==MotionMode::LowPerformance);
    assert(std::strcmp(MotionModeSettingName(persisted),"lowperformance")==0);
    assert(ParseMotionModeSetting("full",persisted));
    assert(persisted==MotionMode::Full);
    assert(std::strcmp(MotionModeSettingName(persisted),"full")==0);
    assert(!ParseMotionModeSetting("ultra",persisted));
    assert(!ParseMotionModeSetting(nullptr,persisted));
    return 0;
}
