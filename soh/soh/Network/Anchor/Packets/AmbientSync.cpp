#include "soh/Network/Anchor/Anchor.h"
#include "soh/Network/Anchor/AmbientSync.h"
#include "soh/Network/Anchor/EnemySync.h"
#include <nlohmann/json.hpp>
#include <libultraship/libultraship.h>
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ActorDB.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

extern "C" {
#include "variables.h"
#include "functions.h"
#include "src/overlays/actors/ovl_En_Niw/z_en_niw.h"
#include "src/overlays/actors/ovl_En_Dog/z_en_dog.h"
extern PlayState* gPlayState;

void EnNiw_ResetAction(EnNiw* enNiw, PlayState* play);
void EnDog_FollowPlayer(EnDog* enDog, PlayState* play);
}

/**
 * AMBIENT_STATE
 *
 * Every client runs its own copy of a town's wandering actors, so each player saw
 * Kakariko's cuccos (and the dogs, the carpenters, the guards...) somewhere else
 * (#4042). These actors now follow one client's copy:
 *
 * - The driver streams pos/rot/focus/speed and the skeleton pose every frame. The
 *   driver is the scene authority EnemySync already elects (lowest same-scene
 *   clientId), except while a player is handling the actor: holding or throwing a
 *   cucco, talking to the NPC, hitting it, or being followed by the dog. That
 *   player claims it ("c": 1) and drives it until CLAIM_HOLD_TICKS after letting go
 *   (and until a thrown cucco lands).
 * - Everyone else still runs the actor's own update, so talking, lifting, getting
 *   pecked and the guards spotting you all work on your own screen, and then copies
 *   the driver's pose over the result ("soft" mirror). A claimed actor is a "hard"
 *   mirror: its update is skipped, so nobody else can pick up the cucco a partner is
 *   holding.
 * - With no fresh stream (driver in another room, or gone) the local copy runs on
 *   its own, as before.
 *
 * An actor is keyed by scene, room, id, params and its spawn position (taken before
 * Init runs), which come from the same room data on every client.
 */

namespace {

constexpr u32 STALE_TICKS = 10;       // a stream older than this no longer drives the copy
constexpr u32 PRUNE_TICKS = 200;      // stream cache entries are dropped after this
constexpr u32 CLAIM_HOLD_TICKS = 40;  // keep driving 2 s after the player lets go
constexpr u32 CLAIM_AIR_TICKS = 200;  // a thrown actor stays claimed until it lands (capped)
constexpr u32 KEEPALIVE_TICKS = 4;   // an unchanged actor is re-sent this often (< STALE_TICKS)
constexpr int32_t MAX_LIMBS = 48;
constexpr uint64_t KEY_MASK = (1ULL << 52) - 1; // JSON-number safe

enum Mode : uint8_t {
    MODE_LOCAL = 0, // no peer / no fresh stream: own AI
    MODE_DRIVE = 1, // scene authority: own AI, streamed
    MODE_CLAIM = 2, // the local player is handling it: own AI, streamed as a claim
    MODE_SOFT = 3,  // own update, then the driver's pose copied over it
    MODE_HARD = 4,  // a partner is handling it: update skipped, pose copied
};

struct Ambient {
    uint64_t key = 0;
    SkelAnime* skel = nullptr;
    Collider* acCollider = nullptr;
    u32 claimUntil = 0;   // local claim while sTick < claimUntil
    u32 airUntil = 0;     // thrown: claim held while airborne, until this tick
    bool wasHeld = false;
    bool hitLatched = false; // this AC_HIT was already counted (some actors never clear it)
    u32 lastSentTick = 0;
    size_t lastSentHash = 0;
    Mode mode = MODE_LOCAL;
    bool cullForced = false;
    bool hadUncull = false;
    bool hadAttention = false; // ACTOR_FLAG_ATTENTION_ENABLED before a hard-mirror stretch
};

struct Remote {
    uint32_t from = 0;
    u32 rxTick = 0;
    bool claim = false;
    Vec3f pos{};
    Vec3s rot{};
    Vec3s shape{};
    Vec3f focus{};
    f32 speed = 0.0f;
    f32 velY = 0.0f;
    std::vector<Vec3s> joints;
};

std::unordered_map<Actor*, Ambient> sTracked;
std::unordered_map<uint64_t, Actor*> sByKey;
std::unordered_map<uint64_t, Remote> sRemote;
std::unordered_map<Actor*, Vec3f> sSpawnPos;
SkelAnime* sSkelRing[8];
u8 sSkelRingIndex = 0;
u32 sTick = 0;
s16 sLastScene = -1;

bool SyncEnabled() {
    return CVarGetInteger(CVAR_REMOTE_ANCHOR("AmbientSync"), 1);
}

// The wanderers. Shopkeepers and other NPCs that stand still stay local: there is
// nothing to drift, and their talk/cutscene state is per player.
bool IsAmbient(s16 id) {
    switch (id) {
        case ACTOR_EN_NIW:            // cuccos (Kakariko, Lon Lon, market alley, Zora's River)
        case ACTOR_EN_NIW_GIRL:       // the girl chasing her cucco in Kakariko
        case ACTOR_EN_DOG:            // market / Kakariko dogs, Richard
        case ACTOR_EN_DAIKU_KAKARIKO: // carpenters walking around Kakariko
        case ACTOR_EN_HEISHI1:        // castle-courtyard patrol guards
        case ACTOR_EN_GE2:            // Gerudo Fortress patrol guards
        case ACTOR_EN_BUTTE:          // butterflies
        case ACTOR_EN_HORSE_NORMAL:   // Lon Lon corral horses
            return true;
        default:
            return false;
    }
}

uint64_t Mix(uint64_t h, int64_t v) {
    h ^= (uint64_t)v + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);
    return h;
}

