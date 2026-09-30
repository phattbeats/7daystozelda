#include "soh/Network/Anchor/Anchor.h"
#include <nlohmann/json.hpp>
#include <libultraship/libultraship.h>

extern "C" {
#include "macros.h"
#include "functions.h"
#include "variables.h"
extern PlayState* gPlayState;
}

/**
 * ENEMY_PLAYER_EFFECT
 *
 * Sent by the enemy authority when one of its enemies, targeting a remote
 * player's puppet, applies an effect that only exists on the victim's machine:
 * direct damage (play->damagePlayer), a grab (play->grabPlayer), a freeze
 * (ReDead scream), or a knockback (func_8002F698). The receiver re-applies it to
 * its own Link through the same engine entry point, so i-frames, cutscene
 * guards, and death handling stay native. See EnemyTargeting.h.
 */

void Anchor::SendPacket_EnemyPlayerEffect(uint32_t targetClientId, u8 kind, s32 amount, s16 rot, f32 speed,
                                          f32 yVel, u8 kbType) {
    if (!IsSaveLoaded() || gPlayState == NULL) {
        return;
    }

    nlohmann::json payload;
    payload["type"] = ENEMY_PLAYER_EFFECT;
    payload["targetClientId"] = targetClientId;
    payload["sceneNum"] = gPlayState->sceneNum;
    payload["kind"] = kind;
    payload["amount"] = amount;
    if (kind == ENEMY_EFFECT_KNOCKBACK) {
        payload["rot"] = rot;
        payload["speed"] = speed;
        payload["yVel"] = yVel;
        payload["kbType"] = kbType;
    }

    SendJsonToRemote(payload);
}

void Anchor::HandlePacket_EnemyPlayerEffect(nlohmann::json payload) {
    if (!IsSaveLoaded() || gPlayState == NULL) {
        return;
    }
    // Stale effect from a scene we already left: drop it.
    if (payload.value("sceneNum", (s16)-1) != gPlayState->sceneNum) {
        return;
    }
    Player* self = GET_PLAYER(gPlayState);
    if (self == NULL || (self->stateFlags1 & PLAYER_STATE1_DEAD) ||
        !(myLifeState == LIFE_STATE_ALIVE || myLifeState == LIFE_STATE_REVIVING)) {
        return;
    }

    u8 kind = payload["kind"].get<u8>();
    s32 amount = payload["amount"].get<s32>();

    switch (kind) {
        case ENEMY_EFFECT_HEALTH:
            // Player_InflictDamage: honors i-frames and blocking cutscenes itself.
            gPlayState->damagePlayer(gPlayState, amount);
            break;
        case ENEMY_EFFECT_GRAB:
            if (!(self->stateFlags2 & PLAYER_STATE2_GRABBED_BY_ENEMY)) {
                // func_80852F38: refuses on its own if we're in a cutscene, etc. If it
                // refuses, the authority's grab latch expires and its enemy lets go.
                gPlayState->grabPlayer(gPlayState, self);
            }
            break;
        case ENEMY_EFFECT_FREEZE:
            if (!Player_InBlockingCsMode(gPlayState, self) && amount > self->actor.freezeTimer) {
                self->actor.freezeTimer = (s16)amount;
            }
            break;
        case ENEMY_EFFECT_KNOCKBACK:
            func_8002F698(gPlayState, &self->actor, payload["speed"].get<f32>(), payload["rot"].get<s16>(),
                          payload["yVel"].get<f32>(), payload["kbType"].get<u8>(), (u32)amount);
            break;
        default:
            break;
    }
}
