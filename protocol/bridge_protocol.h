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
inline constexpr std::uint32_t kMagic = 0x4D524C5A;
inline constexpr std::uint32_t kVersion = 2;
inline constexpr std::uint32_t kHeaderBytes = 64;
inline constexpr std::uint32_t kTelemetryBytes = 120;
inline constexpr std::uint32_t kMappingBytes = kHeaderBytes + kTelemetryBytes;
inline constexpr std::uint64_t kHeartbeatIntervalMs = 250;
inline constexpr std::uint64_t kHeartbeatTimeoutMs = 3000;
inline constexpr std::uint64_t kTelemetryFreshnessMs = 1000;

enum TelemetryValidity : std::uint32_t {
    kContextPlayable = 1u << 0, kPlayerPresent = 1u << 1, kPaused = 1u << 2,
    kCutscene = 1u << 3, kTransition = 1u << 4,
};
inline constexpr std::uint32_t kKnownValidity =
    kContextPlayable | kPlayerPresent | kPaused | kCutscene | kTransition;
enum class InvalidationReason : std::uint32_t {
    None = 0, NoPlayableContext = 1, LoadingOrTransition = 2, NoPlayer = 3, Detached = 4,
};

// Markers equal sequence only after a complete publication. Invalid records zero
// gameplay fields. Positions/velocities are raw OOT values; yaw is a signed
// binary angle with 65536 units per turn.
struct alignas(8) LinkTelemetry {
    std::uint8_t sessionId[16];
    std::uint64_t sequence;
    std::uint64_t captureUptimeMs;
    std::uint64_t publicationBegin;
    std::uint64_t publicationEnd;
    std::uint32_t validity;
    std::uint32_t invalidationReason;
    std::int16_t sceneId;
    std::int16_t roomId;
    std::uint32_t gameplayFrame;
    std::int32_t linkAge; // pinned source: 0 adult, 1 child; -1 unavailable
    float positionX, positionY, positionZ;
    std::int16_t worldYaw;
    std::int16_t shapeYaw;
    std::uint32_t reserved0;
    float velocityX, velocityY, velocityZ, speedXZ;
    std::uint32_t stateFlags1, stateFlags2, bgCheckFlags;
    std::uint32_t reserved1;
};
struct alignas(8) Header {
    std::uint32_t magic, version, byteSize, reserved0;
    std::uint32_t skyrimPid, ootPid;
    std::uint64_t skyrimHeartbeatMs, ootHeartbeatMs;
    std::uint8_t reserved[24];
    LinkTelemetry link;
};
static_assert(std::endian::native == std::endian::little);
static_assert(std::is_standard_layout_v<LinkTelemetry> && std::is_trivially_copyable_v<LinkTelemetry>);
static_assert(sizeof(LinkTelemetry) == kTelemetryBytes && alignof(LinkTelemetry) == 8);
static_assert(offsetof(LinkTelemetry, sequence) == 16);
static_assert(offsetof(LinkTelemetry, captureUptimeMs) == 24);
static_assert(offsetof(LinkTelemetry, publicationBegin) == 32);
static_assert(offsetof(LinkTelemetry, publicationEnd) == 40);
static_assert(offsetof(LinkTelemetry, validity) == 48);
static_assert(offsetof(LinkTelemetry, sceneId) == 56);
static_assert(offsetof(LinkTelemetry, gameplayFrame) == 60);
static_assert(offsetof(LinkTelemetry, linkAge) == 64);
static_assert(offsetof(LinkTelemetry, positionX) == 68);
static_assert(offsetof(LinkTelemetry, worldYaw) == 80);
static_assert(offsetof(LinkTelemetry, velocityX) == 88);
static_assert(offsetof(LinkTelemetry, stateFlags1) == 104);
static_assert(offsetof(LinkTelemetry, reserved1) == 116);
static_assert(std::is_standard_layout_v<Header> && std::is_trivially_copyable_v<Header>);
static_assert(sizeof(Header) == kMappingBytes && alignof(Header) == 8);
static_assert(offsetof(Header, skyrimPid) == 16);
static_assert(offsetof(Header, skyrimHeartbeatMs) == 24);
static_assert(offsetof(Header, reserved) == 40);
static_assert(offsetof(Header, link) == kHeaderBytes);
} // namespace zelrim::protocol
