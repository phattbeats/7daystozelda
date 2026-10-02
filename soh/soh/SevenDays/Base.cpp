#include "SevenDays.h"
#include "SevenDaysNet.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"
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
extern PlayState* gPlayState;
}

/**
 * M5: the base. Placeables live in one record, BaseState, decided by the room
 * owner (the same authority as the material pool) and stored in every member's
 * save ("sevenDays" section v2) and in Anchor's team state for late joiners.
 *
 * Packets (JSON):
 *   PLACE_REQUEST  builder -> owner    reqId, type, scene, room, pos, rot, era
 *   PLACE_RESULT   owner   -> builder  reqId, ok, reason
 *   PACK_REQUEST   player  -> owner    id (one piece) or era + all (the whole base)
 *   BASE_DELTA     owner   -> room     rev + op (add / remove / hp) [+ center]
 *   BASE_STATE     owner   -> room/joiner  the whole base
 *   BASE_REQUEST   joiner  -> owner    the joiner's cached copy; higher rev wins
 *   BASE_HP        enemy authority -> owner  id, hp (owner sequences it into a BASE_DELTA)
 *
 * Every client spawns its own copy of each placeable on OnSceneSpawnActors
 * (room -1, keyed by the stable id); only add/remove/HP events travel.
 */

namespace SevenDays {

using Net::ActingOwner;
using Net::Broadcast;
using Net::Connected;
using Net::IsOwner;
using Net::Now;
using Net::OwnId;
using Net::SendTo;

static const std::string PLACE_REQUEST = "PLACE_REQUEST";
static const std::string PLACE_RESULT = "PLACE_RESULT";
static const std::string PACK_REQUEST = "PACK_REQUEST";
static const std::string BASE_DELTA = "BASE_DELTA";
static const std::string BASE_STATE = "BASE_STATE";
static const std::string BASE_REQUEST = "BASE_REQUEST";
static const std::string BASE_HP = "BASE_HP";

static BaseState sBase;
static std::unordered_map<uint16_t, Actor*> sSpawned; // placeable id -> actor in the current scene

struct InFlightPlace {
    uint32_t reqId = 0;
    double sentAt = 0;
};
static InFlightPlace sPlaceInFlight;
static uint32_t sNextPlaceReq = 1;
static double sLastBaseRequest = -100.0;

const BaseState& GetBase() {
    return sBase;
}

// MARK: - Data

// clang-format off
static const PlaceableInfo sPlaceables[PLACEABLE_COUNT] = {
    //                         kit          name              halfX halfZ height maxHp
    /* PLACEABLE_BARRICADE */ { "barricade", "Barricade",      60,   24,   48,    100 },
    /* PLACEABLE_SPIKES    */ { "spikes",    "Spike strip",    45,   15,   8,     60  },
    /* PLACEABLE_WORKBENCH */ { "workbench", "Workbench",      30,   24,   48,    0   },
    /* PLACEABLE_CHEST     */ { "chest",     "Storage chest",  20,   20,   40,    0   },
    /* PLACEABLE_SIGN      */ { "",          "Sign",           20,   5,    60,    0   },
};
// clang-format on

const PlaceableInfo& GetPlaceableInfo(uint8_t type) {
    return sPlaceables[type < PLACEABLE_COUNT ? type : 0];
}

int FindPlaceableTypeForKit(const std::string& kit) {
    for (int i = 0; i < PLACEABLE_COUNT; i++) {
        if (sPlaceables[i].kit[0] != '\0' && kit == sPlaceables[i].kit) {
            return i;
        }
    }
    return -1;
}

// Outdoor scenes a base can stand in: the ones the raid clock runs in (spec,
// "Bases go anywhere outdoors"). Dungeons, interiors and the Market stay frozen.
struct BaseScene {
    int16_t scene;
    const char* name;
};
static const BaseScene sBaseScenes[] = {
    { SCENE_HYRULE_FIELD, "Hyrule Field" },
    { SCENE_KOKIRI_FOREST, "Kokiri Forest" },
    { SCENE_LOST_WOODS, "the Lost Woods" },
    { SCENE_SACRED_FOREST_MEADOW, "the Sacred Forest Meadow" },
    { SCENE_LON_LON_RANCH, "Lon Lon Ranch" },
    { SCENE_KAKARIKO_VILLAGE, "Kakariko Village" },
    { SCENE_GRAVEYARD, "the Graveyard" },
    { SCENE_LAKE_HYLIA, "Lake Hylia" },
    { SCENE_GERUDO_VALLEY, "Gerudo Valley" },
    { SCENE_GERUDOS_FORTRESS, "Gerudo's Fortress" },
    { SCENE_DESERT_COLOSSUS, "the Desert Colossus" },
    { SCENE_ZORAS_RIVER, "Zora's River" },
    { SCENE_DEATH_MOUNTAIN_TRAIL, "Death Mountain Trail" },
    { SCENE_HYRULE_CASTLE, "Hyrule Castle" },
};

static const char* BaseSceneName(int16_t scene) {
    for (auto& s : sBaseScenes) {
        if (s.scene == scene) {
            return s.name;
        }
    }
    return nullptr;
}

bool IsOutdoorScene(int16_t scene) {
    return BaseSceneName(scene) != nullptr;
}

const char* OutdoorSceneName(int16_t scene) {
    const char* name = BaseSceneName(scene);
    return name != nullptr ? name : "?";
}

static int CurrentEra() {
    return gSaveContext.linkAge == ERA_ADULT ? ERA_ADULT : ERA_CHILD;
}

int CurrentEraNow() {
    return CurrentEra();
}

static Placeable* FindPlaceableMut(uint16_t id) {
    for (auto& p : sBase.placeables) {
        if (p.id == id) {
            return &p;
        }
    }
    return nullptr;
}

const Placeable* FindPlaceable(uint16_t id) {
    return FindPlaceableMut(id);
}

static int CountEra(int era) {
    int n = 0;
    for (auto& p : sBase.placeables) {
        n += p.era == era;
    }
    return n;
}

uint32_t CurrentDay() {
    return sBase.daysSurvived + 1;
}

// MARK: - JSON (packets, save section and team state share one format)

static nlohmann::json PlaceableToJson(const Placeable& p) {
    return { { "id", p.id },     { "type", p.type },
             { "era", p.era },   { "scene", p.scene },
             { "pos", { p.pos[0], p.pos[1], p.pos[2] } },
             { "rot", p.rot },   { "hp", p.hp } };
}

static Placeable PlaceableFromJson(const nlohmann::json& j) {
    Placeable p;
    p.id = j.value("id", (uint16_t)0);
    p.type = j.value("type", (uint8_t)0);
    p.era = j.value("era", (uint8_t)ERA_CHILD);
    p.scene = j.value("scene", (int16_t)0);
    auto pos = j.value("pos", nlohmann::json::array());
    for (int i = 0; i < 3; i++) {
        p.pos[i] = i < (int)pos.size() ? pos[i].get<float>() : 0.0f;
    }
    p.rot = j.value("rot", (int16_t)0);
    p.hp = j.value("hp", (uint16_t)0);
    if (p.type >= PLACEABLE_COUNT) {
        p.type = PLACEABLE_BARRICADE;
    }
    return p;
}

static nlohmann::json CenterToJson(const BaseCenter& c) {
    if (!c.valid) {
        return nullptr;
    }
    return { { "scene", c.scene }, { "pos", { c.pos[0], c.pos[1], c.pos[2] } } };
}

static BaseCenter CenterFromJson(const nlohmann::json& j) {
    BaseCenter c;
    if (!j.is_object()) {
        return c;
    }
    c.valid = true;
    c.scene = j.value("scene", (int16_t)0);
    auto pos = j.value("pos", nlohmann::json::array());
    for (int i = 0; i < 3; i++) {
        c.pos[i] = i < (int)pos.size() ? pos[i].get<float>() : 0.0f;
    }
    return c;
}

nlohmann::json BaseToJson() {
    nlohmann::json j;
    j["rev"] = sBase.rev;
    j["nextId"] = sBase.nextId;
    j["center"] = { CenterToJson(sBase.center[ERA_ADULT]), CenterToJson(sBase.center[ERA_CHILD]) };
    j["placeables"] = nlohmann::json::array();
    for (auto& p : sBase.placeables) {
        j["placeables"].push_back(PlaceableToJson(p));
    }
    j["counters"] = { { "daysSurvived", sBase.daysSurvived },
                      { "hordeNightsSurvived", sBase.hordeNightsSurvived },
                      { "nextRaidDay", sBase.nextRaidDay },
                      { "story", sBase.story },
                      { "nightsFailed", sBase.nightsFailed },
                      { "raidInterval", sBase.raidInterval } };
    j["lootOpened"] = sBase.lootOpened;
    j["blueprints"] = sBase.blueprints;
    return j;
}

void BaseFromJson(const nlohmann::json& j) {
    BaseState b;
    b.rev = j.value("rev", 0u);
    b.nextId = j.value("nextId", (uint16_t)1);
    auto centers = j.value("center", nlohmann::json::array());
    for (int e = 0; e < 2 && e < (int)centers.size(); e++) {
        b.center[e] = CenterFromJson(centers[e]);
    }
    for (auto& pj : j.value("placeables", nlohmann::json::array())) {
        Placeable p = PlaceableFromJson(pj);
        if (p.id != 0) {
            b.placeables.push_back(p);
            b.nextId = std::max<uint16_t>(b.nextId, p.id + 1);
        }
    }
    auto counters = j.value("counters", nlohmann::json::object());
    b.daysSurvived = counters.value("daysSurvived", 0u);
    b.hordeNightsSurvived = counters.value("hordeNightsSurvived", 0u);
    b.nextRaidDay = counters.value("nextRaidDay", 0u);
    b.story = counters.value("story", 0u);
    b.nightsFailed = counters.value("nightsFailed", 0u);
    b.raidInterval = counters.value("raidInterval", 0u);
    b.lootOpened = j.value("lootOpened", std::vector<uint32_t>{});
    b.blueprints = j.value("blueprints", std::vector<std::string>{});
    sBase = b;
}

// MARK: - The boarded-up village

// Kokiri Forest (child era), measured in game: barricades flank the path to the
// Lost Woods bridge (lower exit) and the Lost Woods tunnel (upper exit), leaving a
// gap so the story route stays open; one fence on the green is already broken; the
// workbench and the "Day 1" sign stand by Link's ladder. y is re-snapped to the
// floor when the piece spawns.
struct Seed {
    uint8_t type;
    float x, y, z;
    int16_t rot;
    uint16_t hpPercent;
};
// clang-format off
static const Seed sVillageSeeds[] = {
    { PLACEABLE_WORKBENCH, -170.0f, -80.0f,  960.0f, 0x4000, 100 }, // by Link's ladder
    { PLACEABLE_SIGN,      -165.0f, -80.0f,  880.0f, 0x4000, 100 }, // "Day 1"
    { PLACEABLE_BARRICADE, -1240.0f, -80.0f, -200.0f, 0x4000, 100 }, // Lost Woods bridge path, north side
    { PLACEABLE_BARRICADE, -1240.0f, -80.0f, -380.0f, 0x4000, 100 }, // Lost Woods bridge path, south side
    { PLACEABLE_BARRICADE, -350.0f, 380.0f, -1180.0f, 0x0000, 100 }, // Lost Woods ledge path, west side
    { PLACEABLE_BARRICADE, -150.0f, 380.0f, -1180.0f, 0x0000, 100 }, // Lost Woods ledge path, east side
    { PLACEABLE_BARRICADE,  300.0f,   0.0f,  500.0f, 0x1C72, 35  }, // the broken fence on the village green
};
// clang-format on

void SeedVillage() {
    sBase = {};
    for (auto& s : sVillageSeeds) {
        Placeable p;
        p.id = sBase.nextId++;
        p.type = s.type;
        p.era = ERA_CHILD;
        p.scene = SCENE_KOKIRI_FOREST;
        p.pos[0] = s.x;
        p.pos[1] = s.y;
        p.pos[2] = s.z;
        p.rot = s.rot;
        uint16_t maxHp = GetPlaceableInfo(s.type).maxHp;
        p.hp = (uint16_t)(maxHp * s.hpPercent / 100);
        sBase.placeables.push_back(p);
        if (s.type == PLACEABLE_WORKBENCH && !sBase.center[ERA_CHILD].valid) {
            sBase.center[ERA_CHILD] = { true, SCENE_KOKIRI_FOREST, { s.x, s.y, s.z } };
        }
    }
}

std::string BaseCountsLine() {
    int era = CurrentEra();
    const BaseCenter& c = sBase.center[era];
    if (!c.valid) {
        return "No base yet: place a workbench outdoors to start one.";
    }
    const char* where = BaseSceneName(c.scene);
    return fmt::format("Base: {}/{} pieces in {} | Days survived: {} | Raids survived: {}", CountEra(era), BASE_CAP,
                       where ? where : "?", sBase.daysSurvived, sBase.hordeNightsSurvived);
}

// MARK: - Spawning (every client, its own copy)

static bool SpawnsHere(const Placeable& p) {
    return gPlayState != nullptr && p.scene == gPlayState->sceneNum && p.era == CurrentEra();
}

void OnPlaceableSpawned(uint16_t id, Actor* actor) {
    sSpawned[id] = actor;
}

void OnPlaceableDestroyed(uint16_t id, Actor* actor) {
    auto it = sSpawned.find(id);
    if (it != sSpawned.end() && it->second == actor) {
        sSpawned.erase(it);
    }
}

static void Despawn(uint16_t id) {
    auto it = sSpawned.find(id);
    if (it != sSpawned.end()) {
        Actor_Kill(it->second);
        sSpawned.erase(it);
    }
}

// Bring the current scene's actors in line with sBase (after a state change or scene load).
static void SyncSceneActors() {
    if (gPlayState == nullptr || !BaseEnabled()) {
        return;
    }
    for (auto it = sSpawned.begin(); it != sSpawned.end();) {
        const Placeable* p = FindPlaceable(it->first);
        if (p == nullptr || !SpawnsHere(*p)) {
            Actor_Kill(it->second);
            it = sSpawned.erase(it);
        } else {
            ++it;
        }
    }
    for (auto& p : sBase.placeables) {
        if (SpawnsHere(p) && !sSpawned.contains(p.id)) {
            SpawnPlaceableActor(p);
        }
    }
}

std::vector<std::pair<uint16_t, Actor*>> SpawnedPlaceables() {
    return { sSpawned.begin(), sSpawned.end() };
}

static void DespawnAll() {
    for (auto& [id, actor] : sSpawned) {
        Actor_Kill(actor);
    }
    sSpawned.clear();
}

// MARK: - Owner: deciding

static void BroadcastDelta(nlohmann::json delta) {
    delta["type"] = BASE_DELTA;
    delta["rev"] = sBase.rev;
    Broadcast(delta);
}

static void BroadcastState() {
    nlohmann::json payload;
    payload["type"] = BASE_STATE;
    payload["base"] = BaseToJson();
    Broadcast(payload);
}

static void Toast(const std::string& prefix, const std::string& message, bool error) {
    Notification::Emit({ .prefix = prefix, .message = message, .remainingTime = 3.5f, .mute = true });
    Sfx_PlaySfxCentered(error ? NA_SE_SY_ERROR : NA_SE_SY_GET_ITEM);
}

static void OnPlaceResult(const nlohmann::json& payload) {
    if (payload.value("reqId", 0u) != sPlaceInFlight.reqId) {
        return;
    }
    sPlaceInFlight = {};
    uint8_t type = payload.value("ptype", (uint8_t)0);
    if (payload.value("ok", false)) {
        Toast("Built", GetPlaceableInfo(type).name, false);
    } else {
        Toast(GetPlaceableInfo(type).name, payload.value("reason", "Couldn't build there"), true);
    }
}

static void Reply(uint32_t requester, nlohmann::json result) {
    if (requester == OwnId()) {
        OnPlaceResult(result);
    } else {
        SendTo(requester, result);
    }
}

// The owner's placement check (spec "Anywhere bases"): kit in the pool, the
// scene's clock runs, within 800 of the era's workbench (the first one sets the
// center), one base per era, at most BASE_CAP pieces.
static void ProcessPlaceRequest(const nlohmann::json& payload, uint32_t requester) {
    nlohmann::json result;
    result["type"] = PLACE_RESULT;
    result["reqId"] = payload.value("reqId", 0u);
    uint8_t type = payload.value("ptype", (uint8_t)0xFF);
    result["ptype"] = type;
    auto refuse = [&](const std::string& reason) {
        result["ok"] = false;
        result["reason"] = reason;
        Reply(requester, result);
    };

    if (type >= PLACEABLE_COUNT || GetPlaceableInfo(type).kit[0] == '\0') {
        return refuse("That can't be built");
    }
    const PlaceableInfo& info = GetPlaceableInfo(type);
    int16_t scene = payload.value("scene", (int16_t)-1);
    int era = payload.value("era", ERA_CHILD) == ERA_ADULT ? ERA_ADULT : ERA_CHILD;
    auto posJ = payload.value("pos", nlohmann::json::array());
    if (posJ.size() != 3) {
        return refuse("Bad position");
    }
    float pos[3] = { posJ[0].get<float>(), posJ[1].get<float>(), posJ[2].get<float>() };

    PoolState& pool = Net::MutablePool();
    auto kit = pool.kits.find(info.kit);
    if (kit == pool.kits.end() || kit->second == 0) {
        return refuse(fmt::format("No {} kit in the pool", info.name));
    }
    if (BaseSceneName(scene) == nullptr) {
        return refuse("Bases go outdoors, where the nights come");
    }
    BaseCenter& center = sBase.center[era];
    bool setsCenter = false;
    if (!center.valid) {
        if (type != PLACEABLE_WORKBENCH) {
            return refuse("Build a workbench first: it marks your base");
        }
        setsCenter = true;
    } else {
        if (center.scene != scene) {
            return refuse(fmt::format("Your base is in {}. Pack it up to move", BaseSceneName(center.scene)));
        }
        float dx = pos[0] - center.pos[0], dz = pos[2] - center.pos[2];
        if (sqrtf(dx * dx + dz * dz) > BASE_RADIUS) {
            return refuse("Too far from the base's workbench");
        }
    }
    if (CountEra(era) >= BASE_CAP) {
        return refuse(fmt::format("The base is full ({} pieces)", BASE_CAP));
    }

    Placeable p;
    p.id = sBase.nextId++;
    if (sBase.nextId == 0 || sBase.nextId > 0x7FFF) {
        sBase.nextId = 1;
    }
    p.type = type;
    p.era = era;
    p.scene = scene;
    std::copy(pos, pos + 3, p.pos);
    p.rot = payload.value("rot", (int16_t)0);
    p.hp = info.maxHp;
    sBase.placeables.push_back(p);
    if (setsCenter) {
        center = { true, scene, { pos[0], pos[1], pos[2] } };
    }
    kit->second--;
    pool.rev++;
    sBase.rev++;

    nlohmann::json delta;
    delta["op"] = "add";
    delta["placeable"] = PlaceableToJson(p);
    if (setsCenter) {
        delta["era"] = era;
        delta["center"] = CenterToJson(center);
    }
    BroadcastDelta(delta);
    Net::BroadcastPool();
    if (SpawnsHere(p)) {
        SpawnPlaceableActor(p);
    }

    result["ok"] = true;
    result["id"] = p.id;
    Reply(requester, result);
}

static void RefundKit(const Placeable& p, PoolState& pool) {
    const char* kit = GetPlaceableInfo(p.type).kit;
    if (kit[0] != '\0') {
        pool.kits[kit]++;
    }
}

static void ProcessPackRequest(const nlohmann::json& payload, uint32_t requester) {
    PoolState& pool = Net::MutablePool();
    auto tell = [&](const std::string& msg, bool error) {
        if (requester == OwnId()) {
            Toast("Base", msg, error);
        } else {
            nlohmann::json r;
            r["type"] = PLACE_RESULT;
            r["reqId"] = 0;
            r["notice"] = msg;
            r["error"] = error;
            SendTo(requester, r);
        }
    };

    if (payload.value("all", false)) {
        int era = payload.value("era", ERA_CHILD) == ERA_ADULT ? ERA_ADULT : ERA_CHILD;
        int refunded = 0;
        std::erase_if(sBase.placeables, [&](const Placeable& p) {
            if (p.era != era) {
                return false;
            }
            RefundKit(p, pool);
            refunded++;
            return true;
        });
        sBase.center[era] = {};
        sBase.rev++;
        pool.rev++;
        BroadcastState();
        Net::BroadcastPool();
        SyncSceneActors();
        tell(fmt::format("Packed up the base: {} pieces back in the pool", refunded), false);
        return;
    }

    uint16_t id = payload.value("id", (uint16_t)0);
    Placeable* p = FindPlaceableMut(id);
    if (p == nullptr) {
        return tell("That piece is already gone", true);
    }
    bool isCenter = p->type == PLACEABLE_WORKBENCH && sBase.center[p->era].valid &&
                    fabsf(sBase.center[p->era].pos[0] - p->pos[0]) < 1.0f &&
                    fabsf(sBase.center[p->era].pos[2] - p->pos[2]) < 1.0f;
    if (isCenter && CountEra(p->era) > 1) {
        return tell("The workbench holds the base together. Pack up the whole base to move it", true);
    }
    int era = p->era;
    RefundKit(*p, pool);
    std::string name = GetPlaceableInfo(p->type).name;
    std::erase_if(sBase.placeables, [id](const Placeable& q) { return q.id == id; });
    if (isCenter) {
        sBase.center[era] = {};
    }
    sBase.rev++;
    pool.rev++;
    nlohmann::json delta;
    delta["op"] = "remove";
    delta["id"] = id;
    if (isCenter) {
        delta["era"] = era;
        delta["center"] = nullptr;
    }
    BroadcastDelta(delta);
    Net::BroadcastPool();
    Despawn(id);
    tell(fmt::format("{} packed back into a kit", name), false);
}

// Owner: sequence an HP change (from itself or a scene's enemy authority).
static void ApplyHp(uint16_t id, int hp) {
    Placeable* p = FindPlaceableMut(id);
    if (p == nullptr) {
        return;
    }
    hp = std::clamp(hp, 0, (int)GetPlaceableInfo(p->type).maxHp);
    if (hp == p->hp) {
        return;
    }
    sBase.rev++;
    nlohmann::json delta;
    if (hp == 0) {
        // Broken: the piece is gone (its kit is not refunded).
        std::erase_if(sBase.placeables, [id](const Placeable& q) { return q.id == id; });
        delta["op"] = "remove";
        delta["id"] = id;
        delta["broken"] = true;
        Despawn(id);
    } else {
        if (hp < p->hp) {
            auto it = sSpawned.find(id);
            OnPlaceableHit(id, it != sSpawned.end() ? it->second : nullptr);
        }
        p->hp = (uint16_t)hp;
        delta["op"] = "hp";
        delta["id"] = id;
        delta["hp"] = hp;
    }
    BroadcastDelta(delta);
}

void DamagePlaceable(uint16_t id, int amount) {
    const Placeable* p = FindPlaceable(id);
    if (p == nullptr || GetPlaceableInfo(p->type).maxHp == 0) {
        return;
    }
    int hp = (int)p->hp - amount;
    if (IsOwner()) {
        ApplyHp(id, hp);
    } else {
        // Report the new HP to the owner, which sequences it into a BASE_DELTA. Keep
        // our copy in step meanwhile so back-to-back hits compound instead of each
        // reporting the same value (the owner's delta overwrites it either way).
        nlohmann::json payload;
        payload["type"] = BASE_HP;
        payload["id"] = id;
        payload["hp"] = std::max(hp, 0);
        SendTo(ActingOwner(), payload);
        if (hp > 0) {
            auto it = sSpawned.find(id);
            OnPlaceableHit(id, it != sSpawned.end() ? it->second : nullptr);
            FindPlaceableMut(id)->hp = (uint16_t)hp;
        }
    }
}

// MARK: - Clients: applying

static void SendBaseRequest() {
    if (!Connected() || IsOwner()) {
        return;
    }
    sLastBaseRequest = Now();
    nlohmann::json payload;
    payload["type"] = BASE_REQUEST;
    payload["base"] = BaseToJson();
    SendTo(ActingOwner(), payload);
}

static void ApplyDelta(const nlohmann::json& d) {
    uint32_t rev = d.value("rev", 0u);
    if (rev <= sBase.rev) {
        return; // old news
    }
    if (rev != sBase.rev + 1) {
        // We missed something: ask for the whole base.
        if (Now() - sLastBaseRequest > 1.0) {
            SendBaseRequest();
        }
        return;
    }
    sBase.rev = rev;
    std::string op = d.value("op", "");
    if (d.contains("center")) {
        int era = d.value("era", ERA_CHILD) == ERA_ADULT ? ERA_ADULT : ERA_CHILD;
        sBase.center[era] = CenterFromJson(d["center"]);
    }
    if (op == "add" && d.contains("placeable")) {
        Placeable p = PlaceableFromJson(d["placeable"]);
        if (FindPlaceable(p.id) == nullptr) {
            sBase.placeables.push_back(p);
            sBase.nextId = std::max<uint16_t>(sBase.nextId, p.id + 1);
        }
        if (SpawnsHere(p) && !sSpawned.contains(p.id)) {
            SpawnPlaceableActor(p);
        }
    } else if (op == "remove") {
        uint16_t id = d.value("id", (uint16_t)0);
        auto it = sSpawned.find(id);
        if (it != sSpawned.end() && d.value("broken", false)) {
            Vec3f pos = it->second->world.pos;
            SoundSource_PlaySfxAtFixedWorldPos(gPlayState, &pos, 30, NA_SE_EV_WOODBOX_BREAK);
        }
        std::erase_if(sBase.placeables, [id](const Placeable& q) { return q.id == id; });
        Despawn(id);
    } else if (op == "hp") {
        Placeable* p = FindPlaceableMut(d.value("id", (uint16_t)0));
        if (p != nullptr) {
            uint16_t hp = d.value("hp", p->hp);
            if (hp < p->hp) {
                auto it = sSpawned.find(p->id);
                OnPlaceableHit(p->id, it != sSpawned.end() ? it->second : nullptr);
            }
            p->hp = hp;
        }
    }
}

// Adopt a copy when it is newer. Returns true when it replaced ours.
bool BaseAdoptIfNewer(const nlohmann::json& j, bool force) {
    if (!j.is_object()) {
        return false;
    }
    uint32_t rev = j.value("rev", 0u);
    if (!force && rev <= sBase.rev) {
        return false;
    }
    BaseFromJson(j);
    SyncSceneActors();
    return true;
}

BaseState& Net::MutableBase() {
    return sBase;
}

void Net::CommitBase() {
    sBase.rev++;
    BroadcastState();
    SyncSceneActors();
}

bool BaseOwnsPacket(const std::string& type) {
    return type == PLACE_REQUEST || type == PLACE_RESULT || type == PACK_REQUEST || type == BASE_DELTA ||
           type == BASE_STATE || type == BASE_REQUEST || type == BASE_HP;
}

void BaseHandlePacket(const std::string& type, const nlohmann::json& payload, uint32_t from) {
    if (!BaseEnabled()) {
        return;
    }
    if (type == PLACE_REQUEST) {
        if (IsOwner()) {
            ProcessPlaceRequest(payload, from);
        } else {
            nlohmann::json r;
            r["type"] = PLACE_RESULT;
            r["reqId"] = payload.value("reqId", 0u);
            r["ptype"] = payload.value("ptype", (uint8_t)0);
            r["ok"] = false;
            r["reason"] = "The host changed, try again";
            SendTo(from, r);
        }
    } else if (type == PLACE_RESULT) {
        if (payload.contains("notice")) {
            Toast("Base", payload.value("notice", ""), payload.value("error", false));
        } else {
            OnPlaceResult(payload);
        }
    } else if (type == PACK_REQUEST) {
        if (IsOwner()) {
            ProcessPackRequest(payload, from);
        }
    } else if (type == BASE_HP) {
        if (IsOwner()) {
            ApplyHp(payload.value("id", (uint16_t)0), payload.value("hp", 0));
        }
    } else if (type == BASE_DELTA) {
        if (from == ActingOwner() && !IsOwner()) {
            ApplyDelta(payload);
        }
    } else if (type == BASE_STATE) {
        if (from == ActingOwner() && !IsOwner() && payload.contains("base")) {
            // The owner's copy is canonical once it has answered (it already
            // merged ours by rev), so take it even at an equal rev.
            BaseAdoptIfNewer(payload["base"], true);
        }
    } else if (type == BASE_REQUEST) {
        if (!IsOwner()) {
            return;
        }
        if (payload.contains("base")) {
            BaseAdoptIfNewer(payload["base"], false);
        }
        BroadcastState();
    }
}

void BaseOnOwnerChanged(bool nowOwner) {
    if (nowOwner) {
        BroadcastState();
    } else {
        SendBaseRequest();
    }
}

// MARK: - Placement mode

struct PlacementState {
    bool active = false;
    uint8_t type = 0;
    Actor* ghost = nullptr;
    float pos[3] = {};
    int16_t rot = 0;
    bool valid = false;
    std::string reason;
    int16_t room = -1;
};
static PlacementState sPlace;

bool InPlacement() {
    return sPlace.active;
}

static void EndPlacement() {
    if (sPlace.ghost != nullptr) {
        Actor_Kill(sPlace.ghost);
    }
    sPlace = {};
}

void BeginPlacement(uint8_t type) {
    if (!BaseEnabled() || gPlayState == nullptr || type >= PLACEABLE_COUNT) {
        return;
    }
    const PlaceableInfo& info = GetPlaceableInfo(type);
    auto kit = GetPool().kits.find(info.kit);
    if (kit == GetPool().kits.end() || kit->second == 0) {
        Toast(info.name, "No kit in the pool: craft one at the workbench", true);
        return;
    }
    if (sPlaceInFlight.reqId != 0) {
        Toast(info.name, "Waiting for the host...", true);
        return;
    }
    if (gPlayState->msgCtx.msgMode != MSGMODE_NONE) {
        // Placement ends whenever a textbox is up; say so instead of failing silently.
        Toast(info.name, "Finish talking first", true);
        return;
    }
    EndPlacement();
    Player* player = GET_PLAYER(gPlayState);
    Actor* ghost = Actor_Spawn(&gPlayState->actorCtx, gPlayState, GhostActorId(), player->actor.world.pos.x,
                               player->actor.world.pos.y, player->actor.world.pos.z, 0, 0, 0, type, false);
    if (ghost == nullptr) {
        return;
    }
    ghost->room = -1;
    sPlace.active = true;
    sPlace.type = type;
    sPlace.ghost = ghost;
    // Face the same way as Link, snapped to 45 degrees.
    sPlace.rot = (int16_t)(((player->actor.shape.rot.y + 0x1000) / 0x2000) * 0x2000);
    Notification::Emit({ .prefix = fmt::format("Placing {}", info.name),
                         .message = "C-Left/C-Right rotate, A place, B cancel",
                         .remainingTime = 5.0f,
                         .mute = true });
}

void OnGhostDestroyed(Actor* ghost) {
    if (sPlace.ghost == ghost) {
        sPlace.ghost = nullptr;
        sPlace.active = false;
    }
}

static bool NearExitOrDoor(PlayState* play, const Vec3f& at, f32 floorY) {
    // Scene exits are collision polys with an exit index: probe the floor around.
    static const float kProbe = 100.0f;
    for (int i = 0; i < 9; i++) {
        Vec3f probe = at;
        if (i > 0) {
            float a = (float)(i - 1) * (float)M_PI / 4.0f;
            probe.x += sinf(a) * kProbe;
            probe.z += cosf(a) * kProbe;
        }
        probe.y = floorY + 60.0f;
        CollisionPoly* poly = nullptr;
        s32 bgId = BGCHECK_SCENE;
        f32 y = BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, &probe);
        if (poly != nullptr && y > BGCHECK_Y_MIN && SurfaceType_GetSceneExitIndex(&play->colCtx, poly, bgId) != 0) {
            return true;
        }
    }
    // Doors and loading planes between rooms.
    for (s32 i = 0; i < play->transiActorCtx.numActors; i++) {
        TransitionActorEntry* t = &play->transiActorCtx.list[i];
        float dx = t->pos.x - at.x, dz = t->pos.z - at.z;
        if (dx * dx + dz * dz < kProbe * kProbe && fabsf(t->pos.y - floorY) < 150.0f) {
            return true;
        }
    }
    for (Actor* a = play->actorCtx.actorLists[ACTORCAT_DOOR].head; a != nullptr; a = a->next) {
        float dx = a->world.pos.x - at.x, dz = a->world.pos.z - at.z;
        if (dx * dx + dz * dz < kProbe * kProbe && fabsf(a->world.pos.y - floorY) < 150.0f) {
            return true;
        }
    }
    return false;
}