uint64_t BaseKey(Actor* actor, const Vec3f& spawn) {
    uint64_t h = 1469598103934665603ULL;
    h = Mix(h, gPlayState->sceneNum);
    h = Mix(h, actor->room);
    h = Mix(h, actor->id);
    h = Mix(h, (u16)actor->params);
    h = Mix(h, (int64_t)lroundf(spawn.x));
    h = Mix(h, (int64_t)lroundf(spawn.y));
    h = Mix(h, (int64_t)lroundf(spawn.z));
    return h & KEY_MASK;
}

bool PeerInScene() {
    return EnemySync::HasSameScenePeer();
}

Player* LocalPlayer() {
    return GET_PLAYER(gPlayState);
}

bool IsLocalAttacker(Actor* attacker) {
    Player* player = LocalPlayer();
    return attacker != nullptr &&
           (attacker == &player->actor || attacker->parent == &player->actor);
}

bool IsHeldLocally(Actor* actor) {
    Player* player = LocalPlayer();
    return actor->parent == &player->actor || player->heldActor == actor;
}

// The local player is handling this actor right now.
bool LocallyEngaged(Actor* actor, Ambient& st) {
    if (IsHeldLocally(actor)) {
        return true;
    }
    // ACTOR_FLAG_TALK only lasts the frame the talk starts; the conversation itself
    // is Link's TALKING state with this actor as talkActor.
    Player* player = LocalPlayer();
    if ((actor->flags & ACTOR_FLAG_TALK) ||
        ((player->stateFlags1 & PLAYER_STATE1_TALKING) && player->talkActor == actor)) {
        return true;
    }
    bool hit = st.acCollider != nullptr && (st.acCollider->acFlags & AC_HIT);
    if (hit && !st.hitLatched && IsLocalAttacker(st.acCollider->ac)) {
        st.hitLatched = true;
        return true;
    }
    st.hitLatched = hit;
    if (actor->id == ACTOR_EN_DOG && ((EnDog*)actor)->actionFunc == EnDog_FollowPlayer) {
        return true;
    }
    return false;
}

void UpdateClaim(Actor* actor, Ambient& st) {
    bool held = IsHeldLocally(actor);
    if (LocallyEngaged(actor, st)) {
        st.claimUntil = sTick + CLAIM_HOLD_TICKS;
    }
    if (st.wasHeld && !held) {
        st.airUntil = sTick + CLAIM_AIR_TICKS; // just thrown or put down
    }
    st.wasHeld = held;
    if (st.airUntil > sTick) {
        if (actor->bgCheckFlags & 1) {
            st.airUntil = 0; // landed
        } else {
            st.claimUntil = std::max(st.claimUntil, sTick + CLAIM_HOLD_TICKS);
        }
    }
}

