#include "soh/Network/Anchor/EnemyFxSync.h"
#include "soh/Network/Anchor/EnemySync.h"
#include "soh/Network/Anchor/Anchor.h"
#include <libultraship/libultraship.h>

#include <array>
#include <cstring>
#include <unordered_map>
#include <vector>

extern "C" {
#include "macros.h"
#include "functions.h"
#include "variables.h"
#include "overlays/effects/ovl_Effect_Ss_Dust/z_eff_ss_dust.h"
#include "overlays/effects/ovl_Effect_Ss_KiraKira/z_eff_ss_kirakira.h"
#include "overlays/effects/ovl_Effect_Ss_Bomb/z_eff_ss_bomb.h"
#include "overlays/effects/ovl_Effect_Ss_Blast/z_eff_ss_blast.h"
#include "overlays/effects/ovl_Effect_Ss_D_Fire/z_eff_ss_d_fire.h"
#include "overlays/effects/ovl_Effect_Ss_Bubble/z_eff_ss_bubble.h"
#include "overlays/effects/ovl_Effect_Ss_G_Ripple/z_eff_ss_g_ripple.h"
#include "overlays/effects/ovl_Effect_Ss_G_Splash/z_eff_ss_g_splash.h"
#include "overlays/effects/ovl_Effect_Ss_G_Magma/z_eff_ss_g_magma.h"
#include "overlays/effects/ovl_Effect_Ss_G_Fire/z_eff_ss_g_fire.h"
#include "overlays/effects/ovl_Effect_Ss_Lightning/z_eff_ss_lightning.h"
#include "overlays/effects/ovl_Effect_Ss_Dt_Bubble/z_eff_ss_dt_bubble.h"
#include "overlays/effects/ovl_Effect_Ss_Sibuki/z_eff_ss_sibuki.h"
#include "overlays/effects/ovl_Effect_Ss_Sibuki2/z_eff_ss_sibuki2.h"
#include "overlays/effects/ovl_Effect_Ss_G_Magma2/z_eff_ss_g_magma2.h"
#include "overlays/effects/ovl_Effect_Ss_K_Fire/z_eff_ss_k_fire.h"
#include "overlays/effects/ovl_Effect_Ss_Ice_Piece/z_eff_ss_ice_piece.h"
#include "overlays/effects/ovl_Effect_Ss_Ice_Smoke/z_eff_ss_ice_smoke.h"
extern PlayState* gPlayState;
}