static void Validate(PlayState* play, Player* player) {
    const PlaceableInfo& info = GetPlaceableInfo(sPlace.type);
    s16 yaw = player->actor.shape.rot.y;
    float dist = 60.0f + (float)std::max(info.halfX, info.halfZ) * 0.35f; // 60-80 ahead of Link
    dist = std::min(dist, 80.0f);
    float x = player->actor.world.pos.x + Math_SinS(yaw) * dist;
    float z = player->actor.world.pos.z + Math_CosS(yaw) * dist;
    x = roundf(x / 30.0f) * 30.0f; // 30-unit grid
    z = roundf(z / 30.0f) * 30.0f;

    Vec3f at = { x, player->actor.world.pos.y + 100.0f, z };
    CollisionPoly* poly = nullptr;
    s32 bgId = BGCHECK_SCENE;
    f32 floorY = BgCheck_EntityRaycastFloor4(&play->colCtx, &poly, &bgId, &player->actor, &at);

    sPlace.pos[0] = x;
    sPlace.pos[2] = z;
    sPlace.pos[1] = floorY > BGCHECK_Y_MIN ? floorY : player->actor.world.pos.y;
    sPlace.valid = false;

    if (floorY <= BGCHECK_Y_MIN || poly == nullptr) {
        sPlace.reason = "No ground here";
        return;
    }
    if (bgId != BGCHECK_SCENE) {
        sPlace.reason = "Build on solid ground";
        return;
    }
    if (fabsf(floorY - player->actor.world.pos.y) > 100.0f) {
        sPlace.reason = "Too big a drop";
        return;
    }
    if (COLPOLY_GET_NORMAL(poly->normal.y) < cosf(30.0f * (float)M_PI / 180.0f)) {
        sPlace.reason = "Too steep (over 30 degrees)";
        return;
    }
    f32 waterY;
    WaterBox* waterBox;
    if (WaterBox_GetSurface1(play, &play->colCtx, x, z, &waterY, &waterBox) && waterY > floorY - 5.0f) {
        sPlace.reason = "Not on water";
        return;
    }
    Vec3f ground = { x, floorY, z };
    if (NearExitOrDoor(play, ground, floorY)) {
        sPlace.reason = "Too close to a door or exit";
        return;
    }
    if (BaseSceneName(play->sceneNum) == nullptr) {
        sPlace.reason = "Bases go outdoors, where the nights come";
        return;
    }
    for (auto& p : sBase.placeables) {
        if (!SpawnsHere(p)) {
            continue;
        }
        // Footprints as circles: overlapping boxes are refused, touching ones are fine.
        const PlaceableInfo& other = GetPlaceableInfo(p.type);
        float r = (float)(std::min(info.halfX, info.halfZ) + std::min(other.halfX, other.halfZ));
        float dx = p.pos[0] - x, dz = p.pos[2] - z;
        if (dx * dx + dz * dz < r * r) {
            sPlace.reason = "Something is already there";
            return;
        }
    }
    const BaseCenter& c = sBase.center[CurrentEra()];
    if (c.valid) {
        float dx = c.pos[0] - x, dz = c.pos[2] - z;
        if (c.scene != play->sceneNum) {
            sPlace.reason = "Your base is in another place";
            return;
        }
        if (dx * dx + dz * dz > BASE_RADIUS * BASE_RADIUS) {
            sPlace.reason = "Too far from the base's workbench";
            return;
        }
    } else if (sPlace.type != PLACEABLE_WORKBENCH) {
        sPlace.reason = "Build a workbench first";
        return;
    }
    sPlace.valid = true;
    sPlace.reason.clear();
}

