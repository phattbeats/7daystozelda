#include "soh/Network/Anchor/Anchor.h"
#include "soh/Network/Anchor/WorldObjectSync.h"
#include "soh/Network/Anchor/EnemySync.h"
#include <nlohmann/json.hpp>
#include <libultraship/libultraship.h>
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"

#include <cmath>
#include <string>
#include <unordered_map>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

extern "C" {
#include "variables.h"
#include "functions.h"
#include "macros.h"
#include "src/overlays/actors/ovl_Door_Ana/z_door_ana.h"
#include "src/overlays/actors/ovl_En_Si/z_en_si.h"
extern PlayState* gPlayState;

void DoorAna_WaitClosed(DoorAna* doorAna, PlayState* play);
void DoorAna_WaitOpen(DoorAna* doorAna, PlayState* play);
}

/**
 * WORLD_OBJECT
 *
 * Switch, chest, collectible and Gold Skulltula flags already sync, and HookHandlers.cpp
 * makes the partner's copy of a flagged wall, boulder or switch follow its flag. This
 * file covers what has no flag (#4044):
 *
 * WORLD_OBJECT kind "grotto" { sceneNum, room, pos }: sent when a hidden grotto (DoorAna,
 *   bombed or hammered open; no flag) opens locally. Partners in the scene open their
 *   closed copy at that spot, so nobody is left outside a hole the other one opened.
 *   Leaving the scene closes it again for everyone (vanilla).
 *
 * Not a packet, but here because it is the same audit:
 * - Pickups: every client spawns its own copy of a drop (pots, grass, enemies), and each
 *   heart/ammo/magic pickup was also handed to the partners through GIVE_ITEM, so one
 *   drop paid the partner twice once they picked up their own copy. Those pickups now
 *   stay with the player who grabbed them. Keys, heart pieces and gear still sync, and
 *   rupees still go to the shared wallet.
 * - Gold Skulltula tokens: the token flag was only set after the textbox closed, so the
 *   partner could still collect their own copy and the count went up by two. The flag is
 *   now set (and sent) the moment the token is collected.
 */

