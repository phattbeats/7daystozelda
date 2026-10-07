#include "soh/Network/Anchor/BossAdapters/ActorSyncAdapter.h"
#include "soh/Network/Anchor/EnemySync.h"
#include "soh/Network/Anchor/Anchor.h"
// Pull the C++ side of global.h in under proper linkage before the extern "C"
// overlay header includes it (BossRush.cpp pattern).
#include "soh/OTRGlobals.h"

extern "C" {
#include "functions.h"
#include "macros.h"
#include "variables.h"
#include "src/overlays/actors/ovl_Boss_Dodongo/z_boss_dodongo.h"

void BossDodongo_SetupDeathCutscene(BossDodongo* thisx);
void BossDodongo_SetupWalk(BossDodongo* thisx);
void BossDodongo_SetupExplode(BossDodongo* thisx);
void BossDodongo_SetupInhale(BossDodongo* thisx);
void BossDodongo_IntroCutscene(BossDodongo* thisx, PlayState* play);
void BossDodongo_Walk(BossDodongo* thisx, PlayState* play);
void BossDodongo_Inhale(BossDodongo* thisx, PlayState* play);
void BossDodongo_BlowFire(BossDodongo* thisx, PlayState* play);
void BossDodongo_Roll(BossDodongo* thisx, PlayState* play);
void BossDodongo_Explode(BossDodongo* thisx, PlayState* play);
void BossDodongo_LayDown(BossDodongo* thisx, PlayState* play);
void BossDodongo_Vulnerable(BossDodongo* thisx, PlayState* play);
void BossDodongo_GetUp(BossDodongo* thisx, PlayState* play);
void BossDodongo_Damaged(BossDodongo* thisx, PlayState* play);
void BossDodongo_DeathCutscene(BossDodongo* thisx, PlayState* play);
s32 BossDodongo_AteExplosive(BossDodongo* thisx, PlayState* play);
void BossDodongo_SpawnFire(BossDodongo* thisx, PlayState* play, s16 params);
void BossDodongo_UpdateAmbience(BossDodongo* thisx, PlayState* play, s32 isMirror);
extern PlayState* gPlayState;
}

/**
 * King Dodongo (PHA-4047), the Gohma treatment.
 *
 * Phases come from fields: health (his own s16, not colChkInfo) and unk_1BC,
 * which is non-zero while a cutscene owns him. The intro runs on every client
 * (it starts when that client's Link drops into the pit), and mirroring starts
 * once the local intro is over and the host is fighting.
 *
 * Extras: everything Draw and the room read that Update computes: his health,
 * the roll spin and tilt, the belly and squash scales, the fog colour that
 * turns him red, the hit flash, the shape offset that lowers him into a ball,
 * the fire-breath glow and screen tint, which collider spheres are shrunk
 * (inhaling, rolling), the inhale and flame counters, the corner he is heading
 * for and his direction. The mirror also runs the room itself every frame
 * (BossDodongo_UpdateAmbience: lava waves and bubbles, fire light, wobble).
 *
 * Child actors:
 *  - EN_BDFIRE (fire breath) is excluded from tracking. Every machine spawns
 *    its own flames: the host from the AI, a mirror from the streamed flame
 *    count. A flame burns only the Link of the machine it runs on, so each
 *    player is burned by what they see.
 *  - Bombs aren't synced, so the swallow is reported: a mirror checks its own
 *    bombs against his mouth during the inhale window and sends
 *    KD_EVENT_SWALLOW; the host starts the explosion if he is still inhaling.
 *  - BG_BREAKWALL, the heart container, the blue warp and the lava cooling
 *    come from the defeat sequence, which runs locally on every client.
 *
 * Aggro: the EnemyTargeting puppet swap stays off. Update moves the local Link
 * and the camera in the intro and the death, and the lava and fire light read
 * GET_PLAYER, so swapping it for a whole update is unsafe. He never aims at a
 * position either: BossDodongo_UpdateAim feeds his attack choices (breathe
 * fire, roll, turn back) every living player instead (Anchor_BossAimTargets).
 * Rolling hits whichever Link is local through the re-submitted AT spheres.
 *
 * Defeat: FIGHT -> DEFEATED calls his own SetupDeathCutscene locally. A mirror
 * still in its own intro waits for it to end (two cutscene cameras at once
 * would strand one), then dies at once.
 */

enum KingDodongoPhase : uint8_t {
    KD_PHASE_PREFIGHT = 0,
    KD_PHASE_FIGHT = 1,
    KD_PHASE_DEFEATED = 2,
};

