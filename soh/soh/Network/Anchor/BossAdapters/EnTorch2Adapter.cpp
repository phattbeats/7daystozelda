#include "soh/Network/Anchor/BossAdapters/ActorSyncAdapter.h"
#include "soh/Network/Anchor/EnemySync.h"
// Pull the C++ side of global.h in under proper linkage before the extern "C"
// overlay header includes it (BossRush.cpp pattern).
#include "soh/OTRGlobals.h"

extern "C" {
#include "functions.h"
#include "macros.h"
#include "variables.h"
#include "src/overlays/actors/ovl_En_Torch2/z_en_torch2.h"
}

/**
 * Dark Link (EN_TORCH2, #4055).
 *
 * Dark Link is a Player struct driven by a fake controller. Every input he
 * "presses" is worked out from GET_PLAYER: he copies the sword animation Link
 * swings, counters it, steps to where Link's blade is, and jumps onto it. So one
 * duel belongs to one player: the host's Link. Retargeting him at a remote puppet
 * would feed him a Player struct with no sword state, and a partial split (aim at
 * one player, read the animation of the other) is worse than either. The
 * partner is a bystander who can still hit him; their swings are forwarded to the
 * host and land on his own collider.
 *
 * What the mirror needs, because Update never runs there: his action state, the
 * fade-in alpha (Draw reads sAlpha, a file static), the counter state, and the
 * sword-jump offset (shape.yOffset), all carried in extras. The skeleton is set
 * up through SkelAnime_InitLink, which has no capture hook, so EnTorch2_Init
 * announces it by hand; the pose then streams like any other.
 *
 * While he is waiting (WAIT), either player walking up can wake him, so the
 * nearest-player perception runs; once the duel starts it stays on the host.
 * His death is the generic handoff: the stream stops, the mirror's copy takes the
 * streamed DEATH state and fades out the same way (no finishing-blow camera on
 * the partner).
 */

enum DarkLinkState : uint8_t {
    DL_WAIT = 0,
    DL_ATTACK = 1,
    DL_DEATH = 2,
    DL_DAMAGE = 3,
};

static bool EnTorch2_KeepLocalPerception(Actor* actor) {
    return EnTorch2_MirrorGetState() != DL_WAIT;
}

static uint8_t EnTorch2_GetPhase(Actor* actor) {
    int state = EnTorch2_MirrorGetState();
    if (state == DL_DEATH || actor->colChkInfo.health == 0) {
        return 2;
    }
    return state == DL_WAIT ? 0 : 1;
}

static void EnTorch2_SerializeExtras(Actor* actor, nlohmann::json& x) {
    x["st"] = EnTorch2_MirrorGetState();
    x["al"] = EnTorch2_MirrorGetAlpha();
    x["ct"] = EnTorch2_MirrorGetCounter();
    x["sj"] = EnTorch2_MirrorGetSwordJump();
    x["sh"] = actor->shape.shadowAlpha;
    // Read-only: what the hit check in his Update looks at (test probes).
    Player* pl = (Player*)actor;
    x["iv"] = pl->invincibilityTimer;
    x["sf1"] = pl->stateFlags1;
    x["acf"] = pl->cylinder.base.acFlags;
    x["atq"] = pl->meleeWeaponQuads[0].base.atFlags | (pl->meleeWeaponQuads[1].base.atFlags << 8);
}

// Every field is read through find + is_number: a wrong JSON type would throw a
// C++ exception and take the wasm page down.
static int Torch2_Num(const nlohmann::json& x, const char* k, int dflt) {
    auto it = x.find(k);
    return (it != x.end() && it->is_number()) ? it->get<int>() : dflt;
}

static void EnTorch2_DeserializeExtras(Actor* actor, const nlohmann::json& x) {
    try {
        auto sj = x.find("sj");
        EnTorch2_MirrorApply((Player*)actor, Torch2_Num(x, "st", EnTorch2_MirrorGetState()),
                             Torch2_Num(x, "al", EnTorch2_MirrorGetAlpha()),
                             Torch2_Num(x, "ct", EnTorch2_MirrorGetCounter()),
                             (sj != x.end() && sj->is_number()) ? sj->get<float>() : 0.0f);
        actor->shape.shadowAlpha = (u8)Torch2_Num(x, "sh", actor->shape.shadowAlpha);
    } catch (const std::exception& ex) {
        SPDLOG_WARN("[EnemySync] EnTorch2 extras parse error: {}", ex.what());
    }
}

void RegisterEnTorch2Adapter() {
    ActorSyncAdapter adapter;
    adapter.SerializeExtras = EnTorch2_SerializeExtras;
    adapter.DeserializeExtras = EnTorch2_DeserializeExtras;
    adapter.GetPhase = EnTorch2_GetPhase;
    adapter.KeepLocalPerception = EnTorch2_KeepLocalPerception;
    EnemySync::RegisterAdapter(ACTOR_EN_TORCH2, adapter);
}
