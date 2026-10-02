#ifndef SEVEN_DAYS_H
#define SEVEN_DAYS_H
#ifdef __cplusplus

/**
 * 7 Days to Zelda (PHA-3870). M4: materials, crafting, tunic colors.
 * M5: placeables, the base, saving and the boarded-up village.
 * M6: raids on the base (gamestage budgets, ring spawns, routing to the
 *     workbench, barricade damage, the raid clock, the prologue's scripted
 *     nights, raids on an empty base, the losing-a-night penalty).
 * M7: loot and Majora-style nights (supply caches, pot and crate rolls,
 *     blueprints, boss and Gold Skulltula rewards; the dawn card, the
 *     final-hours clock, red raid nights and moon, the raid track, counters).
 *
 * Everything sits behind gSevenDays.Enabled, and each milestone behind its own
 * switch under it, so plain co-op runs untouched with the master switch off.
 *
 *   gSevenDays.Enabled       0/1  master switch (default 0)
 *   gSevenDays.Crafting      0/1  M4 materials, shared pool, crafting window (default 1)
 *   gSevenDays.TunicColors   0/1  tunic + cap in the player's lobby color (default 1)
 *   gSevenDays.Base          0/1  M5 placeables, placement mode, the base, the village (default 1)
 *   gSevenDays.Raids         0/1  M6 raids, the raid clock, the prologue's nights (default 1; needs Base)
 *   gSevenDays.Loot          0/1  M7 supply caches, pot/crate rolls, blueprints, boss/Skulltula rewards
 *                                 (default 1; needs Crafting)
 *   gSevenDays.Nights        0/1  M7 dawn card, final-hours clock, red raid nights, raid track,
 *                                 counters on pause and file select (default 1; needs Raids)
 *
 * Authority: the shared material pool is decided by the Anchor room owner
 * (roomState.ownerClientId, or the lowest online client id while the owner is
 * offline). Gatherers send GATHER, crafters send CRAFT_REQUEST; the owner dedupes,
 * applies and broadcasts MATERIALS_STATE. Solo play acts as its own owner.
 */

#include <cstdint>
#include <string>
#include <map>
#include <vector>
#include <nlohmann/json.hpp>
#include <libultraship/libultraship.h>

#define CVAR_SEVEN_DAYS(var) "gSevenDays." var

struct Actor;
struct PlayState;