static void SendPlaceRequest() {
    sPlaceInFlight.reqId = sNextPlaceReq++;
    sPlaceInFlight.sentAt = Now();
    nlohmann::json payload;
    payload["type"] = PLACE_REQUEST;
    payload["reqId"] = sPlaceInFlight.reqId;
    payload["ptype"] = sPlace.type;
    payload["scene"] = gPlayState->sceneNum;
    payload["room"] = -1;
    payload["pos"] = { sPlace.pos[0], sPlace.pos[1], sPlace.pos[2] };
    payload["rot"] = sPlace.rot;
    payload["era"] = CurrentEra();
    if (IsOwner()) {
        ProcessPlaceRequest(payload, OwnId());
    } else {
        SendTo(ActingOwner(), payload);
    }
}

// Called from the ghost actor's update. The ghost updates in ACTORCAT_SWITCH,
// before Link, so the buttons it consumes never reach him.
void PlacementUpdate(Actor* ghost, PlayState* play) {
    if (!sPlace.active || sPlace.ghost != ghost) {
        Actor_Kill(ghost);
        return;
    }
    Player* player = GET_PLAYER(play);
    Input* input = &play->state.input[0];
    u16 pressed = input->press.button;
    const u16 consumed = BTN_A | BTN_B | BTN_CLEFT | BTN_CRIGHT;
    input->press.button &= ~consumed;
    input->cur.button &= ~consumed;

    if (pressed & BTN_CLEFT) {
        sPlace.rot += 0x2000;
        Sfx_PlaySfxCentered(NA_SE_SY_CURSOR);
    }
    if (pressed & BTN_CRIGHT) {
        sPlace.rot -= 0x2000;
        Sfx_PlaySfxCentered(NA_SE_SY_CURSOR);
    }

    Validate(play, player);
    ghost->world.pos = { sPlace.pos[0], sPlace.pos[1], sPlace.pos[2] };
    ghost->shape.rot.y = ghost->world.rot.y = sPlace.rot;

    if (pressed & BTN_B) {
        Sfx_PlaySfxCentered(NA_SE_SY_CANCEL);
        EndPlacement();
        return;
    }
    if (pressed & BTN_A) {
        if (!sPlace.valid) {
            Toast(GetPlaceableInfo(sPlace.type).name, sPlace.reason, true);
            return;
        }
        Sfx_PlaySfxCentered(NA_SE_SY_DECIDE);
        SendPlaceRequest();
        EndPlacement();
    }
}

