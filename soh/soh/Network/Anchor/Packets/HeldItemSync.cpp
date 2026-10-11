#include "soh/Network/Anchor/Anchor.h"
#include "soh/Network/Anchor/HeldItemSync.h"
#include <nlohmann/json.hpp>
#include <libultraship/libultraship.h>
#include "soh/Enhancements/game-interactor/GameInteractor.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

extern "C" {
#include "macros.h"
#include "variables.h"
#include "functions.h"
#include "src/overlays/actors/ovl_En_Bom/z_en_bom.h"
#include "src/overlays/actors/ovl_En_Bom_Chu/z_en_bom_chu.h"
#include "src/overlays/actors/ovl_En_Ru1/z_en_ru1.h"
#include "objects/object_ru1/object_ru1.h"
extern PlayState* gPlayState;
}

/**
 * HELD_ITEMS
 *
 * Every client runs its own bombs and pots, so a partner's Link used to carry nothing
 * (#4180): the carry pose synced, the thing in the hands did not, and a thrown bomb or
 * a flying pot was invisible. Now:
 *
 * - Each client streams the items it owns: bombs and bombchus the local player took
 *   out (or that a bombchu of theirs became), and any pot, crate, rock or bush the
 *   local player carries or has thrown. The stream is pos/rot/scale plus what the
 *   item needs to draw (fuse timer for bombs, orientation axes for chus).
 * - A partner spawns a "ghost" of each streamed item: a real actor of the same kind
 *   that is for show only. Bomb ghosts run their own fuse and explosion (flash, sound,
 *   shake) but every collider is switched off, so they hurt nothing and can't be
 *   picked up. The damage and the bombed wall are the owner's; the wall already
 *   syncs through its switch flag. Chu and prop ghosts have their update skipped and
 *   follow the stream; chus vanish when the stream ends (their blast is its own bomb
 *   in the stream), props vanish with a break sound.
 * - A carried prop leaves its original on the partner's screen; the partner's copy is
 *   hidden and frozen while the ghost is out, then removed when the prop breaks.
 * - Princess Ruto in Jabu-Jabu (#4195) is one persistent NPC per client, so she is not
 *   ghosted: the partner's own Ruto is taken over (frozen, posed from the stream, kept
 *   drawn) while the owner carries or throws her. If the partner's copy is gone (she
 *   leaves when her room is left behind) a stand-in Ruto is spawned. When the stream
 *   ends she is dropped where the owner's Ruto came to rest and runs her own logic
 *   again, so she lands, can be picked up, and the throw onto the Sapphire platform
 *   starts that cutscene on the partner's screen too.
 */

