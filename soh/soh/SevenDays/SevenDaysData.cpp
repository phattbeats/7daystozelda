#include "SevenDays.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "variables.h"
#include "functions.h"
}

/**
 * Balancing tables for 7 Days to Zelda. Every number here is a placeholder to
 * tune on game night (PHA-3870 M8); logic reads only these tables.
 */

namespace SevenDays {

static const MaterialInfo sMaterials[MAT_COUNT] = {
    /* MAT_FIBER */ { "Fiber", "Hey! Grass fiber!&The workbench can use that!" },
    /* MAT_STONE */ { "Stone", "Listen! That rock left good stone.&Walls need stone!" },
    /* MAT_WOOD */ { "Wood", "Ow! ...but look, wood!&Roll into a tree once a day for more." },
    /* MAT_BONE */ { "Bone", "Stalchild bones! Gross...&but spikes need them!" },
    /* MAT_ROT */ { "Rot", "Eww, rot! Hold your nose, Link.&It makes Deku Nuts, believe it or not!" },
};

const MaterialInfo& GetMaterialInfo(uint8_t material) {
    return sMaterials[material < MAT_COUNT ? material : 0];
}

// clang-format off
static const GatherSource sGatherSources[] = {
    // actor              material   amt unlock            dedupe s  perDay
    { ACTOR_EN_KUSA,      MAT_FIBER, 2,  UNLOCK_START,     60,       false }, // grass/bushes cut or thrown; regrows
    { ACTOR_EN_ISHI,      MAT_STONE, 1,  UNLOCK_START,     120,      false }, // small rocks lifted and broken
    { ACTOR_OBJ_BOMBIWA,  MAT_STONE, 3,  UNLOCK_BOMB_BAG,  120,      false }, // bombable boulders
    { ACTOR_EN_WOOD02,    MAT_WOOD,  2,  UNLOCK_START,     1800,     true  }, // rolling into a tree, once a day
    { ACTOR_EN_SKB,       MAT_BONE,  1,  UNLOCK_DEKU_TREE, 60,       false }, // Stalchildren
    { ACTOR_EN_RD,        MAT_ROT,   2,  UNLOCK_DEKU_TREE, 60,       false }, // ReDeads and Gibdos
};

static const std::vector<Recipe> sRecipes = {
    // id               name                kind               inputs                                       n  item              requiredItem    out unlock
    { "sticks",         "Deku Sticks",      RECIPE_CONSUMABLE, { { MAT_WOOD, 3 } },                          1, ITEM_STICKS_5,    ITEM_NONE,      5,  UNLOCK_START },
    { "nuts",           "Deku Nuts",        RECIPE_CONSUMABLE, { { MAT_FIBER, 3 }, { MAT_ROT, 1 } },         2, ITEM_NUTS_5,      ITEM_NONE,      5,  UNLOCK_DEKU_TREE },
    { "seeds",          "Deku Seeds",       RECIPE_CONSUMABLE, { { MAT_FIBER, 4 } },                         1, ITEM_SEEDS_30,    ITEM_SLINGSHOT, 30, UNLOCK_START },
    { "arrows",         "Arrows",           RECIPE_CONSUMABLE, { { MAT_WOOD, 2 }, { MAT_BONE, 1 } },         2, ITEM_ARROWS_SMALL,ITEM_BOW,       5,  UNLOCK_DEKU_TREE, true },
    { "bombs",          "Bombs",            RECIPE_CONSUMABLE, { { MAT_STONE, 3 }, { MAT_ROT, 1 } },         2, ITEM_BOMBS_5,     ITEM_BOMB,      5,  UNLOCK_BOMB_BAG, true },

    { "workbench",      "Workbench",        RECIPE_KIT,        { { MAT_WOOD, 4 }, { MAT_STONE, 4 } },        2, ITEM_NONE,        ITEM_NONE,      1,  UNLOCK_START },
    { "barricade",      "Barricade",        RECIPE_KIT,        { { MAT_WOOD, 6 }, { MAT_FIBER, 2 } },        2, ITEM_NONE,        ITEM_NONE,      1,  UNLOCK_START },
    { "torch",          "Torch",            RECIPE_KIT,        { { MAT_WOOD, 2 }, { MAT_FIBER, 1 } },        2, ITEM_NONE,        ITEM_NONE,      1,  UNLOCK_START },
    { "spikes",         "Spike strip",      RECIPE_KIT,        { { MAT_WOOD, 4 }, { MAT_BONE, 3 } },         2, ITEM_NONE,        ITEM_NONE,      1,  UNLOCK_DEKU_TREE, true },
    { "chest",          "Storage chest",    RECIPE_KIT,        { { MAT_WOOD, 8 } },                          1, ITEM_NONE,        ITEM_NONE,      1,  UNLOCK_DEKU_TREE },
    { "scarecrow",      "Scarecrow decoy",  RECIPE_KIT,        { { MAT_WOOD, 4 }, { MAT_FIBER, 4 } },        2, ITEM_NONE,        ITEM_NONE,      1,  UNLOCK_START },
    { "guardbaba",      "Guard Baba",       RECIPE_KIT,        { { MAT_ROT, 3 }, { MAT_FIBER, 2 } },         2, ITEM_NONE,        ITEM_NONE,      1,  UNLOCK_DEKU_TREE },
    { "stonewall",      "Stone wall",       RECIPE_KIT,        { { MAT_STONE, 6 }, { MAT_WOOD, 2 } },        2, ITEM_NONE,        ITEM_NONE,      1,  UNLOCK_BOMB_BAG, true },
    { "bombtrap",       "Bomb-flower trap", RECIPE_KIT,        { { MAT_STONE, 4 }, { MAT_ROT, 2 } },         2, ITEM_NONE,        ITEM_NONE,      1,  UNLOCK_BOMB_BAG, true },
    { "gate",           "Player gate",      RECIPE_KIT,        { { MAT_WOOD, 8 }, { MAT_STONE, 4 } },        2, ITEM_NONE,        ITEM_NONE,      1,  UNLOCK_HOOKSHOT, true },

    { "trade_fiber",    "Sell Fiber",       RECIPE_TRADE,      { { MAT_FIBER, 5 } },                         1, ITEM_NONE,        ITEM_NONE,      5,  UNLOCK_START },
    { "trade_wood",     "Sell Wood",        RECIPE_TRADE,      { { MAT_WOOD, 3 } },                          1, ITEM_NONE,        ITEM_NONE,      5,  UNLOCK_START },
    { "trade_stone",    "Sell Stone",       RECIPE_TRADE,      { { MAT_STONE, 3 } },                         1, ITEM_NONE,        ITEM_NONE,      5,  UNLOCK_START },
    { "trade_bone",     "Sell Bone",        RECIPE_TRADE,      { { MAT_BONE, 2 } },                          1, ITEM_NONE,        ITEM_NONE,      10, UNLOCK_START },
    { "trade_rot",      "Sell Rot",         RECIPE_TRADE,      { { MAT_ROT, 2 } },                           1, ITEM_NONE,        ITEM_NONE,      10, UNLOCK_START },

    // M10 merchants: what they hand over, and the price (out). Every price is at
    // least twice the Trade tab's rate, so selling and buying back never pays.
    { "buy_fiber",      "Fiber",            RECIPE_BUY,        { { MAT_FIBER, 5 } },                         0, ITEM_NONE,        ITEM_NONE,      10, UNLOCK_START },
    { "buy_wood",       "Wood",             RECIPE_BUY,        { { MAT_WOOD, 3 } },                          0, ITEM_NONE,        ITEM_NONE,      10, UNLOCK_START },
    { "buy_stone",      "Stone",            RECIPE_BUY,        { { MAT_STONE, 3 } },                         0, ITEM_NONE,        ITEM_NONE,      10, UNLOCK_START },
    { "buy_bone",       "Bone",             RECIPE_BUY,        { { MAT_BONE, 2 } },                          0, ITEM_NONE,        ITEM_NONE,      20, UNLOCK_START },
    { "buy_rot",        "Rot",              RECIPE_BUY,        { { MAT_ROT, 2 } },                           0, ITEM_NONE,        ITEM_NONE,      20, UNLOCK_START },
    { "buy_wood_bulk",  "Wood",             RECIPE_BUY,        { { MAT_WOOD, 10 } },                         0, ITEM_NONE,        ITEM_NONE,      30, UNLOCK_START },
    { "buy_fiber_bulk", "Fiber",            RECIPE_BUY,        { { MAT_FIBER, 15 } },                        0, ITEM_NONE,        ITEM_NONE,      30, UNLOCK_START },
};

// Kokiri Forest has none: the prologue stays gathering-first. Ore doesn't exist
// yet; Goron City adds it when it does.
static const std::vector<Merchant> sMerchants = {
    // scene                   room anchor x, y, z      (beside)
    { SCENE_MARKET_DAY,        0,   393,   0,    264,   // a market-goer by the bazaar's side of the square
      "Market stall", "A market stall piled with bundles of wood and grass fiber.^\"Building something? Have a look!\"",
      { "buy_wood", "buy_fiber" } },
    { SCENE_KAKARIKO_VILLAGE,  0,   486,   80,   1423,  // the Cucco lady
      "Kakariko lumber", "Spare lumber and fiber from the carpenters' work.^\"Rupees in the box, take what you need.\"",
      { "buy_wood", "buy_fiber" } },
    { SCENE_GORON_CITY,        3,   84,    -3,   -314,  // a Goron on the bottom floor
      "Goron stone", "A heap of good cut stone.^\"Goron rock! The best for walls, brother!\"",
      { "buy_stone" } },
    { SCENE_GRAVEYARD,         1,   -474,  61,   447,   // by the graves, near Dampe's hut
      "Dampe's bone pile", "Dampe's leftovers, stacked by the graves.^\"Heh heh... bones for rupees. Don't ask.\"",
      { "buy_bone" } },
    { SCENE_LON_LON_RANCH,     0,   64,    0,    -567,  // Malon in the yard
      "Talon's crates", "Talon's ranch crates: fence wood and hay fiber by the bundle.^\"Bulk prices for the city folk!\"",
      { "buy_wood_bulk", "buy_fiber_bulk" } },
    { SCENE_ZORAS_DOMAIN,      1,   217,   178,  150,   // the shop ledge's sign
      "Zora salvage", "Things the river washed up, sorted by the Zoras.^\"Smelly, but builders always ask for it.\"",
      { "buy_rot", "buy_bone" } },
    { SCENE_GERUDOS_FORTRESS,  0,   -1224, 93,   -3160, // a guard on the lower yard
      "Gerudo spoils", "Spoils from the desert, guarded day and night.^\"Pay up, or move along.\"",
      { "buy_bone", "buy_rot" } },
};
// clang-format on

// MARK: - M7 loot

// Material weights (Fiber, Stone, Wood, Bone, Rot) by area tier. Pots, crates and
// supply caches roll on these: low tiers pay the forest's materials, deeper
// dungeons pay stone, bone and rot.
// clang-format off
static const uint8_t sLootWeights[][MAT_COUNT] = {
    /* UNLOCK_START            */ { 40, 10, 40, 10, 0  },
    /* UNLOCK_DEKU_TREE        */ { 30, 10, 35, 20, 5  },
    /* UNLOCK_BOMB_BAG         */ { 10, 40, 20, 20, 10 },
    /* UNLOCK_HOOKSHOT         */ { 10, 35, 20, 20, 15 },
    /* UNLOCK_HAMMER           */ { 5,  40, 15, 20, 20 },
    /* UNLOCK_SILVER_GAUNTLETS */ { 5,  35, 15, 25, 20 },
};

struct SceneTierRow {
    int16_t scene;
    uint8_t tier;
};
static const SceneTierRow sSceneTiers[] = {
    { SCENE_DEKU_TREE,               UNLOCK_DEKU_TREE },
    { SCENE_DODONGOS_CAVERN,         UNLOCK_BOMB_BAG },
    { SCENE_JABU_JABU,               UNLOCK_BOMB_BAG },
    { SCENE_BOTTOM_OF_THE_WELL,      UNLOCK_BOMB_BAG },
    { SCENE_FOREST_TEMPLE,           UNLOCK_HOOKSHOT },
    { SCENE_ICE_CAVERN,              UNLOCK_HOOKSHOT },
    { SCENE_WATER_TEMPLE,            UNLOCK_HOOKSHOT },
    { SCENE_FIRE_TEMPLE,             UNLOCK_HAMMER },
    { SCENE_SHADOW_TEMPLE,           UNLOCK_HAMMER },
    { SCENE_SPIRIT_TEMPLE,           UNLOCK_SILVER_GAUNTLETS },
    { SCENE_GERUDO_TRAINING_GROUND,  UNLOCK_SILVER_GAUNTLETS },
    { SCENE_GROTTOS,                 UNLOCK_DEKU_TREE },
};

// Supply caches: 2-4 per dungeon plus three grottos. Each sits beside an anchor
// taken from the room's vanilla actor list (a chest, pot or crate), so it is on a
// floor the player can reach; the exact spot is raycast when it spawns.
static const std::vector<CacheSpot> sCacheSpots = {
    // scene                          room  anchor x, y, z         tier
    { SCENE_DEKU_TREE,                0,    333,   360,   253,     UNLOCK_DEKU_TREE },  // 2F ledge, by the chest
    { SCENE_DEKU_TREE,                2,    -1391, 480,   1391,    UNLOCK_DEKU_TREE },  // slingshot room
    { SCENE_DEKU_TREE,                3,    53,    -845,  -278,    UNLOCK_DEKU_TREE },  // basement, by the chest
    { SCENE_DODONGOS_CAVERN,          1,    1708,  0,     -471,    UNLOCK_BOMB_BAG },   // east corridor pots
    { SCENE_DODONGOS_CAVERN,          3,    2653,  100,   -2031,   UNLOCK_BOMB_BAG },
    { SCENE_DODONGOS_CAVERN,          9,    1656,  591,   -531,    UNLOCK_BOMB_BAG },
    { SCENE_JABU_JABU,                1,    -189,  -340,  -1925,   UNLOCK_BOMB_BAG },   // by the crates
    { SCENE_JABU_JABU,                6,    -1355, 80,    -3612,   UNLOCK_BOMB_BAG },
    { SCENE_JABU_JABU,                14,   645,   -1073, -2408,   UNLOCK_BOMB_BAG },
    { SCENE_BOTTOM_OF_THE_WELL,       0,    463,   0,     -174,    UNLOCK_BOMB_BAG },
    { SCENE_BOTTOM_OF_THE_WELL,       1,    -95,   -720,  -673,    UNLOCK_BOMB_BAG },
    { SCENE_BOTTOM_OF_THE_WELL,       3,    874,   0,     -1294,   UNLOCK_BOMB_BAG },
    { SCENE_FOREST_TEMPLE,            11,   -1645, 1156,  -1297,   UNLOCK_HOOKSHOT },
    { SCENE_FOREST_TEMPLE,            14,   2312,  1093,  -874,    UNLOCK_HOOKSHOT },
    { SCENE_FOREST_TEMPLE,            17,   -404,  -779,  -1041,   UNLOCK_HOOKSHOT },
    { SCENE_ICE_CAVERN,               3,    433,   0,     -732,    UNLOCK_HOOKSHOT },
    { SCENE_ICE_CAVERN,               6,    -1422, 265,   586,     UNLOCK_HOOKSHOT },
    { SCENE_WATER_TEMPLE,             3,    -2314, 320,   770,     UNLOCK_HOOKSHOT },
    { SCENE_WATER_TEMPLE,             16,   -1417, 108,   -3025,   UNLOCK_HOOKSHOT },
    { SCENE_WATER_TEMPLE,             17,   1123,  0,     62,      UNLOCK_HOOKSHOT },
    { SCENE_WATER_TEMPLE,             21,   -2226, 260,   -2487,   UNLOCK_HOOKSHOT },
    { SCENE_FIRE_TEMPLE,              14,   -2072, 4180,  -1135,   UNLOCK_HAMMER },
    { SCENE_FIRE_TEMPLE,              17,   -240,  0,     -369,    UNLOCK_HAMMER },
    { SCENE_FIRE_TEMPLE,              25,   -668,  2800,  -1300,   UNLOCK_HAMMER },
    { SCENE_SHADOW_TEMPLE,            10,   614,   -1343, 3579,    UNLOCK_HAMMER },
    { SCENE_SHADOW_TEMPLE,            16,   5942,  -1143, 2188,    UNLOCK_HAMMER },
    { SCENE_SHADOW_TEMPLE,            20,   4222,  -1363, -916,    UNLOCK_HAMMER },
    { SCENE_SPIRIT_TEMPLE,            0,    -181,  -150,  233,     UNLOCK_SILVER_GAUNTLETS },
    { SCENE_SPIRIT_TEMPLE,            5,    -600,  333,   -1213,   UNLOCK_SILVER_GAUNTLETS },
    { SCENE_SPIRIT_TEMPLE,            16,   819,   887,   -333,    UNLOCK_SILVER_GAUNTLETS },
    { SCENE_GERUDO_TRAINING_GROUND,   9,    2183,  -108,  -1584,   UNLOCK_SILVER_GAUNTLETS },
    { SCENE_GERUDO_TRAINING_GROUND,   10,   -1488, 139,   -3721,   UNLOCK_SILVER_GAUNTLETS },
    // Grottos share rooms between many holes: the key also carries where the hole is.
    { SCENE_GROTTOS,                  0,    13,    -40,   -508,    UNLOCK_DEKU_TREE },  // generic grotto
    { SCENE_GROTTOS,                  4,    3390,  -2,    -258,    UNLOCK_DEKU_TREE },
    { SCENE_GROTTOS,                  8,    1843,  -14,   1014,    UNLOCK_DEKU_TREE },
};
// clang-format on

const std::vector<CacheSpot>& GetCacheSpots() {
    return sCacheSpots;
}

const uint8_t* LootWeights(uint8_t tier) {
    return sLootWeights[tier < 6 ? tier : 5];
}

uint8_t SceneTier(int16_t scene) {
    for (auto& row : sSceneTiers) {
        if (row.scene == scene) {
            return row.tier;
        }
    }
    return UNLOCK_START;
}

// The strong pieces come from caches and bosses (spec: "Tiers and blueprints").
// Caches hold the ones no boss gives; bosses give their tier's key piece.
const std::vector<const char*>& CacheBlueprints() {
    static const std::vector<const char*> list = { "bombtrap", "bombs", "gate", "arrows" };
    return list;
}

const char* BossBlueprint(int16_t bossActorId) {
    switch (bossActorId) {
        case ACTOR_BOSS_GOMA:
            return "spikes"; // Gohma: the spike strip
        case ACTOR_BOSS_DODONGO:
            return "stonewall"; // King Dodongo: the stone wall
        case ACTOR_BOSS_VA:
            return "gate"; // Barinade: the gate that opens for players
    }
    return nullptr;
}

const char* BossName(int16_t bossActorId) {
    switch (bossActorId) {
        case ACTOR_BOSS_GOMA:
            return "Gohma";
        case ACTOR_BOSS_DODONGO:
            return "King Dodongo";
        case ACTOR_BOSS_VA:
            return "Barinade";
        case ACTOR_BOSS_GANONDROF:
            return "Phantom Ganon";
        case ACTOR_BOSS_FD:
        case ACTOR_BOSS_FD2:
            return "Volvagia";
        case ACTOR_BOSS_MO:
            return "Morpha";
        case ACTOR_BOSS_SST:
            return "Bongo Bongo";
        case ACTOR_BOSS_TW:
            return "Twinrova";
        case ACTOR_BOSS_GANON:
        case ACTOR_BOSS_GANON2:
            return "Ganon";
    }
    return nullptr;
}

const std::vector<Merchant>& GetMerchants() {
    return sMerchants;
}

const std::vector<Recipe>& GetRecipes() {
    return sRecipes;
}

const GatherSource* FindGatherSource(int16_t actorId) {
    for (auto& source : sGatherSources) {
        if (source.actorId == actorId) {
            return &source;
        }
    }
    return nullptr;
}

const Recipe* FindRecipe(const std::string& id) {
    for (auto& recipe : sRecipes) {
        if (id == recipe.id) {
            return &recipe;
        }
    }
    return nullptr;
}

// Tiers read the save: items owned, upgrades, boss flags. Hammer and Silver
// Gauntlets open Ore and iron, which arrive with placeables (M5+); their tiers
// already resolve so recipes can be added to the table without logic changes.
bool IsUnlocked(Unlock unlock) {
    switch (unlock) {
        case UNLOCK_START:
            return true;
        case UNLOCK_DEKU_TREE:
            return CHECK_QUEST_ITEM(QUEST_KOKIRI_EMERALD) ||
                   Flags_GetEventChkInf(EVENTCHKINF_OBTAINED_KOKIRI_EMERALD_DEKU_TREE_DEAD);
        case UNLOCK_BOMB_BAG:
            return CUR_UPG_VALUE(UPG_BOMB_BAG) > 0;
        case UNLOCK_HOOKSHOT:
            return INV_CONTENT(ITEM_HOOKSHOT) != ITEM_NONE;
        case UNLOCK_HAMMER:
            return INV_CONTENT(ITEM_HAMMER) == ITEM_HAMMER;
        case UNLOCK_SILVER_GAUNTLETS:
            return CUR_UPG_VALUE(UPG_STRENGTH) >= 2;
    }
    return false;
}

const char* UnlockName(Unlock unlock) {
    switch (unlock) {
        case UNLOCK_START:
            return "";
        case UNLOCK_DEKU_TREE:
            return "Save the Deku Tree";
        case UNLOCK_BOMB_BAG:
            return "Needs the Bomb Bag";
        case UNLOCK_HOOKSHOT:
            return "Needs the Hookshot";
        case UNLOCK_HAMMER:
            return "Needs the Megaton Hammer";
        case UNLOCK_SILVER_GAUNTLETS:
            return "Needs the Silver Gauntlets";
    }
    return "";
}

} // namespace SevenDays
