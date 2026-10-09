#include "soh/Network/Anchor/BossAdapters/ActorSyncAdapter.h"
#include "soh/Network/Anchor/EnemySync.h"
#include "soh/Network/Anchor/Anchor.h"
#include "soh/OTRGlobals.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"

extern "C" {
#include "functions.h"
#include "macros.h"
#include "variables.h"
#include "src/overlays/actors/ovl_Boss_Fd/z_boss_fd.h"
#include "src/overlays/actors/ovl_Boss_Fd2/z_boss_fd2.h"

void BossFd_Wait(BossFd* self, PlayState* play);
void BossFd_Fly(BossFd* self, PlayState* play);
void BossFd_MirrorUpdate(BossFd* self, PlayState* play, s32 burrowed, f32 prevY);
void BossFd2_Wait(BossFd2* self, PlayState* play);
void BossFd2_Idle(BossFd2* self, PlayState* play);
void BossFd2_BreatheFire(BossFd2* self, PlayState* play);
void BossFd2_Vulnerable(BossFd2* self, PlayState* play);
void BossFd2_Death(BossFd2* self, PlayState* play);
void BossFd2_SetupDeath(BossFd2* self, PlayState* play);
void BossFd2_StartDeathHandoff(BossFd2* self, PlayState* play);
void BossFd2_MirrorUpdate(BossFd2* self, PlayState* play, s32 burrowed, s32 breathing);
extern PlayState* gPlayState;
}

/**
 * Volvagia (#4050), the Gohma treatment. Two tracked bosses: BOSS_FD, the
 * flying body, and BOSS_FD2, the hole form (a child of Fd). They share one
 * health pool, Fd's colChkInfo.health, which Fd2's collision check spends.
 *
 * Phases come from Fd's fields, for both actors: PREFIGHT while Fd's intro
 * (introState) runs, FIGHT after, DEFEATED when the shared health is 0. The
 * intro runs on every client (it starts when that client's Link reaches the
 * arena door); a client mirrors only once its own intro is over and the host
 * is fighting.
 *
 * Extras (what Update computes that Draw and the room read):
 *  - Fd: the three skeletons' joint tables (head and both arms; the base stream
 *    carries only one), world rotation (the body ring buffer and mane are built
 *    from it), the action state and the move/var timers (arm sway, mane, glow
 *    all derive from them), whether he is burrowed (Fd draws nothing then), the
 *    exposed-face flag, skin segments, body pulse, the hole he surfaces from.
 *    The mirror also runs the rest of his Update every frame
 *    (BossFd_MirrorUpdate): the body and mane ring buffers, arms, embers,
 *    splash, fog, fire breath effects.
 *  - Fd2: burrowed / breathing / hookshot-able flags, the head turn and jaw,
 *    eye state, animation frame, attack-disable flag.
 *  - Timers that an event sets (fire breath, roar, damage flash, splash, rocks)
 *    are streamed as counters; the mirror restarts its own countdown when the
 *    streamed value rises, so a repeated packet does not freeze it.
 *
 * Child actors:
 *  - EN_VB_BALL (falling rocks, and the bones that fall from Draw during the
 *    death) is excluded from tracking. Each machine spawns its own rocks: the
 *    host from the AI, a mirror from the streamed rock timer; a rock hurts only
 *    the Link of the machine it runs on. Tracking them would double-spawn.
 *  - Fire breath is an effect (BossFdEffect), not an actor: each machine makes
 *    its own and burns its own Link. Fd2's breath is made from the streamed
 *    animation frame.
 *  - The blue warp, heart container and clear flag come from the defeat
 *    sequence, which runs locally on every client.
 *
 * Aggro: the EnemyTargeting puppet swap stays off (the intro, the death camera
 * and the room read GET_PLAYER). Instead Fd's chase and Fd2's turn, emerge and
 * fire aim use the nearest living player (Anchor_BossNearestTarget). Fd2's
 * emerge shove (a scripted knockback, not a collider) still only pushes the
 * host's Link; the partner takes the claw, the fire and the body by collider.
 *
 * Hits: the mirror's colliders submit as usual; a hit on the partner's screen
 * goes to the host as HITREQ with its damage flags, and Fd2's own collision
 * check applies it, so the hammer stun and sword damage work from either side.
 *
 * Defeat: FIGHT -> DEFEATED starts Fd2's own death locally and releases both
 * actors to run it (the cutscene, Fd's collapse, blue warp, heart, clear flag).
 * A mirror still in its own intro waits for it to end (two cutscene cameras at
 * once would strand one), then joins the death at the hand-off to Fd's body.
 */