bool PlacementGhostValid() {
    return sPlace.valid;
}

uint8_t PlacementGhostType() {
    return sPlace.type;
}

// MARK: - Packing up

void RequestPackUp(uint16_t id) {
    nlohmann::json payload;
    payload["type"] = PACK_REQUEST;
    payload["id"] = id;
    if (IsOwner()) {
        ProcessPackRequest(payload, OwnId());
    } else {
        SendTo(ActingOwner(), payload);
    }
}

void RequestPackUpBase(int era) {
    nlohmann::json payload;
    payload["type"] = PACK_REQUEST;
    payload["all"] = true;
    payload["era"] = era;
    if (IsOwner()) {
        ProcessPackRequest(payload, OwnId());
    } else {
        SendTo(ActingOwner(), payload);
    }
}

uint16_t NearestPlaceable(float maxDist) {
    if (gPlayState == nullptr) {
        return 0;
    }
    Player* player = GET_PLAYER(gPlayState);
    uint16_t best = 0;
    float bestD = maxDist * maxDist;
    for (auto& p : sBase.placeables) {
        if (!SpawnsHere(p)) {
            continue;
        }
        float dx = p.pos[0] - player->actor.world.pos.x, dz = p.pos[2] - player->actor.world.pos.z;
        float d = dx * dx + dz * dz;
        if (d < bestD) {
            bestD = d;
            best = p.id;
        }
    }
    return best;
}

