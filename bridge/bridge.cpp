#include "bridge/bridge.h"
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>

namespace zelrim {
namespace {
[[noreturn]] void fail(const char* operation) {
    throw std::runtime_error(std::string(operation) + " failed, Win32 error " + std::to_string(GetLastError()));
}
class Lock {
public:
    Lock(HANDLE mutex, DWORD waitMs) : mutex_(mutex) {
        const auto result = WaitForSingleObject(mutex_, waitMs);
        locked = result == WAIT_OBJECT_0 || result == WAIT_ABANDONED;
        abandoned = result == WAIT_ABANDONED;
        if (result == WAIT_FAILED) fail("WaitForSingleObject");
    }
    ~Lock() { if (locked) ReleaseMutex(mutex_); }
    bool locked = false, abandoned = false;
private: HANDLE mutex_;
};
bool validHeader(const protocol::Header& h) {
    return h.magic == protocol::kMagic && h.version == protocol::kVersion && h.byteSize == protocol::kMappingBytes;
}
bool finite(const TelemetryInput& v) {
    return std::isfinite(v.positionX) && std::isfinite(v.positionY) && std::isfinite(v.positionZ) &&
           std::isfinite(v.velocityX) && std::isfinite(v.velocityY) && std::isfinite(v.velocityZ) &&
           std::isfinite(v.speedXZ);
}
bool finite(const SkyrimTelemetryInput& v) {
    return std::isfinite(v.positionX) && std::isfinite(v.positionY) && std::isfinite(v.positionZ) &&
           std::isfinite(v.rotationX) && std::isfinite(v.rotationY) && std::isfinite(v.rotationZ);
}
bool zeroSession(const std::uint8_t* id) {
    for (unsigned i = 0; i < 16; ++i) if (id[i]) return false;
    return true;
}
void makeSession(std::uint8_t* id) {
    LARGE_INTEGER counter{}; QueryPerformanceCounter(&counter);
    const std::uint64_t a = GetTickCount64() ^ (std::uint64_t(GetCurrentProcessId()) << 32);
    const std::uint64_t b = std::uint64_t(counter.QuadPart) ^ reinterpret_cast<std::uintptr_t>(id);
    std::memcpy(id, &a, 8); std::memcpy(id + 8, &b, 8);
    if (zeroSession(id)) id[0] = 1;
}
void writeRecord(protocol::LinkTelemetry& out, const std::uint8_t* session,
                 std::uint64_t sequence, const TelemetryInput& in) {
    out.publicationEnd = 0;
    out.publicationBegin = sequence;
    protocol::LinkTelemetry next{};
    std::memcpy(next.sessionId, session, 16);
    next.sequence = sequence;
    next.captureUptimeMs = GetTickCount64();
    next.publicationBegin = sequence;
    next.validity = in.validity;
    next.invalidationReason = static_cast<std::uint32_t>(in.invalidationReason);
    next.linkAge = -1;
    if ((in.validity & (protocol::kContextPlayable | protocol::kPlayerPresent)) ==
        (protocol::kContextPlayable | protocol::kPlayerPresent)) {
        next.sceneId = in.sceneId; next.roomId = in.roomId; next.gameplayFrame = in.gameplayFrame;
        next.linkAge = in.linkAge;
        next.positionX = in.positionX; next.positionY = in.positionY; next.positionZ = in.positionZ;
        next.worldYaw = in.worldYaw; next.shapeYaw = in.shapeYaw;
        next.velocityX = in.velocityX; next.velocityY = in.velocityY; next.velocityZ = in.velocityZ;
        next.speedXZ = in.speedXZ;
        next.stateFlags1 = in.stateFlags1; next.stateFlags2 = in.stateFlags2;
        next.bgCheckFlags = in.bgCheckFlags;
    }
    next.publicationEnd = sequence;
    out = next;
}
void writeRecord(protocol::SkyrimTelemetry& out, const std::uint8_t* session,
                 std::uint64_t sequence, const SkyrimTelemetryInput& in) {
    out.publicationEnd = 0;
    out.publicationBegin = sequence;
    protocol::SkyrimTelemetry next{};
    std::memcpy(next.sessionId, session, 16);
    next.sequence = sequence;
    next.captureUptimeMs = GetTickCount64();
    next.publicationBegin = sequence;
    next.validity = in.validity;
    next.invalidationReason = static_cast<std::uint32_t>(in.invalidationReason);
    const auto required = protocol::kSkyrimContextPlayable | protocol::kSkyrimPlayerPresent |
                          protocol::kSkyrimCellPresent;
    if ((in.validity & required) == required) {
        next.playerFormId = in.playerFormId;
        next.cellFormId = in.cellFormId;
        next.worldspaceFormId = in.worldspaceFormId;
        next.positionX = in.positionX; next.positionY = in.positionY; next.positionZ = in.positionZ;
        next.rotationX = in.rotationX; next.rotationY = in.rotationY; next.rotationZ = in.rotationZ;
    }
    next.publicationEnd = sequence;
    out = next;
}
TelemetryState classify(const protocol::LinkTelemetry& t, PeerState peer, std::uint64_t now,
                        bool waitForNew, std::uint64_t rejected, std::uint64_t& age) {
    if (peer != PeerState::Connected) return TelemetryState::Unavailable;
    if (!t.sequence || zeroSession(t.sessionId)) return TelemetryState::Missing;
    if (t.publicationBegin != t.sequence || t.publicationEnd != t.sequence || (waitForNew && t.sequence == rejected))
        return TelemetryState::Unavailable;
    if (t.captureUptimeMs > now) return TelemetryState::Stale;
    age = now - t.captureUptimeMs;
    const bool playable = (t.validity & (protocol::kContextPlayable | protocol::kPlayerPresent)) ==
                          (protocol::kContextPlayable | protocol::kPlayerPresent);
    if ((t.validity & ~protocol::kKnownValidity) || !playable ||
        t.invalidationReason != static_cast<std::uint32_t>(protocol::InvalidationReason::None))
        return TelemetryState::Invalid;
    const bool allFinite = std::isfinite(t.positionX) && std::isfinite(t.positionY) && std::isfinite(t.positionZ) &&
        std::isfinite(t.velocityX) && std::isfinite(t.velocityY) && std::isfinite(t.velocityZ) && std::isfinite(t.speedXZ);
    if (!allFinite || (t.validity & protocol::kTransition)) return TelemetryState::Invalid;
    return age >= protocol::kTelemetryFreshnessMs ? TelemetryState::Stale : TelemetryState::Usable;
}
TelemetryState classify(const protocol::SkyrimTelemetry& t, PeerState peer, std::uint64_t now,
                        bool waitForNew, std::uint64_t rejected, std::uint64_t& age) {
    if (peer != PeerState::Connected) return TelemetryState::Unavailable;
    if (!t.sequence || zeroSession(t.sessionId)) return TelemetryState::Missing;
    if (t.publicationBegin != t.sequence || t.publicationEnd != t.sequence || (waitForNew && t.sequence == rejected))
        return TelemetryState::Unavailable;
    if (t.captureUptimeMs > now) return TelemetryState::Stale;
    age = now - t.captureUptimeMs;
    const auto required = protocol::kSkyrimContextPlayable | protocol::kSkyrimPlayerPresent |
                          protocol::kSkyrimCellPresent;
    const bool context = (t.validity & required) == required;
    if ((t.validity & ~protocol::kKnownSkyrimValidity) || !context ||
        t.invalidationReason != static_cast<std::uint32_t>(protocol::SkyrimInvalidationReason::None) ||
        (t.validity & (protocol::kSkyrimPaused | protocol::kSkyrimLoading | protocol::kSkyrimTransition)))
        return TelemetryState::Invalid;
    const bool allFinite = std::isfinite(t.positionX) && std::isfinite(t.positionY) && std::isfinite(t.positionZ) &&
        std::isfinite(t.rotationX) && std::isfinite(t.rotationY) && std::isfinite(t.rotationZ);
    if (!allFinite) return TelemetryState::Invalid;
    return age >= protocol::kTelemetryFreshnessMs ? TelemetryState::Stale : TelemetryState::Usable;
}
}

Bridge::Bridge(Side side) : side_(side) {
    owner_ = CreateMutexW(nullptr, FALSE, side == Side::Skyrim ? protocol::kSkyrimOwnerName : protocol::kOotOwnerName);
    if (!owner_) fail("CreateMutex(owner)");
    if (GetLastError() == ERROR_ALREADY_EXISTS) { CloseHandle(owner_); owner_ = nullptr; throw std::runtime_error("An endpoint already owns this side"); }
    mutex_ = CreateMutexW(nullptr, FALSE, protocol::kDataMutexName);
    if (!mutex_) { const auto error = GetLastError(); CloseHandle(owner_); SetLastError(error); fail("CreateMutex(data)"); }
}
Bridge::~Bridge() { disconnect(); CloseHandle(mutex_); CloseHandle(owner_); }

bool Bridge::connect() {
    if (attached_) return true;
    Lock lock(mutex_, 100);
    if (!lock.locked) return false;
    bool created = false;
    if (side_ == Side::Skyrim) {
        mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, protocol::kMappingBytes, protocol::kMappingName);
        if (!mapping_) fail("CreateFileMapping");
        created = GetLastError() != ERROR_ALREADY_EXISTS;
    } else {
        mapping_ = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, protocol::kMappingName);
        if (!mapping_) { if (GetLastError() == ERROR_FILE_NOT_FOUND) return false; fail("OpenFileMapping"); }
    }
    header_ = static_cast<protocol::Header*>(MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, protocol::kMappingBytes));
    if (!header_) { const auto error = GetLastError(); CloseHandle(mapping_); mapping_ = nullptr; SetLastError(error); fail("MapViewOfFile"); }
    if (created) { *header_ = {}; header_->version = protocol::kVersion; header_->byteSize = protocol::kMappingBytes; header_->magic = protocol::kMagic; }
    if (!validHeader(*header_)) { UnmapViewOfFile(header_); CloseHandle(mapping_); header_ = nullptr; mapping_ = nullptr; throw std::runtime_error("Incompatible bridge magic, version or byte size"); }
    auto& pid = side_ == Side::Skyrim ? header_->skyrimPid : header_->ootPid;
    auto& beat = side_ == Side::Skyrim ? header_->skyrimHeartbeatMs : header_->ootHeartbeatMs;
    beat = GetTickCount64(); pid = GetCurrentProcessId(); attached_ = true;
    makeSession(sessionId_); nextSequence_ = 1;
    if (side_ == Side::Oot) {
        TelemetryInput initial{}; initial.invalidationReason = protocol::InvalidationReason::NoPlayableContext;
        writeRecord(header_->link, sessionId_, nextSequence_++, initial);
    } else {
        SkyrimTelemetryInput initial{};
        writeRecord(header_->skyrim, sessionId_, nextSequence_++, initial);
    }
    if (lock.abandoned) {
        waitForNewLinkPublication_ = waitForNewSkyrimPublication_ = true;
        rejectedLinkSequence_ = header_->link.sequence;
        rejectedSkyrimSequence_ = header_->skyrim.sequence;
    }
    return true;
}