namespace SevenDays {

enum Material : uint8_t {
    MAT_FIBER,
    MAT_STONE,
    MAT_WOOD,
    MAT_BONE,
    MAT_ROT,
    MAT_COUNT,
};

// Tool/boss tiers that open gathering sources and recipes (PHA-3870 progression).
enum Unlock : uint8_t {
    UNLOCK_START,
    UNLOCK_DEKU_TREE,
    UNLOCK_BOMB_BAG,
    UNLOCK_HOOKSHOT,
    UNLOCK_HAMMER,
    UNLOCK_SILVER_GAUNTLETS,
};

bool Enabled();
bool CraftingEnabled();
bool TunicColorsEnabled();
bool BaseEnabled();
bool IsUnlocked(Unlock unlock);
const char* UnlockName(Unlock unlock);

// Data tables (SevenDaysData.cpp): balancing never touches logic.
struct MaterialInfo {
    const char* name;
    const char* firstLine; // Navi's first-gather line
};
const MaterialInfo& GetMaterialInfo(uint8_t material);

enum RecipeKind : uint8_t {
    RECIPE_CONSUMABLE, // vanilla ammo, granted by Item_Give on CRAFT_RESULT
    RECIPE_KIT,        // placeable kit, a count in the shared pool (placed in M5)
    RECIPE_TRADE,      // materials -> rupees (the workbench's Trade tab)
};

struct RecipeInput {
    uint8_t material;
    uint16_t amount;
};

struct Recipe {
    const char* id; // stable, travels in CRAFT_REQUEST
    const char* name;
    RecipeKind kind;
    RecipeInput inputs[3];
    uint8_t inputCount;
    uint8_t item;           // RECIPE_CONSUMABLE: the ITEM_* passed to Item_Give
    uint8_t requiredItem;   // RECIPE_CONSUMABLE: ITEM_* the player must own (ITEM_NONE: none)
    uint16_t outputCount;   // kits made / rupees paid / ammo shown in the UI
    Unlock unlock;
    bool blueprint = false; // M7: also needs its blueprint (gSevenDays.Loot on)
};
const std::vector<Recipe>& GetRecipes();
const Recipe* FindRecipe(const std::string& id);

// Where materials come from: one row per gathering source.
struct GatherSource {
    int16_t actorId;
    uint8_t material;
    uint8_t amount;
    Unlock unlock;
    uint16_t dedupeSeconds; // owner ignores the same sourceKey for this long (regrowth)
    bool perDay;            // sourceKey also carries the in-game day (trees: once a day)
};
const GatherSource* FindGatherSource(int16_t actorId);

// Shared pool as last known on this client (the owner's copy is authoritative).
struct PoolState {
    uint32_t rev = 0;
    uint32_t materials[MAT_COUNT] = {};
    std::map<std::string, uint32_t> kits;
};
const PoolState& GetPool();

// Crafting: sends CRAFT_REQUEST to the owner (or settles locally when solo/owner).
// False when the request could not be sent (another is still in flight).
bool RequestCraft(const std::string& recipeId);
bool CraftInFlight();
// Why this recipe can't be crafted right now ("" when it can).
std::string CraftBlocker(const Recipe& recipe);

// Anchor glue: one dispatch line in Anchor::ProcessIncomingPacketQueue.
bool IsPacket(const std::string& type);
void HandlePacket(const nlohmann::json& payload);

// Navi lines (custom message table "SevenDays", text ids SEVEN_DAYS_TEXT_BASE+).
constexpr uint16_t SEVEN_DAYS_TEXT_BASE = 0x9700;
constexpr uint16_t SEVEN_DAYS_TEXT_COUNT = 0x40;
bool IsSevenDaysText(uint16_t textId);

// Tunic colors (TunicColors.cpp)
void ApplyTunicOverrideForClient(uint32_t clientId);
void RestoreLocalTunicOverride();

// Crafting window
void ToggleCraftingWindow();
// Opens the window on a given tab (0 Craft, 1 Trade, 2 Base/Storage).
void OpenCraftingWindow(int tab);
int TakeRequestedCraftingTab(); // -1 when no tab switch is pending

// MARK: - M5: placeables and the base (Base.cpp, Placeables.cpp)

enum PlaceableType : uint8_t {
    PLACEABLE_BARRICADE,
    PLACEABLE_SPIKES,
    PLACEABLE_WORKBENCH,
    PLACEABLE_CHEST,
    PLACEABLE_SIGN, // seeded only (the village's "Day 1" sign), no kit
    PLACEABLE_COUNT,
};

struct PlaceableInfo {
    const char* kit;  // recipe id of the kit that builds it ("" = not buildable)
    const char* name;
    int16_t halfX, halfZ, height; // collision box (world units, before rotation)
    uint16_t maxHp;               // 0 = indestructible
};
const PlaceableInfo& GetPlaceableInfo(uint8_t type);
int FindPlaceableTypeForKit(const std::string& kit); // -1 if none

constexpr int ERA_ADULT = 0; // == gSaveContext.linkAge
constexpr int ERA_CHILD = 1;
constexpr int BASE_CAP = 24;
constexpr float BASE_RADIUS = 800.0f;

struct Placeable {
    uint16_t id = 0; // stable, assigned by the owner; also the actor's params
    uint8_t type = 0;
    uint8_t era = ERA_CHILD;
    int16_t scene = 0;
    float pos[3] = {};
    int16_t rot = 0;
    uint16_t hp = 0;
};

struct BaseCenter {
    bool valid = false;
    int16_t scene = 0;
    float pos[3] = {};
};

struct BaseState {
    uint32_t rev = 0;
    uint16_t nextId = 1;
    BaseCenter center[2]; // per era (ERA_ADULT, ERA_CHILD): the first workbench
    std::vector<Placeable> placeables;
    uint32_t daysSurvived = 0;
    uint32_t hordeNightsSurvived = 0;
    // M6 raids (all in "counters"): the day whose night is the next raid (0 = none
    // scheduled yet: the prologue), story beats (RaidStory bits), nights lost.
    uint32_t nextRaidDay = 0;
    uint32_t story = 0;
    uint32_t nightsFailed = 0;
    uint32_t raidInterval = 0; // days between raids, picked on a new save (0: not picked yet)
    std::vector<uint32_t> lootOpened;    // M7: opened caches, paid Skulltula tens, paid bosses (LootKey)
    std::vector<std::string> blueprints; // M7: recipe ids the room has the blueprint for
};
const BaseState& GetBase();
nlohmann::json BaseToJson();
void BaseFromJson(const nlohmann::json& j);
void SeedVillage(); // the boarded-up Kokiri village of a new save
bool BaseOwnsPacket(const std::string& type);
void BaseHandlePacket(const std::string& type, const nlohmann::json& payload, uint32_t from);
void BaseOnFrame();
void BaseOnOwnerChanged(bool nowOwner);
void BaseResetSession();
void BaseRegisterHooks(bool enabled);
std::string BaseCountsLine(); // "Base: 5 pieces in Kokiri Forest"

// Late join: state["sevenDays"] in UPDATE_TEAM_STATE (Anchor's team sync).
nlohmann::json TeamStateJson();
void ApplyTeamStateJson(const nlohmann::json& j);

// Placement mode (a ghost ahead of Link; C-left/C-right rotate, A place, B cancel).
void BeginPlacement(uint8_t type);
bool InPlacement();
void RequestPackUp(uint16_t id);        // one piece back into a kit
void RequestPackUpBase(int era);        // the whole base, every kit refunded
uint16_t NearestPlaceable(float maxDist); // 0 if none

// Placeable actors (Placeables.cpp)
void RegisterPlaceableActors();
int16_t PlaceableActorId();
int16_t GhostActorId();
Actor* SpawnPlaceableActor(const Placeable& p);
void OnPlaceableInteract(uint8_t type); // workbench / chest A-press
// Vanilla Kokiri lines replaced while the village is active (OTRGlobals glue).
bool OverridesVanillaText(uint16_t textId);
constexpr uint16_t TEXT_SIGN_DAY = SEVEN_DAYS_TEXT_BASE + 0x20;
constexpr uint16_t TEXT_WORKBENCH = SEVEN_DAYS_TEXT_BASE + 0x21;
constexpr uint16_t TEXT_CHEST = SEVEN_DAYS_TEXT_BASE + 0x22;
void RegisterVillageMessages(const char* table);
uint32_t CurrentDay(); // the village sign's "Day N" (days survived + 1)

// Internal glue between Base.cpp and Placeables.cpp
const Placeable* FindPlaceable(uint16_t id);
void OnPlaceableSpawned(uint16_t id, Actor* actor);
void OnPlaceableDestroyed(uint16_t id, Actor* actor);
void OnGhostDestroyed(Actor* ghost);
void PlacementUpdate(Actor* ghost, PlayState* play);
bool PlacementGhostValid();
uint8_t PlacementGhostType();
void DamagePlaceable(uint16_t id, int amount); // owner applies; others report to the owner
bool BaseAdoptIfNewer(const nlohmann::json& j, bool force);
bool IsOutdoorScene(int16_t scene); // a scene a base can stand in (the raid clock's scenes)
const char* OutdoorSceneName(int16_t scene);
int CurrentEraNow();
std::vector<std::pair<uint16_t, Actor*>> SpawnedPlaceables(); // id -> actor in this scene
void OnPlaceableHit(uint16_t id, Actor* actor); // the piece shakes (Placeables.cpp)

// MARK: - M6: raids (Raids.cpp)

enum RaidStory : uint32_t {
    STORY_DUSK_DONE = 1 << 0,       // the Kokiri Sword's dusk happened
    STORY_DUSK_ACTIVE = 1 << 1,     // ...and its night isn't over yet
    STORY_FIRST_RAID = 1 << 2,      // Gohma is dead: the prologue is over, raids are scheduled
    STORY_FIRST_RAID_DONE = 1 << 3, // the first raid's dawn came
};

// Navi's staged raid lines (PHA-3870 "Raids, explained in stages"), one save flag each.
enum RaidLine : uint8_t {
    RAIDLINE_EVE,   // 1. the evening before the first raid
    RAIDLINE_START, // 2. the first raid starts
    RAIDLINE_DAWN,  // 3. the first dawn after a raid
    RAIDLINE_AWAY,  // 4. the first raid on the base while everyone was away
    RAIDLINE_LOST,  //    the first lost night
    RAIDLINE_DUSK,  //    the Kokiri Sword's dusk
    RAIDLINE_ENEMY, // 5. each new enemy type's first raid (+ RaidEnemy)
    RAIDLINE_COUNT = RAIDLINE_ENEMY + 5,
};
enum RaidEnemy : uint8_t { RAIDENEMY_STALCHILD, RAIDENEMY_KEESE, RAIDENEMY_WOLFOS, RAIDENEMY_REDEAD, RAIDENEMY_GIBDO };

bool RaidsEnabled();
void QueueRaidNavi(uint8_t line); // SevenDays.cpp: once per save
void RaidsRegisterMessages(const char* table);
void RaidsOnFrame();
void RaidsRegisterHooks(bool enabled);
void RaidsResetSession();
bool RaidOwnsPacket(const std::string& type);
void RaidHandlePacket(const std::string& type, const nlohmann::json& payload, uint32_t from);
void RaidHandleHordeEvent(const nlohmann::json& payload); // HORDE_EVENT with "raid": true
int32_t Gamestage();
bool RaidTonight(); // tonight (or this day's night) is a raid night
bool RaidWaveHere(); // a raid wave is being fought in this scene (ours or the authority's)
uint32_t NightsUntilRaid(); // 0: tonight; UINT32_MAX: none scheduled
uint32_t RaidInterval();    // the save's days between raids (the RaidInterval setting until picked)
void RequestRaidInterval(uint32_t days); // owner only: sets the save's interval

// MARK: - M7: loot (Loot.cpp, data in SevenDaysData.cpp)

bool LootEnabled();
bool NightsEnabled();
bool HasBlueprint(const std::string& recipeId);

// Loot keys in BaseState::lootOpened: the kind in the top 4 bits.
enum LootKind : uint32_t { LOOT_CACHE = 1, LOOT_SKULL = 2, LOOT_BOSS = 3 };
constexpr uint32_t LootKey(LootKind kind, uint32_t value) {
    return ((uint32_t)kind << 28) | (value & 0x0FFFFFFF);
}

// A supply cache's place: next to an anchor (a pot, chest or crate in the
// room's vanilla actor list), floor found by raycast when it spawns.
struct CacheSpot {
    int16_t scene;
    int8_t room;
    int16_t x, y, z; // the anchor
    uint8_t tier;    // area tier (Unlock) for its rolls
};
const std::vector<CacheSpot>& GetCacheSpots();
// Pot/crate/cache roll table for an area tier: weights per material.
const uint8_t* LootWeights(uint8_t tier);
uint8_t SceneTier(int16_t scene); // the area tier of a scene (dungeons by table, else the start tier)
// Blueprints caches can hold, and each boss's key blueprint.
const std::vector<const char*>& CacheBlueprints();
const char* BossBlueprint(int16_t bossActorId); // nullptr: none of its own
const char* BossName(int16_t bossActorId);

// Navi's loot lines share the firsts bitfield (SevenDays.cpp).
enum LootLine : uint8_t {
    LOOTLINE_BLUEPRINT,
    LOOTLINE_CACHE,
    LOOTLINE_POT,
    LOOTLINE_TIER_DEKU, // + (Unlock - UNLOCK_DEKU_TREE)
    LOOTLINE_TIER_BOMB,
    LOOTLINE_TIER_HOOKSHOT,
    LOOTLINE_TIER_HAMMER,
    LOOTLINE_TIER_SILVER,
    LOOTLINE_COUNT,
};
void QueueLootNavi(uint8_t line, bool silent = false); // silent: mark said, don't say it
bool LootLineSaid(uint8_t line);
void LootRegisterMessages(const char* table);
bool LootOwnsPacket(const std::string& type);
void LootHandlePacket(const std::string& type, const nlohmann::json& payload, uint32_t from);
void LootOnFrame();
void LootRegisterHooks(bool enabled);
void LootResetSession();
// Pots and crates pay through the ordinary GATHER path (SevenDays.cpp).
void SendGather(uint8_t material, uint32_t amount, uint64_t sourceKey, uint32_t dedupeSeconds);
uint64_t SourceKeyFor(Actor* actor, uint32_t salt);

// MARK: - M7: Majora-style nights (Nights.cpp)

void NightsOnFrame();
void NightsRegisterHooks(bool enabled);
void NightsResetSession();
// The dawn card ("Dawn of Day 7 · 2 nights until the raid"), shown on every client at dawn.
void ShowDawnCard(uint32_t day, uint32_t untilRaid, uint32_t daysSurvived, uint32_t raidsSurvived);
std::string PauseCountersLine();
// File select (FileSelectMoreInfo): the counters of the file last loaded by SaveManager.
void TakeMetaCounters(int fileNum);
void MarkMetaCounters();

} // namespace SevenDays

class SevenDaysCraftingWindow : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;

    void InitElement() override{};
    void Draw() override;
    void DrawElement() override;
    void UpdateElement() override{};
};

#endif // __cplusplus
#endif // SEVEN_DAYS_H