enum VolvagiaPhase : uint8_t {
    VF_PHASE_PREFIGHT = 0,
    VF_PHASE_FIGHT = 1,
    VF_PHASE_DEFEATED = 2,
};

// Event-set timers streamed as counters (see the file comment).
enum VolvagiaRaise : uint8_t {
    VF_RAISE_ROCK,
    VF_RAISE_BREATH,
    VF_RAISE_ROAR,
    VF_RAISE_FLASH,
    VF_RAISE_APPEAR,
    VF_RAISE_SPLASH,
    VF_RAISE_EMBERS,
    VF_RAISE_FD2_FLASH,
    VF_RAISE_COUNT,
};

struct VolvagiaMirror {
    Actor* fd = nullptr;
    Actor* fd2 = nullptr;
    uint32_t fdFrame = 0;
    uint32_t fd2Frame = 0;
    int16_t last[VF_RAISE_COUNT] = {};
    float fdLastY = 0.0f;
    Actor* pendingDefeat = nullptr; // Fd that was defeated remotely during our own intro
};
static VolvagiaMirror sMirror;

static BossFd* VF_ParentFd(Actor* actor) {
    return actor->id == ACTOR_BOSS_FD ? (BossFd*)actor : (BossFd*)actor->parent;
}

static bool VF_LocalIntroRunning(BossFd* fd) {
    // Not gated on health: the streamed pose has already zeroed it by the time the defeat edge is seen.
    return fd != nullptr && fd->introState != BFD_CS_NONE;
}

static BossFd2* VF_FindFd2(BossFd* fd) {
    for (Actor* a = gPlayState->actorCtx.actorLists[ACTORCAT_BOSS].head; a != nullptr; a = a->next) {
        if (a->id == ACTOR_BOSS_FD2 && a->parent == &fd->actor) {
            return (BossFd2*)a;
        }
    }
    return nullptr;
}

static uint8_t VF_GetPhase(Actor* actor) {
    BossFd* fd = VF_ParentFd(actor);
    if (fd == nullptr) {
        return VF_PHASE_PREFIGHT;
    }
    if (fd->actor.colChkInfo.health == 0) {
        return VF_PHASE_DEFEATED;
    }
    return fd->introState == BFD_CS_NONE ? VF_PHASE_FIGHT : VF_PHASE_PREFIGHT;
}

static bool VF_ShouldMirror(Actor* actor, uint8_t streamedPhase) {
    BossFd* fd = VF_ParentFd(actor);
    return fd != nullptr && streamedPhase == VF_PHASE_FIGHT && fd->introState == BFD_CS_NONE &&
           fd->actor.colChkInfo.health > 0;
}

static void VF_PutJoints(nlohmann::json& x, const char* key, const SkelAnime& skel) {
    if (skel.jointTable == nullptr || skel.limbCount <= 0 || skel.limbCount > 40) {
        return;
    }
    std::vector<int> jt;
    jt.reserve((size_t)skel.limbCount * 3);
    for (int i = 0; i < skel.limbCount; i++) {
        jt.push_back(skel.jointTable[i].x);
        jt.push_back(skel.jointTable[i].y);
        jt.push_back(skel.jointTable[i].z);
    }
    x[key] = jt;
}