Status Bridge::tick(DWORD waitMs) {
    if (!attached_) return {};
    Lock lock(mutex_, waitMs);
    if (!lock.locked) return {};
    if (!validHeader(*header_)) throw std::runtime_error("Bridge protocol changed while attached");
    const auto now = GetTickCount64();
    (side_ == Side::Skyrim ? header_->skyrimHeartbeatMs : header_->ootHeartbeatMs) = now;
    Status result; result.snapshot = *header_;
    const auto pid = side_ == Side::Skyrim ? header_->ootPid : header_->skyrimPid;
    const auto beat = side_ == Side::Skyrim ? header_->ootHeartbeatMs : header_->skyrimHeartbeatMs;
    result.peer = pid == 0 ? PeerState::Disconnected :
        (beat > now || now - beat >= protocol::kHeartbeatTimeoutMs ? PeerState::TimedOut : PeerState::Connected);
    if (lock.abandoned) {
        waitForNewLinkPublication_ = waitForNewSkyrimPublication_ = true;
        rejectedLinkSequence_ = result.snapshot.link.sequence;
        rejectedSkyrimSequence_ = result.snapshot.skyrim.sequence;
    }
    if (waitForNewLinkPublication_ && result.snapshot.link.sequence != rejectedLinkSequence_)
        waitForNewLinkPublication_ = false;
    if (waitForNewSkyrimPublication_ && result.snapshot.skyrim.sequence != rejectedSkyrimSequence_)
        waitForNewSkyrimPublication_ = false;
    result.telemetry = classify(result.snapshot.link, side_ == Side::Skyrim ? result.peer : PeerState::Connected,
                                now, waitForNewLinkPublication_, rejectedLinkSequence_, result.telemetryAgeMs);
    result.skyrimTelemetry = classify(result.snapshot.skyrim,
        side_ == Side::Oot ? result.peer : PeerState::Connected, now,
        waitForNewSkyrimPublication_, rejectedSkyrimSequence_, result.skyrimTelemetryAgeMs);
    return result;
}

