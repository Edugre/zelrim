#include "integrations/skse/movement_authority.h"
#include <cmath>
#include <cstring>

namespace zelrim::skse {
namespace {
constexpr float kRadiansPerBinaryAngle = 6.2831853071795864769f / 65536.0f;
bool finiteConfig(const MovementProofConfig& c) {
    return std::isfinite(c.scale) && c.scale > 0 && std::isfinite(c.yawSign) &&
        (c.yawSign == 1.0f || c.yawSign == -1.0f) &&
        std::isfinite(c.maxSampleDelta) && c.maxSampleDelta > 0 &&
        std::isfinite(c.visualOffsetX);
}
}
MovementDecision MovementAuthority::reset(MovementResetReason reason) noexcept {
    const bool wasCalibrated = calibrated_;
    calibrated_ = false;
    lastSequence_ = 0;
    std::memset(linkSession_, 0, sizeof(linkSession_));
    MovementDecision result;
    result.action = wasCalibrated ? MovementAction::Reset : MovementAction::None;
    result.resetReason = reason;
    return result;
}
MovementDecision MovementAuthority::update(const Status& status) noexcept {
    if (status.peer != PeerState::Connected || status.telemetry != TelemetryState::Usable)
        return reset(MovementResetReason::UnusableLink);
    if (status.skyrimTelemetry != TelemetryState::Usable)
        return reset(MovementResetReason::UnusableSkyrim);
    if (!finiteConfig(config_)) return reset(MovementResetReason::NonFiniteTransform);
    const auto& link = status.snapshot.link;
    const auto& sky = status.snapshot.skyrim;
    if (!calibrated_) {
        calibrated_ = true;
        std::memcpy(linkSession_, link.sessionId, sizeof(linkSession_));
        lastSequence_ = link.sequence;
        sceneId_ = link.sceneId; roomId_ = link.roomId; originYaw_ = link.shapeYaw;
        cellId_ = sky.cellFormId; worldspaceId_ = sky.worldspaceFormId;
        ootOriginX_ = link.positionX; ootOriginY_ = link.positionY; ootOriginZ_ = link.positionZ;
        previousOotX_ = link.positionX; previousOotY_ = link.positionY; previousOotZ_ = link.positionZ;
        previousYaw_ = link.shapeYaw;
        skyrimOriginX_ = sky.positionX + config_.visualOffsetX; skyrimOriginY_ = sky.positionY;
        skyrimOriginZ_ = sky.positionZ; skyrimOriginYaw_ = sky.rotationZ;
        return { MovementAction::Calibrate, MovementResetReason::None, link.sequence,
            skyrimOriginX_, sky.positionY, sky.positionZ, sky.rotationZ };
    }
    if (std::memcmp(linkSession_, link.sessionId, sizeof(linkSession_)) != 0)
        return reset(MovementResetReason::SessionChanged);
    if (link.sceneId != sceneId_ || link.roomId != roomId_ ||
        sky.cellFormId != cellId_ || sky.worldspaceFormId != worldspaceId_)
        return reset(MovementResetReason::SpatialContextChanged);
    if (link.sequence == lastSequence_) return {};
    if (link.sequence < lastSequence_) return reset(MovementResetReason::NonMonotonicSequence);
    const float dx = link.positionX - ootOriginX_;
    const float dy = link.positionY - ootOriginY_;
    const float dz = link.positionZ - ootOriginZ_;
    const float stepX = link.positionX - previousOotX_;
    const float stepY = link.positionY - previousOotY_;
    const float stepZ = link.positionZ - previousOotZ_;
    const float stepDistance = std::sqrt(stepX * stepX + stepY * stepY + stepZ * stepZ);
    if (!std::isfinite(stepDistance) || stepDistance > config_.maxSampleDelta)
        return reset(MovementResetReason::ImplausibleDelta);
    const float x = skyrimOriginX_ + dx * config_.scale;
    const float y = skyrimOriginY_ + dz * config_.scale;
    const float z = skyrimOriginZ_ + dy * config_.scale;
    const auto yawDelta = static_cast<std::int16_t>(
        static_cast<std::uint16_t>(link.shapeYaw) - static_cast<std::uint16_t>(originYaw_));
    const float yaw = skyrimOriginYaw_ + config_.yawSign * float(yawDelta) * kRadiansPerBinaryAngle;
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(yaw))
        return reset(MovementResetReason::NonFiniteTransform);
    const bool unchangedPose = stepX == 0.0f && stepY == 0.0f && stepZ == 0.0f &&
        link.shapeYaw == previousYaw_;
    lastSequence_ = link.sequence;
    previousOotX_ = link.positionX; previousOotY_ = link.positionY; previousOotZ_ = link.positionZ;
    previousYaw_ = link.shapeYaw;
    if (unchangedPose) return {};
    return { MovementAction::Apply, MovementResetReason::None, link.sequence, x, y, z, yaw };
}
const char* movementResetName(MovementResetReason reason) noexcept {
    switch (reason) {
    case MovementResetReason::UnusableLink: return "unusable-link";
    case MovementResetReason::UnusableSkyrim: return "unusable-skyrim";
    case MovementResetReason::SessionChanged: return "session-changed";
    case MovementResetReason::SpatialContextChanged: return "spatial-context-changed";
    case MovementResetReason::NonMonotonicSequence: return "non-monotonic-sequence";
    case MovementResetReason::ImplausibleDelta: return "implausible-delta";
    case MovementResetReason::NonFiniteTransform: return "non-finite-transform";
    default: return "none";
    }
}
}