static void VF_GetJoints(const nlohmann::json& x, const char* key, SkelAnime& skel) {
    if (!x.contains(key) || skel.jointTable == nullptr) {
        return;
    }
    const auto& jt = x[key];
    if ((int)jt.size() != skel.limbCount * 3) {
        return;
    }
    for (int i = 0; i < skel.limbCount; i++) {
        skel.jointTable[i].x = jt[i * 3].get<int16_t>();
        skel.jointTable[i].y = jt[i * 3 + 1].get<int16_t>();
        skel.jointTable[i].z = jt[i * 3 + 2].get<int16_t>();
    }
}

// Restart our own countdown only when the streamed value rises.
static void VF_Raise(VolvagiaRaise slot, int16_t streamed, int16_t* local, bool fresh) {
    if (fresh) {
        *local = streamed;
    } else if (streamed > sMirror.last[slot]) {
        *local = streamed;
    }
    sMirror.last[slot] = streamed;
}

static void VF_FdSerialize(Actor* actor, nlohmann::json& x) {
    BossFd* fd = (BossFd*)actor;
    x["as"] = fd->work[BFD_ACTION_STATE];
    x["mt"] = fd->work[BFD_MOVE_TIMER];
    x["vt"] = fd->work[BFD_VAR_TIMER];
    x["sf"] = fd->work[BFD_STOP_FLAG];
    x["rk"] = fd->work[BFD_ROCK_TIMER];
    x["fb"] = fd->fireBreathTimer;
    x["ro"] = fd->work[BFD_ROAR_TIMER];
    x["df"] = fd->work[BFD_DAMAGE_FLASH_TIMER];
    x["t4"] = fd->timers[4];
    x["sp"] = fd->work[BFD_SPLASH_TIMER];
    x["me"] = fd->work[BFD_MANE_EMBERS_TIMER];
    x["wt"] = fd->actionFunc == BossFd_Wait;
    x["rot"] = { actor->world.rot.x, actor->world.rot.y, actor->world.rot.z };
    x["hole"] = { fd->holePosition.x, fd->holePosition.z };
    x["fe"] = fd->faceExposed;
    x["sk"] = fd->skinSegments;
    x["bp"] = fd->fwork[BFD_BODY_PULSE];
    VF_PutJoints(x, "jh", fd->skelAnimeHead);
    VF_PutJoints(x, "jr", fd->skelAnimeRightArm);
    VF_PutJoints(x, "jl", fd->skelAnimeLeftArm);
    // Not streamed: the intro and death state (they run locally), the effect
    // pool, the rings and the manes (rebuilt each frame by BossFd_MirrorUpdate),
    // bodyFallApart (the death spawns bones from Draw on every client).
}