namespace {

constexpr u32 STALE_TICKS = 6;   // a stream older than this means the item is gone
constexpr u32 AIR_TICKS = 160;   // a thrown prop keeps streaming this long at most
constexpr f32 OWN_RANGE = 110.0f; // a bomb first seen this near Link is Link's
constexpr f32 CHU_BLAST_RANGE = 45.0f;
constexpr u32 CHU_BLAST_TICKS = 12;
constexpr f32 HOME_RANGE = 8.0f;

struct Own {
    uint32_t key = 0;
    u32 airUntil = 0; // props: streaming until this tick after the player lets go
    bool wasHeld = false;
};

struct Remote {
    uint32_t from = 0;
    u32 rxTick = 0;
    s16 scene = -1;
    s16 id = 0;
    s16 params = 0;
    Vec3f pos{};
    Vec3s rot{};
    Vec3s shape{};
    Vec3f home{};
    f32 scale = 0.01f;
    s16 timer = 0;
    bool hasAxes = false;
    f32 axes[9]{};
    bool spent = false; // our ghost already finished (exploded/broke); don't spawn another for this item
    s32 rutoAction = 0;
    s8 rutoRooms[3]{};
};

struct Ghost {
    Actor* actor = nullptr;
    Actor* original = nullptr; // partner-side copy hidden while this carried prop is out
    ActorFunc originalDraw = nullptr;
    // Ruto: the partner's own Ruto taken over (adopted) or a spawned stand-in, and the state to put back.
    bool adopted = false;
    s32 savedAction = 0;
    s32 savedDrawConfig = 0;
    Vec3f savedPos{};
    Vec3s savedRot{};
    Vec3s savedShape{};
};

std::unordered_map<Actor*, Own> sOwn;
std::unordered_set<Actor*> sRejected; // bombs that are not the local player's
std::unordered_map<uint64_t, Remote> sRemote;
std::unordered_map<uint64_t, Ghost> sGhosts;
std::unordered_map<Actor*, uint64_t> sGhostActors;
std::unordered_map<Actor*, ActorFunc> sHidden; // originals hidden under a carried-prop ghost
std::unordered_set<Actor*> sForcedHeld;         // test hook: treat as carried
struct ChuPos {
    Vec3f pos;
    u32 tick;
};
std::vector<ChuPos> sChuPos;
uint32_t sNextKey = 1;
u32 sTick = 0;
s16 sLastScene = -1;

bool SyncEnabled() {
    return CVarGetInteger(CVAR_REMOTE_ANCHOR("HeldItemSync"), 1);
}

bool IsBomb(s16 id) {
    return id == ACTOR_EN_BOM || id == ACTOR_EN_BOM_CHU;
}

bool IsProp(s16 id) {
    return id == ACTOR_OBJ_TSUBO || id == ACTOR_OBJ_KIBAKO || id == ACTOR_EN_KUSA || id == ACTOR_EN_ISHI;
}

bool IsRuto(s16 id) {
    return id == ACTOR_EN_RU1;
}

// Ruto's own states while she is in someone's arms (31 sitting, 32 squirming, 34 sees the Sapphire)
// and while she flies or sinks after a throw (28 falling, 29 in water, 30 sinking).
bool RutoCarried(s32 action) {
    return action == 31 || action == 32 || action == 34;
}

bool RutoAirborne(s32 action) {
    return action >= 28 && action <= 30;
}

bool IsDriven(s16 id) {
    return id != ACTOR_EN_BOM; // bomb ghosts run natively; chus and props follow the stream
}

uint64_t RemoteKey(uint32_t from, uint32_t key) {
    return ((uint64_t)from << 32) | key;
}

Player* LocalPlayer() {
    return GET_PLAYER(gPlayState);
}

bool IsHeldLocally(Actor* actor) {
    Player* player = LocalPlayer();
    return actor->parent == &player->actor || player->heldActor == actor || sForcedHeld.count(actor) > 0;
}

float Dist3(const Vec3f& a, const Vec3f& b) {
    return sqrtf(SQ(a.x - b.x) + SQ(a.y - b.y) + SQ(a.z - b.z));
}

nlohmann::json Vec(const Vec3f& v) {
    return { v.x, v.y, v.z };
}

nlohmann::json Vec(const Vec3s& v) {
    return { v.x, v.y, v.z };
}

Vec3f JsonVecF(const nlohmann::json& j) {
    return Vec3f{ j[0].get<f32>(), j[1].get<f32>(), j[2].get<f32>() };
}

Vec3s JsonVecS(const nlohmann::json& j) {
    return Vec3s{ j[0].get<s16>(), j[1].get<s16>(), j[2].get<s16>() };
}

void Reset() {
    sOwn.clear();
    sRejected.clear();
    sRemote.clear();
    sGhosts.clear();
    sGhostActors.clear();
    sHidden.clear();
    sForcedHeld.clear();
    sChuPos.clear();
}

// ---- sender ------------------------------------------------------------------------

// Whether the local player produced this bomb or chu. Bombs that raids, bosses and
// bombflowers make are on every client already and would only double up.
bool ClaimBomb(Actor* actor) {
    if (IsHeldLocally(actor)) {
        return true;
    }
    if (Dist3(actor->world.pos, LocalPlayer()->actor.world.pos) < OWN_RANGE) {
        return true;
    }
    // The blast of a bombchu of ours: a bomb that appears where it was.
    for (auto& c : sChuPos) {
        if (sTick - c.tick <= CHU_BLAST_TICKS && Dist3(actor->world.pos, c.pos) < CHU_BLAST_RANGE) {
            return true;
        }
    }
    return false;
}

void CollectOwned(std::vector<Actor*>& out) {
    ActorContext* ctx = &gPlayState->actorCtx;
    for (s32 cat : { ACTORCAT_EXPLOSIVE, ACTORCAT_PROP, ACTORCAT_NPC }) {
        for (Actor* a = ctx->actorLists[cat].head; a != NULL; a = a->next) {
            if (a->update == NULL || a->init != NULL || sGhostActors.count(a)) {
                continue;
            }
            if (IsBomb(a->id)) {
                if (sOwn.count(a) == 0) {
                    if (sRejected.count(a)) {
                        continue;
                    }
                    if (!ClaimBomb(a)) {
                        sRejected.insert(a);
                        continue;
                    }
                    sOwn[a].key = sNextKey++;
                }
                out.push_back(a);
            } else if (IsProp(a->id) || IsRuto(a->id)) {
                bool held = IsHeldLocally(a);
                auto it = sOwn.find(a);
                if (it == sOwn.end()) {
                    if (!held) {
                        continue;
                    }
                    it = sOwn.emplace(a, Own{}).first;
                    it->second.key = sNextKey++;
                }
                Own& own = it->second;
                if (held) {
                    own.airUntil = sTick + AIR_TICKS;
                } else if (own.wasHeld) {
                    own.airUntil = sTick + AIR_TICKS; // just thrown
                }
                own.wasHeld = held;
                // Ruto keeps streaming until she has come to rest (her state lags the release by a frame).
                bool landed = IsRuto(a->id) && !held && !RutoAirborne(((EnRu1*)a)->action) &&
                              !RutoCarried(((EnRu1*)a)->action);
                if (!held && (sTick > own.airUntil || landed)) {
                    sOwn.erase(it);
                    continue;
                }
                out.push_back(a);
            }
            if (a->id == ACTOR_EN_BOM_CHU) {
                sChuPos.push_back({ a->world.pos, sTick });
            }
        }
    }
    sChuPos.erase(std::remove_if(sChuPos.begin(), sChuPos.end(),
                                 [](const ChuPos& c) { return sTick - c.tick > CHU_BLAST_TICKS; }),
                  sChuPos.end());
}

nlohmann::json Snapshot(Actor* a) {
    nlohmann::json e;
    e["k"] = sOwn[a].key;
    e["i"] = a->id;
    e["q"] = (u16)a->params;
    e["p"] = Vec(a->world.pos);
    e["r"] = Vec(a->world.rot);
    e["s"] = Vec(a->shape.rot);
    e["z"] = a->scale.x;
    if (a->id == ACTOR_EN_BOM) {
        e["t"] = ((EnBom*)a)->timer;
    } else if (a->id == ACTOR_EN_BOM_CHU) {
        EnBomChu* chu = (EnBomChu*)a;
        e["a"] = nlohmann::json::array({ chu->axisForwards.x, chu->axisForwards.y, chu->axisForwards.z, chu->axisUp.x,
                                         chu->axisUp.y, chu->axisUp.z, chu->axisLeft.x, chu->axisLeft.y,
                                         chu->axisLeft.z });
    } else {
        e["h"] = Vec(a->home.pos);
    }
    if (IsRuto(a->id)) {
        EnRu1* ruto = (EnRu1*)a;
        e["ra"] = ruto->action;
        e["rm"] = nlohmann::json::array({ ruto->roomNum1, ruto->roomNum2, ruto->roomNum3 });
    }
    return e;
}

void SendOwned() {
    Anchor* anchor = Anchor::Instance;
    std::vector<Actor*> owned;
    CollectOwned(owned);
    if (owned.empty()) {
        return;
    }
    nlohmann::json items = nlohmann::json::array();
    for (Actor* a : owned) {
        items.push_back(Snapshot(a));
    }
    nlohmann::json payload;
    payload["type"] = Anchor::HELD_ITEMS;
    payload["quiet"] = true;
    payload["sceneNum"] = gPlayState->sceneNum;
    payload["items"] = items;
    for (auto& [clientId, client] : anchor->clients) {
        if (client.sceneNum == gPlayState->sceneNum && client.online && client.isSaveLoaded && !client.self) {
            payload["targetClientId"] = clientId;
            anchor->SendJsonToRemote(payload);
        }
    }
}

// ---- receiver ----------------------------------------------------------------------

void ApplyPose(Actor* actor, const Remote& r) {
    actor->world.pos = r.pos;
    actor->world.rot = r.rot;
    actor->shape.rot = r.shape;
    Actor_SetScale(actor, r.scale);
    if (actor->id == ACTOR_EN_BOM_CHU && r.hasAxes) {
        EnBomChu* chu = (EnBomChu*)actor;
        memcpy(&chu->axisForwards, &r.axes[0], sizeof(Vec3f));
        memcpy(&chu->axisUp, &r.axes[3], sizeof(Vec3f));
        memcpy(&chu->axisLeft, &r.axes[6], sizeof(Vec3f));
    }
}

// For show only: nothing on a bomb ghost can hit, be hit, push or be lifted.
void Neutralize(Actor* actor) {
    if (actor->id == ACTOR_EN_BOM) {
        EnBom* bomb = (EnBom*)actor;
        bomb->bombCollider.base.acFlags &= ~AC_ON;
        bomb->bombCollider.base.ocFlags1 &= ~OC1_ON;
        bomb->explosionCollider.base.atFlags &= ~AT_ON;
        bomb->explosionCollider.base.acFlags &= ~AC_ON;
        bomb->explosionCollider.base.ocFlags1 &= ~OC1_ON;
    }
    Player* player = LocalPlayer();
    if (player->interactRangeActor == actor) {
        player->interactRangeActor = NULL;
    }
}

void HideOriginal(Ghost& g, const Remote& r) {
    if (!IsProp(r.id)) {
        return;
    }
    ActorContext* ctx = &gPlayState->actorCtx;
    for (Actor* a = ctx->actorLists[ACTORCAT_PROP].head; a != NULL; a = a->next) {
        if (a->id != r.id || a == g.actor || a->update == NULL || a->init != NULL || a->draw == NULL ||
            sGhostActors.count(a) || sHidden.count(a) || fabsf(a->home.pos.x - r.home.x) > HOME_RANGE ||
            fabsf(a->home.pos.y - r.home.y) > HOME_RANGE || fabsf(a->home.pos.z - r.home.z) > HOME_RANGE) {
            continue;
        }
        g.original = a;
        g.originalDraw = a->draw;
        sHidden[a] = a->draw;
        a->draw = NULL;
        return;
    }
}

bool PartnerStillHere(uint32_t from) {
    Anchor* anchor = Anchor::Instance;
    auto it = anchor->clients.find(from);
    return it != anchor->clients.end() && it->second.online && it->second.sceneNum == gPlayState->sceneNum;
}

Actor* FindRutoSwitch() {
    for (Actor* a = gPlayState->actorCtx.actorLists[ACTORCAT_BG].head; a != NULL; a = a->next) {
        if (a->id == ACTOR_BG_BDAN_OBJECTS && a->params == 0) {
            return a;
        }
    }
    return NULL;
}

// The partner's own Ruto, if this client has one that is not already a ghost.
EnRu1* LocalRuto() {
    for (Actor* a = gPlayState->actorCtx.actorLists[ACTORCAT_NPC].head; a != NULL; a = a->next) {
        if (a->id == ACTOR_EN_RU1 && a->update != NULL && a->init == NULL && !sGhostActors.count(a)) {
            return (EnRu1*)a;
        }
    }
    return NULL;
}

void PoseRuto(EnRu1* ruto) {
    ruto->drawConfig = 1; // opaque; her cutscene and wait states draw nothing
    ruto->alpha = 255;
    ruto->actor.shape.shadowAlpha = 255;
    ruto->actor.flags |= ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED;
    Animation_Change(&ruto->skelAnime, (AnimationHeader*)gRutoChildSittingAnim, 1.0f, 0,
                     Animation_GetLastFrame((void*)gRutoChildSittingAnim), ANIMMODE_LOOP, 0.0f);
}

void SpawnRuto(uint64_t key, const Remote& r) {
    EnRu1* ruto = LocalRuto();
    if (ruto != NULL && (IsHeldLocally(&ruto->actor) || RutoCarried(ruto->action))) {
        return; // both players lifted their own Ruto: each keeps theirs
    }
    Ghost& g = sGhosts[key];
    if (ruto != NULL) {
        g.adopted = true;
        g.savedAction = ruto->action;
        g.savedDrawConfig = ruto->drawConfig;
        g.savedPos = ruto->actor.world.pos;
        g.savedRot = ruto->actor.world.rot;
        g.savedShape = ruto->actor.shape.rot;
    } else {
        // Type 10 is her post-cutscene setup: no flag checks, no duplicate check, room -1.
        Actor* actor = Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_EN_RU1, r.pos.x, r.pos.y, r.pos.z,
                                   r.shape.x, r.shape.y, r.shape.z, (r.params & 0xFF00) | 10, false);
        if (actor == NULL || actor->update == NULL) {
            sGhosts.erase(key);
            return;
        }
        ruto = (EnRu1*)actor;
    }
    g.actor = &ruto->actor;
    sGhostActors[g.actor] = key;
    PoseRuto(ruto);
    ApplyPose(g.actor, r);
}

