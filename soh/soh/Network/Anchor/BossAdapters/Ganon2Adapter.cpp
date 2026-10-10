#include "soh/Network/Anchor/BossAdapters/ActorSyncAdapter.h"
#include "soh/Network/Anchor/EnemySync.h"
#include "soh/Network/Anchor/Anchor.h"
#include "soh/OTRGlobals.h"

extern "C" {
#include "functions.h"
#include "macros.h"
#include "variables.h"
#include "src/overlays/actors/ovl_Boss_Ganon2/z_boss_ganon2.h"

u8 BossGanon2_CoopPhase(Actor* thisx);
s32 BossGanon2_CoopIsLying(Actor* thisx);
u8 BossGanon2_CoopCsState(Actor* thisx);
void BossGanon2_CoopStartDownedCs(Actor* thisx, PlayState* play);
void BossGanon2_CoopStartSwordCs(Actor* thisx, PlayState* play);
void BossGanon2_CoopSwordTaken(Actor* thisx);
void BossGanon2_CoopStartDefeat(Actor* thisx, PlayState* play);
void BossGanon2_CoopResume(Actor* thisx, PlayState* play);
void BossGanon2_CoopMirrorUpdate(Actor* thisx, PlayState* play);
extern PlayState* gPlayState;
}

/**
 * Ganon (#4054, Boss_Ganon2), the beast, the Gohma treatment. One tracked boss. Every
 * cutscene runs on every client; the fight itself is streamed.
 *
 * Phases (BossGanon2_CoopPhase), from the action and its cutscene state, never from the
 * action alone:
 *   0 INTRO     the arrival cutscene (Zelda, the sword knocked away); ends with his first guard
 *   1 FIGHT     every attack, stun, hurt, and him lying downed once the cutscene is over
 *   3 DOWN_CS   the cutscene when his health first drops under 21 (Zelda's text)
 *   4 SWORD_CS  the cutscene when Link takes up the Master Sword
 *   2 DEFEATED  the finale (the light arrow, the sword in the head, the Sages)
 * A client mirrors only while its own copy is in FIGHT; a streamed edge into 3, 4 or 2 starts
 * that cutscene locally (BossGanon2_CoopStart*), and the client mirrors again as soon as its
 * own cutscene is over. A client still in a cutscene when the next edge arrives remembers it
 * (the tick in ShouldMirror) and starts it when its own cutscene ends, so two cameras never
 * run at once. The finale releases him for good: it runs natively on every client.
 *
 * Extras: the head turn, the tail sway, the glows (the sword's, the body's, the stun), the
 * flash timer, the eye state, the swing flag that places his sword's collider, whether he
 * faces the player, and whether he is lying.
 *
 * The Master Sword: the intro knocks it out of Link's hand on every client, so every client
 * has one lying in the ring. Only the authority's Ganon can start the cutscene, so a mirror
 * that picks it up while Ganon is lying reports the pickup (event 1) and the authority takes
 * the sword. A mirror that steps on it at any other time leaves it lying.
 *
 * Aggro: the EnemyTargeting puppet swap stays off for bosses (the cutscenes read GET_PLAYER).
 * His facing, his walk and his swings use the nearest living player (the Update override of
 * yawTowardsPlayer / xzDistToPlayer). That is safe because his sword's collider hurts only the
 * Link of the machine it is drawn on, and the ring of fire (BossGanon2_CoopMirrorUpdate) burns
 * the local Link on every machine. Limit: the finale takes both players to the same spot.
 */

enum Ganon2Phase : uint8_t {
    GN2_PHASE_INTRO = 0,
    GN2_PHASE_FIGHT = 1,
    GN2_PHASE_DEFEATED = 2,
    GN2_PHASE_DOWN_CS = 3,
    GN2_PHASE_SWORD_CS = 4,
};

enum Ganon2Event : uint8_t {
    GN2_EVENT_SWORD_TAKEN = 1,
};

struct Ganon2Mirror {
    Actor* boss = nullptr;
    bool lying = false;       // the authority says Ganon is lying and free to be slashed
    bool pendingDown = false; // a streamed cutscene edge that waits for our own cutscene to end
    bool pendingSword = false;
    bool pendingDefeat = false;
    Actor* pendingBoss = nullptr; // the Ganon the pending cutscenes belong to
};
static Ganon2Mirror sMirror;

