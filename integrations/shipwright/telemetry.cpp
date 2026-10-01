#include "integrations/shipwright/heartbeat.h"
#include "global.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"

extern PlayState* gPlayState;

namespace zelrim::shipwright {
namespace {
bool loadedSave = false;
bool hadValidPlayer = false;

std::uint32_t contextBits(const PlayState* play) {
    std::uint32_t bits = 0;
    if (play->pauseCtx.state != 0) bits |= protocol::kPaused;
    if (play->csCtx.state != CS_STATE_IDLE) bits |= protocol::kCutscene;
    if (play->transitionTrigger != TRANS_TRIGGER_OFF || play->transitionMode != TRANS_MODE_OFF)
        bits |= protocol::kTransition;
    return bits;
}
void markInvalid(protocol::InvalidationReason reason, std::uint32_t bits = 0) {
    hadValidPlayer = false;
    invalidate(reason, bits);
}
void capturePlayer() {
    PlayState* play = gPlayState;
    if (!loadedSave || !play) { markInvalid(protocol::InvalidationReason::NoPlayableContext); return; }
    const auto bits = contextBits(play);
    if (bits & protocol::kTransition) { markInvalid(protocol::InvalidationReason::LoadingOrTransition, bits); return; }
    Player* player = GET_PLAYER(play);
    if (!player) { markInvalid(protocol::InvalidationReason::NoPlayer, bits); return; }
    TelemetryInput sample{};
    sample.validity = protocol::kContextPlayable | protocol::kPlayerPresent | bits;
    sample.invalidationReason = protocol::InvalidationReason::None;
    sample.sceneId = play->sceneNum;
    sample.roomId = play->roomCtx.curRoom.num;
    sample.gameplayFrame = play->gameplayFrames;
    sample.linkAge = gSaveContext.linkAge;
    sample.positionX = player->actor.world.pos.x;
    sample.positionY = player->actor.world.pos.y;
    sample.positionZ = player->actor.world.pos.z;
    sample.worldYaw = player->actor.world.rot.y;
    sample.shapeYaw = player->actor.shape.rot.y;
    sample.velocityX = player->actor.velocity.x;
    sample.velocityY = player->actor.velocity.y;
    sample.velocityZ = player->actor.velocity.z;
    sample.speedXZ = player->actor.speedXZ;
    sample.stateFlags1 = player->stateFlags1;
    sample.stateFlags2 = player->stateFlags2;
    sample.bgCheckFlags = player->actor.bgCheckFlags;
    hadValidPlayer = true;
    publish(sample);
}
void frame() {
    tick();
    if (!hadValidPlayer || !gPlayState) return;
    const auto bits = contextBits(gPlayState);
    if (bits & protocol::kPaused) markInvalid(protocol::InvalidationReason::NoPlayableContext, bits);
    else if (bits & protocol::kTransition) markInvalid(protocol::InvalidationReason::LoadingOrTransition, bits);
    else if (!GET_PLAYER(gPlayState)) markInvalid(protocol::InvalidationReason::NoPlayer, bits);
}
}

void installTelemetryHooks() noexcept {
    try {
        GameInteractor::Instance->RegisterGameHook<GameInteractor::OnLoadGame>([](int32_t) {
            loadedSave = true; markInvalid(protocol::InvalidationReason::LoadingOrTransition, protocol::kTransition);
        });
        GameInteractor::Instance->RegisterGameHook<GameInteractor::OnExitGame>([](int32_t) {
            loadedSave = false; markInvalid(protocol::InvalidationReason::NoPlayableContext);
        });
        GameInteractor::Instance->RegisterGameHook<GameInteractor::OnZTitleInit>([](void*) {
            loadedSave = false; markInvalid(protocol::InvalidationReason::NoPlayableContext);
        });
        GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSceneInit>([](int16_t) {
            markInvalid(protocol::InvalidationReason::LoadingOrTransition, protocol::kTransition);
        });
        GameInteractor::Instance->RegisterGameHook<GameInteractor::OnPlayDestroy>([]() {
            markInvalid(protocol::InvalidationReason::NoPlayableContext);
        });
        GameInteractor::Instance->RegisterGameHook<GameInteractor::OnPlayerUpdate>(capturePlayer);
        GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameFrameUpdate>(frame);
    } catch (...) {
        invalidate(protocol::InvalidationReason::NoPlayableContext);
    }
}
} // namespace zelrim::shipwright