// The owner let go of Ruto and she came to rest (or the owner left). Hand her back to her own logic.
void EndRuto(const Ghost& g, const Remote& r) {
    Actor* actor = g.actor;
    sGhostActors.erase(actor);
    if (actor->update == NULL) {
        return;
    }
    EnRu1* ruto = (EnRu1*)actor;
    // Only a Ruto that was waiting to be picked up (27), carried or in the air follows the owner's;
    // one in a talk or cutscene state goes back to it.
    bool fromCarry =
        !g.adopted || g.savedAction == 27 || RutoCarried(g.savedAction) || RutoAirborne(g.savedAction);
    if (!PartnerStillHere(r.from) || !fromCarry) {
        if (!g.adopted) {
            Actor_Kill(actor);
            return;
        }
        actor->world.pos = g.savedPos;
        actor->world.rot = g.savedRot;
        actor->shape.rot = g.savedShape;
        ruto->action = g.savedAction;
        ruto->drawConfig = g.savedDrawConfig;
        return;
    }
    // Drop her where the owner's Ruto is: she falls (state 28), lands and offers to be carried
    // again, or lands on the Sapphire platform and starts that cutscene here as well.
    ruto->roomNum2 = r.rutoRooms[1];
    ruto->roomNum3 = r.rutoRooms[2];
    if (!g.adopted) {
        ruto->roomNum1 = r.rutoRooms[0];
        ruto->unk_28C = (BgBdanObjects*)FindRutoSwitch();
    }
    actor->parent = NULL;
    actor->velocity = Vec3f{ 0.0f, 0.0f, 0.0f };
    actor->speedXZ = 0.0f;
    actor->minVelocityY = -6.8f;
    actor->gravity = -1.3f;
    ruto->action = 28;
    ruto->drawConfig = 1;
}

