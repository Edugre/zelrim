#pragma once
#include "protocol/bridge_protocol.h"
#include <windows.h>
#include <cstdint>

namespace zelrim {
enum class Side { Skyrim, Oot };
enum class PeerState { Disconnected, Connected, TimedOut, Unavailable };
enum class TelemetryState { Unavailable, Missing, Invalid, Stale, Usable };
struct TelemetryInput {
    std::uint32_t validity = 0;
    protocol::InvalidationReason invalidationReason = protocol::InvalidationReason::NoPlayableContext;
    std::int16_t sceneId = 0, roomId = 0;
    std::uint32_t gameplayFrame = 0;
    std::int32_t linkAge = -1;
    float positionX = 0, positionY = 0, positionZ = 0;
    std::int16_t worldYaw = 0, shapeYaw = 0;
    float velocityX = 0, velocityY = 0, velocityZ = 0, speedXZ = 0;
    std::uint32_t stateFlags1 = 0, stateFlags2 = 0, bgCheckFlags = 0;
};
struct Status {
    PeerState peer = PeerState::Unavailable;
    TelemetryState telemetry = TelemetryState::Unavailable;
    std::uint64_t telemetryAgeMs = 0;
    protocol::Header snapshot{};
};
class Bridge {
public:
    explicit Bridge(Side side);
    ~Bridge();
    Bridge(const Bridge&) = delete;
    Bridge& operator=(const Bridge&) = delete;
    bool connect();
    Status tick();
    bool publishTelemetry(const TelemetryInput& input) noexcept; // OOT only, zero wait
    void disconnect(DWORD waitMs = 100) noexcept;
private:
    Side side_;
    HANDLE owner_ = nullptr, mutex_ = nullptr, mapping_ = nullptr;
    protocol::Header* header_ = nullptr;
    bool attached_ = false;
    std::uint8_t sessionId_[16]{};
    std::uint64_t nextSequence_ = 0, rejectedSequence_ = 0;
    bool waitForNewPublication_ = false;
};
} // namespace zelrim
