#include "integrations/skse/api.h"
#include "bridge/bridge.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>

namespace {
// Process-lifetime storage: no thread joins or C++ destructors under loader lock.
struct State {
    const SKSETaskInterface* tasks = nullptr;
    PTP_TIMER timer = nullptr;
    std::atomic<bool> active{false};
    std::atomic<bool> pending{false};
    std::unique_ptr<zelrim::Bridge> bridge;
    HANDLE log = INVALID_HANDLE_VALUE;
    zelrim::PeerState previous = zelrim::PeerState::Unavailable;
    std::uint32_t previousPid = 0;
    bool reported = false;
    bool started = false;
};
State* state = nullptr;
void log(const char* message) noexcept {
    OutputDebugStringA(message);
    if (state && state->log != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(state->log, message, static_cast<DWORD>(std::strlen(message)), &written, nullptr);
        WriteFile(state->log, "\r\n", 2, &written, nullptr);
        FlushFileBuffers(state->log);
    }
}
const char* name(zelrim::PeerState peer) {
    switch (peer) {
    case zelrim::PeerState::Connected: return "connected";
    case zelrim::PeerState::Disconnected: return "disconnected";
    case zelrim::PeerState::TimedOut: return "timed-out";
    default: return "unavailable";
    }
}
class HeartbeatTask final : public TaskDelegate {
public:
    void Run() override {
        if (!state->active.load()) return;
        try {
            if (!state->bridge) state->bridge = std::make_unique<zelrim::Bridge>(zelrim::Side::Skyrim);
            const auto status = state->bridge->connect() ? state->bridge->tick() : zelrim::Status{};
            if (!state->reported || status.peer != state->previous || status.snapshot.ootPid != state->previousPid) {
                char message[256];
                std::snprintf(message, sizeof(message),
                    "Zelrim peer=%s skyrimPid=%lu ootPid=%lu skyrimHeartbeatMs=%llu ootHeartbeatMs=%llu",
                    name(status.peer), static_cast<unsigned long>(status.snapshot.skyrimPid),
                    static_cast<unsigned long>(status.snapshot.ootPid),
                    static_cast<unsigned long long>(status.snapshot.skyrimHeartbeatMs),
                    static_cast<unsigned long long>(status.snapshot.ootHeartbeatMs));
                log(message);
                state->reported = true;
                state->previous = status.peer;
                state->previousPid = status.snapshot.ootPid;
            }
        } catch (const std::exception& error) {
            log(error.what());
            log("Zelrim disabled until process restart");
            state->active.store(false);
            state->bridge.reset();
        } catch (...) {
            log("Zelrim disabled: unknown heartbeat failure");
            state->active.store(false);
            state->bridge.reset();
        }
    }
    void Dispose() override {
        state->pending.store(false);
        delete this;
    }
};
void CALLBACK schedule(PTP_CALLBACK_INSTANCE, void*, PTP_TIMER) {
    if (!state->active.load() || state->pending.exchange(true)) return;
    auto* task = new (std::nothrow) HeartbeatTask;
    if (!task) { state->pending.store(false); return; }
    state->tasks->AddTask(task);
}
void onMessage(SKSEMessagingInterface::Message* message) {
    if (!message || message->type != SKSEMessagingInterface::kMessage_DataLoaded || state->started) return;
    state->started = true;
    state->active.store(true);
    FILETIME due{}; // immediate, then every 250 ms
    SetThreadpoolTimer(state->timer, &due, 250, 0);
    log("Zelrim SKSE scheduling enabled");
}
void openLog() {
    wchar_t path[32768];
    auto length = GetEnvironmentVariableW(L"ZELRIM_LOG_PATH", path, 32768);
    if (!length || length >= 32768) {
        length = GetEnvironmentVariableW(L"LOCALAPPDATA", path, 32000);
        if (!length || length >= 32000) return;
        wcscat_s(path, L"\\Zelrim");
        CreateDirectoryW(path, nullptr);
        wcscat_s(path, L"\\ZelrimSKSE.log");
    }
    state->log = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
}
}

extern "C" __declspec(dllexport) SKSEPluginVersionData SKSEPlugin_Version = {
    SKSEPluginVersionData::kVersion, 1, "Zelrim", "Zelrim", "",
    SKSEPluginVersionData::kVersionIndependentEx_NoStructUse, 0,
    { RUNTIME_VERSION_1_7_104, 0 }, 0
};
extern "C" __declspec(dllexport) bool SKSEPlugin_Load(const SKSEInterface* skse) {
    if (state || !skse || skse->isEditor || skse->runtimeVersion != RUNTIME_VERSION_1_7_104 ||
        !skse->QueryInterface || !skse->GetPluginHandle) return false;
    auto* tasks = static_cast<SKSETaskInterface*>(skse->QueryInterface(kInterface_Task));
    auto* messages = static_cast<SKSEMessagingInterface*>(skse->QueryInterface(kInterface_Messaging));
    if (!tasks || tasks->interfaceVersion < SKSETaskInterface::kInterfaceVersion || !tasks->AddTask ||
        !messages || messages->interfaceVersion < SKSEMessagingInterface::kInterfaceVersion || !messages->RegisterListener)
        return false;
    state = new (std::nothrow) State;
    if (!state) return false;
    state->tasks = tasks;
    state->timer = CreateThreadpoolTimer(schedule, nullptr, nullptr);
    if (!state->timer) { delete state; state = nullptr; return false; }
    // SKSE plugins are process-lifetime. Pin code before handing pointers to SKSE.
    HMODULE self;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&SKSEPlugin_Load), &self)) {
        CloseThreadpoolTimer(state->timer); delete state; state = nullptr; return false;
    }
    if (!messages->RegisterListener(skse->GetPluginHandle(), "SKSE", onMessage)) {
        CloseThreadpoolTimer(state->timer); delete state; state = nullptr; return false;
    }
    openLog();
    log("Zelrim SKSE loaded for runtime 1.7.104; waiting for DataLoaded");
    return true;
}
// Optional orderly shutdown for the test host / future game-thread shutdown hook.
// Must run on the game thread. Not called from DllMain.
extern "C" __declspec(dllexport) void Zelrim_Shutdown() {
    if (!state) return;
    state->active.store(false);
    SetThreadpoolTimer(state->timer, nullptr, 0, 0);
    WaitForThreadpoolTimerCallbacks(state->timer, TRUE);
    state->bridge.reset();
    log("Zelrim SKSE detached");
}
extern "C" __declspec(dllexport) bool Zelrim_IsRunning() {
    return state && state->active.load();
}
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(module);
    // On process termination other threads are gone. Never wait under loader lock.
    // TerminateProcess skips this callback and is detected by heartbeat timeout.
    if (reason == DLL_PROCESS_DETACH && reserved && state && state->bridge)
        state->bridge->disconnect(0);
    return TRUE;
}
