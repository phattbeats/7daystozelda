#include "PuppetFairy.h"
#include "Anchor.h"
#include "soh/cvar_prefixes.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"

#include <string>
#include <unordered_map>
#include <cmath>
#include <cstdio>
#include <chrono>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

extern "C" {
#include "variables.h"
#include "functions.h"
#include "src/overlays/actors/ovl_En_Elf/z_en_elf.h"

extern PlayState* gPlayState;

// z_actor.c: while active, idle Navi (pointing at nobody) glows in these colors
// instead of the cosmetics editor's idle colors. Targeting colors are untouched.
extern Color_RGB8 gAnchorNaviInner;
extern Color_RGB8 gAnchorNaviOuter;
extern u8 gAnchorNaviActive;
}

Color_RGB8 AnchorLocalFairyOuter() {
    return CVarGetColor24(CVAR_REMOTE_ANCHOR("Color"), { 100, 255, 100 });
}

Color_RGB8 AnchorLocalFairyInner() {
    return CVarGetColor24(CVAR_REMOTE_ANCHOR("FairyInner"), { 255, 255, 255 });
}

Color_RGB8 AnchorLocalTunic() {
    return CVarGetColor24(CVAR_REMOTE_ANCHOR("Tunic"), AnchorLocalFairyOuter());
}

// One cosmetic companion fairy per remote-player puppet so every screen shows a
// fairy per player (vanilla only draws the local player's own Navi).
//
// The fairy is a plain ACTOR_EN_ELF spawned with params FAIRY_KOKIRI — a pure
// parent-follower (action func func_80A0353C) with no Navi AI, no C-up/talk
// update, and no attention flag. Its Init reads no parent, so we can spawn it
// parentless. We deliberately do NOT use the engine's parent-follow: that path
// self-kills when the parent dies, and RefreshClientActors frees/respawns
// puppets wholesale, so a raw-Actor* parent would be a same-frame
// use-after-free. Instead we suppress the fairy's own update via a
// ShouldActorUpdate override and drive a custom follow that re-resolves the
// puppet from client.player every frame (never cached — Task 2 discipline).
//
// clientId -> fairy Actor*. Only fairies WE spawned live here; the local Navi
// and every other En_Elf are absent, which is the discriminator that keeps our
// override off them.
static std::unordered_map<uint32_t /*clientId*/, Actor*> sFairies;

// Parked sentinel: puppets get shoved to -9999 by DummyPlayer when out of scene,
// and we mirror that for the fairy. Anything this far out is "parked".
static constexpr f32 PARKED_COORD = -9999.0f;

static bool IsGlobalRoom() {
    return std::string("soh-global") == CVarGetString(CVAR_REMOTE_ANCHOR("RoomId"), "");
}

// CLAMP a per-axis velocity target to +/-20, preserving sign (matches the
// engine follower's func_80A02C98 / func_80A02BD8 clamp).
static f32 ClampFairyVel(f32 v) {
    f32 sign = (v >= 0.0f) ? 1.0f : -1.0f;
    v = fabsf(v);
    if (v > 20.0f) {
        v = 20.0f;
    }
    return v * sign;
}

// Core (prim) and aura (env) of the fairy's glow. Alpha stays the engine's.
static void ApplyFairyColors(EnElf* elf, const AnchorClient& client) {
    elf->innerColor.r = (f32)client.fairyInner.r;
    elf->innerColor.g = (f32)client.fairyInner.g;
    elf->innerColor.b = (f32)client.fairyInner.b;
    elf->outerColor.r = (f32)client.fairyOuter.r;
    elf->outerColor.g = (f32)client.fairyOuter.g;
    elf->outerColor.b = (f32)client.fairyOuter.b;
}

static void SnapFairyAway(EnElf* elf) {
    elf->actor.world.pos.x = PARKED_COORD;
    elf->actor.world.pos.y = PARKED_COORD;
    elf->actor.world.pos.z = PARKED_COORD;
    elf->actor.velocity.x = 0.0f;
    elf->actor.velocity.y = 0.0f;
    elf->actor.velocity.z = 0.0f;
}

