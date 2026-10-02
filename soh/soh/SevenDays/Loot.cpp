#include "SevenDays.h"
#include "SevenDaysNet.h"
#include "soh/ActorDB.h"
#include "soh/ShipInit.hpp"
#include "soh/frame_interpolation.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/custom-message/CustomMessageManager.h"
#include "soh/Network/Anchor/Anchor.h"
#include "soh/Network/Anchor/EnemySync.h"
#include "soh/Notification/Notification.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

extern "C" {
#include "z64.h"
#include "macros.h"
#include "variables.h"
#include "functions.h"
#include "objects/gameplay_dangeon_keep/gameplay_dangeon_keep.h"
extern PlayState* gPlayState;
s32 Object_Spawn(ObjectContext* objectCtx, s16 objectId); // z_scene.c
}

/**
 * M7: loot. Everything a player finds lands in the room's shared pool or its
 * blueprint list, decided by the room owner like the rest of the base:
 *
 *   - Pots and crates (ObjTsubo, ObjKibako, ObjKibako2): a small material roll
 *     weighted to the area's tier, paid through the ordinary GATHER path (the roll
 *     comes from the pot's stable sourceKey, so every client agrees and the owner
 *     pays a pot once).
 *   - Supply caches: SevenDays_Cache, a chest-like ActorDB actor placed from a
 *     per-scene table (2-4 per dungeon plus three grottos). Opened once per save:
 *     the owner records the key in BaseState::lootOpened and pays a bigger
 *     bundle, with a chance at a blueprint.
 *   - Gold Skulltula tokens: a blueprint every 10 tokens.
 *   - Boss defeats: the boss's key blueprint (Gohma: spike strip, King Dodongo:
 *     stone wall) plus materials; the tier itself opens from the save flags.
 *
 * Packets (JSON):
 *   LOOT_REQUEST  finder -> owner   kind (cache | skull | boss), key, tier, spot, boss
 *   LOOT_RESULT   owner  -> room    ok, kind, key, finder, mats[], blueprint, reason
 */

