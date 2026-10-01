// Runs the exact adapter that is compiled into Shipwright, without game assets.
#include "integrations/shipwright/heartbeat.h"
#include <windows.h>
#include <charconv>
#include <iostream>
#include <string_view>

namespace {
bool failed = false;
void log(const char* message) {
    std::cout << message << std::endl;
    if (std::string_view(message).find("disabled until") != std::string_view::npos) failed = true;
}
}
int main(int argc, char** argv) {
    if (argc != 3 || std::string_view(argv[1]) != "--seconds") return 2;
    unsigned seconds = 0;
    const std::string_view value(argv[2]);
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), seconds);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || !seconds) return 2;
    zelrim::shipwright::start(log);
    const auto start = GetTickCount64();
    while (!failed && GetTickCount64() - start < std::uint64_t(seconds) * 1000) {
        zelrim::shipwright::tick();
        Sleep(16); // Simulate the frame callback; adapter internally limits IPC to 4 Hz.
    }
    zelrim::shipwright::stop();
    return failed ? 1 : 0;
}