// Custom follow for one of our fairies. Reimplements the file-static
// func_80A02A20 (hover-bob) + func_80A02C98/func_80A02BD8 (clamped velocity
// solve) from z_en_elf.c against the puppet instead of an Actor* parent.
static void DrivePuppetFairy(uint32_t clientId, Actor* actor) {
    EnElf* elf = (EnElf*)actor;

    // Re-resolve the puppet every frame; never cache a raw Actor* — the puppet
    // may have been freed and respawned by RefreshClientActors. Only touch
    // client.player->... behind the online + non-null guard.
    Player* puppet = nullptr;
    AnchorClient* client = nullptr;
    if (Anchor::Instance != nullptr) {
        auto cit = Anchor::Instance->clients.find(clientId);
        if (cit != Anchor::Instance->clients.end() && cit->second.online &&
            cit->second.player != nullptr && cit->second.player->actor.update != NULL) {
            client = &cit->second;
            puppet = cit->second.player;
        }
    }

    // Parked: puppet in another scene / offline / save not loaded (same predicate
    // DummyPlayer uses). SNAP away — a lerp under the 20/frame clamp would streak
    // the fairy across the whole map. The lifecycle tick reaps it shortly after.
    bool parked = (client == nullptr) || (gPlayState == nullptr) ||
                  (client->sceneNum != gPlayState->sceneNum) || !client->isSaveLoaded || !client->online;
    if (parked) {
        SnapFairyAway(elf);
        return;
    }

    // Re-applied every frame so a mid-session color change shows at once.
    ApplyFairyColors(elf, *client);

    Vec3f puppetPos = puppet->actor.world.pos;
    f32 hover = (1500.0f * elf->actor.scale.y) + 40.0f; // matches z_en_elf.c:687

    // Just came back from being parked → snap next to the puppet before resuming
    // (otherwise it would slowly crawl back from -9999 at 20/frame).
    if (elf->actor.world.pos.x <= (PARKED_COORD + 1.0f)) {
        elf->actor.world.pos.x = puppetPos.x;
        elf->actor.world.pos.y = puppetPos.y + hover;
        elf->actor.world.pos.z = puppetPos.z;
        elf->actor.velocity.x = 0.0f;
        elf->actor.velocity.y = 0.0f;
        elf->actor.velocity.z = 0.0f;
    }

    SkelAnime_Update(&elf->skelAnime);

    // Hover-bob offset — func_80A02A20 with the case-0 params EnElf_Init set for
    // FAIRY_KOKIRI (unk_2AE/2B0/2B4/2B8 already initialised there).
    elf->unk_28C.x = Math_SinS(elf->unk_2AC) * elf->unk_2B8;
    elf->unk_28C.y = Math_SinS(elf->unk_2AA) * elf->unk_2B4;
    elf->unk_28C.z = Math_CosS(elf->unk_2AC) * elf->unk_2B8;
    elf->unk_2AC += elf->unk_2B0;
    elf->unk_2AA += elf->unk_2AE;

    // Follow target = puppet world.pos + hover height.
    Vec3f target = puppetPos;
    target.y += hover;

    // func_80A02C98 (xz) + func_80A02BD8 (y): velocity toward target * 0.2,
    // clamped to 20/axis, stepped in.
    const f32 rate = 0.2f;
    f32 xVel = ClampFairyVel(((target.x + elf->unk_28C.x) - elf->actor.world.pos.x) * rate);
    f32 yVel = ClampFairyVel(((target.y + elf->unk_28C.y) - elf->actor.world.pos.y) * rate);
    f32 zVel = ClampFairyVel(((target.z + elf->unk_28C.z) - elf->actor.world.pos.z) * rate);

    Math_StepToF(&elf->actor.velocity.y, yVel, 32.0f);
    Math_StepToF(&elf->actor.velocity.x, xVel, 1.5f);
    Math_StepToF(&elf->actor.velocity.z, zVel, 1.5f);
    Actor_UpdatePos(&elf->actor);

    // Face travel direction (func_80A0353C tail) and advance the draw timer that
    // drives the env-alpha shimmer (z_en_elf.c:1517). EnElf_Update normally does
    // both of these; we do them here since its update is suppressed.
    elf->unk_2BC = Math_Atan2S(elf->actor.velocity.z, elf->actor.velocity.x);
    elf->actor.shape.rot.y = elf->unk_2BC;
    elf->timer++;

    // Keep the two point lights riding with the fairy (EnElf_UpdateLights default
    // branch: glow radius 100, no-glow radius -1, white).
    Lights_PointGlowSetInfo(&elf->lightInfoGlow, (s16)elf->actor.world.pos.x, (s16)elf->actor.world.pos.y,
                            (s16)elf->actor.world.pos.z, 255, 255, 255, 100);
    Lights_PointNoGlowSetInfo(&elf->lightInfoNoGlow, (s16)elf->actor.world.pos.x, (s16)elf->actor.world.pos.y,
                              (s16)elf->actor.world.pos.z, 255, 255, 255, -1);
}

