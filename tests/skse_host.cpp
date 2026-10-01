#include "integrations/skse/api.h"
#include "bridge/bridge.h"
#include <charconv>
#include <iostream>
#include <mutex>
#include <vector>
#include <string_view>
#include <stdexcept>

namespace {
std::mutex queueMutex;
std::vector<TaskDelegate*> queue;
SKSEMessagingInterface::EventCallback listener = nullptr;
void add(TaskDelegate* task) { std::lock_guard lock(queueMutex); queue.push_back(task); }
bool subscribe(PluginHandle, const char*, SKSEMessagingInterface::EventCallback callback) {
    listener = callback; return true;
}
SKSETaskInterface tasks{ SKSETaskInterface::kInterfaceVersion, add, nullptr };
SKSEMessagingInterface messages{ SKSEMessagingInterface::kInterfaceVersion, subscribe, nullptr, nullptr };
void* query(UInt32 id) {
    if (id == kInterface_Task) return &tasks;
    if (id == kInterface_Messaging) return &messages;
    return nullptr;
}
PluginHandle handle() { return 1; }
void drain() {
    std::vector<TaskDelegate*> pending;
    { std::lock_guard lock(queueMutex); pending.swap(queue); }
    for (auto* task : pending) { task->Run(); task->Dispose(); }
}
void require(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
}
int main(int argc, char** argv) {
    try {
        const bool selfTest = argc == 2 && std::string_view(argv[1]) == "--self-test";
        unsigned seconds = 0;
        if (!selfTest) {
            if (argc != 3 || std::string_view(argv[1]) != "--seconds") return 2;
            const std::string_view value(argv[2]);
            const auto result = std::from_chars(value.data(), value.data() + value.size(), seconds);
            if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || !seconds) return 2;
        }
        // Avoid overwriting the real plugin's log during host tests.
        SetEnvironmentVariableW(L"ZELRIM_LOG_PATH", L"NUL");
        const auto module = LoadLibraryW(ZELRIM_TEST_DLL);
        require(module != nullptr, "LoadLibrary failed");
        auto load = reinterpret_cast<bool (*)(const SKSEInterface*)>(GetProcAddress(module, "SKSEPlugin_Load"));
        auto stop = reinterpret_cast<void (*)()>(GetProcAddress(module, "Zelrim_Shutdown"));
        auto running = reinterpret_cast<bool (*)()>(GetProcAddress(module, "Zelrim_IsRunning"));
        auto* version = reinterpret_cast<SKSEPluginVersionData*>(GetProcAddress(module, "SKSEPlugin_Version"));
        require(load && stop && running && version && version->compatibleVersions[0] == RUNTIME_VERSION_1_7_104, "Missing/invalid plugin exports");
        SKSEInterface api{};
        api.runtimeVersion = RUNTIME_VERSION_1_7_104;
        api.QueryInterface = query;
        api.GetPluginHandle = handle;
        if (selfTest) {
            require(!load(nullptr), "Null API accepted");
            api.isEditor = 1; require(!load(&api), "Editor accepted"); api.isEditor = 0;
            api.runtimeVersion = 0; require(!load(&api), "Unknown runtime accepted"); api.runtimeVersion = RUNTIME_VERSION_1_7_104;
            tasks.interfaceVersion = 0; require(!load(&api), "Old task API accepted");
            tasks.interfaceVersion = SKSETaskInterface::kInterfaceVersion;
        }
        require(load(&api), "Plugin load failed");
        require(listener != nullptr, "No SKSE listener");
        SKSEMessagingInterface::Message message{ "SKSE", SKSEMessagingInterface::kMessage_DataLoaded, 0, nullptr };
        listener(&message);
        if (selfTest) {
            zelrim::Bridge oot(zelrim::Side::Oot);
            Sleep(1000);
            { std::lock_guard lock(queueMutex); require(queue.size() == 1, "More than one task queued while stalled"); }
            require(!oot.connect(), "Timer published without game-thread execution");
            drain();
            require(oot.connect() && oot.tick().peer == zelrim::PeerState::Connected, "Task did not publish heartbeat");
            Sleep(3200);
            require(oot.tick().peer == zelrim::PeerState::TimedOut, "Stalled game thread looks alive");
            drain();
            require(oot.tick().peer == zelrim::PeerState::Connected, "Resumed game thread did not recover");
            Sleep(300);
            stop(); stop(); drain();
            require(oot.tick().peer == zelrim::PeerState::Disconnected, "Orderly shutdown failed");
            std::cout << "PASS SKSE exports, API rejection, bounded queue, stall recovery, shutdown\n";
        } else {
            const auto start = GetTickCount64();
            while (GetTickCount64() - start < std::uint64_t(seconds) * 1000) { drain(); Sleep(16); }
            require(running(), "Plugin disabled after transport error");
            // Exercise actual process-detach cleanup rather than the test export.
        }
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
