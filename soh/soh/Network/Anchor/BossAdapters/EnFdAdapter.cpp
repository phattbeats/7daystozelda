#include "soh/Network/Anchor/BossAdapters/ActorSyncAdapter.h"
#include "soh/Network/Anchor/EnemySync.h"
// Pull the C++ side of global.h in under proper linkage before the extern "C"
// overlay header includes it (BossRush.cpp pattern).
#include "soh/OTRGlobals.h"

extern "C" {
#include "functions.h"
#include "macros.h"
#include "variables.h"
#include "src/overlays/actors/ovl_En_Fd/z_en_fd.h"
#include "src/overlays/actors/ovl_En_Fw/z_en_fw.h"
#include "src/overlays/actors/ovl_En_Fd_Fire/z_en_fd_fire.h"
extern PlayState* gPlayState;
}

/**
 * Flare Dancer (#4055): EN_FD (the dancer), EN_FW (its core) and EN_FD_FIRE (the
 * fire ring). The dancer spawns the core (a child) when it is hit, and the core
 * spawns nothing; the fire ring is spawned by the dancer eight at a time. All three
 * spawn from Update, so they replicate through the generic dynamic-spawn path (with
 * their parent resolved), and the mirror never spawns its own.
 *
 * EN_FD: its body is not drawn while the action is EnFd_Reappear (the Init state a
 * suppressed mirror never leaves), its flame particles are aged by Update, and the
 * joint spheres are placed by its own PostLimbDraw. Extras carry the action and
 * animation, fade, invincibility and the timers, and the mirror runs the particle
 * tick. A mirror's hookshot is reported to the host (the host's copy never feels it).
 * Death: the last core's explosion makes the host announce the defeat; the mirror starts
 * the dancer's removal count-down without another core.
 *
 * EN_FW (core): action, animation and the run, slide and explosion timers. A mirror
 * whose core ends (explosion, or the hop home) runs the explosion locally (own blast and
 * drop) or just ends.
 *
 * EN_FD_FIRE: the shrink-away scale and the velocity the flame stretches by.
 */

static int Fd_Num(const nlohmann::json& x, const char* k, int dflt) {
    auto it = x.find(k);
    return (it != x.end() && it->is_number()) ? it->get<int>() : dflt;
}

static float Fd_Flt(const nlohmann::json& x, const char* k, float dflt) {
    auto it = x.find(k);
    return (it != x.end() && it->is_number()) ? it->get<float>() : dflt;
}

static constexpr uint8_t FD_EVENT_HOOKSHOT = 1;

static void EnFd_SerializeExtras(Actor* actor, nlohmann::json& x) {
    EnFd* d = (EnFd*)actor;
    x["af"] = EnFd_MirrorGetAction(d);
    x["an"] = EnFd_MirrorGetAnim(d);
    x["cf"] = d->skelAnime.curFrame;
    x["ps"] = d->skelAnime.playSpeed;
    x["fa"] = d->fadeAlpha;
    x["sa"] = actor->shape.shadowAlpha;
    x["iv"] = d->invincibilityTimer;
    x["at"] = d->attackTimer;
    x["sp"] = d->spinTimer;
    x["cc"] = d->circlesToComplete;
    x["rd"] = d->runDir;
    x["rr"] = d->runRadius;
    x["ca"] = d->coreActive;
    x["pa"] = actor->params;
    x["att"] = (actor->flags & ACTOR_FLAG_ATTENTION_ENABLED) != 0;
    x["wry"] = actor->world.rot.y;
    x["spd"] = actor->speedXZ;
    x["vy"] = actor->velocity.y;
    x["fh"] = actor->floorHeight;
}