// Per-frame lifecycle: reap fairies whose puppet is gone, then spawn one for
// every live puppet that lacks a fairy.
static void PuppetFairyLifecycle() {
    if (Anchor::Instance == nullptr || gPlayState == nullptr) {
        return;
    }

    auto& clients = Anchor::Instance->clients;

    // 1) Reap. A puppet is live only when the client is online with a non-null
    //    player whose actor is still updating. Read client.player->actor.update
    //    only behind that guard so we never dereference a freed puppet. Erase the
    //    map entry in lockstep with the kill so no stale Actor* survives.
    for (auto it = sFairies.begin(); it != sFairies.end();) {
        auto cit = clients.find(it->first);
        bool puppetLive = (cit != clients.end()) && !cit->second.self && cit->second.online &&
                          cit->second.player != nullptr && cit->second.player->actor.update != NULL;
        if (!puppetLive) {
            Actor_Kill(it->second);
            it = sFairies.erase(it);
        } else {
            ++it;
        }
    }

    // 2) Spawn. Suppressed in the global room, same as nametags (DummyPlayer.cpp).
    if (IsGlobalRoom()) {
        return;
    }

    for (auto& [clientId, client] : clients) {
        if (client.self || !client.online || client.player == nullptr ||
            client.player->actor.update == NULL) {
            continue;
        }
        if (sFairies.count(clientId)) {
            continue;
        }

        Vec3f pos = client.player->actor.world.pos;
        Actor* fairy = Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_EN_ELF, pos.x, pos.y + 60.0f, pos.z, 0,
                                   0, 0, FAIRY_KOKIRI, false);
        if (fairy == nullptr) {
            continue;
        }

        // Tint the core and aura with the client's fairy gradient. Draw reads
        // innerColor/outerColor rgb + a timer-driven alpha, so this is all the
        // tint the fairy needs.
        ApplyFairyColors((EnElf*)fairy, client);

        sFairies[clientId] = fairy;
    }
}

// Our own colors as last sent in client state, so a change made mid-session
// (ImGui picker, console) goes out on its own instead of waiting for a scene change.
static Color_RGB8 sSentColors[3];
static std::chrono::steady_clock::time_point sLastColorSend;
static std::string sRosterSig;

static bool SameColor(const Color_RGB8& a, const Color_RGB8& b) {
    return a.r == b.r && a.g == b.g && a.b == b.b;
}

// Sends at once, then at most every 150 ms while a picker is dragged; the last value
// always goes out because it still differs from what was sent.
static void ResendColorsWhenChanged() {
    Color_RGB8 now[3] = { AnchorLocalFairyInner(), AnchorLocalFairyOuter(), AnchorLocalTunic() };
    if (SameColor(now[0], sSentColors[0]) && SameColor(now[1], sSentColors[1]) &&
        SameColor(now[2], sSentColors[2])) {
        return;
    }
    auto t = std::chrono::steady_clock::now();
    if (t - sLastColorSend < std::chrono::milliseconds(150)) {
        return;
    }
    sLastColorSend = t;
    for (int i = 0; i < 3; i++) {
        sSentColors[i] = now[i];
    }
    Anchor::Instance->SendPacket_UpdateClientState();
}