const Remote* FreshRemote(const Ambient& st) {
    auto it = sRemote.find(st.key);
    if (it == sRemote.end() || sTick - it->second.rxTick > STALE_TICKS) {
        return nullptr;
    }
    return &it->second;
}

Mode ComputeMode(Actor* actor, Ambient& st) {
    if (!PeerInScene()) {
        return MODE_LOCAL;
    }
    const Remote* r = FreshRemote(st);
    bool claimed = st.claimUntil > sTick;
    if (claimed) {
        // Two players grabbing at once: the lower clientId keeps it, unless we are
        // actually holding it (our Link can't be made to let go).
        if (r != nullptr && r->claim && r->from < Anchor::Instance->ownClientId && !IsHeldLocally(actor)) {
            st.claimUntil = 0;
            return MODE_HARD;
        }
        return MODE_CLAIM;
    }
    if (r != nullptr && r->claim) {
        return MODE_HARD;
    }
    if (EnemySync::IsLocalAuthority()) {
        return MODE_DRIVE;
    }
    if (r != nullptr && r->from == EnemySync::CurrentAuthorityId()) {
        return MODE_SOFT;
    }
    return MODE_LOCAL;
}

void ApplyPose(Actor* actor, Ambient& st, const Remote& r) {
    actor->world.pos = r.pos;
    actor->world.rot = r.rot;
    actor->shape.rot = r.shape;
    actor->focus.pos = r.focus;
    actor->speedXZ = r.speed;
    actor->velocity.y = r.velY;
    if (st.skel != nullptr && st.skel->jointTable != nullptr && (int32_t)r.joints.size() == st.skel->limbCount) {
        memcpy(st.skel->jointTable, r.joints.data(), r.joints.size() * sizeof(Vec3s));
    }
}

// After a stretch as a hard mirror the actor's own AI resumes from wherever the
// partner left it. Cuccos wander around a remembered spot: move that spot to here,
// or the cucco walks straight back to where it was picked up.
void ResumeLocal(Actor* actor) {
    if (actor->id == ACTOR_EN_NIW) {
        EnNiw* niw = (EnNiw*)actor;
        niw->unk_2AC = niw->unk_2B8 = actor->world.pos;
        niw->timer5 = niw->timer4 = niw->unk_29E = 0;
        niw->unk_2FC = niw->unk_300 = 0.0f;
        actor->speedXZ = 0.0f;
        actor->shape.rot.x = actor->shape.rot.z = 0;
        actor->world.rot.x = actor->world.rot.z = 0;
        niw->actionFunc = EnNiw_ResetAction;
    }
}

void ForceUncull(Actor* actor, Ambient& st) {
    if (!st.cullForced) {
        st.hadUncull = (actor->flags & ACTOR_FLAG_UPDATE_CULLING_DISABLED) != 0;
        st.cullForced = true;
    }
    actor->flags |= ACTOR_FLAG_UPDATE_CULLING_DISABLED;
}

void RestoreCull(Actor* actor, Ambient& st) {
    if (st.cullForced && !st.hadUncull) {
        actor->flags &= ~ACTOR_FLAG_UPDATE_CULLING_DISABLED;
    }
    st.cullForced = false;
}

// A hard mirror switches the actor's Z-target flag off; put it back when the mirror
// stretch ends by any route (partner released, partner left, sync turned off).
void RestoreAttention(Actor* actor, Ambient& st) {
    if (st.hadAttention) {
        actor->flags |= ACTOR_FLAG_ATTENTION_ENABLED;
    }
    st.hadAttention = false;
}

void Reset() {
    sTracked.clear();
    sByKey.clear();
    sRemote.clear();
    sSpawnPos.clear();
    memset(sSkelRing, 0, sizeof(sSkelRing));
}