static void EnFd_DeserializeExtras(Actor* actor, const nlohmann::json& x) {
    EnFd* d = (EnFd*)actor;
    if (gPlayState == NULL) {
        return;
    }
    try {
        EnFd_MirrorApplyAction(d, Fd_Num(x, "af", EnFd_MirrorGetAction(d)));
        EnFd_MirrorApplyAnim(d, Fd_Num(x, "an", EnFd_MirrorGetAnim(d)));
        d->skelAnime.curFrame = Fd_Flt(x, "cf", d->skelAnime.curFrame);
        d->skelAnime.playSpeed = Fd_Flt(x, "ps", d->skelAnime.playSpeed);
        d->fadeAlpha = Fd_Flt(x, "fa", d->fadeAlpha);
        actor->shape.shadowAlpha = (u8)Fd_Num(x, "sa", actor->shape.shadowAlpha);
        d->invincibilityTimer = (s16)Fd_Num(x, "iv", d->invincibilityTimer);
        d->attackTimer = (s16)Fd_Num(x, "at", d->attackTimer);
        d->spinTimer = (s16)Fd_Num(x, "sp", d->spinTimer);
        d->circlesToComplete = (s16)Fd_Num(x, "cc", d->circlesToComplete);
        d->runDir = (s16)Fd_Num(x, "rd", d->runDir);
        d->runRadius = Fd_Flt(x, "rr", d->runRadius);
        d->coreActive = (u8)Fd_Num(x, "ca", d->coreActive);
        actor->params = (s16)Fd_Num(x, "pa", actor->params);
        auto att = x.find("att");
        if (att != x.end() && att->is_boolean()) {
            if (att->get<bool>()) {
                actor->flags |= ACTOR_FLAG_ATTENTION_ENABLED;
            } else {
                actor->flags &= ~ACTOR_FLAG_ATTENTION_ENABLED;
            }
        }
        actor->world.rot.y = (s16)Fd_Num(x, "wry", actor->world.rot.y);
        actor->speedXZ = Fd_Flt(x, "spd", actor->speedXZ);
        actor->velocity.y = Fd_Flt(x, "vy", actor->velocity.y);
        actor->floorHeight = Fd_Flt(x, "fh", actor->floorHeight);

        // A mirror's hookshot caught the dancer: tell the host, and let go locally.
        if (CHECK_FLAG_ALL(actor->flags, ACTOR_FLAG_HOOKSHOT_ATTACHED)) {
            actor->flags &= ~ACTOR_FLAG_HOOKSHOT_ATTACHED;
            EnemySync::SendAdapterEvent(actor, FD_EVENT_HOOKSHOT);
        }
        EnFd_MirrorStartMusic(d);
        EnFd_MirrorTick(d, gPlayState);
    } catch (const std::exception& ex) {
        SPDLOG_WARN("[EnemySync] EnFd extras parse error: {}", ex.what());
    }
}

static void EnFd_OnRemoteEvent(Actor* actor, uint8_t event) {
    if (event == FD_EVENT_HOOKSHOT && gPlayState != NULL) {
        EnFd_MirrorHookshotHit((EnFd*)actor, gPlayState);
    }
}

static void EnFd_OnRemoteDefeat(Actor* actor) {
    EnFd_MirrorStartDeath((EnFd*)actor);
}

static void EnFw_SerializeExtras(Actor* actor, nlohmann::json& x) {
    EnFw* w = (EnFw*)actor;
    x["af"] = EnFw_MirrorGetAction(w);
    x["an"] = EnFw_MirrorGetAnim(w);
    x["cf"] = w->skelAnime.curFrame;
    x["ps"] = w->skelAnime.playSpeed;
    x["bc"] = w->bounceCnt;
    x["dt"] = w->damageTimer;
    x["et"] = w->explosionTimer;
    x["st"] = w->slideTimer;
    x["ss"] = w->slideSfxTimer;
    x["rt"] = w->returnToParentTimer;
    x["ta"] = w->turnAround;
    x["rr"] = w->runRadius;
    x["rd"] = w->runDirection;
    x["lh"] = w->lastDmgHook;
    x["spd"] = actor->speedXZ;
    x["vy"] = actor->velocity.y;
    x["wry"] = actor->world.rot.y;
    x["cft"] = actor->colorFilterTimer;
    x["cfp"] = actor->colorFilterParams;
    x["fh"] = actor->floorHeight;
}