// Idle Navi wears our lobby fairy, so we see what everyone else sees. Off in the
// global room and when "My Navi uses my fairy colors" is unchecked.
static void UpdateLocalNavi() {
    gAnchorNaviActive = CVarGetInteger(CVAR_REMOTE_ANCHOR("NaviUsesFairyColors"), 1) && !IsGlobalRoom();
    gAnchorNaviInner = AnchorLocalFairyInner();
    gAnchorNaviOuter = AnchorLocalFairyOuter();

    // Navi only copies the idle colors when she switches targets, so repaint an idle,
    // settled Navi directly; a mid-session change then shows on our screen at once.
    if (!gAnchorNaviActive || gPlayState == nullptr || !GameInteractor::IsSaveLoaded()) {
        return;
    }
    Player* player = GET_PLAYER(gPlayState);
    if (player == nullptr || player->naviActor == nullptr ||
        gPlayState->actorCtx.targetCtx.activeCategory != ACTORCAT_PLAYER) {
        return;
    }
    EnElf* navi = (EnElf*)player->naviActor;
    if (navi->unk_29C != 0.0f) {
        return;
    }
    navi->innerColor.r = gAnchorNaviInner.r;
    navi->innerColor.g = gAnchorNaviInner.g;
    navi->innerColor.b = gAnchorNaviInner.b;
    navi->outerColor.r = gAnchorNaviOuter.r;
    navi->outerColor.g = gAnchorNaviOuter.g;
    navi->outerColor.b = gAnchorNaviOuter.b;
}

// Web lobby: remember who was in the room and their tunic, so the next visit can warn
// about a tunic that is hard to tell apart (shell.html, SohNet.roster).
static void PublishRoster() {
#ifdef __EMSCRIPTEN__
    std::string sig;
    char buf[16];
    for (auto& [clientId, client] : Anchor::Instance->clients) {
        if (client.self) {
            continue;
        }
        std::string name;
        for (char c : client.name) {
            if (c != '"' && c != '\\' && (unsigned char)c >= 0x20) {
                name += c;
            }
        }
        snprintf(buf, sizeof(buf), "%02X%02X%02X", client.tunic.r, client.tunic.g, client.tunic.b);
        sig += (sig.empty() ? "" : ",") + std::string("{\"name\":\"") + name + "\",\"tunic\":\"" + buf + "\"}";
    }
    if (sig == sRosterSig) {
        return;
    }
    sRosterSig = sig;
    std::string json = "[" + sig + "]";
    EM_ASM({ if (window.SohNet && window.SohNet.roster) window.SohNet.roster(UTF8ToString($0)); }, json.c_str());
#endif
}

// Per-frame entry point, called by the Anchor per-frame dispatcher in explicit tick
// order (see HookHandlers.cpp).
void PuppetFairyTick() {
    PuppetFairyLifecycle();
    UpdateLocalNavi();
    if (Anchor::Instance != nullptr) {
        ResendColorsWhenChanged();
        PublishRoster();
    }
}

void RegisterPuppetFairyHooks(bool isConnected) {
    // On disconnect the OnActorDestroy/OnSceneInit hooks are torn down, so any
    // surviving entries would go stale and a reconnect's first tick could iterate
    // dangling Actor*. Drop the map. The orphaned fairy actors self-destruct on
    // their own: with the ShouldActorUpdate override gone, their normal update
    // (func_80A0353C) runs, sees the NULL parent, and Actor_Kills them. Mirrors
    // EnemySync::RegisterHooks' Reset-on-disconnect.
    if (!isConnected) {
        sFairies.clear();
        gAnchorNaviActive = 0;
    } else {
        // The handshake / scene-change client state carries the current colors.
        sSentColors[0] = AnchorLocalFairyInner();
        sSentColors[1] = AnchorLocalFairyOuter();
        sSentColors[2] = AnchorLocalTunic();
        sRosterSig.clear();
    }

    // NOTE: the spawn/reap lifecycle is NOT registered here; it is driven by the Anchor
    // per-frame dispatcher (PuppetFairyTick) so the Anchor-internal tick order is explicit.
    // The disconnect path above still drops the tracking map.

    // Follow override: suppress each of our fairies' own update and drive it. Any
    // En_Elf NOT in sFairies (local Navi, forest fairies, healing fairies) falls
    // straight through untouched — that absence is the discriminator.
    COND_ID_HOOK(ShouldActorUpdate, ACTOR_EN_ELF, isConnected, [](void* actorRef, bool* should) {
        Actor* actor = (Actor*)actorRef;
        for (auto& [clientId, fairy] : sFairies) {
            if (fairy == actor) {
                *should = false;
                DrivePuppetFairy(clientId, actor);
                return;
            }
        }
    });

    // Scene teardown frees the fairy actors; drop any entry the instant its actor
    // is destroyed so the follow override never touches a freed pointer.
    COND_HOOK(OnActorDestroy, isConnected, [](void* actorRef) {
        Actor* actor = (Actor*)actorRef;
        for (auto it = sFairies.begin(); it != sFairies.end();) {
            if (it->second == actor) {
                it = sFairies.erase(it);
            } else {
                ++it;
            }
        }
    });

    // Belt-and-suspenders: a fresh scene starts with an empty map.
    COND_HOOK(OnSceneInit, isConnected, [](int16_t sceneNum) { sFairies.clear(); });
}