namespace SevenDays {

using Net::ActingOwner;
using Net::Broadcast;
using Net::Connected;
using Net::IsOwner;
using Net::Now;
using Net::OwnId;
using Net::SendTo;

static const std::string LOOT_REQUEST = "LOOT_REQUEST";
static const std::string LOOT_RESULT = "LOOT_RESULT";

constexpr uint16_t TEXT_LOOT_BASE = SEVEN_DAYS_TEXT_BASE + 0x28; // + LootLine
constexpr uint16_t TEXT_CACHE = SEVEN_DAYS_TEXT_BASE + 0x30;
constexpr uint16_t TEXT_CACHE_EMPTY = SEVEN_DAYS_TEXT_BASE + 0x31;

struct CacheActor {
    Actor actor;
    ColliderCylinder collider;
    uint16_t spot;
    uint32_t key;
    uint8_t talking;
    uint8_t opened;   // as last drawn (for the lid's swing)
    int16_t lidTimer; // frames of the lid swinging open
};

static int16_t sCacheActorId = -1;
static std::unordered_map<uint16_t, Actor*> sCaches;    // spot -> actor in this scene
static std::unordered_map<uint32_t, double> sRequested; // key -> when we last asked the owner
static bool sTierBaseline = false;
static int32_t sTierPoll = 0;

// MARK: - Helpers

static bool Opened(uint32_t key) {
    const auto& v = GetBase().lootOpened;
    return std::find(v.begin(), v.end(), key) != v.end();
}

static const char* RecipeName(const std::string& id) {
    const Recipe* r = FindRecipe(id);
    return r != nullptr ? r->name : id.c_str();
}

static std::string ClientName(uint32_t clientId) {
    if (Connected()) {
        auto it = Anchor::Instance->clients.find(clientId);
        if (it != Anchor::Instance->clients.end() && !it->second.name.empty()) {
            return it->second.name;
        }
    }
    return "Someone";
}

// Grottos share rooms between many holes: tell them apart by where the hole is
// (the return point Link climbs back out to).
static uint32_t GrottoVariant() {
    const RespawnData& r = gSaveContext.respawn[RESPAWN_MODE_RETURN];
    uint32_t h = 2166136261u;
    auto mix = [&h](uint32_t v) { h = (h ^ v) * 16777619u; };
    mix((uint32_t)r.entranceIndex);
    mix((uint32_t)(int32_t)(r.pos.x / 16.0f));
    mix((uint32_t)(int32_t)(r.pos.z / 16.0f));
    return (h ^ (h >> 12)) & 0xFFF;
}

static uint32_t CacheKey(uint16_t spot) {
    const CacheSpot& c = GetCacheSpots()[spot];
    uint32_t variant = c.scene == SCENE_GROTTOS ? GrottoVariant() : 0;
    return LootKey(LOOT_CACHE, ((uint32_t)spot << 12) | variant);
}

static uint8_t RollMaterial(uint8_t tier, uint32_t roll) {
    const uint8_t* w = LootWeights(tier);
    uint32_t total = 0;
    for (uint8_t m = 0; m < MAT_COUNT; m++) {
        total += w[m];
    }
    uint32_t r = total > 0 ? roll % total : 0;
    for (uint8_t m = 0; m < MAT_COUNT; m++) {
        if (r < w[m]) {
            return m;
        }
        r -= w[m];
    }
    return MAT_WOOD;
}

static uint8_t HighestTier() {
    uint8_t tier = UNLOCK_START;
    for (uint8_t t = UNLOCK_DEKU_TREE; t <= UNLOCK_SILVER_GAUNTLETS; t++) {
        if (IsUnlocked((Unlock)t)) {
            tier = t;
        }
    }
    return tier;
}

// MARK: - Owner: deciding

static std::string PickBlueprint(const std::vector<const char*>& pool, uint8_t maxTier) {
    for (int pass = 0; pass < 2; pass++) {
        for (const char* id : pool) {
            const Recipe* r = FindRecipe(id);
            if (r == nullptr || HasBlueprint(id) || (pass == 0 && r->unlock > maxTier)) {
                continue;
            }
            return id;
        }
    }
    return "";
}

static void OnLootResult(const nlohmann::json& r);

static void OwnerLoot(const nlohmann::json& req, uint32_t finder) {
    std::string kind = req.value("kind", "");
    uint32_t key = req.value("key", 0u);
    nlohmann::json result;
    result["type"] = LOOT_RESULT;
    result["kind"] = kind;
    result["key"] = key;
    result["finder"] = finder; // not "clientId": Anchor stamps that with the sender
    result["spot"] = req.value("spot", -1);
    result["boss"] = req.value("boss", -1);

    if (key == 0 || (kind != "cache" && kind != "skull" && kind != "boss")) {
        return;
    }
    if (Opened(key)) {
        result["ok"] = false;
        result["reason"] = kind == "cache" ? "Already opened" : "Already paid";
        if (finder == OwnId()) {
            OnLootResult(result);
        } else {
            SendTo(finder, result);
        }
        return;
    }

    BaseState& b = Net::MutableBase();
    PoolState& pool = Net::MutablePool();
    bool firstCache = kind == "cache" && std::none_of(b.lootOpened.begin(), b.lootOpened.end(),
                                                      [](uint32_t k) { return (k >> 28) == LOOT_CACHE; });
    b.lootOpened.push_back(key);

    uint8_t tier = std::min<uint8_t>(req.value("tier", (uint8_t)UNLOCK_START), UNLOCK_SILVER_GAUNTLETS);
    int rolls = 0, minAmount = 1, maxAmount = 2;
    std::string blueprint;
    if (kind == "cache") {
        rolls = 3;
        minAmount = 2;
        maxAmount = 4;
        // The first cache of a save always teaches blueprints; later ones sometimes hold one.
        if (firstCache || Rand_ZeroOne() < 0.45f) {
            blueprint = PickBlueprint(CacheBlueprints(), tier + 1);
        }
        if (blueprint.empty() && !firstCache) {
            rolls += 2; // no blueprint left (or none this time): a fuller bundle
        }
    } else if (kind == "skull") {
        tier = HighestTier();
        blueprint = PickBlueprint(CacheBlueprints(), UNLOCK_SILVER_GAUNTLETS);
        if (blueprint.empty()) {
            static const std::vector<const char*> keys = { "spikes", "stonewall" };
            blueprint = PickBlueprint(keys, UNLOCK_SILVER_GAUNTLETS);
        }
        rolls = blueprint.empty() ? 4 : 1;
        maxAmount = 3;
    } else if (kind == "boss") {
        int16_t boss = req.value("boss", (int16_t)-1);
        const char* own = BossBlueprint(boss);
        if (own != nullptr && !HasBlueprint(own)) {
            blueprint = own;
        } else {
            blueprint = PickBlueprint(CacheBlueprints(), UNLOCK_SILVER_GAUNTLETS);
        }
        tier = std::max(tier, HighestTier());
        rolls = 4;
        minAmount = 2;
        maxAmount = 4;
    }

    uint32_t mats[MAT_COUNT] = {};
    for (int i = 0; i < rolls; i++) {
        uint8_t m = RollMaterial(tier, (uint32_t)(Rand_ZeroOne() * 100000.0f));
        mats[m] += (uint32_t)(minAmount + (int)(Rand_ZeroOne() * (maxAmount - minAmount + 1)) % (maxAmount - minAmount + 1));
    }
    for (uint8_t m = 0; m < MAT_COUNT; m++) {
        pool.materials[m] += mats[m];
    }
    pool.rev++;
    if (!blueprint.empty()) {
        b.blueprints.push_back(blueprint);
    }
    Net::CommitBase();
    Net::BroadcastPool();

    result["ok"] = true;
    result["mats"] = std::vector<uint32_t>(mats, mats + MAT_COUNT);
    result["blueprint"] = blueprint;
    Broadcast(result);
    OnLootResult(result);
    ESYNC_LOG("[Loot] {} key={:#x} finder={} bp='{}' mats={},{},{},{},{}", kind, key, finder, blueprint, mats[0],
              mats[1], mats[2], mats[3], mats[4]);
}

static void RequestLoot(const std::string& kind, uint32_t key, nlohmann::json extra) {
    double now = Now();
    auto it = sRequested.find(key);
    if (it != sRequested.end() && now - it->second < 4.0) {
        return; // still waiting on the owner
    }
    sRequested[key] = now;
    nlohmann::json req = std::move(extra);
    req["type"] = LOOT_REQUEST;
    req["kind"] = kind;
    req["key"] = key;
    if (IsOwner()) {
        OwnerLoot(req, OwnId());
    } else {
        SendTo(ActingOwner(), req);
    }
}

// MARK: - Everyone: the result

static void OnLootResult(const nlohmann::json& r) {
    std::string kind = r.value("kind", "");
    uint32_t key = r.value("key", 0u);
    sRequested.erase(key);
    bool mine = r.value("finder", 0u) == OwnId();
    int spot = r.value("spot", -1);
    if (spot >= 0) {
        auto it = sCaches.find((uint16_t)spot);
        if (it != sCaches.end() && r.value("ok", false)) {
            ((CacheActor*)it->second)->lidTimer = 20;
        }
    }
    if (!r.value("ok", false)) {
        if (mine && kind == "cache") {
            Notification::Emit(
                { .prefix = "Supply cache", .message = r.value("reason", "Already opened"), .remainingTime = 3.0f });
            Sfx_PlaySfxCentered(NA_SE_SY_ERROR);
        }
        return;
    }
    std::string blueprint = r.value("blueprint", "");
    auto mats = r.value("mats", std::vector<uint32_t>{});
    if (!mine) {
        std::string what = kind == "cache" ? "opened a supply cache" : kind == "boss" ? "brought down a boss" : "turned in tokens";
        Notification::Emit({ .prefix = ClientName(r.value("finder", 0u)), .message = what, .remainingTime = 3.0f, .mute = true });
        if (!blueprint.empty()) {
            Notification::Emit({ .prefix = "Blueprint",
                                 .prefixColor = ImVec4(0.55f, 0.8f, 1.0f, 1.0f),
                                 .message = RecipeName(blueprint),
                                 .remainingTime = 5.0f,
                                 .mute = true });
        }
        return;
    }

    std::string title = kind == "cache"  ? "Supply cache"
                        : kind == "boss" ? fmt::format("{} falls", BossName(r.value("boss", (int16_t)-1))
                                                                       ? BossName(r.value("boss", (int16_t)-1))
                                                                       : "The boss")
                                         : fmt::format("{} Gold Skulltula tokens", (key & 0xFFFF) * 10);
    Notification::Emit({ .prefix = title, .message = "", .remainingTime = 4.0f, .mute = true });
    for (uint8_t m = 0; m < MAT_COUNT && m < mats.size(); m++) {
        if (mats[m] > 0) {
            Notification::Emit({ .prefix = fmt::format("+{}", mats[m]),
                                 .prefixColor = ImVec4(0.6f, 1.0f, 0.6f, 1.0f),
                                 .message = GetMaterialInfo(m).name,
                                 .remainingTime = 4.0f,
                                 .mute = true });
        }
    }
    if (!blueprint.empty()) {
        Notification::Emit({ .prefix = "Blueprint",
                             .prefixColor = ImVec4(0.55f, 0.8f, 1.0f, 1.0f),
                             .message = fmt::format("{} (craft it at the workbench)", RecipeName(blueprint)),
                             .remainingTime = 6.0f,
                             .mute = true });
        QueueLootNavi(LOOTLINE_BLUEPRINT);
    }
    Sfx_PlaySfxCentered(blueprint.empty() ? NA_SE_SY_GET_ITEM : NA_SE_SY_CORRECT_CHIME);
    if (kind == "cache") {
        QueueLootNavi(LOOTLINE_CACHE);
    }
}

bool LootOwnsPacket(const std::string& type) {
    return type == LOOT_REQUEST || type == LOOT_RESULT;
}

void LootHandlePacket(const std::string& type, const nlohmann::json& payload, uint32_t from) {
    if (!LootEnabled()) {
        return;
    }
    if (type == LOOT_REQUEST) {
        if (IsOwner()) {
            OwnerLoot(payload, from);
        } else {
            nlohmann::json r;
            r["type"] = LOOT_RESULT;
            r["kind"] = payload.value("kind", "");
            r["key"] = payload.value("key", 0u);
            r["finder"] = from;
            r["ok"] = false;
            r["reason"] = "The host changed, try again";
            SendTo(from, r);
        }
    } else if (type == LOOT_RESULT) {
        if (from == ActingOwner() || from == OwnId()) {
            OnLootResult(payload);
        }
    }
}

// MARK: - Pots and crates

static void OnBreakable(Actor* actor) {
    if (gPlayState == nullptr || actor->init != NULL) {
        return; // init-time kills (already-broken objects removing themselves) don't pay
    }
    const RoomContext& rooms = gPlayState->roomCtx;
    if (actor->room >= 0 && actor->room != rooms.curRoom.num && actor->room != rooms.prevRoom.num) {
        return; // a room unloading behind a door kills its pots (func_80031B14): nothing broke
    }
    Player* player = GET_PLAYER(gPlayState);
    if (player == nullptr || Math_Vec3f_DistXYZ(&actor->world.pos, &player->actor.world.pos) > 700.0f) {
        return; // broken by someone else far away, or culled: only what we break pays
    }
    bool crate = actor->id != ACTOR_OBJ_TSUBO;
    uint64_t key = SourceKeyFor(actor, 0x7075u);
    // The roll comes from the pot's key: every client (and a retry) rolls the same.
    uint64_t h = key * 0x9E3779B97F4A7C15ull;
    h ^= h >> 29;
    uint32_t chance = (uint32_t)(h % 100), pick = (uint32_t)((h >> 8) % 100000), amt = (uint32_t)((h >> 32) % 3);
    if (chance >= (crate ? 75u : 55u)) {
        return; // empty
    }
    uint8_t tier = SceneTier(gPlayState->sceneNum);
    uint8_t material = RollMaterial(tier, pick);
    uint32_t amount = crate ? 1 + amt : 1 + (amt == 2 ? 1 : 0);
    SendGather(material, amount, key, 600); // a pot pays once per 10 minutes (rooms reload them)
    // Navi's pot line comes with the credit (SevenDays.cpp OnCredited), once the owner has paid.
}

// MARK: - Supply cache actor

static ColliderCylinderInit sCacheCylinderInit = {
    { COLTYPE_NONE, AT_NONE, AC_NONE, OC1_ON | OC1_TYPE_ALL, OC2_TYPE_2, COLSHAPE_CYLINDER },
    { ELEMTYPE_UNK0, { 0x00000000, 0x00, 0x00 }, { 0x00000000, 0x00, 0x00 }, TOUCH_NONE, BUMP_NONE, OCELEM_ON },
    { 24, 34, 0, { 0, 0, 0 } },
};

// Beside the anchor: a floor point 70-110 units away on the anchor's level, not
// water, not too steep, with nothing solid between the two.
static Vec3f PlaceBeside(PlayState* play, const CacheSpot& c) {
    Vec3f anchor = { (f32)c.x, (f32)c.y, (f32)c.z };
    for (f32 r : { 75.0f, 110.0f, 45.0f }) {
        for (int k = 0; k < 8; k++) {
            s16 angle = (s16)(k * 0x2000);
            Vec3f cand = { anchor.x + Math_SinS(angle) * r, anchor.y, anchor.z + Math_CosS(angle) * r };
            Vec3f probe = { cand.x, anchor.y + 60.0f, cand.z };
            CollisionPoly* poly = nullptr;
            s32 bgId = BGCHECK_SCENE;
            f32 floorY = BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, &probe);
            if (floorY <= BGCHECK_Y_MIN || poly == nullptr || bgId != BGCHECK_SCENE || fabsf(floorY - anchor.y) > 40.0f) {
                continue;
            }
            if (COLPOLY_GET_NORMAL(poly->normal.y) < 0.85f) {
                continue;
            }
            f32 waterY;
            WaterBox* waterBox;
            if (WaterBox_GetSurface1(play, &play->colCtx, cand.x, cand.z, &waterY, &waterBox) && waterY > floorY) {
                continue;
            }
            Vec3f from = { anchor.x, anchor.y + 30.0f, anchor.z }, to = { cand.x, floorY + 30.0f, cand.z }, hit;
            CollisionPoly* wall = nullptr;
            if (BgCheck_AnyLineTest1(&play->colCtx, &from, &to, &hit, &wall, true)) {
                continue;
            }
            // Room to stand: nothing solid just above it either.
            Vec3f up = { cand.x, floorY + 5.0f, cand.z }, top = { cand.x, floorY + 60.0f, cand.z };
            if (BgCheck_AnyLineTest1(&play->colCtx, &up, &top, &hit, &wall, true)) {
                continue;
            }
            return { cand.x, floorY, cand.z };
        }
    }
    return anchor;
}

