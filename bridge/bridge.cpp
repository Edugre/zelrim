#include "bridge/bridge.h"
#include <stdexcept>
#include <string>

namespace zelrim {
namespace {
[[noreturn]] void fail(const char* operation) {
    throw std::runtime_error(std::string(operation) + " failed, Win32 error " + std::to_string(GetLastError()));
}
class Lock {
public:
    explicit Lock(HANDLE mutex) : mutex_(mutex) {
        const auto result = WaitForSingleObject(mutex_, 100);
        locked = result == WAIT_OBJECT_0 || result == WAIT_ABANDONED;
        if (result == WAIT_FAILED) fail("WaitForSingleObject");
    }
    ~Lock() { if (locked) ReleaseMutex(mutex_); }
    bool locked = false;
private:
    HANDLE mutex_;
};
bool valid(const protocol::Header& h) {
    return h.magic == protocol::kMagic && h.version == protocol::kVersion &&
           h.byteSize == protocol::kMappingBytes;
}
}

Bridge::Bridge(Side side) : side_(side) {
    // Object existence reserves the role; no thread owns this mutex. Only this
    // endpoint keeps its handle, so even forced process exit releases the role.
    owner_ = CreateMutexW(nullptr, FALSE, side == Side::Skyrim ?
        protocol::kSkyrimOwnerName : protocol::kOotOwnerName);
    if (!owner_) fail("CreateMutex(owner)");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(owner_);
        owner_ = nullptr;
        throw std::runtime_error("An endpoint already owns this side");
    }
    mutex_ = CreateMutexW(nullptr, FALSE, protocol::kDataMutexName);
    if (!mutex_) {
        const auto error = GetLastError();
        CloseHandle(owner_);
        SetLastError(error);
        fail("CreateMutex(data)");
    }
}
Bridge::~Bridge() {
    disconnect();
    CloseHandle(mutex_);
    CloseHandle(owner_);
}
bool Bridge::connect() {
    if (attached_) return true;
    Lock lock(mutex_);
    if (!lock.locked) return false;
    bool created = false;
    if (side_ == Side::Skyrim) {
        mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                     0, protocol::kMappingBytes, protocol::kMappingName);
        if (!mapping_) fail("CreateFileMapping");
        created = GetLastError() != ERROR_ALREADY_EXISTS;
    } else {
        mapping_ = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, protocol::kMappingName);
        if (!mapping_) {
            if (GetLastError() == ERROR_FILE_NOT_FOUND) return false;
            fail("OpenFileMapping");
        }
    }
    header_ = static_cast<protocol::Header*>(MapViewOfFile(
        mapping_, FILE_MAP_ALL_ACCESS, 0, 0, protocol::kMappingBytes));
    if (!header_) {
        const auto error = GetLastError();
        CloseHandle(mapping_);
        mapping_ = nullptr;
        SetLastError(error);
        fail("MapViewOfFile");
    }
    if (created) {
        *header_ = {};
        header_->version = protocol::kVersion;
        header_->byteSize = protocol::kMappingBytes;
        header_->magic = protocol::kMagic;
    }
    if (!valid(*header_)) {
        UnmapViewOfFile(header_);
        CloseHandle(mapping_);
        header_ = nullptr;
        mapping_ = nullptr;
        throw std::runtime_error("Incompatible bridge magic, version or byte size");
    }
    auto& pid = side_ == Side::Skyrim ? header_->skyrimPid : header_->ootPid;
    auto& beat = side_ == Side::Skyrim ? header_->skyrimHeartbeatMs : header_->ootHeartbeatMs;
    beat = GetTickCount64();
    pid = GetCurrentProcessId();
    attached_ = true;
    return true;
}
Status Bridge::tick() {
    if (!attached_) return {};
    Lock lock(mutex_);
    if (!lock.locked) return {};
    if (!valid(*header_)) throw std::runtime_error("Bridge protocol changed while attached");
    const auto now = GetTickCount64();
    (side_ == Side::Skyrim ? header_->skyrimHeartbeatMs : header_->ootHeartbeatMs) = now;
    Status result;
    result.snapshot = *header_;
    const auto pid = side_ == Side::Skyrim ? header_->ootPid : header_->skyrimPid;
    const auto beat = side_ == Side::Skyrim ? header_->ootHeartbeatMs : header_->skyrimHeartbeatMs;
    result.peer = pid == 0 ? PeerState::Disconnected :
        (beat > now || now - beat >= protocol::kHeartbeatTimeoutMs ? PeerState::TimedOut : PeerState::Connected);
    return result;
}
void Bridge::disconnect(DWORD waitMs) noexcept {
    if (header_) {
        // A hung peer must not prevent shutdown. A missed clear falls back to timeout.
        const auto result = WaitForSingleObject(mutex_, waitMs);
        if (result == WAIT_OBJECT_0 || result == WAIT_ABANDONED) {
            if (attached_ && valid(*header_)) {
                (side_ == Side::Skyrim ? header_->skyrimPid : header_->ootPid) = 0;
                (side_ == Side::Skyrim ? header_->skyrimHeartbeatMs : header_->ootHeartbeatMs) = 0;
            }
            ReleaseMutex(mutex_);
        }
        UnmapViewOfFile(header_);
        header_ = nullptr;
    }
    if (mapping_) CloseHandle(mapping_);
    mapping_ = nullptr;
    attached_ = false;
}
} // namespace zelrim