bool Bridge::publishTelemetry(const TelemetryInput& input) noexcept {
    if (side_ != Side::Oot || !attached_ || !header_) return false;
    const bool playable = (input.validity & (protocol::kContextPlayable | protocol::kPlayerPresent)) ==
                          (protocol::kContextPlayable | protocol::kPlayerPresent);
    if ((input.validity & ~protocol::kKnownValidity) ||
        (playable && (input.invalidationReason != protocol::InvalidationReason::None ||
                      (input.validity & protocol::kTransition) || !finite(input))) ||
        (!playable && input.invalidationReason == protocol::InvalidationReason::None)) return false;
    try {
        Lock lock(mutex_, 0);
        if (!lock.locked || !validHeader(*header_)) return false;
        if (lock.abandoned) header_->skyrim = {};
        writeRecord(header_->link, sessionId_, nextSequence_++, input);
        return true;
    } catch (...) { return false; }
}

bool Bridge::publishSkyrimTelemetry(const SkyrimTelemetryInput& input) noexcept {
    if (side_ != Side::Skyrim || !attached_ || !header_) return false;
    const auto required = protocol::kSkyrimContextPlayable | protocol::kSkyrimPlayerPresent |
                          protocol::kSkyrimCellPresent;
    const bool usableContext = (input.validity & required) == required;
    if ((input.validity & ~protocol::kKnownSkyrimValidity) ||
        (usableContext && (input.invalidationReason != protocol::SkyrimInvalidationReason::None ||
            (input.validity & (protocol::kSkyrimPaused | protocol::kSkyrimLoading | protocol::kSkyrimTransition)) ||
            !finite(input))) ||
        (!usableContext && input.invalidationReason == protocol::SkyrimInvalidationReason::None)) return false;
    try {
        Lock lock(mutex_, 0);
        if (!lock.locked || !validHeader(*header_)) return false;
        if (lock.abandoned) header_->link = {};
        writeRecord(header_->skyrim, sessionId_, nextSequence_++, input);
        return true;
    } catch (...) { return false; }
}

void Bridge::disconnect(DWORD waitMs) noexcept {
    if (header_) {
        const auto result = WaitForSingleObject(mutex_, waitMs);
        if (result == WAIT_OBJECT_0 || result == WAIT_ABANDONED) {
            if (attached_ && validHeader(*header_)) {
                (side_ == Side::Skyrim ? header_->skyrimPid : header_->ootPid) = 0;
                (side_ == Side::Skyrim ? header_->skyrimHeartbeatMs : header_->ootHeartbeatMs) = 0;
                if (side_ == Side::Oot) header_->link = {};
                else header_->skyrim = {};
            }
            ReleaseMutex(mutex_);
        }
        UnmapViewOfFile(header_); header_ = nullptr;
    }
    if (mapping_) CloseHandle(mapping_);
    mapping_ = nullptr; attached_ = false; nextSequence_ = rejectedLinkSequence_ = rejectedSkyrimSequence_ = 0;
    waitForNewLinkPublication_ = waitForNewSkyrimPublication_ = false;
}
} // namespace zelrim