void OnInit(Actor* actor) {
    auto sp = sSpawnPos.find(actor);
    if (sp == sSpawnPos.end() || gPlayState == NULL) {
        return;
    }
    Vec3f spawn = sp->second;
    sSpawnPos.erase(sp);

    // Same id/params/spot twice (never in vanilla data, but cheap to guard): number
    // them in init order, which follows the room's actor list on every client.
    uint64_t key = BaseKey(actor, spawn);
    for (int n = 1; sByKey.contains(key); n++) {
        key = Mix(BaseKey(actor, spawn), n) & KEY_MASK;
    }

    Ambient st;
    st.key = key;
    size_t instanceSize = ActorDB::Instance->RetrieveEntry(actor->id).entry.instanceSize;
    uintptr_t base = (uintptr_t)actor;
    for (auto& skel : sSkelRing) {
        if (skel != nullptr && (uintptr_t)skel >= base && (uintptr_t)skel < base + instanceSize) {
            if (st.skel == nullptr) {
                st.skel = skel;
            }
            skel = nullptr;
        }
    }
    sTracked[actor] = st;
    sByKey[key] = actor;
}

void OnShouldUpdate(Actor* actor, bool* should) {
    auto it = sTracked.find(actor);
    if (it == sTracked.end()) {
        return;
    }
    if (!SyncEnabled()) {
        RestoreAttention(actor, it->second);
        it->second.mode = MODE_LOCAL;
        return;
    }
    Ambient& st = it->second;
    UpdateClaim(actor, st);
    Mode prev = st.mode;
    st.mode = ComputeMode(actor, st);
    if (st.mode == MODE_HARD) {
        // Nothing to target or talk to while a partner is handling it (Navi would
        // otherwise hover over the cucco the partner is carrying).
        if (prev != MODE_HARD) {
            st.hadAttention = (actor->flags & ACTOR_FLAG_ATTENTION_ENABLED) != 0;
        }
        actor->flags &= ~ACTOR_FLAG_ATTENTION_ENABLED;
        ApplyPose(actor, st, *FreshRemote(st));
        *should = false;
    } else if (prev == MODE_HARD) {
        RestoreAttention(actor, st);
        ResumeLocal(actor);
    }
}

void OnUpdate(Actor* actor) {
    auto it = sTracked.find(actor);
    if (it == sTracked.end() || !SyncEnabled()) {
        return;
    }
    Ambient& st = it->second;
    if (st.mode != MODE_SOFT) {
        return;
    }
    // The update may have just handed it to us (picked up, talked to, hit).
    UpdateClaim(actor, st);
    if (st.claimUntil > sTick) {
        st.mode = MODE_CLAIM;
        return;
    }
    if (const Remote* r = FreshRemote(st)) {
        ApplyPose(actor, st, *r);
    }
}

f32 Round1(f32 v) {
    return roundf(v * 10.0f) / 10.0f;
}

nlohmann::json Snapshot(Actor* actor, Ambient& st, bool claim) {
    nlohmann::json e;
    e["k"] = st.key;
    if (claim) {
        e["c"] = 1;
    }
    e["p"] = { Round1(actor->world.pos.x), Round1(actor->world.pos.y), Round1(actor->world.pos.z) };
    e["r"] = { actor->world.rot.x, actor->world.rot.y, actor->world.rot.z };
    e["s"] = { actor->shape.rot.x, actor->shape.rot.y, actor->shape.rot.z };
    e["f"] = { Round1(actor->focus.pos.x), Round1(actor->focus.pos.y), Round1(actor->focus.pos.z) };
    e["v"] = { Round1(actor->speedXZ), Round1(actor->velocity.y) };
    SkelAnime* skel = st.skel;
    if (skel != nullptr && skel->jointTable != nullptr && skel->limbCount > 0 && skel->limbCount <= MAX_LIMBS) {
        nlohmann::json jt = nlohmann::json::array();
        for (int32_t i = 0; i < skel->limbCount; i++) {
            jt.push_back(skel->jointTable[i].x);
            jt.push_back(skel->jointTable[i].y);
            jt.push_back(skel->jointTable[i].z);
        }
        e["j"] = jt;
    }
    return e;
}

