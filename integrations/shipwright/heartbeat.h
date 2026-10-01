#pragma once
#include "bridge/bridge.h"
namespace zelrim::shipwright {
// Call serially on the game thread. Callback must not retain message pointers.
using Log = void (*)(const char* message);
void start(Log log) noexcept;
void tick() noexcept;
// Called only from Shipwright's game thread. Keeps at most one pending sample.
void publish(const TelemetryInput& input) noexcept;
void invalidate(protocol::InvalidationReason reason, std::uint32_t contextBits = 0) noexcept;
// Registers read-only hooks in the pinned Shipwright build.
void installTelemetryHooks() noexcept;
void stop() noexcept;
}