enum KingDodongoEvent : uint8_t {
    KD_EVENT_SWALLOW = 1,
};

// Mirror bookkeeping. One King Dodongo exists at a time.
struct KingDodongoMirror {
    Actor* actor = nullptr;
    uint32_t frame = 0;
    int16_t flames = 0;     // flames of the current breath spawned here
    uint8_t inhale = 0;     // last streamed unk_1E2
    bool swallowSent = false;
    Actor* pendingDefeat = nullptr; // defeated remotely during our own intro
};
static KingDodongoMirror sMirror;

static bool KD_LocalIntroRunning(BossDodongo* kd) {
    return kd->health > 0 && kd->unk_1BC != 0;
}

static uint8_t KD_GetPhase(Actor* actor) {
    BossDodongo* kd = (BossDodongo*)actor;
    if (kd->health <= 0) {
        return KD_PHASE_DEFEATED;
    }
    return kd->unk_1BC == 0 ? KD_PHASE_FIGHT : KD_PHASE_PREFIGHT;
}

static void KD_SerializeExtras(Actor* actor, nlohmann::json& x) {
    BossDodongo* kd = (BossDodongo*)actor;
    x["hp"] = kd->health;
    x["cn"] = kd->unk_1A0;
    x["dr"] = kd->unk_1A2;
    x["ih"] = kd->unk_1E2;
    x["ic"] = kd->unk_1AC;
    x["fl"] = kd->unk_1AE;
    x["sp"] = kd->unk_1C4;
    x["tl"] = kd->unk_23C;
    x["bs"] = kd->unk_1F8;
    x["sq"] = kd->unk_208;
    x["yo"] = actor->shape.yOffset;
    x["hf"] = kd->unk_1C0;
    x["fb"] = kd->unk_244;
    x["lc"] = kd->unk_1C8;
    x["lt"] = kd->unk_240;
    x["cf"] = { kd->colorFilterR, kd->colorFilterG, kd->colorFilterB, kd->colorFilterMin, kd->colorFilterMax };
    // Sphere 0 shrinks while he inhales, spheres 6-18 (but 12) while he rolls.
    x["s0"] = kd->collider.elements[0].dim.scale == 0.0f;
    x["rl"] = kd->collider.elements[6].dim.scale == 0.0f;
    // Not streamed: csState/unk_1DA/unk_1C6 (cutscene timers: the intro and the
    // defeat run locally) and the lava/bubble state (UpdateAmbience runs here).
}

// This machine's own flames for the host's breath (see the file comment).
static void KD_MirrorFire(BossDodongo* kd, uint8_t inhale, int16_t flames) {
    if (inhale && !sMirror.inhale) {
        BossDodongo_SpawnFire(kd, gPlayState, -1); // the glow in his mouth, as SetupInhale's caller does
    }
    if (flames < sMirror.flames) {
        sMirror.flames = 0; // a new breath (SetupBlowFire restarts the count)
    }
    // One flame per frame of the breath; catch up after a dropped packet.
    while (sMirror.flames < flames && sMirror.flames < 40) {
        BossDodongo_SpawnFire(kd, gPlayState, sMirror.flames);
        sMirror.flames++;
    }
}

