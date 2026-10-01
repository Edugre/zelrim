#pragma once
#include "protocol/bridge_protocol.h"
#include <windows.h>

namespace zelrim {
enum class Side { Skyrim, Oot };
enum class PeerState { Disconnected, Connected, TimedOut, Unavailable };
struct Status {
    PeerState peer = PeerState::Unavailable;
    protocol::Header snapshot{};
};

// Call from one thread at a time. No game dependencies and no background thread.
class Bridge {
public:
    explicit Bridge(Side side);
    ~Bridge();
    Bridge(const Bridge&) = delete;
    Bridge& operator=(const Bridge&) = delete;
    // False while waiting for the mapping or mutex; other failures throw.
    bool connect();
    Status tick(); // publish heartbeat and read a consistent snapshot
    void disconnect(DWORD waitMs = 100) noexcept;
private:
    Side side_;
    HANDLE owner_ = nullptr;
    HANDLE mutex_ = nullptr;
    HANDLE mapping_ = nullptr;
    protocol::Header* header_ = nullptr;
    bool attached_ = false;
};
} // namespace zelrim
