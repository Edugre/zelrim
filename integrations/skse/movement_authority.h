#pragma once
#include "bridge/bridge.h"
#include <cstdint>

namespace zelrim::skse {
struct MovementProofConfig {
    float scale = 1.0f;
    float yawSign = 1.0f;
    float maxSampleDelta = 1000.0f;
};
enum class MovementAction { None, Calibrate, Apply, Reset };
enum class MovementResetReason {
    None, UnusableLink, UnusableSkyrim, SessionChanged, SpatialContextChanged,
    NonMonotonicSequence, ImplausibleDelta, NonFiniteTransform,
};
struct MovementDecision {
    MovementAction action = MovementAction::None;
    MovementResetReason resetReason = MovementResetReason::None;
    std::uint64_t linkSequence = 0;
    float positionX = 0, positionY = 0, positionZ = 0;
    float rotationZ = 0;
};
class MovementAuthority {
public:
    explicit MovementAuthority(MovementProofConfig config = {}) : config_(config) {}
    MovementDecision update(const Status& status) noexcept;
    MovementDecision reset(MovementResetReason reason) noexcept;
    bool calibrated() const noexcept { return calibrated_; }
private:
    MovementProofConfig config_;
    bool calibrated_ = false;
    std::uint8_t linkSession_[16]{};
    std::uint64_t lastSequence_ = 0;
    std::int16_t sceneId_ = 0, roomId_ = 0, originYaw_ = 0;
    std::uint32_t cellId_ = 0, worldspaceId_ = 0;
    float ootOriginX_ = 0, ootOriginY_ = 0, ootOriginZ_ = 0;
    float previousOotX_ = 0, previousOotY_ = 0, previousOotZ_ = 0;
    float skyrimOriginX_ = 0, skyrimOriginY_ = 0, skyrimOriginZ_ = 0, skyrimOriginYaw_ = 0;
};
const char* movementResetName(MovementResetReason reason) noexcept;
}