static void Cache_Init(Actor* thisx, PlayState* play) {
    CacheActor* self = (CacheActor*)thisx;
    self->spot = (uint16_t)thisx->params;
    if (self->spot >= GetCacheSpots().size()) {
        self->spot = 0xFFFF;
        Actor_Kill(thisx);
        return;
    }
    const CacheSpot& c = GetCacheSpots()[self->spot];
    self->key = CacheKey(self->spot);
    self->talking = 0;
    self->lidTimer = 0;
    self->opened = Opened(self->key);
    thisx->room = c.room;
    Vec3f at = PlaceBeside(play, c);
    thisx->world.pos = thisx->home.pos = thisx->prevPos = at;
    // Face the anchor's room: turn the lid's hinge away from the wall we came off.
    Vec3f anchor = { (f32)c.x, (f32)c.y, (f32)c.z };
    thisx->shape.rot.y = thisx->world.rot.y = Math_Vec3f_Yaw(&at, &anchor) + 0x8000;
    Actor_SetScale(thisx, 1.0f);
    Collider_InitCylinder(play, &self->collider);
    Collider_SetCylinder(play, &self->collider, thisx, &sCacheCylinderInit);
    thisx->colChkInfo.mass = MASS_IMMOVABLE;
    thisx->flags |= ACTOR_FLAG_ATTENTION_ENABLED | ACTOR_FLAG_FRIENDLY;
    thisx->targetMode = 0;
    thisx->textId = self->opened ? TEXT_CACHE_EMPTY : TEXT_CACHE;
    Actor_SetFocus(thisx, 30.0f);
    sCaches[self->spot] = thisx;
    ESYNC_LOG("[Loot] cache {} key={:#x} at ({:.0f},{:.0f},{:.0f}) opened={}", self->spot, self->key, at.x, at.y, at.z,
              self->opened);
}

