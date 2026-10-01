#include "protocol/bridge_protocol.h"
#include <windows.h>
#include <charconv>
#include <iostream>
#include <string_view>

int main(int argc, char** argv) {
    unsigned seconds = 15;
    if (argc != 1) {
        if (argc != 3 || std::string_view(argv[1]) != "--seconds") return 2;
        const std::string_view value(argv[2]);
        const auto result = std::from_chars(value.data(), value.data() + value.size(), seconds);
        if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || !seconds) return 2;
    }
    bool bothSeen = false;
    const auto start = GetTickCount64();
    while (GetTickCount64() - start < std::uint64_t(seconds) * 1000) {
        auto mutex = OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, zelrim::protocol::kDataMutexName);
        auto mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, zelrim::protocol::kMappingName);
        auto* header = mapping ? static_cast<const zelrim::protocol::Header*>(
            MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, zelrim::protocol::kMappingBytes)) : nullptr;
        bool read = false;
        if (mutex && header) {
            const auto lock = WaitForSingleObject(mutex, 100);
            if (lock == WAIT_OBJECT_0 || lock == WAIT_ABANDONED) {
                const auto copy = *header;
                ReleaseMutex(mutex);
                if (copy.magic == zelrim::protocol::kMagic && copy.version == zelrim::protocol::kVersion &&
                    copy.byteSize == zelrim::protocol::kMappingBytes) {
                    const auto now = GetTickCount64();
                    auto alive = [now](std::uint32_t pid, std::uint64_t beat) {
                        return pid && beat <= now && now - beat < zelrim::protocol::kHeartbeatTimeoutMs;
                    };
                    const bool sky = alive(copy.skyrimPid, copy.skyrimHeartbeatMs);
                    const bool oot = alive(copy.ootPid, copy.ootHeartbeatMs);
                    bothSeen |= sky && oot;
                    std::cout << "now=" << now << " skyrimPid=" << copy.skyrimPid
                              << " skyrimHeartbeatMs=" << copy.skyrimHeartbeatMs << " skyrimAlive=" << sky
                              << " ootPid=" << copy.ootPid << " ootHeartbeatMs=" << copy.ootHeartbeatMs
                              << " ootAlive=" << oot << std::endl;
                    read = true;
                }
            }
        }
        if (!read) std::cout << "bridge=unavailable" << std::endl;
        if (header) UnmapViewOfFile(header);
        if (mapping) CloseHandle(mapping);
        if (mutex) CloseHandle(mutex);
        Sleep(1000);
    }
    return bothSeen ? 0 : 1;
}
