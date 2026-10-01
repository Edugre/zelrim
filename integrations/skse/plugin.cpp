#include "integrations/skse/api.h"
#include "integrations/skse/world_telemetry.h"
#include "integrations/skse/movement_authority.h"
#include "bridge/bridge.h"
#include "skse64/GameAPI.h"
#include "skse64/GameForms.h"
#include "skse64/GameMenus.h"
#include "skse64/GameReferences.h"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <set>
#include <string>

RelocAddr<_LookupFormByID> LookupFormByID(0x001E5860);
RelocAddr<_MoveRefrToPosition> MoveRefrToPosition(0x00A5D370);
RelocAddr<_PlaceAtMe_Native> PlaceAtMe_Native(0x00A46DE0);

// The pinned header declares these wrappers, but the plugin deliberately links
// only the small subset of SKSE implementation files it needs.
IMenu* MenuManager::GetMenu(BSFixedString* menuName) {
    if (!menuName->data) return nullptr;
    MenuTableItem* item = menuTable.Find(menuName);
    return item ? item->menuInstance : nullptr;
}

namespace {
class PauseTracker final : public BSTEventSink<MenuOpenCloseEvent> {
public:
    EventResult ReceiveEvent(MenuOpenCloseEvent* event, EventDispatcher<MenuOpenCloseEvent>*) override {
        if (!event || !event->menuName.data) return kEvent_Continue;
        const std::string name(event->menuName.data);
        if (!event->opening) { pausingMenus_.erase(name); return kEvent_Continue; }
        auto* manager = MenuManager::GetSingleton();
        auto* menu = manager ? manager->GetMenu(&event->menuName) : nullptr;
        if (menu && (menu->flags & IMenu::kFlag_PausesGame)) pausingMenus_.insert(name);
        return kEvent_Continue;
    }
    bool paused() const noexcept { return !pausingMenus_.empty(); }
private:
    std::set<std::string> pausingMenus_;
};
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
    zelrim::TelemetryState previousTelemetry = zelrim::TelemetryState::Unavailable;
    std::uint8_t previousSession[16]{};
    std::uint64_t lastTelemetryLogMs = 0;
    zelrim::skse::WorldTelemetryState worldState;
    PauseTracker pauseTracker;
    bool pauseEventsRegistered = false;
    zelrim::TelemetryState previousSkyrimTelemetry = zelrim::TelemetryState::Unavailable;
    std::uint8_t previousSkyrimSession[16]{};
    std::uint64_t lastSkyrimTelemetryLogMs = 0;
    bool testMode = false;
    bool started = false;
    bool movementProofEnabled = false;
    std::uint32_t proxyBaseFormId = 0;
    zelrim::skse::MovementAuthority movementAuthority;
    TESObjectREFR* proxy = nullptr;
    std::uint32_t proxyRuntimeFormId = 0;
    std::uint64_t lastWorldPublicationMs = 0;
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
const char* telemetryName(zelrim::TelemetryState value) {
    switch (value) {
    case zelrim::TelemetryState::Missing: return "missing";
    case zelrim::TelemetryState::Invalid: return "invalid";
    case zelrim::TelemetryState::Stale: return "stale";
    case zelrim::TelemetryState::Usable: return "usable";
    default: return "unavailable";
    }
}
bool environmentEnabled(const wchar_t* name) {
    wchar_t value[16]{};
    const auto count = GetEnvironmentVariableW(name, value, 16);
    return count && count < 16 && (value[0] == L'1' || value[0] == L'y' || value[0] == L'Y');
}
bool environmentUnsigned(const wchar_t* name, std::uint32_t& value) {
    wchar_t text[32]{};
    const auto count = GetEnvironmentVariableW(name, text, 32);
    if (!count || count >= 32) return false;
    wchar_t* end = nullptr;
    const auto parsed = std::wcstoul(text, &end, 0);
    if (!end || *end || parsed > 0xFFFFFFFFul) return false;
    value = static_cast<std::uint32_t>(parsed);
    return true;
}
float environmentFloat(const wchar_t* name, float fallback) {
    wchar_t text[32]{};
    const auto count = GetEnvironmentVariableW(name, text, 32);
    if (!count || count >= 32) return fallback;
    wchar_t* end = nullptr;
    const auto value = std::wcstof(text, &end);
    return end && !*end && std::isfinite(value) ? value : fallback;
}
void configureMovementProof() {
    if (state->testMode || !environmentEnabled(L"ZELRIM_MOVEMENT_PROOF")) return;
    std::uint32_t base = 0;
    if (!environmentUnsigned(L"ZELRIM_PROXY_BASE_FORM_ID", base) || !base) {
        log("Zelrim movement proof requested without ZELRIM_PROXY_BASE_FORM_ID; disabled");
        return;
    }
    const auto scale = environmentFloat(L"ZELRIM_MOVEMENT_SCALE", 1.0f);
    const auto yawSign = environmentFloat(L"ZELRIM_MOVEMENT_YAW_SIGN", 1.0f);
    const auto maxDelta = environmentFloat(L"ZELRIM_MOVEMENT_MAX_DELTA", 1000.0f);
    state->movementAuthority = zelrim::skse::MovementAuthority({ scale, yawSign, maxDelta });
    state->movementProofEnabled = true;
    state->proxyBaseFormId = base;
    log("Zelrim movement proof enabled for disposable no-save validation");
}
void retireProxy(const char* reason, PlayerCharacter* player) noexcept {
    if (!state->proxy) return;
    if (player && player->parentCell && state->proxy->parentCell == player->parentCell) {
        NiPoint3 position = player->pos;
        position.z -= 10000.0f;
        NiPoint3 rotation = player->rot;
        rotation.x = rotation.y = rotation.z = 0.0f;
        UInt32 nullHandle = 0;
        MoveRefrToPosition(state->proxy, &nullHandle, player->parentCell,
            player->parentCell->worldSpace, &position, &rotation);
    }
    char message[256];
    std::snprintf(message, sizeof(message), "Zelrim movement proxy retired form=0x%08lx reason=%s",
        static_cast<unsigned long>(state->proxyRuntimeFormId), reason);
    log(message);
    state->proxy = nullptr;
    state->proxyRuntimeFormId = 0;
}
bool ensureProxy(PlayerCharacter* player) noexcept {
    if (state->proxy) return true;
    if (!player || !player->parentCell) return false;
    auto* base = LookupFormByID(state->proxyBaseFormId);
    if (!base || base->formType == kFormType_NPC || base->formType == kFormType_Character) {
        log("Zelrim movement proof rejected missing/actor proxy base form");
        state->movementProofEnabled = false;
        return false;
    }
    state->proxy = PlaceAtMe_Native(nullptr, 0, player, base, 1, false, false);
    if (!state->proxy) {
        log("Zelrim movement proof could not create proxy");
        state->movementProofEnabled = false;
        return false;
    }
    state->proxyRuntimeFormId = state->proxy->formID;
    char message[256];
    std::snprintf(message, sizeof(message),
        "Zelrim movement proxy created base=0x%08lx form=0x%08lx cell=0x%08lx",
        static_cast<unsigned long>(state->proxyBaseFormId),
        static_cast<unsigned long>(state->proxyRuntimeFormId),
        static_cast<unsigned long>(player->parentCell->formID));
    log(message);
    return true;
}
void applyMovement(const zelrim::skse::MovementDecision& decision, PlayerCharacter* player) noexcept {
    if (decision.action == zelrim::skse::MovementAction::None) return;
    if (decision.action == zelrim::skse::MovementAction::Reset) {
        retireProxy(zelrim::skse::movementResetName(decision.resetReason), player);
        return;
    }
    if (!ensureProxy(player)) return;
    NiPoint3 position{ decision.positionX, decision.positionY, decision.positionZ };
    NiPoint3 rotation{ 0.0f, 0.0f, decision.rotationZ };
    UInt32 nullHandle = 0;
    MoveRefrToPosition(state->proxy, &nullHandle, player->parentCell,
        player->parentCell->worldSpace, &position, &rotation);
    if (decision.action == zelrim::skse::MovementAction::Calibrate || decision.linkSequence % 60 == 0) {
        char message[320];
        std::snprintf(message, sizeof(message),
            "Zelrim movement=%s proxy=0x%08lx linkSequence=%llu pos=(%.3f,%.3f,%.3f) yaw=%.6f",
            decision.action == zelrim::skse::MovementAction::Calibrate ? "calibrated" : "applied",
            static_cast<unsigned long>(state->proxyRuntimeFormId),
            static_cast<unsigned long long>(decision.linkSequence), position.x, position.y, position.z, rotation.z);
        log(message);
    }
}
class HeartbeatTask final : public TaskDelegate {
public:
    void Run() override {
        if (!state->active.load()) return;
        try {
            if (!state->bridge) state->bridge = std::make_unique<zelrim::Bridge>(zelrim::Side::Skyrim);
            zelrim::Status status{};
            if (state->bridge->connect()) {
                zelrim::skse::WorldSample source{};
                auto* player = state->testMode ? nullptr : *g_thePlayer;
                source.playerPresent = player != nullptr;
                if (player) {
                    source.playerFormId = player->formID;
                    source.positionX = player->pos.x; source.positionY = player->pos.y; source.positionZ = player->pos.z;
                    source.rotationX = player->rot.x; source.rotationY = player->rot.y; source.rotationZ = player->rot.z;
                    auto* cell = player->parentCell;
                    source.cellPresent = cell != nullptr;
                    if (cell) {
                        source.cellFormId = cell->formID;
                        source.worldspaceFormId = cell->worldSpace ? cell->worldSpace->formID : 0;
                    }
                }
                const auto captureNow = GetTickCount64();
                if (captureNow - state->lastWorldPublicationMs >= zelrim::protocol::kHeartbeatIntervalMs) {
                    state->bridge->publishSkyrimTelemetry(
                        state->worldState.capture(source, state->pauseTracker.paused()));
                    state->lastWorldPublicationMs = captureNow;
                }
                status = state->bridge->tick(state->movementProofEnabled ? 0 : 100);
                if (state->movementProofEnabled && status.peer != zelrim::PeerState::Unavailable)
                    applyMovement(state->movementAuthority.update(status), player);
            }
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
            const auto now = GetTickCount64();
            const auto& t = status.snapshot.link;
            const bool sessionChanged = std::memcmp(state->previousSession, t.sessionId, 16) != 0;
            const bool transition = sessionChanged || status.telemetry != state->previousTelemetry;
            if (transition || (status.telemetry == zelrim::TelemetryState::Usable &&
                               now - state->lastTelemetryLogMs >= 1000)) {
                char session[33];
                for (unsigned i = 0; i < 16; ++i) std::snprintf(session + i * 2, 3, "%02x", t.sessionId[i]);
                char message[640];
                std::snprintf(message, sizeof(message),
                    "Zelrim telemetry=%s session=%s sequence=%llu ageMs=%llu validity=0x%08lx reason=%lu "
                    "scene=%d room=%d frame=%lu pos=(%.3f,%.3f,%.3f) yaw=(%d,%d) flags=(0x%08lx,0x%08lx,0x%08lx)",
                    telemetryName(status.telemetry), session, static_cast<unsigned long long>(t.sequence),
                    static_cast<unsigned long long>(status.telemetryAgeMs), static_cast<unsigned long>(t.validity),
                    static_cast<unsigned long>(t.invalidationReason), int(t.sceneId), int(t.roomId),
                    static_cast<unsigned long>(t.gameplayFrame), t.positionX, t.positionY, t.positionZ,
                    int(t.worldYaw), int(t.shapeYaw), static_cast<unsigned long>(t.stateFlags1),
                    static_cast<unsigned long>(t.stateFlags2), static_cast<unsigned long>(t.bgCheckFlags));
                log(message);
                std::memcpy(state->previousSession, t.sessionId, 16);
                state->previousTelemetry = status.telemetry;
                state->lastTelemetryLogMs = now;
            }
            const auto& s = status.snapshot.skyrim;
            const bool skyrimSessionChanged = std::memcmp(state->previousSkyrimSession, s.sessionId, 16) != 0;
            const bool skyrimTransition = skyrimSessionChanged ||
                status.skyrimTelemetry != state->previousSkyrimTelemetry;
            if (skyrimTransition || (status.skyrimTelemetry == zelrim::TelemetryState::Usable &&
                                    now - state->lastSkyrimTelemetryLogMs >= 1000)) {
                char session[33];
                for (unsigned i = 0; i < 16; ++i) std::snprintf(session + i * 2, 3, "%02x", s.sessionId[i]);
                char message[640];
                std::snprintf(message, sizeof(message),
                    "Zelrim skyrimTelemetry=%s session=%s sequence=%llu ageMs=%llu validity=0x%08lx reason=%lu "
                    "player=0x%08lx cell=0x%08lx worldspace=0x%08lx pos=(%.3f,%.3f,%.3f) rot=(%.6f,%.6f,%.6f)",
                    telemetryName(status.skyrimTelemetry), session, static_cast<unsigned long long>(s.sequence),
                    static_cast<unsigned long long>(status.skyrimTelemetryAgeMs), static_cast<unsigned long>(s.validity),
                    static_cast<unsigned long>(s.invalidationReason), static_cast<unsigned long>(s.playerFormId),
                    static_cast<unsigned long>(s.cellFormId), static_cast<unsigned long>(s.worldspaceFormId),
                    s.positionX, s.positionY, s.positionZ, s.rotationX, s.rotationY, s.rotationZ);
                log(message);
                std::memcpy(state->previousSkyrimSession, s.sessionId, 16);
                state->previousSkyrimTelemetry = status.skyrimTelemetry;
                state->lastSkyrimTelemetryLogMs = now;
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
    if (!message || !state) return;
    switch (message->type) {
    case SKSEMessagingInterface::kMessage_PreLoadGame:
        state->worldState.preLoad();
        break;
    case SKSEMessagingInterface::kMessage_PostLoadGame:
        state->worldState.postLoad(message->data != nullptr);
        break;
    case SKSEMessagingInterface::kMessage_NewGame:
        state->worldState.newGame();
        break;
    case SKSEMessagingInterface::kMessage_DataLoaded:
        if (state->started) break;
        state->started = true;
        configureMovementProof();
        if (!state->testMode) if (auto* manager = MenuManager::GetSingleton()) {
            manager->MenuOpenCloseEventDispatcher()->AddEventSink(&state->pauseTracker);
            state->pauseEventsRegistered = true;
        }
        state->active.store(true);
        { FILETIME due{}; SetThreadpoolTimer(state->timer, &due,
            state->movementProofEnabled ? 16 : 250, 0); }
        log("Zelrim SKSE scheduling enabled");
        break;
    default:
        break;
    }
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
    if (state->movementProofEnabled) {
        auto* player = state->testMode ? nullptr : *g_thePlayer;
        retireProxy("shutdown", player);
    }
    state->bridge.reset();
    log("Zelrim SKSE detached");
}
extern "C" __declspec(dllexport) bool Zelrim_IsRunning() {
    return state && state->active.load();
}
// Test-host gate: prevents dereferencing Skyrim runtime relocations outside Skyrim.
extern "C" __declspec(dllexport) void Zelrim_EnableTestMode() {
    if (state && !state->started) state->testMode = true;
}
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(module);
    // On process termination other threads are gone. Never wait under loader lock.
    // TerminateProcess skips this callback and is detected by heartbeat timeout.
    if (reason == DLL_PROCESS_DETACH && reserved && state && state->bridge)
        state->bridge->disconnect(0);
    return TRUE;
}
