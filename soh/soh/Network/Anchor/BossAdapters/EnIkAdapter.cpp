#include "soh/Network/Anchor/BossAdapters/ActorSyncAdapter.h"
#include "soh/Network/Anchor/EnemySync.h"
// Pull the C++ side of global.h in under proper linkage before the extern "C"
// overlay header includes it (BossRush.cpp pattern).
#include "soh/OTRGlobals.h"

extern "C" {
#include "functions.h"
#include "macros.h"
#include "variables.h"
#include "src/overlays/actors/ovl_En_Ik/z_en_ik.h"
extern PlayState* gPlayState;
}

/**
 * Iron Knuckle (EN_IK, #4055): the Spirit Temple Nabooru fight (params 0) and
 * the armoured knuckles (params 1-3) in the temple and the castle.
 *
 * Pose, health and the axe quad come from generic mirroring, and the shield's
 * triangles now stream too (EnemySync's `tv`), so the mirror's swords bounce off a
 * raised shield like the host's. Extras carry what the suppressed Update would
 * have set for Draw: the animation state (9 = shield up), the armour-off flags and
 * the axe-swing flag that drives the blur trail. The armour pieces that fly off
 * are spawned locally from the same BodyBreak.
 *
 * Nabooru is cutscene-driven: her intro and her defeat swap actor.update, and
 * both run on every client. GetPhase reads which update is installed (PRE while a
 * cutscene owns her, FIGHT, then DEFEATED once her health is at the 10 that starts
 * the defeat cutscene). The mirror only follows the stream in FIGHT; on the
 * FIGHT -> DEFEATED edge it releases to the local simulation, whose fight update
 * starts the defeat cutscene itself from the streamed health.
 *
 * Aggro: the knuckles take the generic nearest-player targeting on the host (the
 * Nabooru fight stays on the host's Link: her intro and defeat read GET_PLAYER).
 */

enum IkPhase : uint8_t {
    IK_PRE = 0,
    IK_FIGHT = 1,
    IK_DEFEATED = 2,
};

static int Ik_Num(const nlohmann::json& x, const char* k, int dflt) {
    auto it = x.find(k);
    return (it != x.end() && it->is_number()) ? it->get<int>() : dflt;
}

static uint8_t EnIk_GetPhase(Actor* actor) {
    EnIk* ik = (EnIk*)actor;
    if (actor->params == 0 && actor->colChkInfo.health <= 10) {
        return IK_DEFEATED;
    }
    return EnIk_MirrorIsFight(ik) ? IK_FIGHT : IK_PRE;
}

static bool EnIk_ShouldMirror(Actor* actor, uint8_t streamedPhase) {
    return streamedPhase == IK_FIGHT && EnIk_MirrorIsFight((EnIk*)actor);
}

static bool EnIk_OnPhaseChange(Actor* actor, uint8_t fromPhase, uint8_t toPhase) {
    if (toPhase == IK_DEFEATED) {
        ESYNC_LOG("[EnemySync] EnIk defeat handoff (local defeat cutscene)");
        return true;
    }
    return false;
}

static void EnIk_SerializeExtras(Actor* actor, nlohmann::json& x) {
    EnIk* ik = (EnIk*)actor;
    x["st"] = EnIk_MirrorGetState(ik);
    x["a0"] = EnIk_MirrorGetPrevArmor(ik);
    x["a1"] = EnIk_MirrorGetArmor(ik);
    x["ax"] = EnIk_MirrorGetAxe(ik);
}

static void EnIk_DeserializeExtras(Actor* actor, const nlohmann::json& x) {
    EnIk* ik = (EnIk*)actor;
    if (gPlayState == NULL) {
        return;
    }
    try {
        EnIk_MirrorApply(ik, gPlayState, Ik_Num(x, "st", EnIk_MirrorGetState(ik)),
                         Ik_Num(x, "a0", EnIk_MirrorGetPrevArmor(ik)), Ik_Num(x, "a1", EnIk_MirrorGetArmor(ik)),
                         Ik_Num(x, "ax", EnIk_MirrorGetAxe(ik)));
    } catch (const std::exception& ex) {
        SPDLOG_WARN("[EnemySync] EnIk extras parse error: {}", ex.what());
    }
}

void RegisterEnIkAdapter() {
    ActorSyncAdapter adapter;
    adapter.SerializeExtras = EnIk_SerializeExtras;
    adapter.DeserializeExtras = EnIk_DeserializeExtras;
    adapter.GetPhase = EnIk_GetPhase;
    adapter.ShouldMirror = EnIk_ShouldMirror;
    adapter.OnPhaseChange = EnIk_OnPhaseChange;
    adapter.DeriveDamageEffect = true;
    adapter.QuietRemoteDefeat = true;
    EnemySync::RegisterAdapter(ACTOR_EN_IK, adapter);
}