static void Cache_Destroy(Actor* thisx, PlayState* play) {
    CacheActor* self = (CacheActor*)thisx;
    if (self->spot == 0xFFFF) {
        return;
    }
    Collider_DestroyCylinder(play, &self->collider);
    auto it = sCaches.find(self->spot);
    if (it != sCaches.end() && it->second == thisx) {
        sCaches.erase(it);
    }
}

static void RequestOpenCache(uint16_t spot) {
    if (spot >= GetCacheSpots().size()) {
        return;
    }
    const CacheSpot& c = GetCacheSpots()[spot];
    nlohmann::json extra;
    extra["spot"] = spot;
    extra["tier"] = c.tier;
    extra["scene"] = c.scene;
    RequestLoot("cache", CacheKey(spot), extra);
}

static void Cache_Update(Actor* thisx, PlayState* play) {
    CacheActor* self = (CacheActor*)thisx;
    bool opened = Opened(self->key);
    if (opened && !self->opened) {
        self->lidTimer = std::max<int16_t>(self->lidTimer, 1);
    }
    self->opened = opened;
    if (self->lidTimer > 0 && self->lidTimer < 20) {
        self->lidTimer++;
    }
    if (self->talking) {
        if (Actor_TextboxIsClosing(thisx, play) || play->msgCtx.msgMode == MSGMODE_NONE) {
            self->talking = 0;
            if (!opened) {
                RequestOpenCache(self->spot); // pried open: the owner decides what's inside
            }
        }
    } else if (Actor_ProcessTalkRequest(thisx, play)) {
        self->talking = 1;
    } else if (!InPlacement()) {
        thisx->textId = opened ? TEXT_CACHE_EMPTY : TEXT_CACHE;
        func_8002F2CC(thisx, play, 70.0f); // "Check" on A when Link is close
    }
    Collider_UpdateCylinder(thisx, &self->collider);
    CollisionCheck_SetOC(play, &play->colChkCtx, &self->collider.base);
}