static float GN2_F(const nlohmann::json& x, const char* key, float def = 0.0f) {
    auto it = x.find(key);
    return it != x.end() && it->is_number() ? it->get<float>() : def;
}

static int GN2_I(const nlohmann::json& x, const char* key, int def = 0) {
    auto it = x.find(key);
    return it != x.end() && it->is_number() ? (int)it->get<float>() : def;
}

// The array under `key` when it has exactly `n` numbers, else nullptr.
static const nlohmann::json* GN2_Arr(const nlohmann::json& x, const char* key, size_t n) {
    auto it = x.find(key);
    if (it == x.end() || !it->is_array() || it->size() != n) {
        return nullptr;
    }
    for (const auto& v : *it) {
        if (!v.is_number()) {
            return nullptr;
        }
    }
    return &*it;
}

// A new fight (Ganon's Init) or the end of one (his Destroy: scene change, leaving the room): nothing from
// the last fight may carry over, least of all a pending defeat that would kill the next Ganon at once.
extern "C" void Anchor_Ganon2Reset() {
    sMirror = Ganon2Mirror();
}

static void GN2_Owe(Actor* actor, bool& flag) {
    if (sMirror.pendingBoss != actor) {
        sMirror.pendingDown = sMirror.pendingSword = sMirror.pendingDefeat = false;
        sMirror.pendingBoss = actor;
    }
    flag = true;
}

static uint8_t GN2_GetPhase(Actor* actor) {
    return BossGanon2_CoopPhase(actor);
}

// Starts a cutscene the authority is in, or the one we owe it (see the file comment).
// Returns true if our copy is now in a cutscene.
static bool GN2_StartPending(Actor* actor) {
    if (gPlayState == NULL || BossGanon2_CoopPhase(actor) != GN2_PHASE_FIGHT) {
        return false;
    }
    if (sMirror.pendingBoss != actor) {
        sMirror.pendingDown = sMirror.pendingSword = sMirror.pendingDefeat = false;
        return false;
    }
    if (sMirror.pendingDefeat) {
        sMirror.pendingDefeat = sMirror.pendingDown = sMirror.pendingSword = false;
        BossGanon2_CoopStartDefeat(actor, gPlayState);
        ESYNC_LOG("[Ganon2Sync] defeat started locally (deferred)");
        return true;
    }
    if (sMirror.pendingDown) {
        sMirror.pendingDown = false;
        BossGanon2_CoopStartDownedCs(actor, gPlayState);
        ESYNC_LOG("[Ganon2Sync] downed cutscene started locally (deferred)");
        return true;
    }
    if (sMirror.pendingSword) {
        sMirror.pendingSword = false;
        BossGanon2_CoopStartSwordCs(actor, gPlayState);
        ESYNC_LOG("[Ganon2Sync] sword cutscene started locally (deferred)");
        return true;
    }
    return false;
}

static bool GN2_ShouldMirror(Actor* actor, uint8_t streamedPhase) {
    (void)streamedPhase;
    if (GN2_StartPending(actor)) {
        return false;
    }
    // Lying with the cutscene over: stay a picture whatever the authority is doing, or this
    // copy would get up on its own timer.
    if (BossGanon2_CoopIsLying(actor) && actor->colChkInfo.health > 0) {
        return true;
    }
    return BossGanon2_CoopPhase(actor) == GN2_PHASE_FIGHT && streamedPhase == GN2_PHASE_FIGHT &&
           actor->colChkInfo.health > 0;
}

static void GN2_Serialize(Actor* actor, nlohmann::json& x) {
    if (actor->id != ACTOR_BOSS_GANON2) {
        return;
    }
    BossGanon2* b = (BossGanon2*)actor;
    x["cs"] = BossGanon2_CoopCsState(actor);
    x["ly"] = BossGanon2_CoopIsLying(actor) ? 1 : 0;
    x["b4"] = b->unk_334;
    x["b7"] = b->unk_337;
    x["f2"] = b->unk_312;
    x["lk"] = b->unk_313;
    x["e6"] = b->unk_336;
    x["hd"] = { b->unk_31A, b->unk_31C, b->unk_346, b->unk_342, b->unk_316 };
    nlohmann::json tail = nlohmann::json::array();
    for (int i = 0; i < 5; i++) {
        tail.push_back(b->unk_348[i]);
    }
    for (int i = 0; i < 5; i++) {
        tail.push_back(b->unk_352[i]);
    }
    x["tl"] = tail;
    x["gl"] = { b->unk_30C, b->unk_380, b->unk_37C, b->unk_384, b->unk_38C, b->unk_1B4,
                b->unk_324, b->unk_224, b->unk_228, b->unk_394, b->unk_41C };
    x["fl"] = (actor->flags & ACTOR_FLAG_ATTENTION_ENABLED) ? 1 : 0;
}

