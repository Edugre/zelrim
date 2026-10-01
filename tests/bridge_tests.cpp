#include "bridge/bridge.h"
#include <functional>
#include <iostream>
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
    const auto mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
        0, zelrim::protocol::kMappingBytes, zelrim::protocol::kMappingName);
    require(mapping != nullptr && GetLastError() != ERROR_ALREADY_EXISTS, "Mapping leaked or another bridge is running");
    auto* header = static_cast<zelrim::protocol::Header*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 64));
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
void synchronization() {
    zelrim::Bridge skyrim(zelrim::Side::Skyrim);
    require(skyrim.connect(), "Create failed");
    zelrim::Bridge oot(zelrim::Side::Oot);
    require(oot.connect(), "Open failed");
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
    const auto result = skyrim.tick();
    SetEvent(release);
    blocker.join();
    require(result.peer == zelrim::PeerState::Unavailable, "Blocked mutex must fail closed");
    require(skyrim.tick().peer == zelrim::PeerState::Connected, "Abandoned mutex recovery failed");
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
        synchronization();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n'; return 1;
    }
}