static void EnFw_DeserializeExtras(Actor* actor, const nlohmann::json& x) {
    EnFw* w = (EnFw*)actor;
    try {
        EnFw_MirrorApplyAction(w, Fd_Num(x, "af", EnFw_MirrorGetAction(w)));
        EnFw_MirrorApplyAnim(w, Fd_Num(x, "an", EnFw_MirrorGetAnim(w)));
        w->skelAnime.curFrame = Fd_Flt(x, "cf", w->skelAnime.curFrame);
        w->skelAnime.playSpeed = Fd_Flt(x, "ps", w->skelAnime.playSpeed);
        w->bounceCnt = (s16)Fd_Num(x, "bc", w->bounceCnt);
        w->damageTimer = (s16)Fd_Num(x, "dt", w->damageTimer);
        w->explosionTimer = (s16)Fd_Num(x, "et", w->explosionTimer);
        w->slideTimer = (s16)Fd_Num(x, "st", w->slideTimer);
        w->slideSfxTimer = (s16)Fd_Num(x, "ss", w->slideSfxTimer);
        w->returnToParentTimer = (s16)Fd_Num(x, "rt", w->returnToParentTimer);
        w->turnAround = (s16)Fd_Num(x, "ta", w->turnAround);
        w->runRadius = Fd_Flt(x, "rr", w->runRadius);
        w->runDirection = (s16)Fd_Num(x, "rd", w->runDirection);
        w->lastDmgHook = (u8)Fd_Num(x, "lh", w->lastDmgHook);
        actor->speedXZ = Fd_Flt(x, "spd", actor->speedXZ);
        actor->velocity.y = Fd_Flt(x, "vy", actor->velocity.y);
        actor->world.rot.y = (s16)Fd_Num(x, "wry", actor->world.rot.y);
        actor->colorFilterTimer = (u8)Fd_Num(x, "cft", actor->colorFilterTimer);
        actor->colorFilterParams = (u16)Fd_Num(x, "cfp", actor->colorFilterParams);
        actor->floorHeight = Fd_Flt(x, "fh", actor->floorHeight);
    } catch (const std::exception& ex) {
        SPDLOG_WARN("[EnemySync] EnFw extras parse error: {}", ex.what());
    }
}

static void EnFw_OnRemoteDefeat(Actor* actor) {
    EnFw_MirrorFinish((EnFw*)actor);
}

static void EnFdFire_SerializeExtras(Actor* actor, nlohmann::json& x) {
    EnFdFire* f = (EnFdFire*)actor;
    x["sc"] = f->scale;
    x["dt"] = f->deathTimer;
    x["vx"] = actor->velocity.x;
    x["vy"] = actor->velocity.y;
    x["vz"] = actor->velocity.z;
}

static void EnFdFire_DeserializeExtras(Actor* actor, const nlohmann::json& x) {
    EnFdFire* f = (EnFdFire*)actor;
    try {
        f->scale = Fd_Flt(x, "sc", f->scale);
        f->deathTimer = (s16)Fd_Num(x, "dt", f->deathTimer);
        actor->velocity.x = Fd_Flt(x, "vx", actor->velocity.x);
        actor->velocity.y = Fd_Flt(x, "vy", actor->velocity.y);
        actor->velocity.z = Fd_Flt(x, "vz", actor->velocity.z);
    } catch (const std::exception& ex) {
        SPDLOG_WARN("[EnemySync] EnFdFire extras parse error: {}", ex.what());
    }
}

// Update dereferences the parent every frame; a copy without one has nothing to follow.
static void EnFdFire_OnLocalResume(Actor* actor) {
    if (actor->parent == NULL) {
        Actor_Kill(actor);
    }
}

static void EnFw_OnLocalResume(Actor* actor) {
    if (actor->parent == NULL) {
        Actor_Kill(actor);
    }
}

void RegisterEnFdAdapter() {
    ActorSyncAdapter fd;
    fd.SerializeExtras = EnFd_SerializeExtras;
    fd.DeserializeExtras = EnFd_DeserializeExtras;
    fd.OnRemoteEvent = EnFd_OnRemoteEvent;
    fd.OnRemoteDefeat = EnFd_OnRemoteDefeat;
    fd.QuietRemoteDefeat = true;
    EnemySync::RegisterAdapter(ACTOR_EN_FD, fd);

    ActorSyncAdapter fw;
    fw.SerializeExtras = EnFw_SerializeExtras;
    fw.DeserializeExtras = EnFw_DeserializeExtras;
    fw.OnRemoteDefeat = EnFw_OnRemoteDefeat;
    fw.OnLocalResume = EnFw_OnLocalResume;
    fw.QuietRemoteDefeat = true;
    EnemySync::RegisterAdapter(ACTOR_EN_FW, fw);

    ActorSyncAdapter fire;
    fire.SerializeExtras = EnFdFire_SerializeExtras;
    fire.DeserializeExtras = EnFdFire_DeserializeExtras;
    fire.OnLocalResume = EnFdFire_OnLocalResume;
    EnemySync::RegisterAdapter(ACTOR_EN_FD_FIRE, fire);
}