static void DrawBoxDL(PlayState* play, float tx, float ty, float tz, float sx, float sy, float sz, float rx) {
    OPEN_DISPS(play->state.gfxCtx);
    Matrix_Push();
    Matrix_Translate(tx, ty, tz, MTXMODE_APPLY);
    if (rx != 0.0f) {
        Matrix_RotateX(rx, MTXMODE_APPLY);
    }
    Matrix_Scale(sx, sy, sz, MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_OPA_DISP++, (Gfx*)gSmallWoodenBoxDL);
    Matrix_Pop();
    CLOSE_DISPS(play->state.gfxCtx);
}

// A unit box (x/z in -1..1, y in 0..1) for the cache's brass bands.
static Vtx sBandVtx[8];
static bool sBandBuilt = false;

static void DrawBand(PlayState* play, f32 tx, f32 ty, f32 tz, f32 sx, f32 sy, f32 sz, u8 r, u8 g, u8 b) {
    if (!sBandBuilt) {
        sBandBuilt = true;
        static const s16 c[8][3] = { { -1, 0, -1 }, { 1, 0, -1 }, { 1, 0, 1 }, { -1, 0, 1 },
                                     { -1, 1, -1 }, { 1, 1, -1 }, { 1, 1, 1 }, { -1, 1, 1 } };
        for (int i = 0; i < 8; i++) {
            memset(&sBandVtx[i], 0, sizeof(Vtx));
            sBandVtx[i].v.ob[0] = c[i][0];
            sBandVtx[i].v.ob[1] = c[i][1];
            sBandVtx[i].v.ob[2] = c[i][2];
            sBandVtx[i].v.cn[0] = sBandVtx[i].v.cn[1] = sBandVtx[i].v.cn[2] = sBandVtx[i].v.cn[3] = 255;
        }
    }
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    Matrix_Push();
    Matrix_Translate(tx, ty, tz, MTXMODE_APPLY);
    Matrix_Scale(sx, sy, sz, MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gDPPipeSync(POLY_OPA_DISP++);
    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gSPClearGeometryMode(POLY_OPA_DISP++, G_CULL_BOTH | G_LIGHTING | G_TEXTURE_GEN | G_FOG);
    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, r, g, b, 255);
    gSPVertex(POLY_OPA_DISP++, (uintptr_t)sBandVtx, 8, 0);
    gSP2Triangles(POLY_OPA_DISP++, 4, 7, 6, 0, 4, 6, 5, 0); // top
    gSP2Triangles(POLY_OPA_DISP++, 0, 4, 5, 0, 0, 5, 1, 0); // -z
    gSP2Triangles(POLY_OPA_DISP++, 3, 2, 6, 0, 3, 6, 7, 0); // +z
    gSP2Triangles(POLY_OPA_DISP++, 0, 3, 7, 0, 0, 7, 4, 0); // -x
    gSP2Triangles(POLY_OPA_DISP++, 1, 5, 6, 0, 1, 6, 2, 0); // +x
    Matrix_Pop();
    CLOSE_DISPS(play->state.gfxCtx);
}

