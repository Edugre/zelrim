#include "bridge/bridge.h"
#include "integrations/skse/world_telemetry.h"
#include <functional>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
struct Child {
    PROCESS_INFORMATION process{};
    Child(const char* exe, unsigned seconds) {
        std::string command = std::string("\"") + exe + "\" --seconds " + std::to_string(seconds);
        STARTUPINFOA startup{};
        startup.cb = sizeof(startup);
        require(CreateProcessA(nullptr, command.data(), nullptr, nullptr, FALSE,
                              CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process), "CreateProcess failed");
        CloseHandle(process.hThread);
    }
    ~Child() {
        if (WaitForSingleObject(process.hProcess, 0) == WAIT_TIMEOUT) {
            TerminateProcess(process.hProcess, 99);
            WaitForSingleObject(process.hProcess, 5000);
        }
        CloseHandle(process.hProcess);
    }
    void kill() {
        require(TerminateProcess(process.hProcess, 99), "TerminateProcess failed");
        require(WaitForSingleObject(process.hProcess, 5000) == WAIT_OBJECT_0, "Child did not terminate");
    }
    DWORD finish() {
        require(WaitForSingleObject(process.hProcess, 6000) == WAIT_OBJECT_0, "Child did not exit");
        DWORD code = 0;
        require(GetExitCodeProcess(process.hProcess, &code), "GetExitCodeProcess failed");
        return code;
    }
};
void until(const std::function<bool()>& condition, const char* message) {
    const auto start = GetTickCount64();
    do {
        if (condition()) return;
        Sleep(20);
    } while (GetTickCount64() - start < 5000);
    throw std::runtime_error(message);
}
void lifecycle(zelrim::Side side, const char* peerExe, const char* ownExe) {
    zelrim::Bridge bridge(side);
    if (side == zelrim::Side::Oot) require(!bridge.connect(), "OOT must wait for Skyrim");
    else require(bridge.connect(), "Skyrim create failed");
    {
        Child peer(peerExe, 2);
        until([&] { return bridge.connect() && bridge.tick().peer == zelrim::PeerState::Connected; }, "Peer never connected");
        const auto first = bridge.tick().snapshot;
        require(first.skyrimPid != 0 && first.ootPid != 0 && first.skyrimPid != first.ootPid, "Bad process IDs");
        until([&] {
            const auto next = bridge.tick().snapshot;
            return next.skyrimHeartbeatMs > first.skyrimHeartbeatMs && next.ootHeartbeatMs > first.ootHeartbeatMs;
        }, "Both heartbeats must advance");
        Child duplicate(ownExe, 1);
        require(duplicate.finish() == 1, "Duplicate side was accepted");
        until([&] { return bridge.tick().peer == zelrim::PeerState::Disconnected; }, "Clean disconnect was not detected");
        require(peer.finish() == 0, "Peer failed clean exit");
    }
    {
        Child peer(peerExe, 20);
        until([&] { return bridge.tick().peer == zelrim::PeerState::Connected; }, "Restart did not reconnect");
        peer.kill();
        until([&] { return bridge.tick().peer == zelrim::PeerState::TimedOut; }, "Crash did not time out");
    }
    {
        Child peer(peerExe, 1);
        until([&] { return bridge.tick().peer == zelrim::PeerState::Connected; }, "Crash restart did not reconnect");
        until([&] { return bridge.tick().peer == zelrim::PeerState::Disconnected; }, "Restart cleanup failed");
        require(peer.finish() == 0, "Restart exit failed");
    }
    std::cout << "PASS lifecycle side=" << (side == zelrim::Side::Skyrim ? "skyrim" : "oot") << '\n';
}
void incompatible() {
    require(zelrim::protocol::kVersion == 3 && zelrim::protocol::kMappingBytes != 64,
            "Protocol v3 must be rejected by older version/size readers");
    const auto mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
        0, zelrim::protocol::kMappingBytes, zelrim::protocol::kMappingName);
    require(mapping != nullptr && GetLastError() != ERROR_ALREADY_EXISTS, "Mapping leaked or another bridge is running");
    auto* header = static_cast<zelrim::protocol::Header*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, zelrim::protocol::kMappingBytes));
    require(header != nullptr, "Test mapping failed");
    for (int field = 0; field < 3; ++field) {
        *header = {};
        header->magic = zelrim::protocol::kMagic;
        header->version = zelrim::protocol::kVersion;
        header->byteSize = zelrim::protocol::kMappingBytes;
        if (field == 0) ++header->magic;
        if (field == 1) ++header->version;
        if (field == 2) ++header->byteSize;
        for (auto side : {zelrim::Side::Skyrim, zelrim::Side::Oot}) {
            bool rejected = false;
            try { zelrim::Bridge bridge(side); bridge.connect(); }
            catch (const std::runtime_error&) { rejected = true; }
            require(rejected, "Incompatible header accepted");
            require(header->skyrimPid == 0 && header->ootPid == 0, "Rejected mapping was modified");
        }
    }
    UnmapViewOfFile(header);
    CloseHandle(mapping);
    std::cout << "PASS incompatible magic/version/size\n";
}
void telemetry() {
    zelrim::Bridge skyrim(zelrim::Side::Skyrim);
    require(skyrim.connect(), "Telemetry Skyrim create failed");
    std::uint8_t firstSession[16]{};
    {
        zelrim::Bridge oot(zelrim::Side::Oot);
        require(oot.connect(), "Telemetry OOT connect failed");
        auto initial = skyrim.tick();
        require(initial.telemetry == zelrim::TelemetryState::Invalid, "Attachment record must be invalid");
        std::memcpy(firstSession, initial.snapshot.link.sessionId, 16);
        zelrim::TelemetryInput sample{};
        sample.validity = zelrim::protocol::kContextPlayable | zelrim::protocol::kPlayerPresent | zelrim::protocol::kCutscene;
        sample.invalidationReason = zelrim::protocol::InvalidationReason::None;
        sample.sceneId = -3; sample.roomId = 7; sample.gameplayFrame = 0xFFFFFFFEu; sample.linkAge = 1;
        sample.positionX = 0.0f; sample.positionY = -2.5f; sample.positionZ = 3.25f;
        sample.worldYaw = -32768; sample.shapeYaw = 32767;
        sample.velocityX = 1; sample.velocityY = 2; sample.velocityZ = 3; sample.speedXZ = 4;
        sample.stateFlags1 = 0x12345678; sample.stateFlags2 = 0x87654321; sample.bgCheckFlags = 0x201;
        require(oot.publishTelemetry(sample), "Valid telemetry publish failed");
        auto status = skyrim.tick();
        require(status.telemetry == zelrim::TelemetryState::Usable, "Valid telemetry not usable");
        require(status.snapshot.link.positionX == 0.0f && status.snapshot.link.positionY == -2.5f &&
                status.snapshot.link.worldYaw == -32768 && status.snapshot.link.stateFlags1 == 0x12345678,
                "Telemetry values changed");
        const auto sequence = status.snapshot.link.sequence;
        sample.positionX = std::numeric_limits<float>::infinity();
        require(!oot.publishTelemetry(sample), "Non-finite telemetry accepted");
        require(skyrim.tick().snapshot.link.sequence == sequence, "Rejected sample changed record");
        oot.publishTelemetry(zelrim::TelemetryInput{}); // reason is nonzero by default
        require(skyrim.tick().telemetry == zelrim::TelemetryState::Invalid, "Invalid context not reported");
        sample.positionX = 1.0f;
        require(oot.publishTelemetry(sample), "Fresh telemetry republish failed");
        Sleep(static_cast<DWORD>(zelrim::protocol::kTelemetryFreshnessMs));
        require(skyrim.tick().telemetry == zelrim::TelemetryState::Stale, "Old sample not stale");
    }
    require(skyrim.tick().peer == zelrim::PeerState::Disconnected, "Telemetry detach not visible");
    {
        zelrim::Bridge oot(zelrim::Side::Oot);
        require(oot.connect(), "Telemetry restart failed");
        const auto restarted = skyrim.tick();
        require(std::memcmp(firstSession, restarted.snapshot.link.sessionId, 16) != 0,
                "Same-PID restart reused session");
        require(restarted.telemetry == zelrim::TelemetryState::Invalid, "Restart did not invalidate telemetry");
    }
    std::cout << "PASS telemetry validity, freshness, finite values and sessions\n";
}
void skyrimTelemetry() {
    zelrim::Bridge skyrim(zelrim::Side::Skyrim);
    require(skyrim.connect(), "Skyrim telemetry create failed");
    zelrim::Bridge oot(zelrim::Side::Oot);
    require(oot.connect(), "Skyrim telemetry observer connect failed");
    require(oot.tick().skyrimTelemetry == zelrim::TelemetryState::Invalid,
            "Initial Skyrim record must be invalid");
    zelrim::SkyrimTelemetryInput sample{};
    sample.validity = zelrim::protocol::kSkyrimContextPlayable |
        zelrim::protocol::kSkyrimPlayerPresent | zelrim::protocol::kSkyrimCellPresent;
    sample.invalidationReason = zelrim::protocol::SkyrimInvalidationReason::None;
    sample.playerFormId = 0x14; sample.cellFormId = 0xABCDEF01; sample.worldspaceFormId = 0;
    sample.positionX = 0; sample.positionY = -123.5f; sample.positionZ = 456.25f;
    sample.rotationX = -3.0f; sample.rotationY = 0.5f; sample.rotationZ = 6.0f;
    require(skyrim.publishSkyrimTelemetry(sample), "Valid Skyrim telemetry publish failed");
    auto status = oot.tick();
    require(status.skyrimTelemetry == zelrim::TelemetryState::Usable, "Skyrim telemetry not usable");
    require(status.snapshot.skyrim.cellFormId == sample.cellFormId &&
            status.snapshot.skyrim.worldspaceFormId == 0 &&
            status.snapshot.skyrim.positionY == sample.positionY &&
            status.snapshot.skyrim.rotationZ == sample.rotationZ, "Skyrim telemetry values changed");
    for (std::uint32_t pattern = 1; pattern <= 100; ++pattern) {
        sample.positionX = static_cast<float>(pattern);
        sample.positionY = static_cast<float>(pattern + 1000);
        sample.positionZ = static_cast<float>(pattern + 2000);
        sample.rotationX = static_cast<float>(pattern + 3000);
        sample.rotationY = static_cast<float>(pattern + 4000);
        sample.rotationZ = static_cast<float>(pattern + 5000);
        require(skyrim.publishSkyrimTelemetry(sample), "Changing Skyrim telemetry publish failed");
        const auto coherent = oot.tick().snapshot.skyrim;
        require(coherent.positionY == coherent.positionX + 1000 &&
                coherent.positionZ == coherent.positionX + 2000 &&
                coherent.rotationX == coherent.positionX + 3000 &&
                coherent.rotationY == coherent.positionX + 4000 &&
                coherent.rotationZ == coherent.positionX + 5000, "Torn Skyrim telemetry record observed");
    }
    const auto sequence = oot.tick().snapshot.skyrim.sequence;
    sample.rotationY = std::numeric_limits<float>::quiet_NaN();
    require(!skyrim.publishSkyrimTelemetry(sample), "Non-finite Skyrim telemetry accepted");
    require(oot.tick().snapshot.skyrim.sequence == sequence, "Rejected Skyrim sample changed record");
    zelrim::SkyrimTelemetryInput paused{};
    paused.validity = zelrim::protocol::kSkyrimPaused;
    paused.invalidationReason = zelrim::protocol::SkyrimInvalidationReason::Paused;
    require(skyrim.publishSkyrimTelemetry(paused), "Paused invalidation rejected");
    require(oot.tick().skyrimTelemetry == zelrim::TelemetryState::Invalid, "Paused context not invalid");
    std::cout << "PASS Skyrim telemetry validity, identity and finite values\n";
}
void skyrimCaptureState() {
    zelrim::skse::WorldTelemetryState state;
    zelrim::skse::WorldSample sample{};
    require(state.capture(sample, false).invalidationReason ==
        zelrim::protocol::SkyrimInvalidationReason::NoPlayableContext, "Startup context accepted");
    state.preLoad();
    require(state.capture(sample, false).invalidationReason ==
        zelrim::protocol::SkyrimInvalidationReason::Loading, "Loading context not exposed");
    state.postLoad(true);
    require(state.capture(sample, false).invalidationReason ==
        zelrim::protocol::SkyrimInvalidationReason::Loading, "Load settling not retained without player");
    sample.playerPresent = true; sample.playerFormId = 0x14;
    require(state.capture(sample, false).invalidationReason ==
        zelrim::protocol::SkyrimInvalidationReason::Loading, "Load settling not retained without cell");
    sample.cellPresent = true; sample.cellFormId = 1; sample.worldspaceFormId = 2;
    require(state.capture(sample, false).invalidationReason ==
        zelrim::protocol::SkyrimInvalidationReason::Loading, "Settling barrier missing");
    require(state.capture(sample, false).invalidationReason ==
        zelrim::protocol::SkyrimInvalidationReason::None, "Settled sample not usable");
    sample.playerPresent = false;
    require(state.capture(sample, false).invalidationReason ==
        zelrim::protocol::SkyrimInvalidationReason::NoPlayer, "Missing player not exposed");
    sample.playerPresent = true; sample.cellPresent = false;
    require(state.capture(sample, false).invalidationReason ==
        zelrim::protocol::SkyrimInvalidationReason::NoCell, "Missing cell not exposed");
    sample.cellPresent = true;
    sample.cellFormId = 3;
    auto changed = state.capture(sample, false);
    require(changed.invalidationReason == zelrim::protocol::SkyrimInvalidationReason::CellOrWorldspaceChanged &&
            (changed.validity & zelrim::protocol::kSkyrimTransition), "Cell transition barrier missing");
    require(state.capture(sample, false).invalidationReason ==
        zelrim::protocol::SkyrimInvalidationReason::None, "New cell did not settle");
    require(state.capture(sample, true).invalidationReason ==
        zelrim::protocol::SkyrimInvalidationReason::Paused, "Pause not invalidated");
    state.postLoad(false);
    require(state.capture(sample, false).invalidationReason ==
        zelrim::protocol::SkyrimInvalidationReason::NoPlayableContext, "Failed load accepted");
    std::cout << "PASS Skyrim capture loading, pause and cell transition state\n";
}
void synchronization() {
    zelrim::Bridge skyrim(zelrim::Side::Skyrim);
    require(skyrim.connect(), "Create failed");
    zelrim::Bridge oot(zelrim::Side::Oot);
    require(oot.connect(), "Open failed");
    zelrim::TelemetryInput sample{};
    sample.validity = zelrim::protocol::kContextPlayable | zelrim::protocol::kPlayerPresent;
    sample.invalidationReason = zelrim::protocol::InvalidationReason::None;
    require(oot.publishTelemetry(sample), "Pre-abandon publication failed");
    const auto mutex = OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, zelrim::protocol::kDataMutexName);
    require(mutex != nullptr, "OpenMutex failed");
    const auto ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    const auto release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    require(ready && release, "CreateEvent failed");
    std::thread blocker([&] {
        WaitForSingleObject(mutex, INFINITE);
        SetEvent(ready);
        WaitForSingleObject(release, INFINITE);
        // Deliberately abandon the mutex on thread exit.
    });
    WaitForSingleObject(ready, INFINITE);
    const auto publishStart = GetTickCount64();
    require(!oot.publishTelemetry(sample), "Contended telemetry publish must be dropped");
    require(GetTickCount64() - publishStart < 50, "Telemetry publication waited for the mutex");
    const auto result = skyrim.tick();
    SetEvent(release);
    blocker.join();
    require(result.peer == zelrim::PeerState::Unavailable, "Blocked mutex must fail closed");
    const auto abandoned = skyrim.tick();
    require(abandoned.peer == zelrim::PeerState::Connected, "Abandoned mutex recovery failed");
    require(abandoned.telemetry == zelrim::TelemetryState::Unavailable,
            "Abandoned publication was accepted");
    require(oot.publishTelemetry(sample), "Post-abandon publication failed");
    require(skyrim.tick().telemetry == zelrim::TelemetryState::Usable,
            "Complete publication did not recover abandonment");
    // A live process that stops ticking is unavailable too; no PID-based takeover.
    Sleep(static_cast<DWORD>(zelrim::protocol::kHeartbeatTimeoutMs));
    require(skyrim.tick().peer == zelrim::PeerState::TimedOut, "Stalled peer did not time out");
    oot.tick();
    require(skyrim.tick().peer == zelrim::PeerState::Connected, "Stalled peer did not recover");
    CloseHandle(release);
    CloseHandle(ready);
    CloseHandle(mutex);
    std::cout << "PASS contention, abandonment, stall and recovery\n";
}
int main(int argc, char** argv) {
    try {
        require(argc == 3, "Expected paths to both standalone executables");
        lifecycle(zelrim::Side::Skyrim, argv[2], argv[1]);
        lifecycle(zelrim::Side::Oot, argv[1], argv[2]);
        incompatible();
        telemetry();
        skyrimTelemetry();
        skyrimCaptureState();
        synchronization();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n'; return 1;
    }
}
