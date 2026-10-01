#include "integrations/shipwright/heartbeat.h"
#include "bridge/bridge.h"
#include <memory>
#include <cstdio>
#include <optional>

namespace zelrim::shipwright {
namespace {
std::unique_ptr<Bridge> bridge;
Log logger = nullptr;
std::uint64_t lastTick = 0;
bool hasTicked = false;
bool reported = false;
PeerState previous = PeerState::Unavailable;
std::uint32_t previousPid = 0;
std::optional<TelemetryInput> pending;
void emit(const char* message) noexcept {
    try { if (logger) logger(message); } catch (...) {}
}
const char* name(PeerState state) {
    switch (state) {
    case PeerState::Connected: return "connected";
    case PeerState::Disconnected: return "disconnected";
    case PeerState::TimedOut: return "timed-out";
    default: return "unavailable";
    }
}
void disable(const char* reason) noexcept {
    emit("Zelrim heartbeat disabled until adapter restart:");
    emit(reason);
    bridge.reset();
}
}
void start(Log log) noexcept {
    if (bridge) return;
    logger = log;
    lastTick = 0;
    hasTicked = reported = false;
    previousPid = 0;
    pending.reset();
    try {
        bridge = std::make_unique<Bridge>(Side::Oot);
        emit("Zelrim OOT heartbeat started (Local\\Zelrim_v1)");
        tick();
    } catch (const std::exception& error) { disable(error.what()); }
      catch (...) { disable("Unknown initialization error"); }
}
void tick() noexcept {
    if (!bridge) return;
    const auto now = GetTickCount64();
    if (hasTicked && now - lastTick < protocol::kHeartbeatIntervalMs) return;
    lastTick = now;
    hasTicked = true;
    try {
        const auto status = bridge->connect() ? bridge->tick() : Status{};
        if (pending && bridge->publishTelemetry(*pending)) pending.reset();
        if (!reported || status.peer != previous || status.snapshot.skyrimPid != previousPid) {
            char message[256];
            std::snprintf(message, sizeof(message),
                "Zelrim peer=%s skyrimPid=%lu ootPid=%lu skyrimHeartbeatMs=%llu ootHeartbeatMs=%llu",
                name(status.peer), static_cast<unsigned long>(status.snapshot.skyrimPid),
                static_cast<unsigned long>(status.snapshot.ootPid),
                static_cast<unsigned long long>(status.snapshot.skyrimHeartbeatMs),
                static_cast<unsigned long long>(status.snapshot.ootHeartbeatMs));
            emit(message);
            reported = true;
            previous = status.peer;
            previousPid = status.snapshot.skyrimPid;
        }
    } catch (const std::exception& error) { disable(error.what()); }
      catch (...) { disable("Unknown heartbeat error"); }
}
void publish(const TelemetryInput& input) noexcept {
    if (!bridge) return;
    // An invalidation cannot be replaced by an older/later valid capture until it publishes.
    if (pending && pending->invalidationReason != protocol::InvalidationReason::None &&
        input.invalidationReason == protocol::InvalidationReason::None) return;
    pending = input;
    if (bridge->publishTelemetry(*pending)) pending.reset();
}
void invalidate(protocol::InvalidationReason reason, std::uint32_t contextBits) noexcept {
    TelemetryInput input{};
    input.validity = contextBits & (protocol::kPaused | protocol::kCutscene | protocol::kTransition);
    input.invalidationReason = reason;
    publish(input);
}
void stop() noexcept {
    if (bridge) {
        invalidate(protocol::InvalidationReason::Detached);
        bridge.reset();
        emit("Zelrim OOT heartbeat detached");
    }
    logger = nullptr;
    pending.reset();
}
}
