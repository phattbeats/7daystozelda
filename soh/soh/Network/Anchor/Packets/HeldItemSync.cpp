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
};

struct Ghost {
    Actor* actor = nullptr;
    Actor* original = nullptr; // partner-side copy hidden while this carried prop is out
    ActorFunc originalDraw = nullptr;
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
    for (s32 cat : { ACTORCAT_EXPLOSIVE, ACTORCAT_PROP }) {
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
            } else if (IsProp(a->id)) {
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
                if (!held && sTick > own.airUntil) {
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

void SpawnGhost(uint64_t key, const Remote& r) {
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

bool PartnerStillHere(uint32_t from) {
    Anchor* anchor = Anchor::Instance;
    auto it = anchor->clients.find(from);
    return it != anchor->clients.end() && it->second.online && it->second.sceneNum == gPlayState->sceneNum;
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
            if (r.scene == gPlayState->sceneNum) {
                SpawnGhost(key, r);
            }
            continue;
        }
        if (IsDriven(r.id) && git->second.actor->update != NULL) {
            ApplyPose(git->second.actor, r);
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
    nlohmann::json j = { { "tick", sTick },
                         { "own", own },
                         { "ghosts", ghosts },
                         { "hidden", sHidden.size() },
                         { "remote", sRemote.size() },
                         { "rejected", sRejected.size() } };
    out = j.dump();
    return out.c_str();
}
}
#endif
