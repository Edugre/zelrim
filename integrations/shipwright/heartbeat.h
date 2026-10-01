#pragma once
namespace zelrim::shipwright {
// Call serially on the game thread. Callback must not retain message pointers.
using Log = void (*)(const char* message);
void start(Log log) noexcept;
void tick() noexcept;
void stop() noexcept;
}