static void VF_FdDeserialize(Actor* actor, const nlohmann::json& x) {
    BossFd* fd = (BossFd*)actor;
    if (gPlayState == NULL) {
        return;
    }
    bool burrowed = false;
    try {
        uint32_t frame = gPlayState->gameplayFrames;
        bool fresh = sMirror.fd != actor || frame - sMirror.fdFrame > 2;
        if (fresh) {
            sMirror.fd = actor;
            sMirror.fdLastY = actor->world.pos.y;
        }
        sMirror.fdFrame = frame;

        if (x.contains("as")) fd->work[BFD_ACTION_STATE] = x["as"].get<int16_t>();
        if (x.contains("mt")) fd->work[BFD_MOVE_TIMER] = x["mt"].get<int16_t>();
        if (x.contains("vt")) fd->work[BFD_VAR_TIMER] = x["vt"].get<int16_t>();
        if (x.contains("sf")) fd->work[BFD_STOP_FLAG] = x["sf"].get<int16_t>();
        if (x.contains("rk")) VF_Raise(VF_RAISE_ROCK, x["rk"].get<int16_t>(), &fd->work[BFD_ROCK_TIMER], fresh);
        if (x.contains("fb")) VF_Raise(VF_RAISE_BREATH, x["fb"].get<int16_t>(), &fd->fireBreathTimer, fresh);
        if (x.contains("ro")) VF_Raise(VF_RAISE_ROAR, x["ro"].get<int16_t>(), &fd->work[BFD_ROAR_TIMER], fresh);
        if (x.contains("df")) VF_Raise(VF_RAISE_FLASH, x["df"].get<int16_t>(), &fd->work[BFD_DAMAGE_FLASH_TIMER], fresh);
        if (x.contains("t4")) VF_Raise(VF_RAISE_APPEAR, x["t4"].get<int16_t>(), &fd->timers[4], fresh);
        if (x.contains("sp")) VF_Raise(VF_RAISE_SPLASH, x["sp"].get<int16_t>(), &fd->work[BFD_SPLASH_TIMER], fresh);
        if (x.contains("me")) VF_Raise(VF_RAISE_EMBERS, x["me"].get<int16_t>(), &fd->work[BFD_MANE_EMBERS_TIMER], fresh);
        burrowed = x.contains("wt") && x["wt"].get<bool>();
        if (x.contains("rot") && x["rot"].size() == 3) {
            actor->world.rot.x = x["rot"][0].get<int16_t>();
            actor->world.rot.y = x["rot"][1].get<int16_t>();
            actor->world.rot.z = x["rot"][2].get<int16_t>();
        }
        if (x.contains("hole") && x["hole"].size() == 2) {
            fd->holePosition.x = x["hole"][0].get<float>();
            fd->holePosition.z = x["hole"][1].get<float>();
        }
        if (x.contains("fe")) fd->faceExposed = x["fe"].get<uint8_t>();
        if (x.contains("sk")) fd->skinSegments = x["sk"].get<int16_t>();
        if (x.contains("bp")) fd->fwork[BFD_BODY_PULSE] = x["bp"].get<float>();
        VF_GetJoints(x, "jh", fd->skelAnimeHead);
        VF_GetJoints(x, "jr", fd->skelAnimeRightArm);
        VF_GetJoints(x, "jl", fd->skelAnimeLeftArm);
    } catch (const std::exception& ex) {
        SPDLOG_WARN("[VolvagiaSync] Fd extras parse error: {}", ex.what());
        return;
    }
    BossFd_MirrorUpdate(fd, gPlayState, burrowed, sMirror.fdLastY);
    sMirror.fdLastY = actor->world.pos.y;
}

static void VF_Fd2Serialize(Actor* actor, nlohmann::json& x) {
    BossFd2* fd2 = (BossFd2*)actor;
    x["wt"] = fd2->actionFunc == BossFd2_Wait;
    x["bf"] = fd2->actionFunc == BossFd2_BreatheFire;
    x["hk"] = (actor->flags & ACTOR_FLAG_HOOKSHOT_PULLS_PLAYER) != 0;
    x["vt"] = fd2->work[FD2_VAR_TIMER];
    x["df"] = fd2->work[FD2_DAMAGE_FLASH_TIMER];
    x["ey"] = fd2->eyeState;
    x["hr"] = { fd2->headRot.x, fd2->headRot.y };
    x["jw"] = fd2->jawOpening;
    x["da"] = fd2->disableAT;
    x["cf"] = fd2->skelAnime.curFrame;
}

