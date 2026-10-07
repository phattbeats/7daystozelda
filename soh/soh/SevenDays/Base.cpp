#include "SevenDays.h"
#include "SevenDaysNet.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"
#include "soh/Network/Anchor/Anchor.h"
#include "soh/Network/Anchor/EnemySync.h"
#include "soh/Notification/Notification.h"

#include <algorithm>
#include <cmath>
#include <map>
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
 *   REPAIR_REQUEST player  -> owner    id, hammer (PHA-3935: full HP for materials)
 *   UPGRADE_REQUEST player -> owner    id (PHA-3935: Megaton Hammer, wood -> stone -> iron in place)
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
static const std::string REPAIR_REQUEST = "REPAIR_REQUEST";
static const std::string UPGRADE_REQUEST = "UPGRADE_REQUEST";

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
    /* PLACEABLE_BARRICADE */ { "barricade", "Barricade",      60,   10,   48,    100 },
    /* PLACEABLE_SPIKES    */ { "spikes",    "Spike strip",    45,   15,   8,     60  },
    /* PLACEABLE_WORKBENCH */ { "workbench", "Workbench",      27,   18,   35,    0   }, // PHA-3856: 1.2x desk
    /* PLACEABLE_CHEST     */ { "chest",     "Storage chest",  26,   20,   44,    0   },
    /* PLACEABLE_SIGN      */ { "",          "Sign",           20,   5,    60,    0   },
    /* PLACEABLE_SCARECROW */ { "scarecrow", "Scarecrow decoy", 18,   18,   75,    80  },
    /* PLACEABLE_GUARDBABA */ { "guardbaba", "Guard Baba",     18,   18,   50,    60  },
    /* PLACEABLE_TORCH     */ { "torch",     "Torch",          10,   10,   60,    0   },
    /* PLACEABLE_STONEWALL */ { "stonewall", "Stone wall",     60,   30,   60,    200 },
    /* PLACEABLE_BOMBTRAP  */ { "bombtrap",  "Bomb-flower trap", 22, 22,   8,     0   },
    /* PLACEABLE_GATE      */ { "gate",      "Player gate",    60,   8,    90,    150 },
    /* PLACEABLE_IRONWALL  */ { "ironwall",  "Iron wall",      60,   30,   60,    400 },
    /* PLACEABLE_PALISADE  */ { "palisade",  "Palisade wall",  62,   16,   96,    150 },
    // PHA-3945: floors tile on a 120 grid; the deck, ladder and stairs are a storey (104) tall.
    /* PLACEABLE_FLOOR_PLANK */ { "floorplank", "Plank floor",  60,   60,   8,     100 },
    /* PLACEABLE_FLOOR_RANCH */ { "floorranch", "Ranch floor",  60,   60,   6,     80  },
    /* PLACEABLE_FLOOR_STONE */ { "floorstone", "Stone platform", 60, 60,   24,    200 },
    /* PLACEABLE_DECK        */ { "deck",       "Festival deck", 60,  60,   STOREY_HEIGHT, 150 },
    /* PLACEABLE_STEP        */ { "step",       "Wooden step",  30,   30,   52,    80  },
    /* PLACEABLE_LADDER      */ { "ladder",     "Ladder",       15,   4,    STOREY_HEIGHT, 60 },
    /* PLACEABLE_STAIRS      */ { "stairs",     "Inn staircase", 32,  60,   STOREY_HEIGHT, 120 },
    /* PLACEABLE_DOOR_SWAMP  */ { "doorswamp",  "Swamp door",   60,   8,    100,   120 },
    /* PLACEABLE_DOOR_MUSIC  */ { "doormusic",  "Music Box door", 60, 8,    100,   150 },
    /* PLACEABLE_DOOR_PIRATE */ { "doorpirate", "Pirate door",  60,   8,    100,   200 },
    // PHA-3962: furniture, Majora's Mask models at about 1.3x, sized to Link. The box is
    // the whole piece; seats and beds collide only up to the seat (BuildShape).
    /* PLACEABLE_CHAIR_INN     */ { "chairinn",     "Inn chair",        16, 16, 57, 0 },
    /* PLACEABLE_CHAIR_MILKBAR */ { "chairmilkbar", "Milk Bar chair",   10, 11, 55, 0 },
    /* PLACEABLE_BENCH         */ { "bench",        "Inn bench",        60, 23, 39, 0 },
    /* PLACEABLE_BED_INN       */ { "bedinn",       "Inn bed",          47, 70, 31, 0 },
    /* PLACEABLE_BED_MAYOR     */ { "bedmayor",     "Mayor's bed",      47, 68, 47, 0 },
    /* PLACEABLE_DRESSER       */ { "dresser",      "Inn dresser",      20, 10, 58, 0 },
    /* PLACEABLE_DRAWERS       */ { "drawers",      "Chest of drawers", 20, 13, 58, 0 },
    /* PLACEABLE_BOOKSHELF     */ { "bookshelf",    "Bookshelf",        48, 12, 96, 0 },
    /* PLACEABLE_PAINTING      */ { "painting",     "Skull Kid painting", 29, 3, 73, 0 },
    /* PLACEABLE_MILKCAN       */ { "milkcan",      "Milk can",         20, 17, 53, 0 },
    /* PLACEABLE_RUG           */ { "rug",          "Rug",              46, 46, 1,  0 },
    /* PLACEABLE_BARREL        */ { "barrel",       "Barrel",           30, 27, 60, 0 },
    /* PLACEABLE_BARREL_ROMANI */ { "barrelromani", "Ranch barrel",     37, 32, 52, 0 },
    /* PLACEABLE_WAGONWHEEL    */ { "wagonwheel",   "Wagon wheel",      33, 4,  65, 0 },
    /* PLACEABLE_STALL         */ { "stall",        "Festival stall",   60, 60, 70, 0 },
};
// clang-format on

const PlaceableInfo& GetPlaceableInfo(uint8_t type) {
    return sPlaceables[type < PLACEABLE_COUNT ? type : 0];
}

bool IsFloorType(uint8_t type) {
    return type == PLACEABLE_FLOOR_PLANK || type == PLACEABLE_FLOOR_RANCH || type == PLACEABLE_FLOOR_STONE;
}

bool IsWalkOverType(uint8_t type) {
    return type == PLACEABLE_SPIKES || IsFloorType(type) || type == PLACEABLE_RUG;
}

FurnitureUse FurnitureUseOf(uint8_t type) {
    switch (type) {
        case PLACEABLE_CHAIR_INN:
        case PLACEABLE_CHAIR_MILKBAR:
        case PLACEABLE_BENCH:
            return USE_SIT;
        case PLACEABLE_BED_INN:
        case PLACEABLE_BED_MAYOR:
            return USE_SLEEP;
        case PLACEABLE_RUG:
            return USE_LIE;
        case PLACEABLE_MILKCAN:
            return USE_DRINK;
        case PLACEABLE_DRESSER:
        case PLACEABLE_DRAWERS:
            return USE_STORAGE;
        case PLACEABLE_BOOKSHELF:
        case PLACEABLE_PAINTING:
            return USE_READ;
    }
    return USE_NONE;
}

// The models' own seat and mattress heights at the size they are drawn (Placeables.cpp).
int16_t FurnitureSeatHeight(uint8_t type) {
    switch (type) {
        case PLACEABLE_CHAIR_INN:
            return 26; // 20 in the inn, at 1.3x
        case PLACEABLE_CHAIR_MILKBAR:
            return 27; // 210 at 0.13
        case PLACEABLE_BENCH:
            return 20;
        case PLACEABLE_BED_INN:
            return 29;
        case PLACEABLE_BED_MAYOR:
            return 21;
        case PLACEABLE_RUG:
            return 1;
    }
    return 0;
}