static void KD_DeserializeExtras(Actor* actor, const nlohmann::json& x) {
    BossDodongo* kd = (BossDodongo*)actor;
    if (gPlayState == NULL) {
        return;
    }
    uint8_t inhale = 0;
    int16_t flames = 0;
    int16_t inhaleCount = 0;
    try {
        if (x.contains("hp")) kd->health = x["hp"].get<int16_t>();
        if (x.contains("cn")) kd->unk_1A0 = x["cn"].get<int16_t>();
        if (x.contains("dr")) kd->unk_1A2 = x["dr"].get<int16_t>();
        if (x.contains("ih")) inhale = x["ih"].get<uint8_t>();
        if (x.contains("ic")) inhaleCount = x["ic"].get<int16_t>();
        if (x.contains("fl")) flames = x["fl"].get<int16_t>();
        if (x.contains("sp")) kd->unk_1C4 = x["sp"].get<int16_t>();
        if (x.contains("tl")) kd->unk_23C = x["tl"].get<float>();
        if (x.contains("bs")) kd->unk_1F8 = x["bs"].get<float>();
        if (x.contains("sq")) kd->unk_208 = x["sq"].get<float>();
        if (x.contains("yo")) actor->shape.yOffset = x["yo"].get<float>();
        if (x.contains("hf")) kd->unk_1C0 = x["hf"].get<int16_t>();
        if (x.contains("fb")) kd->unk_244 = x["fb"].get<float>();
        if (x.contains("lc")) kd->unk_1C8 = x["lc"].get<int16_t>();
        if (x.contains("lt")) kd->unk_240 = x["lt"].get<float>();
        if (x.contains("cf") && x["cf"].size() == 5) {
            kd->colorFilterR = x["cf"][0].get<float>();
            kd->colorFilterG = x["cf"][1].get<float>();
            kd->colorFilterB = x["cf"][2].get<float>();
            kd->colorFilterMin = x["cf"][3].get<float>();
            kd->colorFilterMax = x["cf"][4].get<float>();
        }
        bool shrink0 = x.contains("s0") && x["s0"].get<bool>();
        bool rolling = x.contains("rl") && x["rl"].get<bool>();
        kd->collider.elements[0].dim.scale = shrink0 ? 0.0f : 1.0f;
        for (int i = 6; i < 19; i++) {
            if (i != 12) {
                kd->collider.elements[i].dim.scale = rolling ? 0.0f : 1.0f;
            }
        }
    } catch (const std::exception& ex) {
        SPDLOG_WARN("[KingDodongoSync] extras parse error: {}", ex.what());
        return;
    }
    kd->unk_1E2 = inhale; // the mouth glow fades when this drops
    // If the local AI takes over, he walks upright (OnLocalResume).
    kd->unk_228 = 9200.0f;

    // A fresh start (first mirrored frame, or back after a gap): take the
    // counters as they are, so an old breath doesn't burst out of him now.
    uint32_t frame = gPlayState->gameplayFrames;
    if (sMirror.actor != actor || frame - sMirror.frame > 2) {
        sMirror.actor = actor;
        sMirror.flames = flames;
        sMirror.inhale = inhale;
        sMirror.swallowSent = false;
    }
    sMirror.frame = frame;

    KD_MirrorFire(kd, inhale, flames);
    sMirror.inhale = inhale;

    // Our own bomb in his mouth while he inhales (vanilla window: 20 < count < 82).
    if (!inhale) {
        sMirror.swallowSent = false;
    } else if (!sMirror.swallowSent && inhaleCount > 20 && inhaleCount < 82 &&
               BossDodongo_AteExplosive(kd, gPlayState)) {
        sMirror.swallowSent = true;
        Audio_PlayActorSound2(actor, NA_SE_EN_DODO_K_DRINK);
        EnemySync::SendAdapterEvent(actor, KD_EVENT_SWALLOW);
        ESYNC_LOG("[KingDodongoSync] swallowed our bomb (inhale {}), told the host", inhaleCount);
    }

    kd->unk_19E++;
    BossDodongo_UpdateAmbience(kd, gPlayState, true);
}

static void KD_StartDefeat(BossDodongo* kd) {
    if (kd->health > 0) {
        kd->health = 0;
    }
    BossDodongo_SetupDeathCutscene(kd);
    Enemy_StartFinishingBlow(gPlayState, &kd->actor);
}

static bool KD_OnPhaseChange(Actor* actor, uint8_t fromPhase, uint8_t toPhase) {
    BossDodongo* kd = (BossDodongo*)actor;
    ESYNC_LOG("[KingDodongoSync] phase {}->{}", fromPhase, toPhase);
    if (toPhase != KD_PHASE_DEFEATED) {
        return false;
    }
    if (KD_LocalIntroRunning(kd)) {
        // Our own intro owns the camera: die when it ends.
        sMirror.pendingDefeat = actor;
        ESYNC_LOG("[KingDodongoSync] defeat deferred until the local intro ends");
        return true;
    }
    if (fromPhase == KD_PHASE_FIGHT) {
        KD_StartDefeat(kd);
        ESYNC_LOG("[KingDodongoSync] defeat handoff (SetupDeathCutscene called locally)");
        return true;
    }
    return false;
}

static void KD_OnRemoteDefeat(Actor* actor) {
    BossDodongo* kd = (BossDodongo*)actor;
    if (kd->health <= 0) {
        return; // the defeat is already running here
    }
    if (KD_LocalIntroRunning(kd)) {
        sMirror.pendingDefeat = actor;
        ESYNC_LOG("[KingDodongoSync] remote defeat deferred until the local intro ends");
        return;
    }
    KD_StartDefeat(kd);
    ESYNC_LOG("[KingDodongoSync] remote defeat (missed phase edge, SetupDeathCutscene called locally)");
}

static bool KD_ShouldMirror(Actor* actor, uint8_t streamedPhase) {
    BossDodongo* kd = (BossDodongo*)actor;
    return streamedPhase == KD_PHASE_FIGHT && kd->unk_1BC == 0 && kd->health > 0;
}

