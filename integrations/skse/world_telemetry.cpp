#include "integrations/skse/world_telemetry.h"

namespace zelrim::skse {
namespace {
SkyrimTelemetryInput invalid(protocol::SkyrimInvalidationReason reason, std::uint32_t bits = 0) {
    SkyrimTelemetryInput value{};
    value.validity = bits;
    value.invalidationReason = reason;
    return value;
}
}
void WorldTelemetryState::preLoad() noexcept {
    playable_ = false; loading_ = true; settling_ = false; haveIdentity_ = false;
}
void WorldTelemetryState::postLoad(bool success) noexcept {
    playable_ = success; loading_ = false; settling_ = success; haveIdentity_ = false;
}
void WorldTelemetryState::newGame() noexcept {
    playable_ = true; loading_ = false; settling_ = true; haveIdentity_ = false;
}
void WorldTelemetryState::reset() noexcept {
    playable_ = loading_ = settling_ = haveIdentity_ = false;
}
SkyrimTelemetryInput WorldTelemetryState::capture(const WorldSample& sample, bool paused) noexcept {
    if (loading_) return invalid(protocol::SkyrimInvalidationReason::Loading, protocol::kSkyrimLoading);
    if (!playable_) return invalid(protocol::SkyrimInvalidationReason::NoPlayableContext);
    if (paused) return invalid(protocol::SkyrimInvalidationReason::Paused, protocol::kSkyrimPaused);
    if (settling_) {
        if (!sample.playerPresent || !sample.cellPresent)
            return invalid(protocol::SkyrimInvalidationReason::Loading, protocol::kSkyrimLoading);
        settling_ = false; haveIdentity_ = true;
        cellFormId_ = sample.cellFormId; worldspaceFormId_ = sample.worldspaceFormId;
        return invalid(protocol::SkyrimInvalidationReason::Loading, protocol::kSkyrimLoading);
    }
    if (!sample.playerPresent) return invalid(protocol::SkyrimInvalidationReason::NoPlayer);
    if (!sample.cellPresent) return invalid(protocol::SkyrimInvalidationReason::NoCell,
        protocol::kSkyrimContextPlayable | protocol::kSkyrimPlayerPresent);
    if (haveIdentity_ && (sample.cellFormId != cellFormId_ || sample.worldspaceFormId != worldspaceFormId_)) {
        cellFormId_ = sample.cellFormId; worldspaceFormId_ = sample.worldspaceFormId;
        return invalid(protocol::SkyrimInvalidationReason::CellOrWorldspaceChanged,
            protocol::kSkyrimTransition);
    }
    haveIdentity_ = true;
    cellFormId_ = sample.cellFormId; worldspaceFormId_ = sample.worldspaceFormId;
    SkyrimTelemetryInput value{};
    value.validity = protocol::kSkyrimContextPlayable | protocol::kSkyrimPlayerPresent |
                     protocol::kSkyrimCellPresent;
    value.invalidationReason = protocol::SkyrimInvalidationReason::None;
    value.playerFormId = sample.playerFormId; value.cellFormId = sample.cellFormId;
    value.worldspaceFormId = sample.worldspaceFormId;
    value.positionX = sample.positionX; value.positionY = sample.positionY; value.positionZ = sample.positionZ;
    value.rotationX = sample.rotationX; value.rotationY = sample.rotationY; value.rotationZ = sample.rotationZ;
    return value;
}
}