static void VF_Fd2Deserialize(Actor* actor, const nlohmann::json& x) {
    BossFd2* fd2 = (BossFd2*)actor;
    BossFd* fd = VF_ParentFd(actor);
    if (gPlayState == NULL || fd == nullptr) {
        return;
    }
    bool burrowed = false;
    bool breathing = false;
    try {
        uint32_t frame = gPlayState->gameplayFrames;
        bool fresh = sMirror.fd2 != actor || frame - sMirror.fd2Frame > 2;
        if (fresh) {
            sMirror.fd2 = actor;
        }
        sMirror.fd2Frame = frame;

        burrowed = x.contains("wt") && x["wt"].get<bool>();
        breathing = x.contains("bf") && x["bf"].get<bool>();
        if (x.contains("hk")) {
            if (x["hk"].get<bool>()) {
                actor->flags |= ACTOR_FLAG_HOOKSHOT_PULLS_PLAYER;
            } else {
                actor->flags &= ~ACTOR_FLAG_HOOKSHOT_PULLS_PLAYER;
            }
        }
        if (x.contains("vt")) fd2->work[FD2_VAR_TIMER] = x["vt"].get<int16_t>();
        if (x.contains("df")) {
            VF_Raise(VF_RAISE_FD2_FLASH, x["df"].get<int16_t>(), &fd2->work[FD2_DAMAGE_FLASH_TIMER], fresh);
        }
        if (x.contains("ey")) fd2->eyeState = x["ey"].get<uint8_t>();
        if (x.contains("hr") && x["hr"].size() == 2) {
            fd2->headRot.x = x["hr"][0].get<int16_t>();
            fd2->headRot.y = x["hr"][1].get<int16_t>();
        }
        if (x.contains("jw")) fd2->jawOpening = x["jw"].get<float>();
        if (x.contains("da")) fd2->disableAT = x["da"].get<uint8_t>();
        if (x.contains("cf")) fd2->skelAnime.curFrame = x["cf"].get<float>();
    } catch (const std::exception& ex) {
        SPDLOG_WARN("[VolvagiaSync] Fd2 extras parse error: {}", ex.what());
        return;
    }
    // CollisionCheck sets these from the face flag; it does not run on a mirror.
    if (!fd->faceExposed) {
        fd2->collider.elements[0].info.elemType = ELEMTYPE_UNK2;
        fd2->collider.base.colType = COLTYPE_METAL;
    } else {
        fd2->collider.elements[0].info.elemType = ELEMTYPE_UNK3;
        fd2->collider.base.colType = COLTYPE_HIT3;
    }
    BossFd2_MirrorUpdate(fd2, gPlayState, burrowed, breathing);
}

// Starts Fd2's own death here (and so Fd's collapse, the blue warp and the
// heart). Safe to call from either actor's edge: the second call is a no-op.
static void VF_StartDefeat(BossFd* fd) {
    BossFd2* fd2 = VF_FindFd2(fd);
    if (fd2 == nullptr) {
        SPDLOG_WARN("[VolvagiaSync] defeat: no Fd2 on this client, cannot start its death");
        return;
    }
    if (fd2->actionFunc == BossFd2_Death) {
        return;
    }
    fd->actor.colChkInfo.health = 0;
    // Fd waits in his burrow while Fd2 is up; Fd2's death hands over to him.
    fd->actionFunc = BossFd_Wait;
    BossFd2_SetupDeath(fd2, gPlayState);
    fd2->work[FD2_DAMAGE_FLASH_TIMER] = 10;
    fd2->work[FD2_INVINC_TIMER] = 30000;
    Audio_QueueSeqCmd(0x1 << 28 | SEQ_PLAYER_BGM_MAIN << 24 | 0x100FF);
    Audio_PlayActorSound2(&fd2->actor, NA_SE_EN_VALVAISA_DEAD);
    Enemy_StartFinishingBlow(gPlayState, &fd2->actor);
    GameInteractor_ExecuteOnBossDefeat(&fd2->actor);
}

static bool VF_OnPhaseChange(Actor* actor, uint8_t fromPhase, uint8_t toPhase) {
    BossFd* fd = VF_ParentFd(actor);
    ESYNC_LOG("[VolvagiaSync] phase {}->{} (id {})", fromPhase, toPhase, actor->id);
    if (toPhase != VF_PHASE_DEFEATED || fd == nullptr) {
        return false;
    }
    if (VF_LocalIntroRunning(fd)) {
        // Our own intro owns the camera: join the death when it ends.
        sMirror.pendingDefeat = &fd->actor;
        ESYNC_LOG("[VolvagiaSync] defeat deferred until the local intro ends");
        return true;
    }
    if (fromPhase == VF_PHASE_FIGHT) {
        VF_StartDefeat(fd);
        ESYNC_LOG("[VolvagiaSync] defeat handoff (Fd2 death started locally)");
        return true;
    }
    return false;
}