void SpawnGhost(uint64_t key, const Remote& r) {
    if (IsRuto(r.id)) {
        SpawnRuto(key, r);
        return;
    }
    Actor* actor = Actor_Spawn(&gPlayState->actorCtx, gPlayState, r.id, r.pos.x, r.pos.y, r.pos.z, r.shape.x,
                               r.shape.y, r.shape.z, r.params, false);
    if (actor == NULL) {
        return;
    }
    actor->room = -1;
    actor->flags |= ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED;
    sGhostActors[actor] = key;
    Ghost& g = sGhosts[key];
    g.actor = actor;
    if (r.id == ACTOR_EN_BOM) {
        ((EnBom*)actor)->timer = r.timer;
    }
    HideOriginal(g, r);
    ApplyPose(actor, r);
}

// The owner's item is gone: it exploded, broke, or the owner left.
void EndGhost(uint64_t key, const Remote& r) {
    auto it = sGhosts.find(key);
    if (it == sGhosts.end()) {
        return;
    }
    Ghost g = it->second;
    sGhosts.erase(it);
    Actor* actor = g.actor;
    if (IsRuto(r.id)) {
        EndRuto(g, r);
        return;
    }

    if (g.original != NULL && sHidden.erase(g.original)) {
        if (PartnerStillHere(r.from)) {
            Actor_Kill(g.original); // the partner broke it: their copy goes too, without a drop
        } else {
            g.original->draw = g.originalDraw;
        }
    }

    if (actor->update == NULL) {
        return;
    }
    if (actor->id == ACTOR_EN_BOM) {
        EnBom* bomb = (EnBom*)actor;
        if (bomb->actor.params == BOMB_BODY) {
            bomb->timer = std::min<s16>(bomb->timer, 1); // finishes as a harmless blast
        }
        return;
    }
    if (IsProp(actor->id)) {
        Audio_PlayActorSound2(actor, actor->id == ACTOR_EN_ISHI ? NA_SE_EV_ROCK_BROKEN : NA_SE_EV_POT_BROKEN);
    }
    Actor_Kill(actor);
    sGhostActors.erase(actor);
}