static void GN2_Deserialize(Actor* actor, const nlohmann::json& x) {
    if (actor->id != ACTOR_BOSS_GANON2 || gPlayState == NULL) {
        return;
    }
    BossGanon2* b = (BossGanon2*)actor;
    sMirror.boss = actor;
    sMirror.lying = GN2_I(x, "ly") != 0;

    b->unk_334 = (u8)GN2_I(x, "b4", b->unk_334);
    b->unk_337 = (u8)GN2_I(x, "b7", b->unk_337);
    b->unk_312 = (u8)GN2_I(x, "f2");
    b->unk_313 = (u8)GN2_I(x, "lk");
    b->unk_336 = (u8)GN2_I(x, "e6");
    if (auto a = GN2_Arr(x, "hd", 5)) {
        b->unk_31A = (s16)(*a)[0].get<float>();
        b->unk_31C = (s16)(*a)[1].get<float>();
        b->unk_346 = (s16)(*a)[2].get<float>();
        b->unk_342 = (s16)(*a)[3].get<float>();
        // unk_316 is streamed for the picture's sake only; our own knock-back timer counts it down.
    }
    if (auto a = GN2_Arr(x, "tl", 10)) {
        for (int i = 0; i < 5; i++) {
            b->unk_348[i] = (s16)(*a)[i].get<float>();
            b->unk_352[i] = (s16)(*a)[5 + i].get<float>();
        }
    }
    if (auto a = GN2_Arr(x, "gl", 11)) {
        b->unk_30C = (*a)[0].get<float>();
        b->unk_380 = (*a)[1].get<float>();
        b->unk_37C = (*a)[2].get<float>();
        b->unk_384 = (*a)[3].get<float>();
        b->unk_38C = (*a)[4].get<float>();
        b->unk_1B4 = (*a)[5].get<float>();
        b->unk_324 = (*a)[6].get<float>();
        b->unk_224 = (*a)[7].get<float>();
        b->unk_228 = (*a)[8].get<float>();
        b->unk_394 = (*a)[9].get<float>();
        b->unk_41C = (*a)[10].get<float>();
    }
    if (GN2_I(x, "fl") != 0) {
        actor->flags |= ACTOR_FLAG_ATTENTION_ENABLED;
    } else {
        actor->flags &= ~ACTOR_FLAG_ATTENTION_ENABLED;
    }
    BossGanon2_CoopMirrorUpdate(actor, gPlayState);
}

static bool GN2_LocalCutsceneRunning(Actor* actor) {
    return BossGanon2_CoopPhase(actor) != GN2_PHASE_FIGHT;
}

static bool GN2_OnPhaseChange(Actor* actor, uint8_t fromPhase, uint8_t toPhase) {
    ESYNC_LOG("[Ganon2Sync] phase {}->{}", fromPhase, toPhase);
    if (gPlayState == NULL) {
        return false;
    }
    switch (toPhase) {
        case GN2_PHASE_DEFEATED:
            if (GN2_LocalCutsceneRunning(actor)) {
                GN2_Owe(actor, sMirror.pendingDefeat);
                ESYNC_LOG("[Ganon2Sync] defeat deferred until the local cutscene ends");
                return false;
            }
            BossGanon2_CoopStartDefeat(actor, gPlayState);
            ESYNC_LOG("[Ganon2Sync] defeat handoff (finale started locally)");
            return true;
        case GN2_PHASE_DOWN_CS:
            if (GN2_LocalCutsceneRunning(actor)) {
                GN2_Owe(actor, sMirror.pendingDown);
            } else {
                BossGanon2_CoopStartDownedCs(actor, gPlayState);
            }
            return false;
        case GN2_PHASE_SWORD_CS:
            if (GN2_LocalCutsceneRunning(actor)) {
                GN2_Owe(actor, sMirror.pendingSword);
            } else {
                BossGanon2_CoopStartSwordCs(actor, gPlayState);
            }
            return false;
        default:
            return false;
    }
}