static void KD_OnLocalResume(Actor* actor) {
    BossDodongo* kd = (BossDodongo*)actor;
    if (kd->health <= 0 || kd->unk_1BC != 0) {
        return;
    }
    // His actionFunc is still the walk the intro ended in (or whatever the last
    // local stretch left), so restart the walk from the streamed pose.
    BossDodongo_SetupWalk(kd);
    kd->unk_1E2 = 0;
    ESYNC_LOG("[KingDodongoSync] local AI resumes (walk)");
}

static void KD_OnRemoteEvent(Actor* actor, uint8_t event) {
    BossDodongo* kd = (BossDodongo*)actor;
    if (event != KD_EVENT_SWALLOW) {
        return;
    }
    // The partner's bomb went down his throat on their screen. Take it if he
    // is still inhaling here (the window may have closed in transit).
    if (kd->actionFunc == BossDodongo_Inhale && kd->unk_1AC > 20 && kd->health > 0) {
        Audio_PlayActorSound2(actor, NA_SE_EN_DODO_K_DRINK);
        BossDodongo_SetupExplode(kd);
        ESYNC_LOG("[KingDodongoSync] partner's bomb swallowed (inhale {})", kd->unk_1AC);
    } else {
        ESYNC_LOG("[KingDodongoSync] partner's bomb arrived after the inhale (inhale {})", kd->unk_1AC);
    }
}

void RegisterKingDodongoAdapter() {
    ActorSyncAdapter adapter;
    adapter.SerializeExtras = KD_SerializeExtras;
    adapter.DeserializeExtras = KD_DeserializeExtras;
    adapter.GetPhase = KD_GetPhase;
    adapter.OnPhaseChange = KD_OnPhaseChange;
    adapter.ShouldMirror = KD_ShouldMirror;
    adapter.OnRemoteDefeat = KD_OnRemoteDefeat;
    adapter.OnLocalResume = KD_OnLocalResume;
    adapter.OnRemoteEvent = KD_OnRemoteEvent;
    EnemySync::RegisterAdapter(ACTOR_BOSS_DODONGO, adapter);
}

