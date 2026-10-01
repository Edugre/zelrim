#pragma once
#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <new>
#include <string>
#include <windows.h>
using UInt8 = std::uint8_t;
using UInt16 = std::uint16_t;
using UInt32 = unsigned long;
using UInt64 = unsigned long long;
using SInt8 = std::int8_t;
using SInt16 = std::int16_t;
using SInt32 = long;
using SInt64 = long long;
#ifndef STATIC_ASSERT
#define STATIC_ASSERT(expression) static_assert(expression)
#endif
#ifndef ASSERT
#define ASSERT(expression) ((void)0)
#endif
#ifndef _MESSAGE
#define _MESSAGE(...) ((void)0)
#endif
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
