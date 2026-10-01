#pragma once
#include <cstdint>
using UInt32 = std::uint32_t;
using UInt64 = std::uint64_t;
#include "skse64/PluginAPI.h"
#include "skse64_common/skse_version.h"

// Public TaskDelegate ABI from pinned SKSE's skse64/GameThreads.h. That header
// also imports engine structures we deliberately do not depend on.
class TaskDelegate {
public:
    virtual void Run() = 0;
    virtual void Dispose() = 0;
};
static_assert(sizeof(TaskDelegate) == sizeof(void*));
