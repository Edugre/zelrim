#include "bridge/bridge.h"
#include <charconv>
#include <iostream>
#include <string_view>

namespace {
volatile LONG stopping = 0;
BOOL WINAPI stop(DWORD event) {
    if (event != CTRL_C_EVENT && event != CTRL_BREAK_EVENT) return FALSE;
    InterlockedExchange(&stopping, 1);
    return TRUE;
}
const char* name(zelrim::PeerState state) {
    switch (state) {
    case zelrim::PeerState::Connected: return "connected";
    case zelrim::PeerState::Disconnected: return "disconnected";
    case zelrim::PeerState::TimedOut: return "timed-out";
    default: return "unavailable";
    }
}
const char* telemetryName(zelrim::TelemetryState state) {
    switch (state) {
    case zelrim::TelemetryState::Missing: return "missing";
    case zelrim::TelemetryState::Invalid: return "invalid";
    case zelrim::TelemetryState::Stale: return "stale";
    case zelrim::TelemetryState::Usable: return "usable";
    default: return "unavailable";
    }
}
}
int main(int argc, char** argv) {
    unsigned seconds = 0;
    if (argc != 1) {
        if (argc != 3 || std::string_view(argv[1]) != "--seconds") {
            std::cerr << "Usage: " << argv[0] << " [--seconds N]\n"; return 2;
        }
        const std::string_view value(argv[2]);
        const auto result = std::from_chars(value.data(), value.data() + value.size(), seconds);
        if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || seconds == 0) {
            std::cerr << "N must be a positive integer\n"; return 2;
        }
    }
    SetConsoleCtrlHandler(stop, TRUE);
    try {
#ifdef ZELRIM_SKYRIM_SIDE
        constexpr auto side = zelrim::Side::Skyrim;
        constexpr auto label = "skyrim";
#else
        constexpr auto side = zelrim::Side::Oot;
        constexpr auto label = "oot";
#endif
        zelrim::Bridge bridge(side);
        std::cout << label << " pid=" << GetCurrentProcessId() << std::endl;
        auto previous = zelrim::PeerState::Unavailable;
        bool first = true;
        const auto start = GetTickCount64();
        while (!InterlockedCompareExchange(&stopping, 0, 0) &&
               (!seconds || GetTickCount64() - start < std::uint64_t(seconds) * 1000)) {
            const auto status = bridge.connect() ? bridge.tick() : zelrim::Status{};
#ifndef ZELRIM_SKYRIM_SIDE
            if (status.snapshot.ootPid) {
                zelrim::TelemetryInput sample{};
                sample.validity = zelrim::protocol::kContextPlayable | zelrim::protocol::kPlayerPresent;
                sample.invalidationReason = zelrim::protocol::InvalidationReason::None;
                sample.sceneId = 1; sample.roomId = 2;
                sample.gameplayFrame = static_cast<std::uint32_t>(GetTickCount64() / 16);
                sample.linkAge = 1;
                sample.positionX = static_cast<float>(sample.gameplayFrame);
                sample.positionY = sample.positionX + 1.0f; sample.positionZ = sample.positionX + 2.0f;
                sample.worldYaw = static_cast<std::int16_t>(sample.gameplayFrame);
                sample.shapeYaw = static_cast<std::int16_t>(sample.gameplayFrame + 1);
                sample.velocityX = 1.0f; sample.velocityY = 2.0f; sample.velocityZ = 3.0f; sample.speedXZ = 4.0f;
                sample.stateFlags1 = sample.gameplayFrame; sample.stateFlags2 = ~sample.gameplayFrame;
                sample.bgCheckFlags = 1;
                bridge.publishTelemetry(sample);
            }
#endif
            if (first || status.peer != previous) {
                std::cout << "peer=" << name(status.peer)
                          << " skyrimPid=" << status.snapshot.skyrimPid
                          << " ootPid=" << status.snapshot.ootPid
                          << " skyrimHeartbeatMs=" << status.snapshot.skyrimHeartbeatMs
                          << " ootHeartbeatMs=" << status.snapshot.ootHeartbeatMs << std::endl;
                std::cout << "telemetry=" << telemetryName(status.telemetry)
                          << " sequence=" << status.snapshot.link.sequence
                          << " ageMs=" << status.telemetryAgeMs << std::endl;
                previous = status.peer;
                first = false;
            }
            Sleep(static_cast<DWORD>(zelrim::protocol::kHeartbeatIntervalMs));
        }
        bridge.disconnect();
        std::cout << "detached" << std::endl;
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl; return 1;
    }
}