#ifdef __EMSCRIPTEN__
#include <nlohmann/json.hpp>

// Test hooks for the web two-player color test (tools/webtest/colors-test.py).
static nlohmann::json ColorJson(const Color_RGB8& c) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%02X%02X%02X", c.r, c.g, c.b);
    return buf;
}

static nlohmann::json ColorJson(f32 r, f32 g, f32 b) {
    return ColorJson(Color_RGB8{ (u8)r, (u8)g, (u8)b });
}

extern "C" {

EMSCRIPTEN_KEEPALIVE
const char* anchor_test_colors() {
    static std::string out;
    nlohmann::json j;
    j["mine"] = { { "fairyInner", ColorJson(AnchorLocalFairyInner()) },
                  { "fairyOuter", ColorJson(AnchorLocalFairyOuter()) },
                  { "tunic", ColorJson(AnchorLocalTunic()) } };
    j["naviActive"] = gAnchorNaviActive;
    if (gPlayState != nullptr && GameInteractor::IsSaveLoaded()) {
        Player* player = GET_PLAYER(gPlayState);
        if (player != nullptr && player->naviActor != nullptr) {
            EnElf* navi = (EnElf*)player->naviActor;
            j["navi"] = { { "inner", ColorJson(navi->innerColor.r, navi->innerColor.g, navi->innerColor.b) },
                          { "outer", ColorJson(navi->outerColor.r, navi->outerColor.g, navi->outerColor.b) } };
        }
    }
    j["clients"] = nlohmann::json::array();
    if (Anchor::Instance != nullptr) {
        for (auto& [clientId, client] : Anchor::Instance->clients) {
            nlohmann::json c = { { "id", clientId },
                                 { "name", client.name },
                                 { "self", client.self },
                                 { "color", ColorJson(client.color) },
                                 { "fairyInner", ColorJson(client.fairyInner) },
                                 { "fairyOuter", ColorJson(client.fairyOuter) },
                                 { "tunic", ColorJson(client.tunic) } };
            auto it = sFairies.find(clientId);
            if (it != sFairies.end()) {
                EnElf* elf = (EnElf*)it->second;
                c["fairy"] = { { "inner", ColorJson(elf->innerColor.r, elf->innerColor.g, elf->innerColor.b) },
                               { "outer", ColorJson(elf->outerColor.r, elf->outerColor.g, elf->outerColor.b) },
                               { "x", elf->actor.world.pos.x },
                               { "y", elf->actor.world.pos.y },
                               { "z", elf->actor.world.pos.z } };
            }
            // #4021: whether our own Navi can hover over (and chime at) this player's puppet.
            if (client.player != nullptr && client.player->actor.update != NULL && gPlayState != nullptr) {
                c["puppetAttention"] = (client.player->actor.flags & ACTOR_FLAG_ATTENTION_ENABLED) != 0;
                c["naviHovering"] = gPlayState->actorCtx.targetCtx.arrowPointedActor == &client.player->actor;
            }
            j["clients"].push_back(c);
        }
    }
    out = j.dump();
    return out.c_str();
}

// which: "inner", "outer" or "tunic"; hex: "RRGGBB". Same CVars as the ImGui pickers.
EMSCRIPTEN_KEEPALIVE
void anchor_test_set_color(const char* which, const char* hex) {
    unsigned int r = 0, g = 0, b = 0;
    if (sscanf(hex, "%02x%02x%02x", &r, &g, &b) != 3) {
        return;
    }
    std::string w = which;
    const char* cvar = w == "inner"   ? CVAR_REMOTE_ANCHOR("FairyInner")
                       : w == "tunic" ? CVAR_REMOTE_ANCHOR("Tunic")
                                      : CVAR_REMOTE_ANCHOR("Color");
    CVarSetColor24(cvar, { (u8)r, (u8)g, (u8)b });
}

} // extern "C"
#endif