static void VF_OnRemoteDefeat(Actor* actor) {
    BossFd* fd = VF_ParentFd(actor);
    if (fd == nullptr || gPlayState == NULL) {
        return;
    }
    BossFd2* fd2 = VF_FindFd2(fd);
    if (fd2 == nullptr || fd2->actionFunc == BossFd2_Death) {
        return; // the defeat is already running here
    }
    if (VF_LocalIntroRunning(fd)) {
        sMirror.pendingDefeat = &fd->actor;
        ESYNC_LOG("[VolvagiaSync] remote defeat deferred until the local intro ends");
        return;
    }
    VF_StartDefeat(fd);
    ESYNC_LOG("[VolvagiaSync] remote defeat (missed phase edge, Fd2 death started locally)");
}

static void VF_OnLocalResume(Actor* actor) {
    ESYNC_LOG("[VolvagiaSync] local AI resumes (id {})", actor->id);
}

void RegisterVolvagiaAdapter() {
    ActorSyncAdapter fd;
    fd.SerializeExtras = VF_FdSerialize;
    fd.DeserializeExtras = VF_FdDeserialize;
    fd.GetPhase = VF_GetPhase;
    fd.OnPhaseChange = VF_OnPhaseChange;
    fd.ShouldMirror = VF_ShouldMirror;
    fd.OnRemoteDefeat = VF_OnRemoteDefeat;
    fd.OnLocalResume = VF_OnLocalResume;
    EnemySync::RegisterAdapter(ACTOR_BOSS_FD, fd);

    ActorSyncAdapter fd2 = fd;
    fd2.SerializeExtras = VF_Fd2Serialize;
    fd2.DeserializeExtras = VF_Fd2Deserialize;
    EnemySync::RegisterAdapter(ACTOR_BOSS_FD2, fd2);
}

// z_boss_fd.c, Init and Destroy: drop every per-fight flag so an aborted fight
// (room left, host change, disconnect) can't kill the next Volvagia the moment
// its intro ends, even if the new actor reuses the old address.
extern "C" void Anchor_VolvagiaReset(Actor* fdActor) {
    (void)fdActor;
    sMirror = VolvagiaMirror();
}