namespace EnemyFxSync {

namespace {

constexpr size_t MAX_FX_BYTES = 72;
constexpr size_t MAX_SFX_PER_FRAME = 12;   // per actor, authority capture
constexpr size_t MAX_FX_PER_FRAME = 8;     // per actor, authority capture
constexpr size_t MAX_SFX_QUEUED = 24;      // per key, mirror queue
constexpr size_t MAX_FX_QUEUED = 16;       // per key, mirror queue

constexpr u32 DEFERRED_SFX_FLAGS =
    ACTOR_FLAG_SFX_ACTOR_POS_2 | ACTOR_AUDIO_FLAG_SFX_CENTERED_1 | ACTOR_AUDIO_FLAG_SFX_CENTERED_2 | ACTOR_FLAG_SFX_TIMER;

// Pointer-free effect init structs only: their bytes mean the same thing on
// every machine. Excluded on purpose: types holding Actor*/Gfx*/s16* (En_Fire,
// En_Ice, Fcircle, Fhg_Flash, Fire_Tail, G_Spk, Hahen, Kakera, Solder_Srch_Ball),
// Bomb2 (carries a frame-interpolation epoch), death effects (Dead_*: deaths play
// locally on each side), HitMark (collision-local), Stick/Stone1/Extra (not enemy AI).
size_t WhitelistedSize(s32 type) {
    switch (type) {
        case EFFECT_SS_DUST: return sizeof(EffectSsDustInitParams);
        case EFFECT_SS_KIRAKIRA: return sizeof(EffectSsKiraKiraInitParams);
        case EFFECT_SS_BOMB: return sizeof(EffectSsBombInitParams);
        case EFFECT_SS_BLAST: return sizeof(EffectSsBlastParams);
        case EFFECT_SS_D_FIRE: return sizeof(EffectSsDFireInitParams);
        case EFFECT_SS_BUBBLE: return sizeof(EffectSsBubbleInitParams);
        case EFFECT_SS_G_RIPPLE: return sizeof(EffectSsGRippleInitParams);
        case EFFECT_SS_G_SPLASH: return sizeof(EffectSsGSplashInitParams);
        case EFFECT_SS_G_MAGMA: return sizeof(EffectSsGMagmaInitParams);
        case EFFECT_SS_G_FIRE: return sizeof(EffectSsGFireInitParams);
        case EFFECT_SS_LIGHTNING: return sizeof(EffectSsLightningInitParams);
        case EFFECT_SS_DT_BUBBLE: return sizeof(EffectSsDtBubbleInitParams);
        case EFFECT_SS_SIBUKI: return sizeof(EffectSsSibukiInitParams);
        case EFFECT_SS_SIBUKI2: return sizeof(EffectSsSibuki2InitParams);
        case EFFECT_SS_G_MAGMA2: return sizeof(EffectSsGMagma2InitParams);
        case EFFECT_SS_K_FIRE: return sizeof(EffectSsKFireInitParams);
        case EFFECT_SS_ICE_PIECE: return sizeof(EffectSsIcePieceInitParams);
        case EFFECT_SS_ICE_SMOKE: return sizeof(EffectSsIceSmokeInitParams);
        default: return 0;
    }
}

struct Fx {
    u8 type = 0;
    u8 priority = 0;
    u8 len = 0;
    alignas(16) std::array<u8, MAX_FX_BYTES> bytes{};
};

struct Capture {
    std::vector<u16> sfx;
    u16 deferredSfx = 0;
    u32 deferredFlags = 0;
    std::vector<Fx> fx;
};

struct RemoteQueue {
    std::vector<u16> sfx;
    u16 deferredSfx = 0; // latest stream frame's deferred sfx (0 = none)
    u32 deferredFlags = 0;
    // Frames left to keep re-asserting it. 2 bridges one missing/late stream packet,
    // so looping actor->sfx sounds don't stutter on jitter.
    u8 deferredFrames = 0;
    std::vector<Fx> fx;
};

Actor* sCapturing = nullptr; // actor whose update is running, if capture-eligible
std::unordered_map<Actor*, Capture> sCaptures;
std::unordered_map<uint64_t, RemoteQueue> sRemote;

bool CaptureEligible(Actor* actor) {
    if (actor->category != ACTORCAT_ENEMY && actor->category != ACTORCAT_BOSS) {
        return false; // cheap reject for the other few hundred actors per frame
    }
    // isConnected first: after a disconnect the destroy hooks that prune sCaptures are
    // unregistered, so nothing may be captured.
    return Anchor::Instance != nullptr && Anchor::Instance->isConnected && Enabled() && EnemySync::SyncEnabled() && EnemySync::MirroringEnabled() && EnemySync::IsLocalAuthority() &&
           EnemySync::HasSameScenePeer() && EnemySync::IsTrackedEnemy(actor) && !EnemySync::IsDying(actor);
}

} // namespace

bool Enabled() {
    return CVarGetInteger(CVAR_REMOTE_ANCHOR("EnemyFxSync"), 1);
}

void BeginActorUpdate(Actor* actor) {
    sCapturing = CaptureEligible(actor) ? actor : nullptr;
}

void EndActorUpdate(Actor* actor) {
    if (sCapturing != actor) {
        sCapturing = nullptr;
        return;
    }
    sCapturing = nullptr;
    // Deferred sfx: set during update, played from Actor_DrawAll this frame.
    if (actor->sfx != 0) {
        Capture& c = sCaptures[actor];
        c.deferredSfx = actor->sfx;
        c.deferredFlags = actor->flags & DEFERRED_SFX_FLAGS;
    }
}

void AppendToSnapshot(Actor* actor, nlohmann::json& entry) {
    auto it = sCaptures.find(actor);
    if (it == sCaptures.end()) {
        return;
    }
    Capture& c = it->second;
    if (!c.sfx.empty()) {
        entry["sfx"] = c.sfx;
    }
    if (c.deferredSfx != 0) {
        entry["dsfx"] = c.deferredSfx;
        entry["dsfl"] = c.deferredFlags;
    }
    if (!c.fx.empty()) {
        nlohmann::json fx = nlohmann::json::array();
        for (const Fx& f : c.fx) {
            std::vector<int> bytes(f.bytes.begin(), f.bytes.begin() + f.len);
            fx.push_back({ { "t", f.type }, { "p", f.priority }, { "b", bytes } });
        }
        entry["fx"] = fx;
    }
    sCaptures.erase(it);
}

void EndFrame() {
    // Captures for enemies that didn't make it into a snapshot this frame (left the
    // stream, projectile, no peer) must not pile up and replay late.
    sCaptures.clear();
}

void Ingest(uint64_t key, const nlohmann::json& e) {
    if (!Enabled()) {
        return;
    }
    bool any = e.contains("sfx") || e.contains("dsfx") || e.contains("fx");
    auto it = sRemote.find(key);
    if (!any) {
        if (it != sRemote.end()) {
            it->second.deferredFrames = 0; // the authority's copy went quiet
        }
        return;
    }
    RemoteQueue& q = sRemote[key];
    if (e.contains("sfx")) {
        for (const auto& s : e["sfx"]) {
            if (q.sfx.size() >= MAX_SFX_QUEUED) {
                break;
            }
            q.sfx.push_back(s.get<u16>());
        }
    }
    if (e.contains("dsfx")) {
        q.deferredSfx = e["dsfx"].get<u16>();
        q.deferredFlags = e.value("dsfl", (u32)0) & DEFERRED_SFX_FLAGS;
        q.deferredFrames = 2;
    } else {
        q.deferredFrames = 0;
    }
    if (e.contains("fx")) {
        for (const auto& f : e["fx"]) {
            if (q.fx.size() >= MAX_FX_QUEUED) {
                break;
            }
            Fx fx;
            fx.type = f.value("t", (u8)0);
            fx.priority = f.value("p", (u8)0);
            const auto& b = f["b"];
            size_t expected = WhitelistedSize(fx.type);
            // Same build on both sides (Anchor rejects mismatched clientVersions), so the
            // size must match exactly; anything else is dropped rather than misread.
            if (expected == 0 || b.size() != expected || expected > MAX_FX_BYTES) {
                continue;
            }
            fx.len = (u8)expected;
            for (size_t i = 0; i < expected; i++) {
                fx.bytes[i] = (u8)b[i].get<int>();
            }
            q.fx.push_back(fx);
        }
    }
}

void Replay(Actor* actor, uint64_t key) {
    auto it = sRemote.find(key);
    if (it == sRemote.end() || gPlayState == NULL) {
        return;
    }
    RemoteQueue& q = it->second;
    for (u16 sfxId : q.sfx) {
        Audio_PlaySoundGeneral(sfxId, &actor->projectedPos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
    }
    q.sfx.clear();
    if (q.deferredFrames > 0 && q.deferredSfx != 0) {
        // Actor_UpdateAll zeroed actor->sfx at the top of this frame's pass; setting it
        // here lets Actor_DrawAll play it exactly like the authority's copy did.
        actor->sfx = q.deferredSfx;
        actor->flags = (actor->flags & ~DEFERRED_SFX_FLAGS) | q.deferredFlags;
        q.deferredFrames--;
    }
    for (Fx& f : q.fx) {
        EffectSs_Spawn(gPlayState, f.type, f.priority, f.bytes.data());
    }
    q.fx.clear();
}

void Drop(uint64_t key) {
    sRemote.erase(key);
}

void Reset() {
    sCapturing = nullptr;
    sCaptures.clear();
    sRemote.clear();
}

void Forget(Actor* actor) {
    sCaptures.erase(actor);
    if (sCapturing == actor) {
        sCapturing = nullptr;
    }
}

} // namespace EnemyFxSync

extern "C" void Anchor_RecordActorSfx(u16 sfxId, Vec3f* pos) {
    Actor* a = EnemyFxSync::sCapturing;
    if (a == nullptr || pos != &a->projectedPos) {
        return; // not an enemy AI sound (or not the capturing actor's own position)
    }
    auto& c = EnemyFxSync::sCaptures[a];
    if (c.sfx.size() < EnemyFxSync::MAX_SFX_PER_FRAME) {
        c.sfx.push_back(sfxId);
    }
}

extern "C" void Anchor_RecordEffectSpawn(s32 type, s32 priority, void* initParams) {
    Actor* a = EnemyFxSync::sCapturing;
    if (a == nullptr || initParams == nullptr) {
        return;
    }
    size_t size = EnemyFxSync::WhitelistedSize(type);
    if (size == 0 || size > EnemyFxSync::MAX_FX_BYTES) {
        return;
    }
    auto& c = EnemyFxSync::sCaptures[a];
    if (c.fx.size() >= EnemyFxSync::MAX_FX_PER_FRAME) {
        return;
    }
    EnemyFxSync::Fx f;
    f.type = (u8)type;
    f.priority = (u8)priority;
    f.len = (u8)size;
    std::memcpy(f.bytes.data(), initParams, size);
    c.fx.push_back(f);
}