static void Cache_Draw(Actor* thisx, PlayState* play) {
    CacheActor* self = (CacheActor*)thisx;
    bool opened = self->opened;
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    // A banded supply crate: gold while sealed, weathered once emptied.
    if (opened) {
        gDPSetGrayscaleColor(POLY_OPA_DISP++, 120, 100, 80, 200);
    } else {
        gDPSetGrayscaleColor(POLY_OPA_DISP++, 255, 205, 80, 170);
    }
    gSPGrayscale(POLY_OPA_DISP++, true);
    CLOSE_DISPS(play->state.gfxCtx);

    // The body (a small crate, a little wider than tall) and its lid, which swings
    // open when the cache is opened.
    DrawBoxDL(play, 0.0f, 0.0f, 0.0f, 0.17f, 0.13f, 0.14f, 0.0f);
    float swing = opened ? (self->lidTimer > 0 ? std::min(1.0f, self->lidTimer / 20.0f) : 1.0f) : 0.0f;
    DrawBoxDL(play, 0.0f, 26.0f + swing * 6.0f, -14.0f * swing, 0.18f, 0.03f, 0.15f, -1.1f * swing);

    OPEN_DISPS(play->state.gfxCtx);
    gSPGrayscale(POLY_OPA_DISP++, false);
    CLOSE_DISPS(play->state.gfxCtx);

    // Brass bands: two straps over the body and one around it (dull once emptied).
    u8 br = opened ? 110 : 235, bg = opened ? 90 : 185, bb = opened ? 50 : 60;
    DrawBand(play, -9.0f, -0.2f, 0.0f, 1.6f, 26.6f, 14.8f, br, bg, bb);
    DrawBand(play, 9.0f, -0.2f, 0.0f, 1.6f, 26.6f, 14.8f, br, bg, bb);
    DrawBand(play, 0.0f, 10.0f, 0.0f, 17.7f, 3.0f, 14.8f, br, bg, bb);

    if (!opened && (play->gameplayFrames % 24) == 0) {
        // A glint now and then, so a sealed cache catches the eye in a dark room.
        Vec3f pos = { thisx->world.pos.x + Rand_CenteredFloat(30.0f), thisx->world.pos.y + 34.0f,
                      thisx->world.pos.z + Rand_CenteredFloat(30.0f) };
        Vec3f vel = { 0.0f, 0.4f, 0.0f }, accel = { 0.0f, 0.0f, 0.0f };
        Color_RGBA8 prim = { 255, 255, 200, 255 }, env = { 255, 200, 0, 0 };
        EffectSsKiraKira_SpawnSmall(play, &pos, &vel, &accel, &prim, &env);
    }
}

static void RegisterCacheActor() {
    if (sCacheActorId >= 0 || ActorDB::Instance == nullptr) {
        return;
    }
    ActorDBInit cache;
    cache.name = "SevenDays_Cache";
    cache.desc = "7 Days to Zelda supply cache (opened once per save)";
    cache.category = ACTORCAT_PROP;
    cache.flags = ACTOR_FLAG_UPDATE_CULLING_DISABLED;
    cache.objectId = OBJECT_GAMEPLAY_KEEP;
    cache.instanceSize = sizeof(CacheActor);
    cache.init = Cache_Init;
    cache.destroy = Cache_Destroy;
    cache.update = Cache_Update;
    cache.draw = Cache_Draw;
    sCacheActorId = (int16_t)ActorDB::Instance->AddEntry(cache).entry.id;
}

// Spawn this scene's caches for the rooms that are loaded (each belongs to its
// room, so it unloads with it).
static void SpawnCachesHere() {
    if (gPlayState == nullptr || !LootEnabled()) {
        return;
    }
    RegisterCacheActor();
    if (sCacheActorId < 0) {
        return;
    }
    const auto& spots = GetCacheSpots();
    s8 cur = gPlayState->roomCtx.curRoom.num, prev = gPlayState->roomCtx.prevRoom.num;
    for (uint16_t i = 0; i < spots.size(); i++) {
        const CacheSpot& c = spots[i];
        if (c.scene != gPlayState->sceneNum || (c.room != cur && c.room != prev) || sCaches.contains(i)) {
            continue;
        }
        Actor* a = Actor_Spawn(&gPlayState->actorCtx, gPlayState, sCacheActorId, c.x, c.y, c.z, 0, 0, 0, (s16)i, false);
        if (a != nullptr) {
            a->room = c.room;
        }
    }
}

// MARK: - Per frame

void LootOnFrame() {
    if (gPlayState == nullptr || !GameInteractor::IsSaveLoaded(true)) {
        return;
    }
    // Gold Skulltula tokens: a blueprint every 10 (the owner pays each ten once).
    int16_t tokens = gSaveContext.inventory.gsTokens;
    for (uint32_t ten = 1; ten <= (uint32_t)std::max<int16_t>(0, tokens) / 10 && ten <= 10; ten++) {
        uint32_t key = LootKey(LOOT_SKULL, ten);
        if (!Opened(key)) {
            RequestLoot("skull", key, nlohmann::json::object());
            break;
        }
    }
    // Navi's tier lines, when the item or boss lands (tiers already open when the
    // file loaded are marked said without a word).
    if (++sTierPoll >= 20) {
        sTierPoll = 0;
        // Bosses beaten before this save had M7 (an M6 save) never fired OnBossDefeat
        // here: pay their reward once, or their key blueprint (spike strip, stone wall)
        // would lock a recipe the save could already craft. The owner's lootOpened
        // dedupes against the live OnBossDefeat path.
        static const struct {
            int16_t boss;
            bool (*beaten)();
        } kBeaten[] = {
            { ACTOR_BOSS_GOMA, []() { return IsUnlocked(UNLOCK_DEKU_TREE); } },
            { ACTOR_BOSS_DODONGO, []() { return (bool)CHECK_QUEST_ITEM(QUEST_GORON_RUBY); } },
            { ACTOR_BOSS_VA, []() { return (bool)CHECK_QUEST_ITEM(QUEST_ZORA_SAPPHIRE); } },
        };
        for (const auto& b : kBeaten) {
            uint32_t key = LootKey(LOOT_BOSS, (uint32_t)(uint16_t)b.boss);
            if (!Opened(key) && b.beaten()) {
                nlohmann::json extra;
                extra["boss"] = b.boss;
                extra["tier"] = (uint8_t)UNLOCK_START;
                RequestLoot("boss", key, extra);
                break;
            }
        }
        for (uint8_t t = UNLOCK_DEKU_TREE; t <= UNLOCK_SILVER_GAUNTLETS; t++) {
            if (IsUnlocked((Unlock)t)) {
                QueueLootNavi(LOOTLINE_TIER_DEKU + (t - UNLOCK_DEKU_TREE), !sTierBaseline);
            }
        }
        sTierBaseline = true;
    }
}

