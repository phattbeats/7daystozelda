#ifndef SEVEN_DAYS_H
#define SEVEN_DAYS_H
#ifdef __cplusplus

/**
 * 7 Days to Zelda (PHA-3870). M4: materials, crafting, tunic colors.
 * M5: placeables, the base, saving and the boarded-up village.
 *
 * Everything sits behind gSevenDays.Enabled, and each milestone behind its own
 * switch under it, so plain co-op runs untouched with the master switch off.
 *
 *   gSevenDays.Enabled       0/1  master switch (default 0)
 *   gSevenDays.Crafting      0/1  M4 materials, shared pool, crafting window (default 1)
 *   gSevenDays.TunicColors   0/1  tunic + cap in the player's lobby color (default 1)
 *   gSevenDays.Base          0/1  M5 placeables, placement mode, the base, the village (default 1)
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
    std::vector<uint32_t> lootOpened; // M7 supply caches, kept so the format is stable
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