Vec3f JsonVecF(const nlohmann::json& j) {
    return Vec3f{ j[0].get<f32>(), j[1].get<f32>(), j[2].get<f32>() };
}

Vec3s JsonVecS(const nlohmann::json& j) {
    return Vec3s{ j[0].get<s16>(), j[1].get<s16>(), j[2].get<s16>() };
}

} // namespace

void Anchor::HandlePacket_AmbientState(nlohmann::json payload) {
    if (!IsSaveLoaded() || gPlayState == NULL || !SyncEnabled()) {
        return;
    }
    if (!payload.contains("sceneNum") || payload["sceneNum"].get<s16>() != gPlayState->sceneNum ||
        !payload.contains("clientId") || !payload.contains("actors")) {
        return;
    }
    uint32_t from = payload["clientId"].get<uint32_t>();
    for (auto& e : payload["actors"]) {
        uint64_t key = e["k"].get<uint64_t>();
        bool claim = e.contains("c");
        auto it = sRemote.find(key);
        // A partner handling the actor outranks the authority's plain stream.
        if (!claim && it != sRemote.end() && it->second.claim && it->second.from != from &&
            sTick - it->second.rxTick <= STALE_TICKS) {
            continue;
        }
        Remote& r = sRemote[key];
        r.from = from;
        r.rxTick = sTick;
        r.claim = claim;
        r.pos = JsonVecF(e["p"]);
        r.rot = JsonVecS(e["r"]);
        r.shape = JsonVecS(e["s"]);
        r.focus = JsonVecF(e["f"]);
        r.speed = e["v"][0].get<f32>();
        r.velY = e["v"][1].get<f32>();
        r.joints.clear();
        if (e.contains("j")) {
            const auto& jt = e["j"];
            for (size_t i = 0; i + 2 < jt.size(); i += 3) {
                r.joints.push_back(Vec3s{ jt[i].get<s16>(), jt[i + 1].get<s16>(), jt[i + 2].get<s16>() });
            }
        }
    }
}

void RegisterAmbientSyncHooks(bool isConnected) {
    Reset();
    sLastScene = -1;

    COND_HOOK(OnSceneInit, isConnected, [](int16_t sceneNum) { Reset(); });

    COND_HOOK(ShouldActorInit, isConnected, [](void* refActor, bool* should) {
        Actor* actor = (Actor*)refActor;
        if (IsAmbient(actor->id)) {
            sSpawnPos[actor] = actor->world.pos;
        }
    });

    COND_HOOK(OnSkelAnimeInit, isConnected, [](void* skelAnime) {
        if (skelAnime != NULL) {
            sSkelRing[sSkelRingIndex] = (SkelAnime*)skelAnime;
            sSkelRingIndex = (sSkelRingIndex + 1) % 8;
        }
    });

    COND_HOOK(OnActorInit, isConnected, [](void* refActor) {
        Actor* actor = (Actor*)refActor;
        if (IsAmbient(actor->id)) {
            OnInit(actor);
        }
    });

    COND_HOOK(ShouldActorUpdate, isConnected,
              [](void* refActor, bool* should) { OnShouldUpdate((Actor*)refActor, should); });

    COND_HOOK(OnActorUpdate, isConnected, [](void* refActor) { OnUpdate((Actor*)refActor); });

    COND_HOOK(OnCollisionCheckSetAC, isConnected, [](void* refActor, void* collider) {
        auto it = sTracked.find((Actor*)refActor);
        if (it != sTracked.end()) {
            it->second.acCollider = (Collider*)collider;
        }
    });

    COND_HOOK(OnActorDestroy, isConnected, [](void* refActor) {
        Actor* actor = (Actor*)refActor;
        sSpawnPos.erase(actor);
        auto it = sTracked.find(actor);
        if (it != sTracked.end()) {
            sByKey.erase(it->second.key);
            sTracked.erase(it);
        }
    });
}