void LootResetSession() {
    sRequested.clear();
    sTierBaseline = false;
    sTierPoll = 0;
}

// MARK: - Navi

void LootRegisterMessages(const char* table) {
    // clang-format off
    static const char* lines[LOOTLINE_COUNT] = {
        /* BLUEPRINT */ "A blueprint! Now the workbench knows how to build something new.^Blueprints belong to everyone in the village, Link!",
        /* CACHE     */ "A supply cache! Someone hid these for hard times.^Each one only opens once, so take everything!",
        /* POT       */ "Hey, there were scraps in that pot!^Pots and crates can hold materials too.",
        /* DEKU      */ "The Deku Tree is gone... and the dead are stirring.^Their bones and rot can be built into spikes!",
        /* BOMB      */ "Link! Bombs crack stone. Stone walls hold twice as long as wood!",
        /* HOOKSHOT  */ "With the Hookshot, nothing on a ledge is out of reach!^We can build a gate that only opens for us, too!",
        /* HAMMER    */ "That hammer can rebuild walls stronger: wood to stone to iron!",
        /* SILVER    */ "Silver Gauntlets! Those silver rocks will make iron walls!",
    };
    // clang-format on
    for (uint8_t i = 0; i < LOOTLINE_COUNT; i++) {
        CustomMessageManager::Instance->CreateMessage(table, TEXT_LOOT_BASE + i,
                                                      CustomMessage(lines[i], TEXTBOX_TYPE_BLUE, TEXTBOX_POS_BOTTOM));
    }
    CustomMessageManager::Instance->CreateMessage(
        table, TEXT_CACHE,
        CustomMessage("A supply cache, nailed shut and banded in brass.^You pry the lid open...", TEXTBOX_TYPE_BLACK,
                      TEXTBOX_POS_BOTTOM));
    CustomMessageManager::Instance->CreateMessage(
        table, TEXT_CACHE_EMPTY,
        CustomMessage("An empty supply cache. Someone already took everything.", TEXTBOX_TYPE_BLACK,
                      TEXTBOX_POS_BOTTOM));
}

// MARK: - Registration

void LootRegisterHooks(bool enabled) {
    COND_HOOK(OnSceneSpawnActors, enabled, []() { SpawnCachesHere(); });
    COND_HOOK(OnActorKill, enabled, [](void* refActor) {
        Actor* actor = (Actor*)refActor;
        if (actor->id == ACTOR_OBJ_TSUBO || actor->id == ACTOR_OBJ_KIBAKO || actor->id == ACTOR_OBJ_KIBAKO2) {
            OnBreakable(actor);
        }
    });
    COND_HOOK(OnBossDefeat, enabled, [](void* refActor) {
        Actor* actor = (Actor*)refActor;
        if (BossName(actor->id) == nullptr) {
            return;
        }
        nlohmann::json extra;
        extra["boss"] = actor->id;
        extra["tier"] = SceneTier(gPlayState != nullptr ? gPlayState->sceneNum : 0);
        // Volvagia's two halves share one key, as do Ganon's.
        int16_t id = actor->id == ACTOR_BOSS_FD ? ACTOR_BOSS_FD2 : actor->id == ACTOR_BOSS_GANON ? ACTOR_BOSS_GANON2 : actor->id;
        RequestLoot("boss", LootKey(LOOT_BOSS, (uint32_t)(uint16_t)id), extra);
    });
    if (enabled) {
        SpawnCachesHere();
    } else {
        for (auto& [spot, actor] : sCaches) {
            Actor_Kill(actor);
        }
        sCaches.clear();
    }
}

} // namespace SevenDays