bool IsDoorType(uint8_t type) {
    return type == PLACEABLE_GATE || type == PLACEABLE_DOOR_SWAMP || type == PLACEABLE_DOOR_MUSIC ||
           type == PLACEABLE_DOOR_PIRATE;
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

// PHA-3935 (M9): towns that board themselves up once the raids have started, like
// the Kokiri village. Decoration only: not in BaseState, so never packed up, damaged,
// counted or saved. Ids from DECOR_ID_BASE; child era, after the first raid. Each pair
// flanks the way in and leaves the path open. y is re-snapped to the floor on spawn.
constexpr uint16_t DECOR_ID_BASE = 0xF000;
struct DecorSeed {
    int16_t scene;
    uint8_t type;
    float x, y, z;
    int16_t rot;
    uint16_t hpPercent;
};
// clang-format off
static const DecorSeed sTownDecor[] = {
    { SCENE_KAKARIKO_VILLAGE, PLACEABLE_BARRICADE, -2100.0f, 138.0f, 1182.0f, 0x0000, 80 }, // inside the field gate, north
    { SCENE_KAKARIKO_VILLAGE, PLACEABLE_BARRICADE, -2100.0f, 138.0f,  942.0f, 0x0000, 45 }, // ...south, already knocked askew
};
// clang-format on
static std::vector<Placeable> sDecor;

static bool IsDecor(uint16_t id) {
    return id >= DECOR_ID_BASE;
}

static const Placeable* FindDecor(uint16_t id) {
    if (sDecor.empty()) {
        for (size_t i = 0; i < std::size(sTownDecor); i++) {
            const DecorSeed& s = sTownDecor[i];
            Placeable p;
            p.id = (uint16_t)(DECOR_ID_BASE + i);
            p.type = s.type;
            p.era = ERA_CHILD;
            p.scene = s.scene;
            p.pos[0] = s.x;
            p.pos[1] = s.y;
            p.pos[2] = s.z;
            p.rot = s.rot;
            p.hp = (uint16_t)(GetPlaceableInfo(s.type).maxHp * s.hpPercent / 100);
            sDecor.push_back(p);
        }
    }
    size_t i = id - DECOR_ID_BASE;
    return i < sDecor.size() ? &sDecor[i] : nullptr;
}

const Placeable* FindPlaceable(uint16_t id) {
    return IsDecor(id) ? FindDecor(id) : FindPlaceableMut(id);
}

int BaseCap() {
    return std::clamp(CVarGetInteger(CVAR_SEVEN_DAYS("BaseCap"), BASE_CAP_DEFAULT), 1, BASE_CAP_MAX);
}

static int CountEra(int era) {
    int n = 0;
    for (auto& p : sBase.placeables) {
        n += p.era == era;
    }
    return n;
}

// The cap is the scene's collision budget, so each scene counts its own pieces.
static int CountEraIn(int era, int16_t scene) {
    int n = 0;
    for (auto& p : sBase.placeables) {
        n += p.era == era && p.scene == scene;
    }
    return n;
}

static std::map<std::string, int> sRefusals; // owner: why placements were refused (sevendays_test_base)

// PHA-4062: the chunked collision lists (Placeables.cpp) hold DYNA_BUDGET polygons, vertices and
// poly nodes per scene. The first two are exact sums; the nodes (one per polygon per grid cell it
// crosses) are estimated from the polygon count, so a base of long, flat pieces cannot run them out
// and silently lose collision (DynaSSNodeList_GetNextNodeIdx returns SS_NULL once the list is full).
static constexpr int kCollisionNodesPerPolyQ = 5; // nodes = polys * 5 / 4: measured 1.03 per poly (mixed base), PHA-4062
static constexpr int kCollisionBudgetPercent = 90;

static bool CollisionFits(int era, int16_t scene, uint8_t type) {
    CollisionCost add = PieceCollisionCost(type);
    int polys = add.polys, verts = add.verts;
    for (auto& p : sBase.placeables) {
        if (p.era == era && p.scene == scene) {
            CollisionCost c = PieceCollisionCost(p.type);
            polys += c.polys;
            verts += c.verts;
        }
    }
    int cap = DYNA_BUDGET / 100 * kCollisionBudgetPercent;
    return polys <= cap && verts <= cap && polys * kCollisionNodesPerPolyQ / 4 <= cap;
}

// PHA-4027: outposts. Away from the base's scene, a workbench starts a camp, and
// pieces go within BASE_RADIUS of any of the era's workbenches in that scene. The
// base itself (raids, the village) stays where its first workbench is.
static bool NearOutpostWorkbench(int era, int16_t scene, float x, float z) {
    for (auto& p : sBase.placeables) {
        if (p.era != era || p.scene != scene || p.type != PLACEABLE_WORKBENCH) {
            continue;
        }
        float dx = p.pos[0] - x, dz = p.pos[2] - z;
        if (dx * dx + dz * dz <= BASE_RADIUS * BASE_RADIUS) {
            return true;
        }
    }
    return false;
}

uint32_t CurrentDay() {
    return sBase.daysSurvived + 1;
}

// MARK: - JSON (packets, save section and team state share one format)

static nlohmann::json PlaceableToJson(const Placeable& p) {
    nlohmann::json j = { { "id", p.id },     { "type", p.type },
                         { "era", p.era },   { "scene", p.scene },
                         { "pos", { p.pos[0], p.pos[1], p.pos[2] } },
                         { "rot", p.rot },   { "hp", p.hp } };
    if (p.stacked) {
        j["stk"] = true;
    }
    return j;
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
    p.stacked = j.value("stk", false);
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
                      { "raidInterval", sBase.raidInterval },
                      { "seedRev", sBase.seedRev },
                      { "night",
                        { { "day", sBase.nightDay },
                          { "fought", sBase.nightFought },
                          { "failed", sBase.nightFailed },
                          { "gamestage", sBase.nightGamestage },
                          { "warded", sBase.nightWarded } } } };
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
    b.seedRev = counters.value("seedRev", 0u);
    auto night = counters.value("night", nlohmann::json::object());
    b.nightDay = night.value("day", 0u);
    b.nightFought = night.value("fought", false);
    b.nightFailed = night.value("failed", false);
    b.nightGamestage = night.value("gamestage", 0);
    b.nightWarded = night.value("warded", false);
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
// PHA-3904 (Brandon, 2026-10-04): palisade walls around the village. They carry the
// barricade lines at both Lost Woods exits out toward the cliffs (the gaps stay open,
// so the story route does too), and fence the north ramp and east edge of the dip by
// Link's house, leaving a way in. 96 tall, so Link can't climb them; raiders break through.
static const Seed sPalisadeSeeds[] = {
    { PLACEABLE_PALISADE, -1240.0f, -80.0f, -502.0f, 0x4000, 100 }, // Lost Woods bridge path, north of the barricades
    { PLACEABLE_PALISADE, -1240.0f, -80.0f, -626.0f, 0x4000, 100 },
    { PLACEABLE_PALISADE, -1240.0f, -80.0f,  -78.0f, 0x4000, 100 }, // ...south of them
    { PLACEABLE_PALISADE,  -472.0f, 380.0f, -1180.0f, 0x0000, 100 }, // Lost Woods ledge path, west of the barricades
    { PLACEABLE_PALISADE,  -596.0f, 380.0f, -1180.0f, 0x0000, 100 },
    { PLACEABLE_PALISADE,   -28.0f, 380.0f, -1180.0f, 0x0000, 100 }, // ...east of them
    { PLACEABLE_PALISADE,  -188.0f, -60.0f,  640.0f, 0x0000, 100 }, // the yard's north ramp, west side
    { PLACEABLE_PALISADE,   108.0f, -60.0f,  640.0f, 0x0000, 100 }, // ...east side
    { PLACEABLE_PALISADE,   170.0f, -80.0f,  800.0f, 0x4000, 100 }, // the yard's east edge
    { PLACEABLE_PALISADE,   170.0f, -80.0f,  924.0f, 0x4000, 100 },
    { PLACEABLE_PALISADE,   170.0f, -80.0f, 1048.0f, 0x4000, 100 },
};
// clang-format on

// Bump when the village gains pieces, so saves made before get them (SeedVillageUpgrade).
constexpr uint32_t SEED_REV = 1;

static void AddSeed(const Seed& s) {
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
}

void SeedVillage() {
    sBase = {};
    for (auto& s : sVillageSeeds) {
        AddSeed(s);
        if (s.type == PLACEABLE_WORKBENCH && !sBase.center[ERA_CHILD].valid) {
            sBase.center[ERA_CHILD] = { true, SCENE_KOKIRI_FOREST, { s.x, s.y, s.z } };
        }
    }
    for (auto& s : sPalisadeSeeds) {
        AddSeed(s);
    }
    sBase.seedRev = SEED_REV;
}

// A save from before the palisades: add the ones with room, while the base is still
// the child-era one in Kokiri Forest. Anything the players built stays; a palisade
// that would overlap a piece is left out.
void SeedVillageUpgrade() {
    if (sBase.seedRev >= SEED_REV) {
        return;
    }
    sBase.seedRev = SEED_REV;
    const BaseCenter& c = sBase.center[ERA_CHILD];
    if (!c.valid || c.scene != SCENE_KOKIRI_FOREST) {
        return;
    }
    for (auto& p : sBase.placeables) {
        if (p.era == ERA_RUINS) {
            return;
        }
    }
    for (auto& s : sPalisadeSeeds) {
        if (CountEra(ERA_CHILD) >= BaseCap()) {
            break;
        }
        const PlaceableInfo& info = GetPlaceableInfo(s.type);
        bool clear = true;
        for (auto& p : sBase.placeables) {
            if (p.scene != SCENE_KOKIRI_FOREST) {
                continue;
            }
            const PlaceableInfo& other = GetPlaceableInfo(p.type);
            float r = (float)(info.halfX + std::max(other.halfX, other.halfZ));
            float dx = p.pos[0] - s.x, dz = p.pos[2] - s.z;
            if (dx * dx + dz * dz < r * r && fabsf(p.pos[1] - s.y) < 150.0f) {
                clear = false;
                break;
            }
        }
        if (clear) {
            AddSeed(s);
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
    return fmt::format("Base: {}/{} pieces in {} | Days survived: {} | Raids survived: {}", CountEra(era), BaseCap(),
                       where ? where : "?", sBase.daysSurvived, sBase.hordeNightsSurvived);
}

// MARK: - Spawning (every client, its own copy)

bool IsRuin(const Placeable& p) {
    return p.era == ERA_RUINS;
}

// The ruins of the child base stand in the adult era.
static bool SpawnsHere(const Placeable& p) {
    return gPlayState != nullptr && p.scene == gPlayState->sceneNum &&
           (p.era == CurrentEra() || (IsRuin(p) && CurrentEra() == ERA_ADULT));
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

static bool DecorHere(const Placeable& p) {
    return gPlayState != nullptr && p.scene == gPlayState->sceneNum && CurrentEra() == ERA_CHILD &&
           (sBase.story & STORY_FIRST_RAID_DONE) != 0 && !IS_RANDO;
}

// Bring the current scene's actors in line with sBase (after a state change or scene load).
static void SyncSceneActors() {
    if (gPlayState == nullptr || !BaseEnabled()) {
        return;
    }
    for (auto it = sSpawned.begin(); it != sSpawned.end();) {
        const Placeable* p = FindPlaceable(it->first);
        // A piece that turned to ruins under its actor (the seven-year jump at the base)
        // is respawned too, so it draws and acts as a ruin.
        if (p == nullptr || !(IsDecor(p->id) ? DecorHere(*p) : SpawnsHere(*p)) ||
            PlaceableActorIsRuin(it->second) != IsRuin(*p)) {
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
    for (size_t i = 0; i < std::size(sTownDecor); i++) {
        const Placeable* p = FindDecor((uint16_t)(DECOR_ID_BASE + i));
        if (p != nullptr && DecorHere(*p) && !sSpawned.contains(p->id)) {
            SpawnPlaceableActor(*p);
        }
    }
}

std::vector<std::pair<uint16_t, Actor*>> SpawnedPlaceables() {
    return { sSpawned.begin(), sSpawned.end() };
}

Actor* SpawnedPlaceableActor(uint16_t id) {
    auto it = sSpawned.find(id);
    return it != sSpawned.end() ? it->second : nullptr;
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

static void EndPlacement();

static void OnPlaceResult(const nlohmann::json& payload) {
    if (payload.value("reqId", 0u) != sPlaceInFlight.reqId) {
        return;
    }
    sPlaceInFlight = {};
    uint8_t type = payload.value("ptype", (uint8_t)0);
    if (payload.value("ok", false)) {
        // PHA-4018: placement stays open for the next kit of the same piece until B,
        // the pause menu, or the last kit.
        auto kit = GetPool().kits.find(GetPlaceableInfo(type).kit);
        uint32_t left = payload.value("left", kit != GetPool().kits.end() ? kit->second : 0u);
        if (InPlacement() && PlacementGhostType() == type) {
            if (left == 0) {
                EndPlacement();
                Toast("Built", fmt::format("{} (that was the last kit)", GetPlaceableInfo(type).name), false);
                return;
            }
            Toast("Built", fmt::format("{} ({} left: A place another, B done)", GetPlaceableInfo(type).name, left),
                  false);
            return;
        }
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
// center), one base per era plus workbench outposts elsewhere (PHA-4027), at most
// BaseCap() pieces per scene.
static void ProcessPlaceRequest(const nlohmann::json& payload, uint32_t requester) {
    nlohmann::json result;
    result["type"] = PLACE_RESULT;
    result["reqId"] = payload.value("reqId", 0u);
    uint8_t type = payload.value("ptype", (uint8_t)0xFF);
    result["ptype"] = type;
    auto refuse = [&](const std::string& reason) {
        sRefusals[reason]++;
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
            if (type != PLACEABLE_WORKBENCH && !NearOutpostWorkbench(era, scene, pos[0], pos[2])) {
                return refuse("Build a workbench first: it starts a camp here");
            }
        } else {
            float dx = pos[0] - center.pos[0], dz = pos[2] - center.pos[2];
            if (sqrtf(dx * dx + dz * dz) > BASE_RADIUS) {
                return refuse("Too far from the base's workbench");
            }
        }
    }
    if (CountEraIn(era, scene) >= BaseCap()) {
        return refuse(fmt::format("The base is full ({} pieces)", BaseCap()));
    }
    if (!CollisionFits(era, scene, type)) {
        return refuse("The ground here cannot take more structure");
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
    p.stacked = payload.value("stk", false);
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
    result["left"] = kit->second; // PHA-4018: the builder keeps placing while this is above 0
    Reply(requester, result);
}

// PHA-3935: a piece's kit materials scaled by num/den, rounded down or up.
static std::vector<RecipeInput> ScaledKit(const Placeable& p, uint32_t num, uint32_t den, bool roundUp) {
    std::vector<RecipeInput> out;
    const Recipe* r = FindRecipe(GetPlaceableInfo(p.type).kit);
    if (r == nullptr || den == 0) {
        return out;
    }
    for (int i = 0; i < r->inputCount; i++) {
        uint32_t n = r->inputs[i].amount * num;
        n = roundUp ? (n + den - 1) / den : n / den;
        if (n > 0) {
            out.push_back({ r->inputs[i].material, (uint16_t)n });
        }
    }
    return out;
}

static std::string MaterialsText(const std::vector<RecipeInput>& list) {
    std::string s;
    for (auto& in : list) {
        s += fmt::format("{}{} {}", s.empty() ? "" : ", ", in.amount, GetMaterialInfo(in.material).name);
    }
    return s.empty() ? "nothing" : s;
}

static bool Damaged(const Placeable& p) {
    uint16_t maxHp = GetPlaceableInfo(p.type).maxHp;
    return maxHp > 0 && p.hp < maxHp;
}

// What a repair costs: the kit's materials for the missing share of HP, rounded up.
// The Megaton Hammer halves it (spec: "fast repair").
static std::vector<RecipeInput> RepairInputs(const Placeable& p, bool hammer) {
    uint16_t maxHp = GetPlaceableInfo(p.type).maxHp;
    return ScaledKit(p, maxHp - p.hp, hammer ? maxHp * 2u : maxHp, true);
}

// Packing up: a whole piece goes back into its kit. A damaged one only returns the
// share of the kit's materials its HP still holds, so packing up is never a free repair.
static std::vector<RecipeInput> RefundKit(const Placeable& p, PoolState& pool, bool* asKit = nullptr) {
    const char* kit = GetPlaceableInfo(p.type).kit;
    if (asKit != nullptr) {
        *asKit = false;
    }
    if (kit[0] == '\0' || IsRuin(p)) {
        return {};
    }
    if (!Damaged(p)) {
        pool.kits[kit]++;
        if (asKit != nullptr) {
            *asKit = true;
        }
        return {};
    }
    auto back = ScaledKit(p, p.hp, GetPlaceableInfo(p.type).maxHp, false);
    for (auto& in : back) {
        pool.materials[in.material] += in.amount;
    }
    return back;
}

static void ApplyHp(uint16_t id, int hp);
static void DropPiecesOn(const Placeable& gone);

// Where a piece stood, for DropPiecesOn: its spawned actor's y after any re-snap.
static Placeable AsItStood(const Placeable& p) {
    Placeable gone = p;
    auto spawned = sSpawned.find(p.id);
    if (spawned != sSpawned.end() && spawned->second != nullptr) {
        gone.pos[1] = spawned->second->world.pos.y;
    }
    return gone;
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
        int refunded = 0, salvaged = 0;
        std::erase_if(sBase.placeables, [&](const Placeable& p) {
            // Seeded pieces with no kit (the village's "Day 1" sign) aren't the base's to pack.
            if (p.era != era || GetPlaceableInfo(p.type).kit[0] == '\0') {
                return false;
            }
            bool asKit = false;
            RefundKit(p, pool, &asKit);
            (asKit ? refunded : salvaged)++;
            return true;
        });
        sBase.center[era] = {};
        sBase.rev++;
        pool.rev++;
        BroadcastState();
        Net::BroadcastPool();
        SyncSceneActors();
        tell(salvaged == 0 ? fmt::format("Packed up the base: {} pieces back in the pool", refunded)
                           : fmt::format("Packed up the base: {} kits back in the pool, {} damaged pieces salvaged",
                                         refunded, salvaged),
             false);
        return;
    }

    uint16_t id = payload.value("id", (uint16_t)0);
    Placeable* p = FindPlaceableMut(id);
    if (p == nullptr) {
        return tell("That piece is already gone", true);
    }
    bool isCenter = p->type == PLACEABLE_WORKBENCH && !IsRuin(*p) && sBase.center[p->era].valid &&
                    fabsf(sBase.center[p->era].pos[0] - p->pos[0]) < 1.0f &&
                    fabsf(sBase.center[p->era].pos[2] - p->pos[2]) < 1.0f;
    if (isCenter && CountEra(p->era) > 1) {
        return tell("The workbench holds the base together. Pack up the whole base to move it", true);
    }
    if (GetPlaceableInfo(p->type).kit[0] == '\0' && !IsRuin(*p)) {
        return tell(fmt::format("The {} stays: there's no kit to pack it into", GetPlaceableInfo(p->type).name), true);
    }
    int era = p->era;
    bool ruin = IsRuin(*p);
    bool asKit = false;
    auto salvage = RefundKit(*p, pool, &asKit);
    std::string name = GetPlaceableInfo(p->type).name;
    Placeable gone = AsItStood(*p);
    std::erase_if(sBase.placeables, [id](const Placeable& q) { return q.id == id; }); // p dangles from here
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
    // Whatever stood on it with nothing else under it falls and breaks, as when it breaks.
    size_t before = sBase.placeables.size();
    DropPiecesOn(gone);
    size_t fell = before - sBase.placeables.size();
    std::string fallen = fell == 0 ? "" : fmt::format(". {} piece{} on it fell", fell, fell == 1 ? "" : "s");
    if (ruin) {
        tell(fmt::format("Cleared the ruined {}{}", name, fallen), false);
    } else if (asKit) {
        tell(fmt::format("{} packed back into a kit{}", name, fallen), false);
    } else {
        tell(fmt::format("{} was damaged: salvaged {}. Repair it first to get the kit back{}", name,
                         MaterialsText(salvage), fallen),
             false);
    }
}

// PHA-3935: the Megaton Hammer rebuilds walls in place, wood -> stone -> iron. It costs
// the difference between the two kits (never less than one of each new material),
// and the piece keeps its id, place and share of HP.
static int UpgradeTargetOf(uint8_t type) {
    switch (type) {
        case PLACEABLE_BARRICADE:
            return PLACEABLE_STONEWALL;
        case PLACEABLE_STONEWALL:
            return PLACEABLE_IRONWALL;
    }
    return -1;
}

static Unlock UpgradeUnlock(int to) {
    return to == PLACEABLE_IRONWALL ? UNLOCK_SILVER_GAUNTLETS : UNLOCK_HAMMER;
}

static std::vector<RecipeInput> UpgradeInputs(uint8_t from, uint8_t to) {
    std::vector<RecipeInput> out;
    const Recipe* a = FindRecipe(GetPlaceableInfo(from).kit);
    const Recipe* b = FindRecipe(GetPlaceableInfo(to).kit);
    if (a == nullptr || b == nullptr) {
        return out;
    }
    for (int i = 0; i < b->inputCount; i++) {
        int have = 0;
        for (int k = 0; k < a->inputCount; k++) {
            have += a->inputs[k].material == b->inputs[i].material ? a->inputs[k].amount : 0;
        }
        int need = std::max<int>(b->inputs[i].amount - have, have == 0 ? 1 : 0);
        if (need > 0) {
            out.push_back({ b->inputs[i].material, (uint16_t)need });
        }
    }
    return out;
}

static const Placeable* UpgradeBlocker(const Placeable& p, uint8_t to);

static void ProcessUpgradeRequest(const nlohmann::json& payload, uint32_t requester) {
    auto tell = [&](const std::string& msg, bool error) {
        if (requester == OwnId()) {
            Toast("Upgrade", msg, error);
        } else {
            nlohmann::json r;
            r["type"] = PLACE_RESULT;
            r["reqId"] = 0;
            r["notice"] = msg;
            r["error"] = error;
            SendTo(requester, r);
        }
    };
    uint16_t id = payload.value("id", (uint16_t)0);
    Placeable* p = FindPlaceableMut(id);
    if (p == nullptr || IsRuin(*p)) {
        return tell("That piece is gone", true);
    }
    int to = UpgradeTargetOf(p->type);
    if (to < 0) {
        return tell(fmt::format("The {} can't be upgraded", GetPlaceableInfo(p->type).name), true);
    }
    if (!payload.value("hammer", false)) {
        return tell("Upgrading walls takes the Megaton Hammer", true);
    }
    if (to == PLACEABLE_IRONWALL && !payload.value("silver", false)) {
        return tell("Iron walls take the Silver Gauntlets' iron", true);
    }
    if (const Placeable* in = UpgradeBlocker(*p, (uint8_t)to)) {
        return tell(fmt::format("No room for a {} there: the {} is in the way", GetPlaceableInfo(to).name,
                                GetPlaceableInfo(in->type).name),
                    true);
    }
    auto cost = UpgradeInputs(p->type, (uint8_t)to);
    PoolState& pool = Net::MutablePool();
    for (auto& in : cost) {
        if (pool.materials[in.material] < in.amount) {
            return tell(fmt::format("Upgrading to a {} takes {}", GetPlaceableInfo(to).name, MaterialsText(cost)), true);
        }
    }
    for (auto& in : cost) {
        pool.materials[in.material] -= in.amount;
    }
    pool.rev++;
    std::string from = GetPlaceableInfo(p->type).name;
    uint16_t oldMax = GetPlaceableInfo(p->type).maxHp, newMax = GetPlaceableInfo(to).maxHp;
    p->hp = (uint16_t)std::max<int>(1, oldMax > 0 ? (int)p->hp * newMax / oldMax : newMax);
    p->type = (uint8_t)to;
    sBase.rev++;
    nlohmann::json delta;
    delta["op"] = "replace";
    delta["placeable"] = PlaceableToJson(*p);
    BroadcastDelta(delta);
    Net::BroadcastPool();
    Placeable copy = *p;
    Despawn(id);
    if (SpawnsHere(copy)) {
        SpawnPlaceableActor(copy);
    }
    tell(fmt::format("{} rebuilt as a {} for {}", from, GetPlaceableInfo(to).name, MaterialsText(cost)), false);
}

static void ProcessRepairRequest(const nlohmann::json& payload, uint32_t requester) {
    auto tell = [&](const std::string& msg, bool error) {
        if (requester == OwnId()) {
            Toast("Repair", msg, error);
        } else {
            nlohmann::json r;
            r["type"] = PLACE_RESULT;
            r["reqId"] = 0;
            r["notice"] = msg;
            r["error"] = error;
            SendTo(requester, r);
        }
    };
    uint16_t id = payload.value("id", (uint16_t)0);
    Placeable* p = FindPlaceableMut(id);
    if (p == nullptr) {
        return tell("That piece is gone", true);
    }
    const PlaceableInfo& info = GetPlaceableInfo(p->type);
    if (IsRuin(*p)) {
        return tell("Ruins can't be repaired. Clear them and build again", true);
    }
    if (!Damaged(*p)) {
        return tell(fmt::format("The {} doesn't need repairs", info.name), true);
    }
    auto cost = RepairInputs(*p, payload.value("hammer", false));
    PoolState& pool = Net::MutablePool();
    for (auto& in : cost) {
        if (pool.materials[in.material] < in.amount) {
            return tell(fmt::format("Repairing the {} takes {}", info.name, MaterialsText(cost)), true);
        }
    }
    for (auto& in : cost) {
        pool.materials[in.material] -= in.amount;
    }
    pool.rev++;
    ApplyHp(id, info.maxHp);
    Net::BroadcastPool();
    tell(fmt::format("{} repaired for {}", info.name, MaterialsText(cost)), false);
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
        Placeable gone = AsItStood(*p);
        std::erase_if(sBase.placeables, [id](const Placeable& q) { return q.id == id; });
        delta["op"] = "remove";
        delta["id"] = id;
        delta["broken"] = true;
        Despawn(id);
        BroadcastDelta(delta);
        DropPiecesOn(gone); // PHA-3945: what stood on it falls with it
        return;
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
    if (IsDecor(id)) {
        return; // a town's boards are scenery
    }
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
    } else if (op == "replace" && d.contains("placeable")) {
        // An upgrade: same id and place, new type. The actor caches its type: respawn it.
        Placeable q = PlaceableFromJson(d["placeable"]);
        if (Placeable* p = FindPlaceableMut(q.id)) {
            *p = q;
        }
        Despawn(q.id);
        if (SpawnsHere(q)) {
            SpawnPlaceableActor(q);
        }
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
           type == BASE_STATE || type == BASE_REQUEST || type == BASE_HP || type == REPAIR_REQUEST ||
           type == UPGRADE_REQUEST;
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
    } else if (type == REPAIR_REQUEST) {
        if (IsOwner()) {
            ProcessRepairRequest(payload, from);
        }
    } else if (type == UPGRADE_REQUEST) {
        if (IsOwner()) {
            ProcessUpgradeRequest(payload, from);
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
    bool stacked = false; // PHA-3945: on top of another piece
    int16_t finalRot = 0; // PHA-3945: rot, or the angle a snap turned it to
    bool armed = false;   // A has been up since placement began: a held A doesn't place
    bool unpaused = false; // PHA-4018: the pause menu has been shut since placement began
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
                         .message = "C-Left/C-Right rotate, A place, B done",
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

// MARK: - PHA-3945: stacking and snapping

constexpr float TILE = 120.0f; // floors and the deck are 120 x 120

static bool IsTileType(uint8_t type) {
    return IsFloorType(type) || type == PLACEABLE_DECK;
}

// A piece's box: footprint centre, half extents and yaw, and the heights it spans.
struct PieceBox {
    float x, z, y0, y1, hx, hz;
    int16_t rot;
};

static PieceBox BoxAt(uint8_t type, float x, float y, float z, int16_t rot) {
    const PlaceableInfo& info = GetPlaceableInfo(type);
    return { x, z, y, y + info.height, (float)info.halfX, (float)info.halfZ, rot };
}

// The spawned actor's y when there is one: Placeable_Init re-snaps ground pieces.
static PieceBox BoxOf(const Placeable& p) {
    auto it = sSpawned.find(p.id);
    float y = it != sSpawned.end() && it->second != nullptr ? it->second->world.pos.y : p.pos[1];
    return BoxAt(p.type, p.pos[0], y, p.pos[2], p.rot);
}

// World offset (dx, dz) in a piece's frame. Its local +z is ahead of Link when he placed it.
static void ToLocal(int16_t rot, float dx, float dz, float* lx, float* lz) {
    float c = Math_CosS(rot), s = Math_SinS(rot);
    *lx = dx * c - dz * s;
    *lz = dx * s + dz * c;
}

static void ToWorld(int16_t rot, float lx, float lz, float* dx, float* dz) {
    float c = Math_CosS(rot), s = Math_SinS(rot);
    *dx = lx * c + lz * s;
    *dz = -lx * s + lz * c;
}

// Separating axes: do two footprints overlap by more than `slack`?
static bool FootprintsOverlap(const PieceBox& a, const PieceBox& b, float slack) {
    const PieceBox* boxes[2] = { &a, &b };
    float dx = b.x - a.x, dz = b.z - a.z;
    for (const PieceBox* axisBox : boxes) {
        float c = Math_CosS(axisBox->rot), s = Math_SinS(axisBox->rot);
        float axes[2][2] = { { c, -s }, { s, c } }; // the box's local x and z, in the world
        for (auto& u : axes) {
            float dist = fabsf(dx * u[0] + dz * u[1]);
            float r = 0.0f;
            for (const PieceBox* q : boxes) {
                float qc = Math_CosS(q->rot), qs = Math_SinS(q->rot);
                r += q->hx * fabsf(u[0] * qc - u[1] * qs) + q->hz * fabsf(u[0] * qs + u[1] * qc);
            }
            if (dist >= r - slack) {
                return false;
            }
        }
    }
    return true;
}

static bool FootprintContains(const PieceBox& b, float x, float z, float margin) {
    float lx, lz;
    ToLocal(b.rot, x - b.x, z - b.z, &lx, &lz);
    return fabsf(lx) <= b.hx + margin && fabsf(lz) <= b.hz + margin;
}

// The piece Link is standing on (a floor, a wall top, the stairs), or null.
static const Placeable* PieceUnderLink(Player* player) {
    if (player->actor.floorBgId == BGCHECK_SCENE || !(player->actor.bgCheckFlags & 1)) {
        return nullptr;
    }
    const Placeable* best = nullptr;
    float bestTop = -1e9f;
    float y = player->actor.world.pos.y;
    for (auto& p : sBase.placeables) {
        if (!SpawnsHere(p) || IsRuin(p)) {
            continue;
        }
        PieceBox b = BoxOf(p);
        if (!FootprintContains(b, player->actor.world.pos.x, player->actor.world.pos.z, 4.0f)) {
            continue;
        }
        // Anywhere on the stairs counts as their top: the floor goes where they lead.
        bool on = p.type == PLACEABLE_STAIRS ? (y > b.y0 - 2.0f && y < b.y1 + 6.0f) : fabsf(y - b.y1) < 12.0f;
        if (on && b.y1 > bestTop) {
            best = &p;
            bestTop = b.y1;
        }
    }
    return best;
}

// The piece whose top is at y under (x, z): what a stacked piece stands on.
static const Placeable* PieceWithTopAt(float x, float z, float y) {
    for (auto& p : sBase.placeables) {
        if (SpawnsHere(p) && !IsRuin(p)) {
            PieceBox b = BoxOf(p);
            if (fabsf(b.y1 - y) < 3.0f && FootprintContains(b, x, z, 2.0f)) {
                return &p;
            }
        }
    }
    return nullptr;
}

// Floors and decks line up with the tile they are next to, turned the same way.
static void SnapToTile(const PieceBox& ref, float* x, float* z, int16_t* rot) {
    float lx, lz, dx, dz;
    ToLocal(ref.rot, *x - ref.x, *z - ref.z, &lx, &lz);
    ToWorld(ref.rot, roundf(lx / TILE) * TILE, roundf(lz / TILE) * TILE, &dx, &dz);
    *x = ref.x + dx;
    *z = ref.z + dz;
    *rot = ref.rot;
}

static const Placeable* NearestTileAtLevel(float x, float z, float y) {
    const Placeable* best = nullptr;
    float bestD = 200.0f * 200.0f;
    for (auto& p : sBase.placeables) {
        if (!SpawnsHere(p) || IsRuin(p) || !IsTileType(p.type)) {
            continue;
        }
        PieceBox b = BoxOf(p);
        float dx = b.x - x, dz = b.z - z;
        if (fabsf(b.y0 - y) < 20.0f && dx * dx + dz * dz < bestD) {
            best = &p;
            bestD = dx * dx + dz * dz;
        }
    }
    return best;
}

// Ladders and stairs pointed at the edge of something about a storey up (a floor on the
// walls, the deck) snap flush against that edge, facing it, so they lead onto it.
static bool SnapToLanding(uint8_t type, float* x, float* z, float groundY, int16_t* rot) {
    const PlaceableInfo& info = GetPlaceableInfo(type);
    const Placeable* best = nullptr;
    float bestD = 60.0f;
    for (auto& p : sBase.placeables) {
        if (!SpawnsHere(p) || IsRuin(p) || p.type == PLACEABLE_LADDER || p.type == PLACEABLE_STAIRS) {
            continue;
        }
        PieceBox b = BoxOf(p);
        float rise = b.y1 - groundY;
        if (rise < 40.0f || rise > 140.0f) {
            continue;
        }
        float lx, lz;
        ToLocal(b.rot, *x - b.x, *z - b.z, &lx, &lz);
        float d = sqrtf(SQ(std::max(0.0f, fabsf(lx) - b.hx)) + SQ(std::max(0.0f, fabsf(lz) - b.hz)));
        if (d < bestD) {
            best = &p;
            bestD = d;
        }
    }
    if (best == nullptr) {
        return false;
    }
    PieceBox b = BoxOf(*best);
    float lx, lz;
    ToLocal(b.rot, *x - b.x, *z - b.z, &lx, &lz);
    // The edge the target is past (or nearest to), and where along it.
    float cx, cz, outX, outZ;
    if (fabsf(lx) - b.hx > fabsf(lz) - b.hz) {
        float side = lx < 0.0f ? -1.0f : 1.0f;
        float along = std::clamp(lz, -std::max(0.0f, b.hz - info.halfX), std::max(0.0f, b.hz - info.halfX));
        cx = side * (b.hx + info.halfZ), cz = along, outX = side, outZ = 0.0f;
    } else {
        float side = lz < 0.0f ? -1.0f : 1.0f;
        float along = std::clamp(lx, -std::max(0.0f, b.hx - info.halfX), std::max(0.0f, b.hx - info.halfX));
        cx = along, cz = side * (b.hz + info.halfZ), outX = 0.0f, outZ = side;
    }
    float dx, dz, ox, oz;
    ToWorld(b.rot, cx, cz, &dx, &dz);
    ToWorld(b.rot, outX, outZ, &ox, &oz);
    *x = b.x + dx;
    *z = b.z + dz;
    // The piece's +z (the stairs' high end) points back at the landing.
    *rot = Math_Atan2S(-oz, -ox);
    // Out past whatever stands under the landing's edge (the wall it rests on).
    PieceBox me = BoxAt(type, *x, groundY, *z, *rot);
    for (int step = 0; step < 16; step++) {
        bool clear = true;
        for (auto& p : sBase.placeables) {
            if (SpawnsHere(p) && !IsRuin(p)) {
                PieceBox o = BoxOf(p);
                if (me.y0 < o.y1 - 1.0f && o.y0 < me.y1 - 1.0f && FootprintsOverlap(me, o, 1.0f)) {
                    clear = false;
                    break;
                }
            }
        }
        if (clear) {
            break;
        }
        me.x = *x += ox * 3.0f;
        me.z = *z += oz * 3.0f;
    }
    return true;
}

// The piece that stops `p` being rebuilt as `to` in place (the new wall is thicker and
// taller), or null: one it would cut into, or one standing on its top that the new top
// would swallow or leave in mid-air.
static const Placeable* UpgradeBlocker(const Placeable& p, uint8_t to) {
    PieceBox now = BoxOf(p);
    PieceBox next = BoxAt(to, now.x, now.y0, now.z, now.rot);
    for (auto& q : sBase.placeables) {
        bool here = q.scene == p.scene && (q.era == p.era || (IsRuin(q) && p.era == ERA_ADULT));
        if (q.id == p.id || !here) {
            continue;
        }
        PieceBox o = BoxOf(q);
        bool cuts = next.y0 < o.y1 - 1.0f && o.y0 < next.y1 - 1.0f && FootprintsOverlap(next, o, 1.0f);
        bool onTop = fabsf(o.y0 - now.y1) <= 3.0f && fabsf(next.y1 - now.y1) > 3.0f && FootprintsOverlap(now, o, 1.0f);
        if (cuts || onTop) {
            return &q;
        }
    }
    return nullptr;
}

// PHA-3945: when a piece breaks, whatever stood on it with nothing else under it falls
// and breaks too (and in turn what stood on that). Floors that only reach out from the
// level beside them stand on nothing, so they stay.
static void DropPiecesOn(const Placeable& gone) {
    PieceBox below = BoxAt(gone.type, gone.pos[0], gone.pos[1], gone.pos[2], gone.rot);
    std::vector<uint16_t> falling;
    for (auto& p : sBase.placeables) {
        if (!p.stacked || p.era != gone.era || p.scene != gone.scene) {
            continue;
        }
        PieceBox b = BoxOf(p);
        if (fabsf(b.y0 - below.y1) > 3.0f || !FootprintsOverlap(b, below, 1.0f)) {
            continue;
        }
        bool held = false;
        for (auto& q : sBase.placeables) {
            if (q.id != p.id && q.era == p.era && q.scene == p.scene) {
                PieceBox o = BoxOf(q);
                if (fabsf(o.y1 - b.y0) <= 3.0f && FootprintsOverlap(b, o, 1.0f)) {
                    held = true;
                    break;
                }
            }
        }
        if (!held) {
            falling.push_back(p.id);
        }
    }
    for (uint16_t id : falling) {
        ApplyHp(id, 0);
    }
}

// Owner: break a piece wherever it stands (an empty-base raid, away from its scene); what
// stood on it falls too. The types of every piece that went, itself first.
std::vector<uint8_t> BreakPiece(uint16_t id) {
    std::vector<uint8_t> gone;
    if (!IsOwner() || FindPlaceable(id) == nullptr) {
        return gone;
    }
    std::map<uint16_t, uint8_t> before;
    for (auto& p : sBase.placeables) {
        before[p.id] = p.type;
    }
    gone.push_back(before[id]);
    ApplyHp(id, 0);
    for (auto& p : sBase.placeables) {
        before.erase(p.id);
    }
    for (auto& [other, type] : before) {
        if (other != id) {
            gone.push_back(type);
        }
    }
    return gone;
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
    int16_t rot = sPlace.rot;

    sPlace.valid = false;
    sPlace.stacked = false;
    sPlace.finalRot = rot;
    sPlace.pos[0] = x;
    sPlace.pos[2] = z;
    sPlace.pos[1] = player->actor.world.pos.y;

    float y;
    const Placeable* under = IsFloorType(sPlace.type) ? PieceUnderLink(player) : nullptr;
    if (under != nullptr) {
        // PHA-3945: a floor placed from up on the base goes in at the level Link stands
        // on, next to the tile he is on: that is how a second storey grows out.
        PieceBox b = BoxOf(*under);
        if (IsTileType(under->type)) {
            SnapToTile(b, &x, &z, &rot);
        }
        y = b.y1 - info.height;
        sPlace.stacked = true;
    } else {
        Vec3f at = { x, player->actor.world.pos.y + 100.0f, z };
        CollisionPoly* poly = nullptr;
        s32 bgId = BGCHECK_SCENE;
        f32 floorY = BgCheck_EntityRaycastFloor4(&play->colCtx, &poly, &bgId, &player->actor, &at);
        sPlace.pos[1] = floorY > BGCHECK_Y_MIN ? floorY : player->actor.world.pos.y;
        if (floorY <= BGCHECK_Y_MIN || poly == nullptr) {
            sPlace.reason = "No ground here";
            return;
        }
        if (bgId != BGCHECK_SCENE) {
            // On top of another piece: a floor, the deck, a wall.
            if (PieceWithTopAt(x, z, floorY) == nullptr) {
                sPlace.reason = "Build on solid ground";
                return;
            }
            sPlace.stacked = true;
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
        if (!sPlace.stacked && WaterBox_GetSurface1(play, &play->colCtx, x, z, &waterY, &waterBox) &&
            waterY > floorY - 5.0f) {
            sPlace.reason = "Not on water";
            return;
        }
        y = floorY;
        if (IsTileType(sPlace.type)) {
            if (const Placeable* n = NearestTileAtLevel(x, z, y)) {
                PieceBox b = BoxOf(*n);
                SnapToTile(b, &x, &z, &rot);
                y = b.y0; // flush with it, whatever the ground does
                sPlace.stacked = sPlace.stacked || n->stacked;
            }
        } else if ((sPlace.type == PLACEABLE_LADDER || sPlace.type == PLACEABLE_STAIRS) &&
                   SnapToLanding(sPlace.type, &x, &z, y, &rot)) {
            // The foot of it: the ground (or the piece) under where it snapped to.
            Vec3f foot = { x, y + 60.0f, z };
            CollisionPoly* footPoly = nullptr;
            s32 footBg = BGCHECK_SCENE;
            f32 footY = BgCheck_EntityRaycastFloor3(&play->colCtx, &footPoly, &footBg, &foot);
            if (footY > BGCHECK_Y_MIN && fabsf(footY - y) < 40.0f) {
                y = footY;
                sPlace.stacked = footBg != BGCHECK_SCENE;
            }
        }
    }
    sPlace.pos[0] = x;
    sPlace.pos[1] = y;
    sPlace.pos[2] = z;
    sPlace.finalRot = rot;

    Vec3f ground = { x, y, z };
    if (NearExitOrDoor(play, ground, y)) {
        sPlace.reason = "Too close to a door or exit";
        return;
    }
    if (BaseSceneName(play->sceneNum) == nullptr) {
        sPlace.reason = "Bases go outdoors, where the nights come";
        return;
    }
    // PHA-3945: boxes that overlap in the footprint and in height are refused; touching
    // ones are fine, and a piece on top of another (or under a floor) is too.
    PieceBox me = BoxAt(sPlace.type, x, y, z, rot);
    for (auto& p : sBase.placeables) {
        if (!SpawnsHere(p)) {
            continue;
        }
        PieceBox other = BoxOf(p);
        if (me.y0 < other.y1 - 1.0f && other.y0 < me.y1 - 1.0f && FootprintsOverlap(me, other, 1.0f)) {
            sPlace.reason = "Something is already there";
            return;
        }
    }
    const BaseCenter& c = sBase.center[CurrentEra()];
    if (c.valid) {
        float dx = c.pos[0] - x, dz = c.pos[2] - z;
        if (c.scene != play->sceneNum) {
            if (sPlace.type != PLACEABLE_WORKBENCH && !NearOutpostWorkbench(CurrentEra(), play->sceneNum, x, z)) {
                sPlace.reason = "Build a workbench first: it starts a camp here";
                return;
            }
        } else if (dx * dx + dz * dz > BASE_RADIUS * BASE_RADIUS) {
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
    payload["rot"] = sPlace.finalRot;
    payload["era"] = CurrentEra();
    if (sPlace.stacked) {
        payload["stk"] = true;
    }
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
    // The ghost's first frame has no check behind it yet, and the A that picked the kit
    // may still be down: A only places once it has been up since placement began.
    bool armed = sPlace.armed;
    if (!(input->cur.button & BTN_A)) {
        sPlace.armed = true;
    }
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
    ghost->shape.rot.y = ghost->world.rot.y = sPlace.finalRot;

    if (pressed & BTN_B) {
        Sfx_PlaySfxCentered(NA_SE_SY_CANCEL);
        EndPlacement();
        return;
    }
    if ((pressed & BTN_A) && armed) {
        if (sPlaceInFlight.reqId != 0) {
            return; // the last one hasn't landed yet; its answer says whether a kit is left
        }
        if (!sPlace.valid) {
            Toast(GetPlaceableInfo(sPlace.type).name, sPlace.reason.empty() ? "Can't build here" : sPlace.reason,
                  true);
            return;
        }
        Sfx_PlaySfxCentered(NA_SE_SY_DECIDE);
        // PHA-4018: the ghost stays up; OnPlaceResult ends placement on the last kit.
        SendPlaceRequest();
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

void RequestRepair(uint16_t id) {
    nlohmann::json payload;
    payload["type"] = REPAIR_REQUEST;
    payload["id"] = id;
    payload["hammer"] = IsUnlocked(UNLOCK_HAMMER);
    if (IsOwner()) {
        ProcessRepairRequest(payload, OwnId());
    } else {
        SendTo(ActingOwner(), payload);
    }
}

std::string RepairCost(uint16_t id) {
    const Placeable* p = FindPlaceable(id);
    if (p == nullptr || IsRuin(*p) || !Damaged(*p)) {
        return "";
    }
    return MaterialsText(RepairInputs(*p, IsUnlocked(UNLOCK_HAMMER)));
}

int UpgradeTarget(uint16_t id) {
    const Placeable* p = FindPlaceable(id);
    if (p == nullptr || IsRuin(*p) || IsDecor(id)) {
        return -1;
    }
    int to = UpgradeTargetOf(p->type);
    return to >= 0 && IsUnlocked(UNLOCK_HAMMER) && IsUnlocked(UpgradeUnlock(to)) ? to : -1;
}

std::string UpgradeCost(uint16_t id) {
    int to = UpgradeTarget(id);
    return to < 0 ? "" : MaterialsText(UpgradeInputs(FindPlaceable(id)->type, (uint8_t)to));
}

void RequestUpgrade(uint16_t id) {
    nlohmann::json payload;
    payload["type"] = UPGRADE_REQUEST;
    payload["id"] = id;
    payload["hammer"] = IsUnlocked(UNLOCK_HAMMER);
    payload["silver"] = IsUnlocked(UNLOCK_SILVER_GAUNTLETS);
    if (IsOwner()) {
        ProcessUpgradeRequest(payload, OwnId());
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
        // PHA-3945: with floors stacked over walls, the piece at Link's height wins.
        PieceBox b = BoxOf(p);
        float y = player->actor.world.pos.y;
        float above = std::max(0.0f, std::max(b.y0 - (y + 40.0f), y - b.y1));
        float d = dx * dx + dz * dz + above * above * 4.0f;
        if (d < bestD) {
            bestD = d;
            best = p.id;
        }
    }
    return best;
}

// MARK: - Per frame / session

// PHA-3935: the seven-year jump. The first time the owner is an adult, the child base
// becomes ruins (still standing in the adult era, broken and harmless) and half its
// kits' materials go back into the pool (spec, "One base per era").
static void RuinChildBase() {
    if (!IsOwner() || (sBase.story & STORY_RUINS) || CurrentEra() != ERA_ADULT || gPlayState == nullptr ||
        !GameInteractor::IsSaveLoaded(true) || gPlayState->transitionTrigger != TRANS_TRIGGER_OFF ||
        gPlayState->transitionMode != TRANS_MODE_OFF || Player_InCsMode(gPlayState)) {
        return; // after the scene has loaded, so the notice is seen
    }
    PoolState& pool = Net::MutablePool();
    uint32_t got[MAT_COUNT] = {};
    int ruined = 0;
    for (auto& p : sBase.placeables) {
        if (p.era != ERA_CHILD) {
            continue;
        }
        p.era = ERA_RUINS;
        ruined++;
        for (auto& in : ScaledKit(p, 1, 2, false)) {
            got[in.material] += in.amount;
        }
    }
    sBase.story |= STORY_RUINS;
    sBase.center[ERA_CHILD] = {};
    std::vector<RecipeInput> list;
    for (uint8_t m = 0; m < MAT_COUNT; m++) {
        pool.materials[m] += got[m];
        if (got[m] > 0) {
            list.push_back({ m, (uint16_t)got[m] });
        }
    }
    pool.rev++;
    sBase.rev++;
    BroadcastState();
    Net::BroadcastPool();
    SyncSceneActors();
    if (ruined > 0) {
        std::string msg = fmt::format("Seven years on, the old base is ruins. Salvaged {}", MaterialsText(list));
        Notification::Emit({ .prefix = "Ruins", .message = msg, .remainingTime = 8.0f });
        nlohmann::json r;
        r["type"] = PLACE_RESULT;
        r["reqId"] = 0;
        r["notice"] = msg;
        r["error"] = false;
        Broadcast(r);
    }
}

void BaseOnFrame() {
    RuinChildBase();
    if (sPlaceInFlight.reqId != 0 && Now() - sPlaceInFlight.sentAt > 5.0) {
        sPlaceInFlight = {};
        Toast("Base", "The host didn't answer", true);
    }
    if (sPlace.active && gPlayState != nullptr) {
        Player* player = GET_PLAYER(gPlayState);
        // PHA-4018: placement repeats, so opening the pause menu is one of the ways out.
        // Placement starts while the Workbench page is still closing; only a pause that
        // opens after that counts.
        bool paused = gPlayState->pauseCtx.state != 0;
        if (!paused) {
            sPlace.unpaused = true;
        }
        if (player == nullptr || gPlayState->msgCtx.msgMode != MSGMODE_NONE || (paused && sPlace.unpaused) ||
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

// OnSceneSpawnActors also fires when a door or doorway loads another room of the
// same scene (Kokiri Forest has three). Placeables are room -1 actors and outlive
// the swap, so only a new scene (OnSceneInit) forgets them.
static bool sSceneFresh = false;

void BaseRegisterHooks(bool enabled) {
    COND_HOOK(OnSceneInit, enabled, [](int16_t sceneNum) { sSceneFresh = true; });
    COND_HOOK(OnSceneSpawnActors, enabled, []() {
        if (sSceneFresh) {
            // A new scene: last scene's actors are gone (their destroy callbacks ran).
            sSceneFresh = false;
            sSpawned.clear();
            sPlace = {};
        }
        RegisterPlaceableActors();
        SyncSceneActors();
    });
    COND_HOOK(OnLoadGame, enabled, [](int32_t fileNum) { RegisterPlaceableActors(); });
    RestRegisterHooks(enabled);
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
    sPlace.rot = sPlace.finalRot = (int16_t)rot;
    sPlace.stacked = false;
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
void sevendays_test_upgrade(int id) {
    if (BaseEnabled()) {
        RequestUpgrade((uint16_t)id);
    }
}

EMSCRIPTEN_KEEPALIVE
const char* sevendays_test_upgrade_cost(int id) {
    static std::string out;
    out = UpgradeCost((uint16_t)id);
    return out.c_str();
}

EMSCRIPTEN_KEEPALIVE
void sevendays_test_repair(int id) {
    if (BaseEnabled()) {
        RequestRepair((uint16_t)id);
    }
}

EMSCRIPTEN_KEEPALIVE
const char* sevendays_test_repair_cost(int id) {
    static std::string out;
    out = RepairCost((uint16_t)id);
    return out.c_str();
}

EMSCRIPTEN_KEEPALIVE
void sevendays_test_open_window(int tab) {
    if (BaseEnabled()) {
        OpenCraftingWindow(tab);
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
        j["room"] = gPlayState->roomCtx.curRoom.num;
        // Placeable actors alive in the scene (each placeable should have exactly one).
        int n = 0;
        for (Actor* a = gPlayState->actorCtx.actorLists[ACTORCAT_BG].head; a != nullptr; a = a->next) {
            n += a->id == PlaceableActorId();
        }
        j["placeableActors"] = n;
        // PHA-3916: dynamic collision headroom (slots in use, the lists' size and
        // fill) and the actor arena left after the bigger lists.
        DynaCollisionContext& dyna = gPlayState->colCtx.dyna;
        int slots = 0;
        for (int i = 0; i < BG_ACTOR_MAX; i++) {
            slots += (dyna.bgActorFlags[i] & 1) != 0;
        }
        u32 maxFree = 0, free = 0, alloc = 0;
        ZeldaArena_GetSizes(&maxFree, &free, &alloc);
        int cPolys = 0, cVerts = 0, cChunks = 0;
        CollisionInUse(cPolys, cVerts, cChunks);
        j["refusals"] = sRefusals;
        j["dyna"] = { { "slots", slots },
                      { "polys", cPolys },
                      { "verts", cVerts },
                      { "chunks", cChunks },
                      { "vtxMax", dyna.vtxListMax },
                      { "polyMax", dyna.polyListMax },
                      { "nodeMax", dyna.polyNodes.max },
                      { "nodes", dyna.polyNodes.count } };
        j["arenaFree"] = free;
        j["floorBgId"] = player->actor.floorBgId;
        j["frames"] = gPlayState->gameplayFrames;
        j["wallBgId"] = player->actor.wallBgId;
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
