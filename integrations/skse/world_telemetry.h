#pragma once
#include "bridge/bridge.h"

namespace zelrim::skse {
struct WorldSample {
    bool playerPresent = false;
    bool cellPresent = false;
    std::uint32_t playerFormId = 0, cellFormId = 0, worldspaceFormId = 0;
    float positionX = 0, positionY = 0, positionZ = 0;
    float rotationX = 0, rotationY = 0, rotationZ = 0;
};

class WorldTelemetryState {
public:
    void preLoad() noexcept;
    void postLoad(bool success) noexcept;
    void newGame() noexcept;
    void reset() noexcept;
    SkyrimTelemetryInput capture(const WorldSample& sample, bool paused) noexcept;
private:
    bool playable_ = false;
    bool loading_ = false;
    bool settling_ = false;
    bool haveIdentity_ = false;
    std::uint32_t cellFormId_ = 0, worldspaceFormId_ = 0;
};
}
