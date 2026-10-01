#include "protocol/bridge_protocol.h"
#include <windows.h>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <string_view>

namespace {
bool alive(std::uint32_t pid, std::uint64_t beat, std::uint64_t now) {
    return pid && beat <= now && now - beat < zelrim::protocol::kHeartbeatTimeoutMs;
}
bool sessionPresent(const std::uint8_t* value) {
    for (unsigned i = 0; i < 16; ++i) if (value[i]) return true;
    return false;
}
void session(std::ostream& out, const std::uint8_t* value) {
    const auto flags = out.flags(); const auto fill = out.fill();
    out << std::hex << std::setfill('0');
    for (unsigned i = 0; i < 16; ++i) out << std::setw(2) << unsigned(value[i]);
    out.flags(flags); out.fill(fill);
}
}
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
                if (lock != WAIT_ABANDONED && copy.magic == zelrim::protocol::kMagic &&
                    copy.version == zelrim::protocol::kVersion && copy.byteSize == zelrim::protocol::kMappingBytes) {
                    const auto now = GetTickCount64();
                    const bool sky = alive(copy.skyrimPid, copy.skyrimHeartbeatMs, now);
                    const bool oot = alive(copy.ootPid, copy.ootHeartbeatMs, now);
                    bothSeen |= sky && oot;
                    const auto& t = copy.link;
                    const bool complete = t.sequence && sessionPresent(t.sessionId) &&
                        t.publicationBegin == t.sequence && t.publicationEnd == t.sequence;
                    const auto age = complete && t.captureUptimeMs <= now ? now - t.captureUptimeMs : 0;
                    const bool context = (t.validity & (zelrim::protocol::kContextPlayable | zelrim::protocol::kPlayerPresent)) ==
                        (zelrim::protocol::kContextPlayable | zelrim::protocol::kPlayerPresent);
                    const bool finite = std::isfinite(t.positionX) && std::isfinite(t.positionY) && std::isfinite(t.positionZ) &&
                        std::isfinite(t.velocityX) && std::isfinite(t.velocityY) && std::isfinite(t.velocityZ) && std::isfinite(t.speedXZ);
                    const bool valid = complete && context && !(t.validity & ~zelrim::protocol::kKnownValidity) &&
                        !(t.validity & zelrim::protocol::kTransition) && !t.invalidationReason && finite;
                    const char* state = !oot ? "unavailable" : !complete ? "missing" : !valid ? "invalid" :
                        age >= zelrim::protocol::kTelemetryFreshnessMs ? "stale" : "usable";
                    std::cout << "now=" << now << " skyrimPid=" << copy.skyrimPid << " skyrimAlive=" << sky
                              << " ootPid=" << copy.ootPid << " ootAlive=" << oot << " telemetry=" << state
                              << " session="; session(std::cout, t.sessionId);
                    std::cout << " sequence=" << t.sequence << " ageMs=" << age << " validity=0x" << std::hex
                              << t.validity << std::dec << " reason=" << t.invalidationReason;
                    if (valid) std::cout << " scene=" << t.sceneId << " room=" << t.roomId << " frame=" << t.gameplayFrame
                        << " pos=(" << t.positionX << ',' << t.positionY << ',' << t.positionZ << ") yaw=(" << t.worldYaw
                        << ',' << t.shapeYaw << ") flags=(0x" << std::hex << t.stateFlags1 << ",0x" << t.stateFlags2
                        << ",0x" << t.bgCheckFlags << std::dec << ')';
                    std::cout << std::endl; read = true;
                }
            }
        }
        if (!read) std::cout << "bridge=unavailable" << std::endl;
        if (header) UnmapViewOfFile(header); if (mapping) CloseHandle(mapping); if (mutex) CloseHandle(mutex);
        Sleep(1000);
    }
    return bothSeen ? 0 : 1;
}