// MARK: - Test hooks

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
using namespace SevenDays;
// For tools/webtest and the live playthrough. "open" opens the nearest spawned
// cache through the same request the A-press sends; "open:N" a given spot;
// "boss:ACTORID" runs the boss-defeat reward; "tokens:N" sets the token count;
// "warp:ENTRANCE" changes scene; "spawn:pot" / "spawn:crate" put a breakable
// pot or crate in front of Link (call again once its object has loaded).
extern "C" {
EMSCRIPTEN_KEEPALIVE
const char* sevendays_test_loot_state() {
    static std::string out;
    nlohmann::json j;
    const BaseState& b = GetBase();
    j["enabled"] = LootEnabled();
    j["blueprints"] = b.blueprints;
    j["lootOpened"] = b.lootOpened;
    j["tokens"] = gSaveContext.inventory.gsTokens;
    j["owner"] = Net::IsOwner();
    j["pool"] = std::vector<uint32_t>(GetPool().materials, GetPool().materials + MAT_COUNT);
    j["scene"] = gPlayState != nullptr ? gPlayState->sceneNum : -1;
    j["room"] = gPlayState != nullptr ? gPlayState->roomCtx.curRoom.num : -1;
    j["caches"] = nlohmann::json::array();
    j["recipes"] = nlohmann::json::object();
    for (auto& r : GetRecipes()) {
        if (r.blueprint) {
            j["recipes"][r.id] = CraftBlocker(r);
        }
    }
    j["lines"] = nlohmann::json::array();
    for (uint8_t i = 0; i < LOOTLINE_COUNT; i++) {
        j["lines"].push_back(LootLineSaid(i));
    }
    if (gPlayState != nullptr) {
        Player* player = GET_PLAYER(gPlayState);
        for (auto& [spot, a] : sCaches) {
            CacheActor* c = (CacheActor*)a;
            j["caches"].push_back({ { "spot", spot },
                                    { "key", c->key },
                                    { "opened", Opened(c->key) },
                                    { "room", a->room },
                                    { "pos", { (int)a->world.pos.x, (int)a->world.pos.y, (int)a->world.pos.z } },
                                    { "yaw", a->shape.rot.y },
                                    { "dist", player != nullptr ? (int)Math_Vec3f_DistXYZ(&a->world.pos, &player->actor.world.pos) : -1 } });
        }
        int n = 0;
        for (Actor* a = gPlayState->actorCtx.actorLists[ACTORCAT_PROP].head; a != nullptr; a = a->next) {
            n += a->id == ACTOR_OBJ_TSUBO || a->id == ACTOR_OBJ_KIBAKO || a->id == ACTOR_OBJ_KIBAKO2;
        }
        j["breakables"] = n;
        if (player != nullptr) {
            j["link"] = { (int)player->actor.world.pos.x, (int)player->actor.world.pos.y, (int)player->actor.world.pos.z };
        }
    }
    out = j.dump();
    return out.c_str();
}

EMSCRIPTEN_KEEPALIVE
const char* sevendays_test_loot(const char* cmdC) {
    static std::string out;
    out = "";
    if (!LootEnabled() || gPlayState == nullptr) {
        return "off";
    }
    std::string cmd = cmdC;
    Player* player = GET_PLAYER(gPlayState);
    if (cmd == "open") {
        f32 best = 1e9f;
        int spot = -1;
        for (auto& [s, a] : sCaches) {
            f32 d = Math_Vec3f_DistXYZ(&a->world.pos, &player->actor.world.pos);
            if (d < best) {
                best = d;
                spot = s;
            }
        }
        if (spot >= 0) {
            RequestOpenCache((uint16_t)spot);
            out = std::to_string(spot);
        } else {
            out = "none";
        }
    } else if (cmd.rfind("open:", 0) == 0) {
        RequestOpenCache((uint16_t)std::stoi(cmd.substr(5)));
        out = "ok";
    } else if (cmd.rfind("boss:", 0) == 0) {
        int16_t id = (int16_t)std::stoi(cmd.substr(5));
        nlohmann::json extra;
        extra["boss"] = id;
        extra["tier"] = SceneTier(gPlayState->sceneNum);
        RequestLoot("boss", LootKey(LOOT_BOSS, (uint32_t)(uint16_t)id), extra);
        out = "ok";
    } else if (cmd.rfind("tokens:", 0) == 0) {
        gSaveContext.inventory.gsTokens = (s16)std::stoi(cmd.substr(7));
        out = "ok";
    } else if (cmd.rfind("warp:", 0) == 0) {
        gPlayState->nextEntranceIndex = (s16)std::stoi(cmd.substr(5), nullptr, 0);
        gPlayState->transitionTrigger = TRANS_TRIGGER_START;
        gPlayState->transitionType = TRANS_TYPE_FADE_BLACK;
        out = "ok";
    } else if (cmd.rfind("room:", 0) == 0) {
        // Load another room of this scene (the way a door does) so Link can be
        // warped into it: call once to request, again until it answers "ready".
        int room = std::stoi(cmd.substr(5));
        if (gPlayState->roomCtx.curRoom.num == room) {
            out = "ready";
        } else if (gPlayState->roomCtx.status == 0) {
            func_8009728C(gPlayState, &gPlayState->roomCtx, room);
            out = "loading";
        } else if (func_800973FC(gPlayState, &gPlayState->roomCtx)) {
            func_80097534(gPlayState, &gPlayState->roomCtx);
            out = gPlayState->roomCtx.curRoom.num == room ? "ready" : "swapped";
        } else {
            out = "loading";
        }
    } else if (cmd == "spawn:pot" || cmd == "spawn:crate") {
        bool pot = cmd == "spawn:pot";
        s16 objectId = pot ? OBJECT_TSUBO : OBJECT_GAMEPLAY_DANGEON_KEEP;
        s32 bank = Object_GetIndex(&gPlayState->objectCtx, objectId);
        if (bank < 0) {
            Object_Spawn(&gPlayState->objectCtx, objectId);
            return "loading";
        }
        if (!Object_IsLoaded(&gPlayState->objectCtx, bank)) {
            return "loading";
        }
        Vec3f at = player->actor.world.pos;
        at.x += Math_SinS(player->actor.shape.rot.y) * 60.0f;
        at.z += Math_CosS(player->actor.shape.rot.y) * 60.0f;
        // Pot params: bit 8 picks object_tsubo; crate params 0xFFFF: no drop, no flag.
        Actor* a = Actor_Spawn(&gPlayState->actorCtx, gPlayState, pot ? ACTOR_OBJ_TSUBO : ACTOR_OBJ_KIBAKO, at.x,
                               at.y, at.z, 0, 0, 0, pot ? 0x0100 : (s16)0xFFFF, false);
        out = a != nullptr ? "spawned" : "failed";
    }
    return out.c_str();
}
}
#endif