static void GN2_OnRemoteDefeat(Actor* actor) {
    if (gPlayState == NULL || BossGanon2_CoopPhase(actor) == GN2_PHASE_DEFEATED) {
        return;
    }
    if (GN2_LocalCutsceneRunning(actor)) {
        GN2_Owe(actor, sMirror.pendingDefeat);
        ESYNC_LOG("[Ganon2Sync] remote defeat deferred until the local cutscene ends");
        return;
    }
    BossGanon2_CoopStartDefeat(actor, gPlayState);
    ESYNC_LOG("[Ganon2Sync] remote defeat (missed phase edge, finale started locally)");
}

static void GN2_OnLocalResume(Actor* actor) {
    ESYNC_LOG("[Ganon2Sync] local AI resumes");
    // A cutscene we still owe survives the resume (the stream went stale, or we became the host, while
    // this client was in its own cutscene); Anchor_Ganon2Tick starts it when that cutscene ends.
    if (gPlayState == NULL || GN2_LocalCutsceneRunning(actor)) {
        return;
    }
    if (sMirror.pendingBoss == actor && (sMirror.pendingDefeat || sMirror.pendingDown || sMirror.pendingSword)) {
        GN2_StartPending(actor);
        return;
    }
    BossGanon2_CoopResume(actor, gPlayState);
}

// z_boss_ganon2.c, every Update of a Ganon that runs its own AI (the host, or a copy that resumed): starts
// a cutscene this client owes once its own cutscene is over.
extern "C" void Anchor_Ganon2Tick(Actor* boss) {
    if (sMirror.pendingBoss == boss && (sMirror.pendingDefeat || sMirror.pendingDown || sMirror.pendingSword)) {
        GN2_StartPending(boss);
    }
}

static void GN2_OnRemoteEvent(Actor* actor, uint8_t event) {
    if (event == GN2_EVENT_SWORD_TAKEN) {
        BossGanon2_CoopSwordTaken(actor);
    }
}

void RegisterGanon2Adapter() {
    ActorSyncAdapter a;
    a.SerializeExtras = GN2_Serialize;
    a.DeserializeExtras = GN2_Deserialize;
    a.GetPhase = GN2_GetPhase;
    a.OnPhaseChange = GN2_OnPhaseChange;
    a.ShouldMirror = GN2_ShouldMirror;
    a.OnRemoteDefeat = GN2_OnRemoteDefeat;
    a.OnLocalResume = GN2_OnLocalResume;
    a.OnRemoteEvent = GN2_OnRemoteEvent;
    a.DropUnconsumedHits = true;
    EnemySync::RegisterAdapter(ACTOR_BOSS_GANON2, a);
}

// z_boss_ganon2.c, when his arrival cutscene ends and he takes his first guard: the partner
// may already be deep in the fight, so the tick in ShouldMirror takes over from here.
extern "C" void Anchor_Ganon2IntroOver(Actor* boss) {
    (void)boss;
    ESYNC_LOG("[Ganon2Sync] local intro over");
}