// MARK: - Per frame / session

void BaseOnFrame() {
    if (sPlaceInFlight.reqId != 0 && Now() - sPlaceInFlight.sentAt > 5.0) {
        sPlaceInFlight = {};
        Toast("Base", "The host didn't answer", true);
    }
    if (sPlace.active && gPlayState != nullptr) {
        Player* player = GET_PLAYER(gPlayState);
        if (player == nullptr || gPlayState->msgCtx.msgMode != MSGMODE_NONE ||
            (player->stateFlags1 & (PLAYER_STATE1_DEAD | PLAYER_STATE1_IN_CUTSCENE))) {
            EndPlacement();
        }
    }
}

void BaseResetSession() {
    sPlace = {};
    sPlaceInFlight = {};
    sSpawned.clear();
    sLastBaseRequest = -100.0;
}

void BaseRegisterHooks(bool enabled) {
    COND_HOOK(OnSceneSpawnActors, enabled, []() {
        // A new scene: last scene's actors are gone (their destroy callbacks ran).
        sSpawned.clear();
        sPlace = {};
        RegisterPlaceableActors();
        SyncSceneActors();
    });
    COND_HOOK(OnLoadGame, enabled, [](int32_t fileNum) { RegisterPlaceableActors(); });
    if (!enabled) {
        if (sPlace.active) {
            EndPlacement();
        }
        if (gPlayState != nullptr) {
            DespawnAll();
        }
    } else if (gPlayState != nullptr) {
        RegisterPlaceableActors();
        SyncSceneActors();
    }
}