void ReceiveTick() {
    std::vector<uint64_t> done;
    for (auto& [key, r] : sRemote) {
        u32 age = sTick - r.rxTick;
        auto git = sGhosts.find(key);
        if (age > STALE_TICKS) {
            EndGhost(key, r);
            done.push_back(key);
            continue;
        }
        if (git == sGhosts.end()) {
            // A bomb first seen mid-blast would spawn in its explosion state with no fuse and never be killed.
            bool lateBlast = r.id == ACTOR_EN_BOM && r.params != BOMB_BODY;
            if (r.scene == gPlayState->sceneNum && !r.spent && !lateBlast) {
                SpawnGhost(key, r);
            }
            continue;
        }
        Ghost& g = git->second;
        if (IsRuto(r.id) && !g.adopted && LocalRuto() != NULL) {
            // The partner's own Ruto came back (her room loaded): drop the stand-in and take hers over.
            Actor* standIn = g.actor;
            sGhostActors.erase(standIn);
            sGhosts.erase(git);
            Actor_Kill(standIn);
            continue;
        }
        if (IsDriven(r.id) && g.actor->update != NULL) {
            ApplyPose(g.actor, r);
            if (IsRuto(r.id)) {
                SkelAnime_Update(&((EnRu1*)g.actor)->skelAnime);
            }
        }
    }
    for (uint64_t key : done) {
        sRemote.erase(key);
    }
}

