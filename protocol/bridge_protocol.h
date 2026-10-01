#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace zelrim::protocol {
inline constexpr wchar_t kMappingName[] = L"Local\\Zelrim_v1";
inline constexpr wchar_t kDataMutexName[] = L"Local\\Zelrim_v1_data";
inline constexpr wchar_t kSkyrimOwnerName[] = L"Local\\Zelrim_v1_skyrim_owner";
inline constexpr wchar_t kOotOwnerName[] = L"Local\\Zelrim_v1_oot_owner";
inline constexpr std::uint32_t kMagic = 0x4D524C5A; // bytes "ZLRM"
inline constexpr std::uint32_t kVersion = 1;
inline constexpr std::uint32_t kMappingBytes = 64;
inline constexpr std::uint64_t kHeartbeatIntervalMs = 250;
inline constexpr std::uint64_t kHeartbeatTimeoutMs = 3000;

// All accesses, including initialization and snapshots, require kDataMutexName.
// Timestamps use the same host's GetTickCount64(), in milliseconds since boot.
// PID == 0 means detached. No pointers, native handles, bools or atomics on wire.
struct alignas(8) Header {
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t byteSize;
    std::uint32_t reserved0;
    std::uint32_t skyrimPid;
    std::uint32_t ootPid;
    std::uint64_t skyrimHeartbeatMs;
    std::uint64_t ootHeartbeatMs;
    std::uint8_t reserved[24];
};
static_assert(std::endian::native == std::endian::little);
static_assert(std::is_standard_layout_v<Header> && std::is_trivially_copyable_v<Header>);
static_assert(sizeof(Header) == kMappingBytes && alignof(Header) == 8);
static_assert(offsetof(Header, magic) == 0);
static_assert(offsetof(Header, version) == 4);
static_assert(offsetof(Header, byteSize) == 8);
static_assert(offsetof(Header, reserved0) == 12);
static_assert(offsetof(Header, skyrimPid) == 16);
static_assert(offsetof(Header, ootPid) == 20);
static_assert(offsetof(Header, skyrimHeartbeatMs) == 24);
static_assert(offsetof(Header, ootHeartbeatMs) == 32);
static_assert(offsetof(Header, reserved) == 40);
} // namespace zelrim::protocol
