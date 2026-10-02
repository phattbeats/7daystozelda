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
    { "arrows",         "Arrows",           RECIPE_CONSUMABLE, { { MAT_WOOD, 2 }, { MAT_BONE, 1 } },         2, ITEM_ARROWS_SMALL,ITEM_BOW,       5,  UNLOCK_DEKU_TREE },
    { "bombs",          "Bombs",            RECIPE_CONSUMABLE, { { MAT_STONE, 3 }, { MAT_ROT, 1 } },         2, ITEM_BOMBS_5,     ITEM_BOMB,      5,  UNLOCK_BOMB_BAG },

    { "workbench",      "Workbench",        RECIPE_KIT,        { { MAT_WOOD, 4 }, { MAT_STONE, 4 } },        2, ITEM_NONE,        ITEM_NONE,      1,  UNLOCK_START },
    { "barricade",      "Barricade",        RECIPE_KIT,        { { MAT_WOOD, 6 }, { MAT_FIBER, 2 } },        2, ITEM_NONE,        ITEM_NONE,      1,  UNLOCK_START },
    { "torch",          "Torch",            RECIPE_KIT,        { { MAT_WOOD, 2 }, { MAT_FIBER, 1 } },        2, ITEM_NONE,        ITEM_NONE,      1,  UNLOCK_START },
    { "spikes",         "Spike strip",      RECIPE_KIT,        { { MAT_WOOD, 4 }, { MAT_BONE, 3 } },         2, ITEM_NONE,        ITEM_NONE,      1,  UNLOCK_DEKU_TREE },
    { "chest",          "Storage chest",    RECIPE_KIT,        { { MAT_WOOD, 8 } },                          1, ITEM_NONE,        ITEM_NONE,      1,  UNLOCK_DEKU_TREE },
    { "stonewall",      "Stone wall",       RECIPE_KIT,        { { MAT_STONE, 6 }, { MAT_WOOD, 2 } },        2, ITEM_NONE,        ITEM_NONE,      1,  UNLOCK_BOMB_BAG },
    { "bombtrap",       "Bomb-flower trap", RECIPE_KIT,        { { MAT_STONE, 4 }, { MAT_ROT, 2 } },         2, ITEM_NONE,        ITEM_NONE,      1,  UNLOCK_BOMB_BAG },
    { "gate",           "Player gate",      RECIPE_KIT,        { { MAT_WOOD, 8 }, { MAT_STONE, 4 } },        2, ITEM_NONE,        ITEM_NONE,      1,  UNLOCK_HOOKSHOT },

    { "trade_fiber",    "Sell Fiber",       RECIPE_TRADE,      { { MAT_FIBER, 5 } },                         1, ITEM_NONE,        ITEM_NONE,      5,  UNLOCK_START },
    { "trade_wood",     "Sell Wood",        RECIPE_TRADE,      { { MAT_WOOD, 3 } },                          1, ITEM_NONE,        ITEM_NONE,      5,  UNLOCK_START },
    { "trade_stone",    "Sell Stone",       RECIPE_TRADE,      { { MAT_STONE, 3 } },                         1, ITEM_NONE,        ITEM_NONE,      5,  UNLOCK_START },
    { "trade_bone",     "Sell Bone",        RECIPE_TRADE,      { { MAT_BONE, 2 } },                          1, ITEM_NONE,        ITEM_NONE,      10, UNLOCK_START },
    { "trade_rot",      "Sell Rot",         RECIPE_TRADE,      { { MAT_ROT, 2 } },                           1, ITEM_NONE,        ITEM_NONE,      10, UNLOCK_START },
};
// clang-format on

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