void OnShouldUpdate(Actor* actor, bool* should) {
    auto g = sGhostActors.find(actor);
    if (g != sGhostActors.end()) {
        if (IsDriven(actor->id)) {
            *should = false;
        }
        return;
    }
    if (sHidden.count(actor)) {
        *should = false;
    }
}

void OnUpdate(Actor* actor) {
    auto g = sGhostActors.find(actor);
    if (g == sGhostActors.end()) {
        return;
    }
    Neutralize(actor);
    if (!IsDriven(actor->id)) {
        auto r = sRemote.find(g->second);
        // A bomb ghost follows its owner's bomb until it explodes on its own.
        if (r != sRemote.end() && actor->params == BOMB_BODY && r->second.params == BOMB_BODY) {
            actor->world.pos = r->second.pos;
            actor->world.rot = r->second.rot;
        }
    }
}

} // namespace

void RegisterHeldItemSyncHooks(bool isConnected) {
    Reset();
    sLastScene = -1;

    COND_HOOK(OnSceneInit, isConnected, [](int16_t sceneNum) { Reset(); });

    COND_HOOK(ShouldActorUpdate, isConnected,
              [](void* refActor, bool* should) { OnShouldUpdate((Actor*)refActor, should); });

    COND_HOOK(OnActorUpdate, isConnected, [](void* refActor) { OnUpdate((Actor*)refActor); });

    COND_HOOK(OnActorDestroy, isConnected, [](void* refActor) {
        Actor* actor = (Actor*)refActor;
        sOwn.erase(actor);
        sRejected.erase(actor);
        sForcedHeld.erase(actor);
        sHidden.erase(actor);
        auto g = sGhostActors.find(actor);
        if (g != sGhostActors.end()) {
            auto r = sRemote.find(g->second);
            if (r != sRemote.end()) {
                r->second.spent = true;
            }
            sGhosts.erase(g->second);
            sGhostActors.erase(g);
        }
    });
}

void HeldItemSyncTick() {
    sTick++;
    Anchor* anchor = Anchor::Instance;
    if (gPlayState == NULL || anchor == nullptr || !anchor->IsSaveLoaded() || !SyncEnabled()) {
        return;
    }
    if (gPlayState->sceneNum != sLastScene) {
        sLastScene = gPlayState->sceneNum;
        Reset();
    }
    ReceiveTick();
    SendOwned();
}

void Anchor::HandlePacket_HeldItems(nlohmann::json payload) {
    if (!IsSaveLoaded() || gPlayState == NULL || !SyncEnabled() || !payload.contains("sceneNum") ||
        payload["sceneNum"].get<s16>() != gPlayState->sceneNum || !payload.contains("clientId") ||
        !payload.contains("items")) {
        return;
    }
    uint32_t from = payload["clientId"].get<uint32_t>();
    for (auto& e : payload["items"]) {
        Remote& r = sRemote[RemoteKey(from, e["k"].get<uint32_t>())];
        r.from = from;
        r.rxTick = sTick;
        r.scene = payload["sceneNum"].get<s16>();
        r.id = e["i"].get<s16>();
        r.params = e["q"].get<s16>();
        r.pos = JsonVecF(e["p"]);
        r.rot = JsonVecS(e["r"]);
        r.shape = JsonVecS(e["s"]);
        r.scale = e["z"].get<f32>();
        r.timer = e.value("t", (s16)0);
        if (e.contains("h")) {
            r.home = JsonVecF(e["h"]);
        }
        if (e.contains("ra")) {
            r.rutoAction = e["ra"].get<s32>();
            for (int i = 0; i < 3; i++) {
                r.rutoRooms[i] = e["rm"][i].get<s8>();
            }
        }
        r.hasAxes = e.contains("a");
        if (r.hasAxes) {
            for (int i = 0; i < 9; i++) {
                r.axes[i] = e["a"][i].get<f32>();
            }
        }
    }
}

