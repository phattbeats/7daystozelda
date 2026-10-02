#include "soh/Network/Anchor/Anchor.h"
#include "soh/Network/Anchor/EnemyTargeting.h"
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
    }
    if (kind == ENEMY_EFFECT_KNOCKBACK || kind == ENEMY_EFFECT_HEALTH) {
        payload["kbType"] = kbType; // HEALTH: 1 = grab bite
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
    u8 kind = payload["kind"].get<u8>();
    if (kind == ENEMY_EFFECT_GRAB_REFUSED) {
        // We're the authority; the victim couldn't be grabbed.
        EnemyTargeting::ClearGrabLatch(payload["clientId"].get<uint32_t>());
        return;
    }
    Player* self = GET_PLAYER(gPlayState);
    if (self == NULL || (self->stateFlags1 & PLAYER_STATE1_DEAD) ||
        !(myLifeState == LIFE_STATE_ALIVE || myLifeState == LIFE_STATE_REVIVING)) {
        return;
    }

    s32 amount = payload["amount"].get<s32>();

    switch (kind) {
        case ENEMY_EFFECT_HEALTH:
            // kbType carries the grab-bite tag: a bite for a grab we never entered
            // (refused, or escaped before it landed) doesn't apply.
            if (payload.value("kbType", (u8)0) != 0 && !(self->stateFlags2 & PLAYER_STATE2_GRABBED_BY_ENEMY)) {
                break;
            }
            // Player_InflictDamage: honors i-frames and blocking cutscenes itself.
            gPlayState->damagePlayer(gPlayState, amount);
            break;
        case ENEMY_EFFECT_GRAB: {
            // A ReDead zeroes the victim's freeze right before grabbing; our copy of
            // that freeze came as a separate effect, so clear it here too.
            self->actor.freezeTimer = 0;
            bool grabbed = (self->stateFlags2 & PLAYER_STATE2_GRABBED_BY_ENEMY) != 0;
            // func_80852F38 refuses on its own (cutscene, hookshot, ...). Tell the
            // authority so its enemy lets go instead of biting at nothing.
            if (!grabbed && !gPlayState->grabPlayer(gPlayState, self)) {
                SendPacket_EnemyPlayerEffect(payload["clientId"].get<uint32_t>(), ENEMY_EFFECT_GRAB_REFUSED, 0, 0,
                                             0.0f, 0.0f, 0);
            }
            break;
        }
        case ENEMY_EFFECT_RELEASE:
            // Same writes the enemies make on a real Link when they let go.
            if (self->stateFlags2 & PLAYER_STATE2_GRABBED_BY_ENEMY) {
                self->stateFlags2 &= ~PLAYER_STATE2_GRABBED_BY_ENEMY;
                self->actor.parent = NULL;
                self->av2.actionVar2 = 200;
            }
            break;
        case ENEMY_EFFECT_FREEZE:
            if (!Player_InBlockingCsMode(gPlayState, self) && amount > self->actor.freezeTimer) {
                self->actor.freezeTimer = (s16)amount;
            }
            break;
        case ENEMY_EFFECT_KNOCKBACK:
            // Enemies with attack colliders already hit us natively (mirrored AT
            // colliders); by the time the relayed knockback lands we're in i-frames or
            // the damage action, so skip it rather than launch us twice.
            if (self->invincibilityTimer != 0 || (self->stateFlags1 & PLAYER_STATE1_DAMAGED)) {
                break;
            }
            func_8002F698(gPlayState, &self->actor, payload["speed"].get<f32>(), payload["rot"].get<s16>(),
                          payload["yVel"].get<f32>(), payload["kbType"].get<u8>(), (u32)amount);
            break;
        default:
            break;
    }
}