// z_boss_ganon2.c, the Master Sword's pickup check. 0: run the pickup as usual (we are the
// authority, or alone); 1: it was picked up for a mirror (the authority has been told);
// 2: not yet, leave it lying.
extern "C" s32 Anchor_Ganon2SwordPickup(Actor* boss) {
    if (EnemySync::CurrentAuthorityId() == UINT32_MAX || EnemySync::IsLocalAuthority()) {
        return 0;
    }
    if (!sMirror.lying || sMirror.boss != boss) {
        return 2;
    }
    EnemySync::SendAdapterEvent(boss, GN2_EVENT_SWORD_TAKEN);
    return 1;
}

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
extern "C" {
// #4054 boss rig. Reports Ganon's sync state on this client:
// cmd 0 report only; 1 land a hit on the head collider the way the collision check would
// (arg = dmgFlags, default sword) on element 0 (front) or, with cmd 6, element 15 (tail);
// 2 set the boss health to arg (authority); 3 un-clear this room; 4 teleport Link to (arg, y, z)
// of the ring; 5 set csState to arg (authority); 8 start the downed cutscene (authority);
// 9 start the finale (authority); 10 put the Master Sword on B.
EMSCRIPTEN_KEEPALIVE
const char* anchor_test_gn2(int cmd, int arg) {
    static std::string out;
    static ColliderInfo sToucher;
    nlohmann::json j;
    if (gPlayState == NULL) {
        return "{}";
    }
    BossGanon2* boss = nullptr;
    int hearts = 0, warps = 0, zelda = 0;
    for (int cat = 0; cat < ACTORCAT_MAX; cat++) {
        for (Actor* a = gPlayState->actorCtx.actorLists[cat].head; a != nullptr; a = a->next) {
            if (a->update == NULL) {
                continue;
            }
            if (a->id == ACTOR_BOSS_GANON2) {
                boss = (BossGanon2*)a;
            }
            hearts += a->id == ACTOR_ITEM_B_HEART;
            warps += a->id == ACTOR_DOOR_WARP1;
            zelda += a->id == ACTOR_EN_ZL3;
        }
    }
    Player* player = GET_PLAYER(gPlayState);
    j["scene"] = gPlayState->sceneNum;
    j["auth"] = EnemySync::CurrentAuthorityId();
    j["own"] = Anchor::Instance != nullptr ? Anchor::Instance->ownClientId : 0;
    j["link"] = { player->actor.world.pos.x, player->actor.world.pos.y, player->actor.world.pos.z };
    j["health"] = gSaveContext.health;
    j["iframes"] = player->invincibilityTimer;
    j["csAction"] = player->csAction;
    j["hearts"] = hearts;
    j["warps"] = warps;
    j["zelda"] = zelda;
    j["clear"] = Flags_GetClear(gPlayState, gPlayState->roomCtx.curRoom.num);
    if (cmd == 3) {
        Flags_UnsetClear(gPlayState, gPlayState->roomCtx.curRoom.num);
    } else if (cmd == 4) {
        player->actor.world.pos.x = (float)arg;
    } else if (cmd == 10) {
        // the Master Sword on B, as the finale's stab needs it
        Item_Give(gPlayState, ITEM_SWORD_MASTER);
        gSaveContext.equips.buttonItems[0] = ITEM_SWORD_MASTER;
        Inventory_ChangeEquipment(EQUIP_TYPE_SWORD, EQUIP_VALUE_SWORD_MASTER);
        Interface_LoadItemIcon1(gPlayState, 0);
    }
    j["present"] = boss != nullptr;
    if (boss == nullptr) {
        out = j.dump();
        return out.c_str();
    }
    if (cmd == 1 || cmd == 6) {
        memset(&sToucher, 0, sizeof(sToucher));
        sToucher.toucher.dmgFlags = arg != 0 ? (u32)arg : DMG_SLASH_KOKIRI;
        sToucher.toucher.damage = 1;
        sToucher.toucherFlags = TOUCH_ON | TOUCH_HIT;
        ColliderJntSphElement* el = &boss->unk_424.elements[cmd == 6 ? 15 : 0];
        boss->unk_424.base.acFlags |= AC_HIT;
        boss->unk_424.base.ac = &player->actor;
        el->info.bumperFlags |= BUMP_HIT;
        el->info.acHitInfo = &sToucher;
    } else if (cmd == 2 && !EnemySync::IsSuppressed(&boss->actor)) {
        boss->actor.colChkInfo.health = arg;
    } else if (cmd == 5 && !EnemySync::IsSuppressed(&boss->actor)) {
        boss->csState = arg;
    } else if (cmd == 8 && !EnemySync::IsSuppressed(&boss->actor)) {
        // what the hit path does when his health first drops under 21
        boss->actor.colChkInfo.health = 20;
        BossGanon2_CoopStartDownedCs(&boss->actor, gPlayState);
    } else if (cmd == 9 && !EnemySync::IsSuppressed(&boss->actor)) {
        BossGanon2_CoopStartDefeat(&boss->actor, gPlayState);
    }
    j["phase"] = GN2_GetPhase(&boss->actor);
    j["hp"] = boss->actor.colChkInfo.health;
    j["cs"] = boss->csState;
    j["b4"] = boss->unk_334;
    j["lying"] = BossGanon2_CoopIsLying(&boss->actor) != 0;
    j["sup"] = EnemySync::IsSuppressed(&boss->actor);
    j["dying"] = EnemySync::IsDying(&boss->actor);
    j["pos"] = { boss->actor.world.pos.x, boss->actor.world.pos.y, boss->actor.world.pos.z };
    j["look"] = boss->unk_313;
    j["head"] = { boss->unk_1B8.x, boss->unk_1B8.y, boss->unk_1B8.z };
    j["swing"] = boss->unk_312;
    j["pending"] = { sMirror.pendingDown, sMirror.pendingSword, sMirror.pendingDefeat };
    out = j.dump();
    return out.c_str();
}
}
#endif
