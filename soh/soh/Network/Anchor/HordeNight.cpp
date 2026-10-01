#include "soh/Network/Anchor/HordeNight.h"
#include "soh/Network/Anchor/EnemySync.h"
#include "soh/Network/Anchor/Anchor.h"
#include "soh/Notification/Notification.h"
#include <libultraship/libultraship.h>

#include <algorithm>
#include <vector>

extern "C" {
#include "macros.h"
#include "functions.h"
#include "variables.h"
extern PlayState* gPlayState;
}

namespace HordeNight {

namespace {

struct SpawnEntry {
    int16_t actorId;
    int16_t params;
    int32_t weight;
};

// ReDead/Gibdo params: high byte 0x3F makes their on-death switch flag a TEMP
// switch (0x20-0x3F, reset on scene exit) instead of flag 0, which would be a
// permanent — and Anchor-synced — switch in the field/ranch. Low byte 0x01 =
// standing ReDead; 0xFE (bit 0x80 set) = standing Gibdo (params become -2).
constexpr int16_t REDEAD_PARAMS = 0x3F01;
constexpr int16_t GIBDO_PARAMS = 0x3FFE;
constexpr int16_t WOLFOS_PARAMS = (int16_t)0xFF00; // same as the vanilla field spawner
constexpr int16_t STALCHILD_PARAMS = 0;
constexpr int16_t BIG_STALCHILD_PARAMS = 10; // vanilla escalates by 5 per 10 kills

const std::vector<SpawnEntry>& CompositionFor(int32_t horde) {
    static const std::vector<SpawnEntry> first = {
        { ACTOR_EN_SKB, STALCHILD_PARAMS, 70 },
        { ACTOR_EN_RD, REDEAD_PARAMS, 30 },
    };
    static const std::vector<SpawnEntry> second = {
        { ACTOR_EN_SKB, STALCHILD_PARAMS, 50 },
        { ACTOR_EN_RD, REDEAD_PARAMS, 30 },
        { ACTOR_EN_RD, GIBDO_PARAMS, 12 },
        { ACTOR_EN_WF, WOLFOS_PARAMS, 8 },
    };
    static const std::vector<SpawnEntry> later = {
        { ACTOR_EN_SKB, STALCHILD_PARAMS, 30 },
        { ACTOR_EN_SKB, BIG_STALCHILD_PARAMS, 10 },
        { ACTOR_EN_RD, REDEAD_PARAMS, 28 },
        { ACTOR_EN_RD, GIBDO_PARAMS, 17 },
        { ACTOR_EN_WF, WOLFOS_PARAMS, 15 },
    };
    return horde <= 1 ? first : (horde == 2 ? second : later);
}

bool sActive = false;
int32_t sActiveHorde = 0;
int32_t sSpawnTimer = 0;

bool IsHordeScene(s16 sceneNum) {
    return sceneNum == SCENE_HYRULE_FIELD || sceneNum == SCENE_LON_LON_RANCH;
}

int32_t Interval() {
    return std::max(1, CVarGetInteger(CVAR_REMOTE_ANCHOR("HordeInterval"), 3));
}

bool Forced() {
    return CVarGetInteger(CVAR_REMOTE_ANCHOR("HordeNightForce"), 0) != 0;
}

int32_t MaxAlive(int32_t horde) {
    int32_t base = std::max(1, CVarGetInteger(CVAR_REMOTE_ANCHOR("HordeMaxAlive"), 10));
    return std::min(24, base + 2 * std::max(0, horde - 1));
}

// ReDeads/Gibdos are leashed: they only chase within ~150 units of home.pos and
// otherwise walk back to it (z_en_rd.c). During a horde, drag each one's home
// toward the nearest living player at shambling pace so they close in like
// zombies instead of standing where they spawned.
constexpr f32 HORDE_SHAMBLE_SPEED = 1.2f; // units per frame

bool AnyHordeOnlyEnemies() {
    for (Actor* a = gPlayState->actorCtx.actorLists[ACTORCAT_ENEMY].head; a != nullptr; a = a->next) {
        if (a->id == ACTOR_EN_RD || a->id == ACTOR_EN_WF) {
            return true;
        }
    }
    return false;
}

void ShambleTowardPlayers(const std::vector<Actor*>& players) {
    if (players.empty()) {
        return;
    }
    for (Actor* a = gPlayState->actorCtx.actorLists[ACTORCAT_ENEMY].head; a != nullptr; a = a->next) {
        if (a->id != ACTOR_EN_RD || a->colChkInfo.health == 0) {
            continue;
        }
        Actor* nearest = players[0];
        f32 best = Math_Vec3f_DistXZ(&a->home.pos, &nearest->world.pos);
        for (size_t i = 1; i < players.size(); i++) {
            f32 d = Math_Vec3f_DistXZ(&a->home.pos, &players[i]->world.pos);
            if (d < best) {
                best = d;
                nearest = players[i];
            }
        }
        if (best > 1.0f) {
            f32 step = std::min(HORDE_SHAMBLE_SPEED, best);
            a->home.pos.x += (nearest->world.pos.x - a->home.pos.x) / best * step;
            a->home.pos.z += (nearest->world.pos.z - a->home.pos.z) / best * step;
            a->home.pos.y = a->world.pos.y; // stay on its own ground; only steer in XZ
        }
    }
}

int32_t SpawnFrames() {
    return std::max(5, CVarGetInteger(CVAR_REMOTE_ANCHOR("HordeSpawnFrames"), 30));
}

// Living players to ring spawns around: the local Link (if alive) and every
// same-scene living puppet.
std::vector<Actor*> LivingPlayers() {
    std::vector<Actor*> out;
    Player* local = GET_PLAYER(gPlayState);
    u8 ls = Anchor::Instance != nullptr ? Anchor::Instance->myLifeState : (u8)LIFE_STATE_ALIVE;
    if (local != nullptr && !(local->stateFlags1 & PLAYER_STATE1_DEAD) &&
        (ls == LIFE_STATE_ALIVE || ls == LIFE_STATE_REVIVING)) {
        out.push_back(&local->actor);
    }
    if (Anchor::Instance != nullptr) {
        for (uint32_t cid : EnemySync::PerceptionTargets()) {
            auto it = Anchor::Instance->clients.find(cid);
            if (it != Anchor::Instance->clients.end() && it->second.player != nullptr && IsClientAlive(it->second)) {
                out.push_back(&it->second.player->actor);
            }
        }
    }
    return out;
}

const SpawnEntry& Pick(int32_t horde) {
    const auto& table = CompositionFor(horde);
    int32_t total = 0;
    for (const auto& e : table) {
        total += e.weight;
    }
    int32_t roll = (int32_t)(Rand_ZeroOne() * total);
    for (const auto& e : table) {
        if (roll < e.weight) {
            return e;
        }
        roll -= e.weight;
    }
    return table.front();
}

// One spawn attempt. Returns false when the chosen spot was unusable (caller retries soon).
bool TrySpawn(int32_t horde) {
    std::vector<Actor*> players = LivingPlayers();
    if (players.empty()) {
        return false;
    }
    Actor* anchor = players[(size_t)(Rand_ZeroOne() * players.size()) % players.size()];
    Player* local = GET_PLAYER(gPlayState);

    s16 angle = (s16)(Rand_ZeroOne() * 0x10000);
    f32 dist = 250.0f + Rand_ZeroOne() * 200.0f;
    Vec3f pos;
    pos.x = anchor->world.pos.x + Math_SinS(angle) * dist;
    pos.y = anchor->world.pos.y + 120.0f;
    pos.z = anchor->world.pos.z + Math_CosS(angle) * dist;

    CollisionPoly* poly;
    s32 bgId;
    f32 floorY = BgCheck_EntityRaycastFloor4(&gPlayState->colCtx, &poly, &bgId, &local->actor, &pos);
    // No floor, a moving platform, or a cliff between us and the player: try elsewhere.
    if (floorY <= BGCHECK_Y_MIN || bgId != BGCHECK_SCENE || fabsf(floorY - anchor->world.pos.y) > 150.0f) {
        return false;
    }
    // Not on water (a waterbox surface above the floor here).
    f32 waterY;
    WaterBox* waterBox;
    if (WaterBox_GetSurface1(gPlayState, &gPlayState->colCtx, pos.x, pos.z, &waterY, &waterBox) && waterY > floorY) {
        return false;
    }

    const SpawnEntry& e = Pick(horde);
    s16 yaw = (s16)(angle + 0x8000); // face the player it spawned around
    Actor* spawned = Actor_Spawn(&gPlayState->actorCtx, gPlayState, e.actorId, pos.x, floorY, pos.z, 0, yaw, 0,
                                 e.params, false);
    if (spawned == nullptr) {
        return false;
    }
    ESYNC_LOG("[HordeNight] spawned id={} params={:#x} horde={}", e.actorId, (uint16_t)e.params, horde);
    return true;
}

void Announce(bool start, int32_t horde) {
    if (start) {
        Notification::Emit({
            .prefix = "Horde night",
            .message = horde > 1 ? fmt::format("#{}. The dead are restless.", horde) : "The dead are restless.",
            .remainingTime = 6.0f,
        });
        Sfx_PlaySfxCentered(NA_SE_EN_REDEAD_AIM);
    } else {
        Notification::Emit({
            .prefix = "Dawn",
            .message = "The horde retreats.",
            .remainingTime = 5.0f,
        });
    }
}

// dawn = a genuine night->day edge seen by the authority in a horde scene. Any other
// end (authority handed over, mode switched off) must NOT clear enemies: the kills
// would replicate and wipe the horde for the players still fighting it.
void EndHorde(bool dawn) {
    if (!sActive) {
        return;
    }
    sActive = false;
    if (!dawn) {
        return;
    }
    // Clear horde-only types (Stalchildren burrow at dawn on their own). Actor_Kill
    // goes through EnemySync's kill hook, which replicates the removal to mirrors.
    if (gPlayState != NULL && IsHordeScene(gPlayState->sceneNum)) {
        for (Actor* a = gPlayState->actorCtx.actorLists[ACTORCAT_ENEMY].head; a != nullptr; a = a->next) {
            if (a->id == ACTOR_EN_RD || a->id == ACTOR_EN_WF) {
                Actor_Kill(a);
            }
        }
    }
    Announce(false, sActiveHorde);
    if (Anchor::Instance != nullptr) {
        Anchor::Instance->SendPacket_HordeEvent(false, sActiveHorde);
    }
}

} // namespace

bool Enabled() {
    return CVarGetInteger(CVAR_REMOTE_ANCHOR("HordeNight"), 0) != 0;
}

int32_t CurrentHordeNumber() {
    int32_t interval = Interval();
    // totalDays increments at dawn, so tonight belongs to the current totalDays.
    if (!Forced() && (!IS_NIGHT || (gSaveContext.totalDays % interval) != interval - 1)) {
        return 0;
    }
    return gSaveContext.totalDays / interval + 1;
}

void PerFrameTick() {
    if (gPlayState == NULL) {
        return;
    }
    bool eligible = Enabled() && EnemySync::SyncEnabled() && EnemySync::MirroringEnabled() &&
                    EnemySync::IsLocalAuthority() && IsHordeScene(gPlayState->sceneNum);
    if (!eligible) {
        EndHorde(false); // lost authority / mode off: stop spawning, leave enemies alone
        return;
    }
    int32_t horde = CurrentHordeNumber();
    if (horde == 0) {
        EndHorde(true); // dawn (or the force switch was turned off)
        return;
    }

    if (!sActive) {
        sActive = true;
        sActiveHorde = horde;
        sSpawnTimer = SpawnFrames() * 2; // a beat after the announcement
        // Horde-only enemies already here = we inherited a horde in progress
        // (authority handover or scene re-entry): pick it up quietly.
        if (!AnyHordeOnlyEnemies()) {
            Announce(true, horde);
            if (Anchor::Instance != nullptr) {
                Anchor::Instance->SendPacket_HordeEvent(true, horde);
            }
        }
        return;
    }

    // Hold everything while paused and during cutscenes, text, and transitions.
    Player* local = GET_PLAYER(gPlayState);
    if (local == nullptr || gPlayState->pauseCtx.state != 0 || gPlayState->csCtx.state != CS_STATE_IDLE ||
        Player_InCsMode(gPlayState) || gPlayState->transitionTrigger != TRANS_TRIGGER_OFF ||
        gPlayState->transitionMode != TRANS_MODE_OFF) {
        return;
    }

    ShambleTowardPlayers(LivingPlayers());

    if (sSpawnTimer > 0) {
        sSpawnTimer--;
        return;
    }
    if (gPlayState->actorCtx.actorLists[ACTORCAT_ENEMY].length >= MaxAlive(horde)) {
        sSpawnTimer = SpawnFrames() / 2; // check again soon
        return;
    }
    sSpawnTimer = TrySpawn(horde) ? SpawnFrames() : 5;
}

void Reset() {
    sActive = false;
    sActiveHorde = 0;
    sSpawnTimer = 0;
}

} // namespace HordeNight