// z_boss_dodongo.c, at the end of the intro: the partner won while our intro played.
extern "C" s32 Anchor_KingDodongoDefeatPending(Actor* actor) {
    if (sMirror.pendingDefeat != actor) {
        return false;
    }
    sMirror.pendingDefeat = nullptr;
    ESYNC_LOG("[KingDodongoSync] local intro over: starting the deferred defeat");
    return true;
}

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
extern "C" {
// PHA-4047 boss rig. Reports King Dodongo's sync state on this client:
// cmd 0 report only; 1 spawn a lit bomb in his mouth (what a thrown bomb does
// while he inhales); 2 land a Kokiri Sword hit on his head sphere the way the
// collision check would; 3 make the host's copy inhale now; 4 set his health
// to arg (host); 5 un-clear this room (so he is back on the next visit).
// Every call returns the report.
EMSCRIPTEN_KEEPALIVE
const char* anchor_test_kd(int cmd, int arg) {
    static std::string out;
    static ColliderInfo sToucher;
    nlohmann::json j;
    if (gPlayState == NULL) {
        return "{}";
    }
    BossDodongo* kd = nullptr;
    for (Actor* a = gPlayState->actorCtx.actorLists[ACTORCAT_BOSS].head; a != nullptr; a = a->next) {
        if (a->id == ACTOR_BOSS_DODONGO && a->update != NULL) {
            kd = (BossDodongo*)a;
            break;
        }
    }
    Player* player = GET_PLAYER(gPlayState);
    j["scene"] = gPlayState->sceneNum;
    j["auth"] = EnemySync::CurrentAuthorityId();
    j["own"] = Anchor::Instance != nullptr ? Anchor::Instance->ownClientId : 0;
    j["link"] = { player->actor.world.pos.x, player->actor.world.pos.y, player->actor.world.pos.z };
    j["health"] = gSaveContext.health;
    j["burning"] = player->bodyIsBurning;
    j["iframes"] = player->invincibilityTimer;
    j["csAction"] = player->csAction;
    int flames = 0, explosives = 0;
    for (Actor* a = gPlayState->actorCtx.actorLists[ACTORCAT_ENEMY].head; a != nullptr; a = a->next) {
        flames += a->id == ACTOR_EN_BDFIRE && a->update != NULL;
    }
    for (Actor* a = gPlayState->actorCtx.actorLists[ACTORCAT_EXPLOSIVE].head; a != nullptr; a = a->next) {
        explosives++;
    }
    j["flames"] = flames;
    j["explosives"] = explosives;
    int hearts = 0, warps = 0;
    for (int cat = 0; cat < ACTORCAT_MAX; cat++) {
        for (Actor* a = gPlayState->actorCtx.actorLists[cat].head; a != nullptr; a = a->next) {
            hearts += a->id == ACTOR_ITEM_B_HEART && a->update != NULL;
            warps += a->id == ACTOR_DOOR_WARP1 && a->update != NULL;
        }
    }
    j["hearts"] = hearts;
    j["warps"] = warps;
    j["clear"] = Flags_GetClear(gPlayState, gPlayState->roomCtx.curRoom.num);
    if (cmd == 5) {
        Flags_UnsetClear(gPlayState, gPlayState->roomCtx.curRoom.num);
    }
    j["present"] = kd != nullptr;
    if (kd == nullptr) {
        out = j.dump();
        return out.c_str();
    }
    Actor* actor = &kd->actor;
    if (cmd == 1) {
        Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_EN_BOM, kd->mouthPos.x, kd->mouthPos.y, kd->mouthPos.z,
                    0, 0, 0, 0, false);
    } else if (cmd == 2) {
        memset(&sToucher, 0, sizeof(sToucher));
        sToucher.toucher.dmgFlags = DMG_SLASH_KOKIRI;
        sToucher.toucher.damage = 1;
        sToucher.toucherFlags = TOUCH_ON | TOUCH_HIT;
        kd->collider.base.acFlags |= AC_HIT;
        kd->collider.base.ac = &player->actor;
        kd->collider.elements[0].info.bumperFlags |= BUMP_HIT;
        kd->collider.elements[0].info.acHitInfo = &sToucher;
        kd->collider.elements[0].info.bumper.hitPos = { (s16)actor->focus.pos.x, (s16)actor->focus.pos.y,
                                                        (s16)actor->focus.pos.z };
        actor->colChkInfo.damage = 1;
    } else if (cmd == 3 && !EnemySync::IsSuppressed(actor) && kd->health > 0 && kd->unk_1BC == 0) {
        BossDodongo_SetupInhale(kd);
        BossDodongo_SpawnFire(kd, gPlayState, -1);
    } else if (cmd == 4 && !EnemySync::IsSuppressed(actor)) {
        kd->health = arg;
    }
    // Labels for the rig's logs only (the sync itself never reads actionFunc).
    static const std::pair<BossDodongoActionFunc, const char*> sActs[] = {
        { BossDodongo_IntroCutscene, "intro" }, { BossDodongo_Walk, "walk" },
        { BossDodongo_Inhale, "inhale" },       { BossDodongo_BlowFire, "fire" },
        { BossDodongo_Roll, "roll" },           { BossDodongo_Explode, "explode" },
        { BossDodongo_LayDown, "laydown" },     { BossDodongo_Vulnerable, "vulnerable" },
        { BossDodongo_GetUp, "getup" },         { BossDodongo_Damaged, "damaged" },
        { BossDodongo_DeathCutscene, "death" },
    };
    const char* act = "?";
    for (auto& [fn, name] : sActs) {
        if (kd->actionFunc == fn) {
            act = name;
        }
    }
    j["act"] = act;
    j["phase"] = KD_GetPhase(actor);
    j["hp"] = kd->health;
    j["cs"] = kd->csState;
    j["bc"] = kd->unk_1BC;
    j["sup"] = EnemySync::IsSuppressed(actor);
    j["dying"] = EnemySync::IsDying(actor);
    j["key"] = std::to_string(EnemySync::KeyForActor(actor));
    j["pos"] = { actor->world.pos.x, actor->world.pos.y, actor->world.pos.z };
    j["rotY"] = actor->shape.rot.y;
    j["yo"] = actor->shape.yOffset;
    j["spin"] = kd->unk_1C4;
    j["ih"] = kd->unk_1E2;
    j["ic"] = kd->unk_1AC;
    j["fl"] = kd->unk_1AE;
    j["corner"] = kd->unk_1A0;
    j["dir"] = kd->unk_1A2;
    j["t1DA"] = kd->unk_1DA;
    j["roll"] = kd->collider.elements[6].dim.scale == 0.0f;
    j["fogR"] = kd->colorFilterR;
    j["mouth"] = { kd->mouthPos.x, kd->mouthPos.y, kd->mouthPos.z };
    j["pending"] = sMirror.pendingDefeat == actor;
    out = j.dump();
    return out.c_str();
}
}
#endif