// MARK: - Test hooks

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
// For tools/webtest: drive the real PLACE_REQUEST / PACK_REQUEST paths and read
// the base back. Inert unless gSevenDays.Enabled and .Base are on.
extern "C" {
EMSCRIPTEN_KEEPALIVE
int sevendays_test_place(int type, double x, double y, double z, int rot) {
    if (!BaseEnabled() || gPlayState == nullptr || type < 0 || type >= PLACEABLE_COUNT) {
        return 0;
    }
    sPlace.type = (uint8_t)type;
    sPlace.pos[0] = (float)x;
    sPlace.pos[1] = (float)y;
    sPlace.pos[2] = (float)z;
    sPlace.rot = (int16_t)rot;
    SendPlaceRequest();
    return (int)sPlaceInFlight.reqId;
}

// Place where placement mode would put it: ahead of Link, on the grid, validated.
EMSCRIPTEN_KEEPALIVE
const char* sevendays_test_place_ahead(int type) {
    static std::string out;
    nlohmann::json j;
    if (!BaseEnabled() || gPlayState == nullptr || type < 0 || type >= PLACEABLE_COUNT) {
        j["ok"] = false;
        out = j.dump();
        return out.c_str();
    }
    Player* player = GET_PLAYER(gPlayState);
    sPlace.type = (uint8_t)type;
    sPlace.rot = player->actor.shape.rot.y;
    Validate(gPlayState, player);
    j["valid"] = sPlace.valid;
    j["reason"] = sPlace.reason;
    j["pos"] = { sPlace.pos[0], sPlace.pos[1], sPlace.pos[2] };
    if (sPlace.valid) {
        SendPlaceRequest();
        j["reqId"] = sPlaceInFlight.reqId;
    }
    sPlace.active = false;
    out = j.dump();
    return out.c_str();
}

// Grants kits on the owner (tests only: e.g. spike strips before the Deku Tree tier).
EMSCRIPTEN_KEEPALIVE
void sevendays_test_grant_kit(const char* kit, int count) {
    if (!BaseEnabled() || !IsOwner() || FindPlaceableTypeForKit(kit) < 0) {
        return;
    }
    PoolState& pool = Net::MutablePool();
    pool.kits[kit] += count;
    pool.rev++;
    Net::BroadcastPool();
}

EMSCRIPTEN_KEEPALIVE
void sevendays_test_begin_placement(int type) {
    BeginPlacement((uint8_t)type);
}

EMSCRIPTEN_KEEPALIVE
void sevendays_test_pack(int id) {
    if (BaseEnabled()) {
        RequestPackUp((uint16_t)id);
    }
}

EMSCRIPTEN_KEEPALIVE
void sevendays_test_damage(int id, int amount) {
    if (BaseEnabled()) {
        DamagePlaceable((uint16_t)id, amount);
    }
}

EMSCRIPTEN_KEEPALIVE
const char* sevendays_test_base() {
    static std::string out;
    nlohmann::json j = BaseToJson();
    j["owner"] = IsOwner();
    j["spawned"] = nlohmann::json::array();
    for (auto& [id, actor] : sSpawned) {
        j["spawned"].push_back({ { "id", id }, { "x", actor->world.pos.x }, { "y", actor->world.pos.y },
                                 { "z", actor->world.pos.z } });
    }
    j["placing"] = sPlace.active;
    j["ghostValid"] = sPlace.valid;
    j["ghostReason"] = sPlace.reason;
    if (gPlayState != nullptr) {
        Player* player = GET_PLAYER(gPlayState);
        j["scene"] = gPlayState->sceneNum;
        j["link"] = { player->actor.world.pos.x, player->actor.world.pos.y, player->actor.world.pos.z,
                      player->actor.shape.rot.y };
        j["talkActor"] = player->talkActor != nullptr ? player->talkActor->id : -1;
        j["cam"] = Camera_GetCamDirYaw(GET_ACTIVE_CAM(gPlayState));
        j["msg"] = gPlayState->msgCtx.msgMode;
        j["textId"] = gPlayState->msgCtx.textId;
    }
    out = j.dump();
    return out.c_str();
}

// Teleport Link (for measuring the village and placing in tests).
EMSCRIPTEN_KEEPALIVE
void sevendays_test_warp_link(double x, double y, double z, int yaw) {
    if (!Enabled() || gPlayState == nullptr) {
        return;
    }
    Player* player = GET_PLAYER(gPlayState);
    player->actor.world.pos = { (float)x, (float)y, (float)z };
    player->actor.home.pos = player->actor.world.pos;
    player->actor.prevPos = player->actor.world.pos;
    player->actor.shape.rot.y = player->actor.world.rot.y = player->yaw = (s16)yaw;
}
}
#endif

} // namespace SevenDays