namespace {

constexpr f32 SAME_POS = 2.0f;
constexpr u32 PICKUP_WINDOW = 20; // frames between the EnItem00 hand-off and the give

u32 sTick = 0;
u32 sPickupUntil = 0;
s16 sPickupItem = ITEM_NONE;
std::unordered_map<Actor*, bool> sGrottoClosed; // closed before this frame's update

nlohmann::json VecJson(const Vec3f& v) {
    return nlohmann::json::array({ v.x, v.y, v.z });
}

Vec3f JsonVec(const nlohmann::json& j) {
    return Vec3f{ j[0].get<f32>(), j[1].get<f32>(), j[2].get<f32>() };
}

// The ITEM_* that Item_Give / the get-item hand-off gives for a heart/ammo/magic drop,
// ITEM_NONE for everything else (rupees, keys, heart pieces, gear).
s16 PersonalItemFor(s16 dropType) {
    switch (dropType) {
        case ITEM00_HEART:
            return ITEM_HEART;
        case ITEM00_BOMBS_A:
        case ITEM00_BOMBS_B:
            return ITEM_BOMBS_5;
        case ITEM00_ARROWS_SINGLE:
            return ITEM_BOW;
        case ITEM00_ARROWS_SMALL:
            return ITEM_ARROWS_SMALL;
        case ITEM00_ARROWS_MEDIUM:
            return ITEM_ARROWS_MEDIUM;
        case ITEM00_ARROWS_LARGE:
            return ITEM_ARROWS_LARGE;
        case ITEM00_BOMBCHU:
            return ITEM_BOMBCHUS_5;
        case ITEM00_STICK:
            return ITEM_STICK;
        case ITEM00_NUTS:
            return ITEM_NUTS_5;
        case ITEM00_SEEDS:
            return ITEM_SEEDS;
        case ITEM00_MAGIC_LARGE:
            return ITEM_MAGIC_LARGE;
        case ITEM00_MAGIC_SMALL:
            return ITEM_MAGIC_SMALL;
        default:
            return ITEM_NONE;
    }
}

void OpenGrotto(DoorAna* grotto) {
    if (grotto->actor.params & 0x200) {
        Collider_DestroyCylinder(gPlayState, &grotto->collider);
    }
    grotto->actor.params &= ~0x0300;
    grotto->actionFunc = DoorAna_WaitOpen;
    Audio_PlaySoundGeneral(NA_SE_SY_CORRECT_CHIME, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                           &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
}

void SendGrotto(Actor* actor) {
    Anchor* anchor = Anchor::Instance;
    if (anchor == nullptr || !anchor->IsSaveLoaded() || gPlayState == NULL) {
        return;
    }

    nlohmann::json payload;
    payload["type"] = Anchor::WORLD_OBJECT;
    payload["quiet"] = true;
    payload["kind"] = "grotto";
    payload["sceneNum"] = gPlayState->sceneNum;
    payload["room"] = actor->room;
    payload["pos"] = VecJson(actor->home.pos);

    for (auto& [clientId, client] : anchor->clients) {
        if (client.sceneNum == gPlayState->sceneNum && client.online && client.isSaveLoaded && !client.self) {
            payload["targetClientId"] = clientId;
            anchor->SendJsonToRemote(payload);
        }
    }
}

// Gohma's blue warp and heart container (#4046). Each client's defeat sequence picks
// the warp's spot from its own Link and Gohma poses, so the two copies landed in
// different places. The enemy authority's spawn position wins: it is sent once, and the
// partner snaps its copy to it, whether the copy already spawned or spawns later.
struct BossSpot {
    bool have = false;
    Vec3f pos{};
};
BossSpot sBossSpot[2]; // 0 = Door_Warp1, 1 = Item_B_Heart

int BossSpotSlot(s16 actorId) {
    return actorId == ACTOR_DOOR_WARP1 ? 0 : 1;
}

void SnapActor(Actor* actor, const Vec3f& pos) {
    actor->world.pos = pos;
    actor->home.pos = pos;
    actor->prevPos = pos;
}

void SendBossSpot(Actor* actor) {
    Anchor* anchor = Anchor::Instance;
    if (anchor == nullptr || !anchor->IsSaveLoaded() || gPlayState == NULL) {
        return;
    }
    nlohmann::json payload;
    payload["type"] = Anchor::WORLD_OBJECT;
    payload["quiet"] = true;
    payload["kind"] = "bossspot";
    payload["sceneNum"] = gPlayState->sceneNum;
    payload["actorId"] = actor->id;
    payload["pos"] = VecJson(actor->world.pos);

    for (auto& [clientId, client] : anchor->clients) {
        if (client.sceneNum == gPlayState->sceneNum && client.online && client.isSaveLoaded && !client.self) {
            payload["targetClientId"] = clientId;
            anchor->SendJsonToRemote(payload);
        }
    }
}

void OnBossSpotActorInit(Actor* actor) {
    if (gPlayState == NULL || gPlayState->sceneNum != SCENE_DEKU_TREE_BOSS || Anchor::Instance == nullptr ||
        !CVarGetInteger("gRemote.Anchor.BossSpotSync", 1)) {
        return;
    }
    BossSpot& spot = sBossSpot[BossSpotSlot(actor->id)];
    if (EnemySync::IsLocalAuthority() || !EnemySync::HasSameScenePeer()) {
        SendBossSpot(actor);
    } else if (spot.have) {
        SnapActor(actor, spot.pos);
    }
}

} // namespace

bool WorldObject_IsPersonalPickup(const GetItemEntry& itemEntry) {
    return sTick <= sPickupUntil && itemEntry.modIndex == MOD_NONE && sPickupItem != ITEM_NONE &&
           itemEntry.itemId == sPickupItem;
}

void Anchor::HandlePacket_WorldObject(nlohmann::json payload) {
    if (!IsSaveLoaded() || gPlayState == NULL || payload["sceneNum"].get<s16>() != gPlayState->sceneNum) {
        return;
    }

    if (payload["kind"].get<std::string>() == "bossspot" && gPlayState->sceneNum == SCENE_DEKU_TREE_BOSS &&
        CVarGetInteger("gRemote.Anchor.BossSpotSync", 1)) {
        s16 actorId = payload["actorId"].get<s16>();
        if (actorId != ACTOR_DOOR_WARP1 && actorId != ACTOR_ITEM_B_HEART) {
            return;
        }
        BossSpot& spot = sBossSpot[BossSpotSlot(actorId)];
        spot.have = true;
        spot.pos = JsonVec(payload["pos"]);
        if (EnemySync::IsLocalAuthority()) {
            return;
        }
        for (s32 cat = 0; cat < ACTORCAT_MAX; cat++) {
            for (Actor* a = gPlayState->actorCtx.actorLists[cat].head; a != NULL; a = a->next) {
                if (a->id == actorId && a->update != NULL) {
                    SnapActor(a, spot.pos);
                }
            }
        }
        return;
    }

    if (payload["kind"].get<std::string>() == "grotto") {
        Vec3f pos = JsonVec(payload["pos"]);
        for (auto& [actor, closed] : sGrottoClosed) {
            DoorAna* grotto = (DoorAna*)actor;
            if (grotto->actionFunc == DoorAna_WaitClosed && (grotto->actor.params & 0x200) &&
                fabsf(actor->home.pos.x - pos.x) <= SAME_POS && fabsf(actor->home.pos.y - pos.y) <= SAME_POS &&
                fabsf(actor->home.pos.z - pos.z) <= SAME_POS) {
                OpenGrotto(grotto);
                closed = false;
            }
        }
    }
}

void RegisterWorldObjectHooks(bool isConnected) {
    sGrottoClosed.clear();
    sPickupUntil = 0;
    sPickupItem = ITEM_NONE;
    sBossSpot[0] = sBossSpot[1] = BossSpot{};

    COND_HOOK(OnSceneInit, isConnected, [](int16_t) { sBossSpot[0] = sBossSpot[1] = BossSpot{}; });
    COND_ID_HOOK(OnActorInit, ACTOR_DOOR_WARP1, isConnected,
                 [](void* refActor) { OnBossSpotActorInit(static_cast<Actor*>(refActor)); });
    COND_ID_HOOK(OnActorInit, ACTOR_ITEM_B_HEART, isConnected,
                 [](void* refActor) { OnBossSpotActorInit(static_cast<Actor*>(refActor)); });

    COND_VB_SHOULD(VB_GIVE_ITEM_FROM_ITEM_00, isConnected, {
        EnItem00* item = va_arg(args, EnItem00*);
        if (*should) {
            // A placed pickup carries a collectible flag, which syncs: the partner's copy
            // vanishes, so they must be given the item. Only unflagged drops are personal.
            sPickupItem = item->collectibleFlag == 0 ? PersonalItemFor(item->actor.params) : ITEM_NONE;
            sPickupUntil = sTick + PICKUP_WINDOW;
        }
    });

    COND_VB_SHOULD(VB_GIVE_ITEM_SKULL_TOKEN, isConnected, {
        EnSi* token = va_arg(args, EnSi*);
        if (*should) {
            // EnSi sets this only once the textbox closes; doing it now sends SET_FLAG
            // while the partner's copy is still waiting to be collected.
            SET_GS_FLAGS((token->actor.params & 0x1F00) >> 8, token->actor.params & 0xFF);
            GameInteractor_ExecuteOnFlagSet(FLAG_GS_TOKEN, token->actor.params);
        }
    });

    // Only grottos that are bombed or hammered open have something to sync; the Song of
    // Storms ones open by the player that plays it. Registered at init too: an off-screen
    // grotto doesn't update, but a partner can still open it.
    COND_ID_HOOK(OnActorInit, ACTOR_DOOR_ANA, isConnected, [](void* refActor) {
        DoorAna* grotto = static_cast<DoorAna*>(refActor);
        sGrottoClosed[&grotto->actor] = grotto->actionFunc == DoorAna_WaitClosed && (grotto->actor.params & 0x200);
    });
    COND_ID_HOOK(ShouldActorUpdate, ACTOR_DOOR_ANA, isConnected, [](void* refActor, bool* should) {
        DoorAna* grotto = static_cast<DoorAna*>(refActor);
        sGrottoClosed[&grotto->actor] = grotto->actionFunc == DoorAna_WaitClosed && (grotto->actor.params & 0x200);
    });
    COND_ID_HOOK(OnActorUpdate, ACTOR_DOOR_ANA, isConnected, [](void* refActor) {
        DoorAna* grotto = static_cast<DoorAna*>(refActor);
        auto it = sGrottoClosed.find(&grotto->actor);
        if (it != sGrottoClosed.end() && it->second && grotto->actionFunc == DoorAna_WaitOpen) {
            it->second = false;
            SendGrotto(&grotto->actor);
        }
    });
    COND_ID_HOOK(OnActorDestroy, ACTOR_DOOR_ANA, isConnected,
                 [](void* refActor) { sGrottoClosed.erase(static_cast<Actor*>(refActor)); });
}

void WorldObjectTick() {
    sTick++;
}

#ifdef __EMSCRIPTEN__
extern "C" {

// #4044 tests: every tracked grotto (closed = still hidden). open=<index> lands a hit
// on that grotto's collider, as a bomb would.
EMSCRIPTEN_KEEPALIVE
const char* anchor_test_grottos(int open) {
    static std::string out;
    nlohmann::json j = nlohmann::json::array();
    int i = 0;
    for (auto& [actor, closed] : sGrottoClosed) {
        DoorAna* grotto = (DoorAna*)actor;
        if (i == open && grotto->actionFunc == DoorAna_WaitClosed && (actor->params & 0x200)) {
            grotto->collider.base.acFlags |= AC_HIT;
        }
        j.push_back({ { "room", actor->room },
                      { "params", (u16)actor->params },
                      { "pos", VecJson(actor->home.pos) },
                      { "closed", grotto->actionFunc == DoorAna_WaitClosed } });
        i++;
    }
    out = j.dump();
    return out.c_str();
}

// #4044 tests. cmd 0: spawn actor `a` with params `b` `c` units in front of Link
// (c = 0: at his feet). cmd 1 / 3: set / clear switch flag `a` the way gameplay does
// (broadcasts).
// Always returns this client's view: switches, tokens, rocks, ammo and token count.
EMSCRIPTEN_KEEPALIVE
const char* anchor_test_world(int cmd, int a, int b, int c) {
    static std::string out;
    if (gPlayState == NULL) {
        return "{}";
    }
    Player* player = GET_PLAYER(gPlayState);
    if (cmd == 0) {
        Vec3f pos = player->actor.world.pos;
        pos.x += Math_SinS(player->actor.shape.rot.y) * c;
        pos.z += Math_CosS(player->actor.shape.rot.y) * c;
        Actor_Spawn(&gPlayState->actorCtx, gPlayState, (s16)a, pos.x, pos.y, pos.z, 0, 0, 0, (s16)b, true);
    } else if (cmd == 1) {
        Flags_SetSwitch(gPlayState, a);
    } else if (cmd == 3) {
        Flags_UnsetSwitch(gPlayState, a);
    }

    nlohmann::json actors = nlohmann::json::array();
    for (s32 cat = 0; cat < ACTORCAT_MAX; cat++) {
        for (Actor* actor = gPlayState->actorCtx.actorLists[cat].head; actor != NULL; actor = actor->next) {
            if (actor->id == ACTOR_OBJ_SWITCH || actor->id == ACTOR_EN_SI || actor->id == ACTOR_EN_ISHI ||
                actor->id == ACTOR_EN_ITEM00 || actor->id == ACTOR_OBJ_SYOKUDAI) {
                actors.push_back({ { "id", actor->id },
                                   { "params", (u16)actor->params },
                                   { "scaleY", actor->scale.y },
                                   { "pos", VecJson(actor->world.pos) } });
            }
        }
    }
    nlohmann::json j = {
        { "scene", gPlayState->sceneNum },
        { "actors", actors },
        { "swch", gPlayState->actorCtx.flags.swch },
        { "tempSwch", gPlayState->actorCtx.flags.tempSwch },
        { "bombs", AMMO(ITEM_BOMB) },
        { "nuts", AMMO(ITEM_NUT) },
        { "sticks", AMMO(ITEM_STICK) },
        { "health", gSaveContext.health },
        { "gsTokens", gSaveContext.inventory.gsTokens },
        { "gsFlags0", gSaveContext.gsFlags[0] },
        { "link", VecJson(player->actor.world.pos) },
    };
    out = j.dump();
    return out.c_str();
}
}
#endif