void AmbientSyncTick() {
    sTick++;
    Anchor* anchor = Anchor::Instance;
    if (gPlayState == NULL || anchor == nullptr || !anchor->IsSaveLoaded()) {
        return;
    }
    if (gPlayState->sceneNum != sLastScene) {
        sLastScene = gPlayState->sceneNum;
        sRemote.clear();
    }
    for (auto it = sRemote.begin(); it != sRemote.end();) {
        if (sTick - it->second.rxTick > PRUNE_TICKS) {
            it = sRemote.erase(it);
        } else {
            ++it;
        }
    }

    bool peer = SyncEnabled() && PeerInScene();
    nlohmann::json actors = nlohmann::json::array();
    for (auto& [actor, st] : sTracked) {
        if (!peer) {
            RestoreCull(actor, st);
            RestoreAttention(actor, st);
            st.mode = MODE_LOCAL;
            continue;
        }
        // Actors next to the partner must keep moving here too (driver), and the
        // ShouldActorUpdate hook must fire to apply the stream (mirror).
        ForceUncull(actor, st);
        if (actor->update == NULL) {
            continue;
        }
        if (st.mode == MODE_DRIVE || st.mode == MODE_CLAIM) {
            nlohmann::json e = Snapshot(actor, st, st.mode == MODE_CLAIM);
            size_t hash = std::hash<std::string>{}(e.dump());
            if (hash == st.lastSentHash && sTick - st.lastSentTick < KEEPALIVE_TICKS) {
                continue; // idle: the partner's copy is still fresh
            }
            st.lastSentHash = hash;
            st.lastSentTick = sTick;
            actors.push_back(std::move(e));
        }
    }
    if (actors.empty()) {
        return;
    }

    nlohmann::json payload;
    payload["type"] = Anchor::AMBIENT_STATE;
    payload["quiet"] = true;
    payload["sceneNum"] = gPlayState->sceneNum;
    payload["actors"] = actors;
    for (auto& [clientId, client] : anchor->clients) {
        if (client.sceneNum == gPlayState->sceneNum && client.online && client.isSaveLoaded && !client.self) {
            payload["targetClientId"] = clientId;
            anchor->SendJsonToRemote(payload);
        }
    }
}

#ifdef __EMSCRIPTEN__
extern "C" {

// #4042 tests: every tracked ambient actor with its mode (0 local, 1 drive,
// 2 claim, 3 soft mirror, 4 hard mirror) and stream age. index >= 0 claims that
// actor as if the local player had picked it up and moves it by (dx, dz), so a
// test can check that the partner's copy follows.
EMSCRIPTEN_KEEPALIVE
const char* anchor_test_ambient(int index, double dx, double dz) {
    static std::string out;
    nlohmann::json list = nlohmann::json::array();
    int i = 0;
    for (auto& [actor, st] : sTracked) {
        if (i == index) {
            st.claimUntil = sTick + CLAIM_HOLD_TICKS * 3;
            actor->world.pos.x += (f32)dx;
            actor->world.pos.z += (f32)dz;
            actor->prevPos = actor->world.pos;
            if (actor->id == ACTOR_EN_NIW) {
                EnNiw* niw = (EnNiw*)actor;
                niw->unk_2AC = niw->unk_2B8 = actor->world.pos;
            }
        }
        auto r = sRemote.find(st.key);
        list.push_back({ { "i", i },
                         { "k", std::to_string(st.key) },
                         { "id", actor->id },
                         { "params", (u16)actor->params },
                         { "room", actor->room },
                         { "pos", { actor->world.pos.x, actor->world.pos.y, actor->world.pos.z } },
                         { "mode", (int)st.mode },
                         { "claim", st.claimUntil > sTick },
                         { "skel", st.skel != nullptr ? st.skel->limbCount : 0 },
                         { "age", r != sRemote.end() ? (int64_t)(sTick - r->second.rxTick) : -1 },
                         { "from", r != sRemote.end() ? (int64_t)r->second.from : -1 } });
        i++;
    }
    nlohmann::json j = { { "tick", sTick },
                         { "own", Anchor::Instance ? Anchor::Instance->ownClientId : 0 },
                         { "auth", EnemySync::CurrentAuthorityId() },
                         { "peer", gPlayState ? PeerInScene() : false },
                         { "scene", gPlayState ? gPlayState->sceneNum : -1 },
                         { "actors", list } };
    out = j.dump();
    return out.c_str();
}
}
#endif