// z_boss_fd.c, at the end of Fd's intro: the partner won while our intro played.
extern "C" void Anchor_VolvagiaIntroOver(Actor* fdActor) {
    if (sMirror.pendingDefeat != fdActor || gPlayState == NULL) {
        return;
    }
    sMirror.pendingDefeat = nullptr;
    BossFd* fd = (BossFd*)fdActor;
    BossFd2* fd2 = VF_FindFd2(fd);
    fd->actor.colChkInfo.health = 0;
    if (fd2 != nullptr) {
        BossFd2_StartDeathHandoff(fd2, gPlayState);
    } else {
        SPDLOG_WARN("[VolvagiaSync] deferred defeat: no Fd2 on this client, health zeroed only");
    }
    ESYNC_LOG("[VolvagiaSync] local intro over: joining the deferred defeat at the hand-off");
}

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
extern "C" {
// #4050 boss rig. Reports Volvagia's sync state on this client:
// cmd 0 report only; 1 land a hammer hit on Fd2's face collider the way the
// collision check would (host); 2 land a sword hit (host); 3 set the shared
// health to arg (host); 4 un-clear this room (so he is back on the next visit);
// 5 teleport Link to (arg, 100, 0) so the intro and the fight can be reached.
EMSCRIPTEN_KEEPALIVE
const char* anchor_test_vf(int cmd, int arg) {
    static std::string out;
    static ColliderInfo sToucher;
    nlohmann::json j;
    if (gPlayState == NULL) {
        return "{}";
    }
    BossFd* fd = nullptr;
    BossFd2* fd2 = nullptr;
    for (Actor* a = gPlayState->actorCtx.actorLists[ACTORCAT_BOSS].head; a != nullptr; a = a->next) {
        if (a->id == ACTOR_BOSS_FD && a->update != NULL) {
            fd = (BossFd*)a;
        } else if (a->id == ACTOR_BOSS_FD2 && a->update != NULL) {
            fd2 = (BossFd2*)a;
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
    int rocks = 0, hearts = 0, warps = 0;
    for (int cat = 0; cat < ACTORCAT_MAX; cat++) {
        for (Actor* a = gPlayState->actorCtx.actorLists[cat].head; a != nullptr; a = a->next) {
            rocks += a->id == ACTOR_EN_VB_BALL && a->update != NULL;
            hearts += a->id == ACTOR_ITEM_B_HEART && a->update != NULL;
            warps += a->id == ACTOR_DOOR_WARP1 && a->update != NULL;
        }
    }
    j["rocks"] = rocks;
    j["hearts"] = hearts;
    j["warps"] = warps;
    j["clear"] = Flags_GetClear(gPlayState, gPlayState->roomCtx.curRoom.num);
    if (cmd == 4) {
        Flags_UnsetClear(gPlayState, gPlayState->roomCtx.curRoom.num);
    } else if (cmd == 5) {
        player->actor.world.pos.x = (float)arg;
        player->actor.world.pos.y = 100.0f;
        player->actor.world.pos.z = 0.0f;
    }
    j["present"] = fd != nullptr && fd2 != nullptr;
    if (fd == nullptr || fd2 == nullptr) {
        out = j.dump();
        return out.c_str();
    }
    if (cmd == 1 || cmd == 2) {
        memset(&sToucher, 0, sizeof(sToucher));
        sToucher.toucher.dmgFlags = cmd == 1 ? 0x40000040 : DMG_SLASH_KOKIRI;
        sToucher.toucher.damage = 1;
        sToucher.toucherFlags = TOUCH_ON | TOUCH_HIT;
        fd2->collider.base.acFlags |= AC_HIT;
        fd2->collider.base.ac = &player->actor;
        fd2->collider.elements[0].info.bumperFlags |= BUMP_HIT;
        fd2->collider.elements[0].info.acHitInfo = &sToucher;
        fd2->actor.colChkInfo.damage = 1;
    } else if (cmd == 3 && !EnemySync::IsSuppressed(&fd->actor)) {
        fd->actor.colChkInfo.health = arg;
    }
    j["phase"] = VF_GetPhase(&fd->actor);
    j["hp"] = fd->actor.colChkInfo.health;
    j["intro"] = fd->introState;
    j["fdState"] = fd->work[BFD_ACTION_STATE];
    j["fdWait"] = fd->actionFunc == BossFd_Wait;
    j["fd2Wait"] = fd2->actionFunc == BossFd2_Wait;
    j["fd2Act"] = fd2->actionFunc == BossFd2_Idle         ? "idle"
                  : fd2->actionFunc == BossFd2_BreatheFire ? "fire"
                  : fd2->actionFunc == BossFd2_Vulnerable  ? "stun"
                  : fd2->actionFunc == BossFd2_Death       ? "death"
                                                           : "other";
    j["fd2Death"] = fd2->deathState;
    j["face"] = fd->faceExposed;
    j["supFd"] = EnemySync::IsSuppressed(&fd->actor);
    j["supFd2"] = EnemySync::IsSuppressed(&fd2->actor);
    j["dyingFd"] = EnemySync::IsDying(&fd->actor);
    j["fdPos"] = { fd->actor.world.pos.x, fd->actor.world.pos.y, fd->actor.world.pos.z };
    j["fd2Pos"] = { fd2->actor.world.pos.x, fd2->actor.world.pos.y, fd2->actor.world.pos.z };
    j["holeIdx"] = fd->holeIndex;
    j["rockT"] = fd->work[BFD_ROCK_TIMER];
    j["fireT"] = fd->fireBreathTimer;
    j["handoff"] = fd->handoffSignal;
    j["pending"] = sMirror.pendingDefeat != nullptr;
    out = j.dump();
    return out.c_str();
}
}
#endif
