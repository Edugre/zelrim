#include "integrations/shipwright/heartbeat.h"
#include "bridge/bridge.h"
#include <memory>
#include <cstdio>

namespace zelrim::shipwright {
namespace {
std::unique_ptr<Bridge> bridge;
Log logger = nullptr;
std::uint64_t lastTick = 0;
bool hasTicked = false;
bool reported = false;
PeerState previous = PeerState::Unavailable;
std::uint32_t previousPid = 0;
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
void stop() noexcept {
    if (bridge) {
        bridge.reset();
        emit("Zelrim OOT heartbeat detached");
    }
    logger = nullptr;
}
}