#ifdef __EMSCRIPTEN__
extern "C" {

// #4180 tests. cmd 0: dump owned items, ghosts and hidden originals.
// 1: spawn a bomb at Link's feet (a = fuse timer, 0 keeps the default).
// 2: spawn a bombchu at Link's feet.
// 3: spawn a pot (a = 0) / crate (1) / rock (2) / bush (3) and treat it as carried (b = 1) or thrown.
// 4: move the first forced actor by (a, c) on x/z and by b on y.
// 5: kill every forced actor (as if it broke).
// 6: put Deku Sticks (a = 0) or bombs (a = 1) on C-Left, with ammo.
// 7: set the stick Link is holding to flame timer a (lit when non-zero).
// 8: put this client's Ruto (spawned if missing) a units ahead of Link, sitting and ready to be lifted.
// The dump also lists each puppet's stick length and flame.
EMSCRIPTEN_KEEPALIVE
const char* anchor_test_held(int cmd, double a, double b, double c) {
    static std::string out;
    if (gPlayState != NULL) {
        Player* player = GET_PLAYER(gPlayState);
        Vec3f pos = player->actor.world.pos;
        switch (cmd) {
            case 1: {
                Actor* bomb = Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_EN_BOM, pos.x, pos.y + 20, pos.z, 0,
                                          0, 0, 0, false);
                if (bomb != NULL && a > 0) {
                    ((EnBom*)bomb)->timer = (s16)a;
                }
                break;
            }
            case 2:
                Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_EN_BOM_CHU, pos.x, pos.y + 5, pos.z,
                            player->actor.shape.rot.x, player->actor.shape.rot.y, player->actor.shape.rot.z, 0, false);
                break;
            case 3: {
                static const s16 ids[] = { ACTOR_OBJ_TSUBO, ACTOR_OBJ_KIBAKO, ACTOR_EN_ISHI, ACTOR_EN_KUSA };
                Actor* prop = Actor_Spawn(&gPlayState->actorCtx, gPlayState, ids[(int)a & 3], pos.x + 40, pos.y,
                                          pos.z, 0, 0, 0, 0, false);
                if (prop != NULL && b != 0) {
                    sForcedHeld.insert(prop);
                }
                break;
            }
            case 4:
                for (Actor* f : sForcedHeld) {
                    f->world.pos.x += (f32)a;
                    f->world.pos.y += (f32)b;
                    f->world.pos.z += (f32)c;
                    break;
                }
                break;
            case 5:
                for (Actor* f : std::vector<Actor*>(sForcedHeld.begin(), sForcedHeld.end())) {
                    sForcedHeld.erase(f);
                    Actor_Kill(f);
                }
                break;
            case 6: {
                u8 item = a == 0 ? ITEM_STICK : ITEM_BOMB;
                Item_Give(gPlayState, a == 0 ? ITEM_STICKS_10 : ITEM_BOMB_BAG_20);
                Item_Give(gPlayState, a == 0 ? ITEM_STICKS_10 : ITEM_BOMBS_20);
                gSaveContext.equips.buttonItems[1] = item;
                gSaveContext.equips.cButtonSlots[0] = a == 0 ? SLOT_STICK : SLOT_BOMB;
                Interface_LoadItemIcon1(gPlayState, 1);
                break;
            }
            case 7:
                player->unk_860 = (s16)a;
                break;
            case 8: {
                s16 yaw = player->actor.shape.rot.y;
                Vec3f at = { pos.x + Math_SinS(yaw) * (f32)a, pos.y, pos.z + Math_CosS(yaw) * (f32)a };
                EnRu1* ruto = LocalRuto();
                if (ruto == NULL) {
                    ruto = (EnRu1*)Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_EN_RU1, at.x, at.y, at.z, 0,
                                               yaw + 0x8000, 0, 10, false);
                }
                if (ruto != NULL && ruto->actor.update != NULL) {
                    s8 room = gPlayState->roomCtx.curRoom.num;
                    ruto->actor.world.pos = at;
                    ruto->actor.room = -1;
                    ruto->roomNum1 = ruto->roomNum2 = ruto->roomNum3 = room;
                    ruto->unk_28C = (BgBdanObjects*)FindRutoSwitch();
                    PoseRuto(ruto);
                    ruto->actor.velocity = Vec3f{ 0.0f, 0.0f, 0.0f };
                    ruto->actor.speedXZ = 0.0f;
                    ruto->actor.minVelocityY = -6.8f;
                    ruto->actor.gravity = -1.3f;
                    ruto->action = 28; // falls into place, then offers to be lifted (27)
                }
                break;
            }
        }
    }
    nlohmann::json own = nlohmann::json::array();
    for (auto& [actor, o] : sOwn) {
        own.push_back({ { "k", o.key }, { "id", actor->id }, { "x", actor->world.pos.x }, { "y", actor->world.pos.y } });
    }
    nlohmann::json ghosts = nlohmann::json::array();
    for (auto& [actor, key] : sGhostActors) {
        nlohmann::json g = { { "key", key }, { "id", actor->id }, { "params", (u16)actor->params },
                             { "x", actor->world.pos.x }, { "y", actor->world.pos.y }, { "z", actor->world.pos.z } };
        if (actor->id == ACTOR_EN_BOM) {
            EnBom* bomb = (EnBom*)actor;
            g["timer"] = bomb->timer;
            g["at"] = (bomb->explosionCollider.base.atFlags & AT_ON) != 0;
            g["ac"] = (bomb->bombCollider.base.acFlags & AC_ON) != 0;
        }
        ghosts.push_back(g);
    }
    nlohmann::json puppets = nlohmann::json::array();
    if (Anchor::Instance != nullptr) {
        for (auto& [id, cl] : Anchor::Instance->clients) {
            if (cl.player != nullptr && !cl.self) {
                puppets.push_back({ { "id", id },
                                    { "stickLength", cl.player->unk_85C },
                                    { "stickFlame", cl.player->unk_860 },
                                    { "item", cl.player->heldItemAction } });
            }
        }
    }
    nlohmann::json rutos = nlohmann::json::array();
    if (gPlayState != NULL) {
        for (Actor* r = gPlayState->actorCtx.actorLists[ACTORCAT_NPC].head; r != NULL; r = r->next) {
            if (r->id != ACTOR_EN_RU1) {
                continue;
            }
            EnRu1* ruto = (EnRu1*)r;
            auto g = sGhostActors.find(r);
            rutos.push_back({ { "action", ruto->action },
                              { "draw", ruto->drawConfig },
                              { "alpha", ruto->alpha },
                              { "ghost", g != sGhostActors.end() },
                              { "adopted", g != sGhostActors.end() && sGhosts.count(g->second) &&
                                               sGhosts[g->second].adopted },
                              { "held", r->parent != NULL },
                              { "rooms", { ruto->roomNum1, ruto->roomNum2, ruto->roomNum3 } },
                              { "x", r->world.pos.x },
                              { "y", r->world.pos.y },
                              { "z", r->world.pos.z } });
        }
    }
    Player* self = gPlayState != NULL ? GET_PLAYER(gPlayState) : NULL;
    nlohmann::json j = { { "tick", sTick },
                         { "puppets", puppets },
                         { "self", self ? nlohmann::json{ { "stickLength", self->unk_85C },
                                                          { "stickFlame", self->unk_860 },
                                                          { "item", self->heldItemAction } }
                                        : nlohmann::json() },
                         { "own", own },
                         { "ghosts", ghosts },
                         { "rutos", rutos },
                         { "room", gPlayState != NULL ? gPlayState->roomCtx.curRoom.num : -1 },
                         { "scene", gPlayState != NULL ? gPlayState->sceneNum : -1 },
                         { "pos", self ? Vec(self->actor.world.pos) : nlohmann::json() },
                         { "hidden", sHidden.size() },
                         { "remote", sRemote.size() },
                         { "rejected", sRejected.size() } };
    out = j.dump();
    return out.c_str();
}
}
#endif
