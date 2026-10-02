#include "SevenDays.h"
#include "SevenDaysNet.h"
#include "soh/ShipInit.hpp"
#include "soh/SaveManager.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/custom-message/CustomMessageManager.h"
#include "soh/Network/Anchor/Anchor.h"
#include "soh/Network/Anchor/EnemySync.h"
#include "soh/Notification/Notification.h"

extern "C" {
#include "overlays/actors/ovl_En_Ishi/z_en_ishi.h"
}

#include <algorithm>
#include <chrono>
#include <deque>
#include <unordered_map>

extern "C" {
#include "z64.h"
#include "macros.h"
#include "variables.h"
#include "functions.h"
extern PlayState* gPlayState;
extern Color_RGB8 gSevenDaysTunicColor;
extern u8 gSevenDaysTunicColorActive;
}

/**
 * M4: gathering, the shared pool, crafting and Navi's first lines.
 *
 * Packets (JSON, sent through Anchor like HORDE_EVENT):
 *   GATHER           gatherer -> owner      material, amount, sourceKey, dedupe
 *   CRAFT_REQUEST    crafter  -> owner      recipe, reqId
 *   CRAFT_RESULT     owner    -> crafter    reqId, recipe, ok, reason + pool
 *   MATERIALS_STATE  owner    -> room       the pool (+ who was just credited)
 *   MATERIALS_REQUEST joiner  -> owner      the joiner's cached pool; higher rev wins
 */

namespace SevenDays {

using Net::ActingOwner;
using Net::Broadcast;
using Net::Connected;
using Net::IsOwner;
using Net::Now;
using Net::OwnId;
using Net::SendTo;

static const std::string GATHER = "GATHER";
static const std::string CRAFT_REQUEST = "CRAFT_REQUEST";
static const std::string CRAFT_RESULT = "CRAFT_RESULT";
static const std::string MATERIALS_STATE = "MATERIALS_STATE";
static const std::string MATERIALS_REQUEST = "MATERIALS_REQUEST";

static const char* CUSTOM_MESSAGE_TABLE = "SevenDays";

// Navi first-time lines, one save flag each.
enum FirstLine : uint8_t {
    FIRST_GATHER_BASE = 0, // + material
    FIRST_CRAFT = MAT_COUNT,
    FIRST_TRADE,
    FIRST_COUNT,
};

static const char* sFirstLineText[FIRST_COUNT - MAT_COUNT] = {
    /* FIRST_CRAFT */ "You made it yourself, Link!&Kits wait in the pool until you build.",
    /* FIRST_TRADE */ "Rupees for rocks and twigs?&Mido's shield isn't so far off now!",
};

static PoolState sPool;
static uint32_t sFirsts = 0; // bit per FirstLine, saved with the file
static std::deque<uint16_t> sPendingNavi;

// Owner-side dedupe: sourceKey -> expiry (seconds on the steady clock).
static std::unordered_map<uint64_t, double> sSeenSources;

struct InFlightCraft {
    uint32_t reqId = 0;
    std::string recipe;
    double sentAt = 0;
};
static InFlightCraft sInFlight;
static uint32_t sNextReqId = 1;
static uint32_t sLastActingOwner = UINT32_MAX;

double Net::Now() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

bool Enabled() {
    return CVarGetInteger(CVAR_SEVEN_DAYS("Enabled"), 0) != 0;
}

bool CraftingEnabled() {
    return Enabled() && CVarGetInteger(CVAR_SEVEN_DAYS("Crafting"), 1) != 0;
}

bool BaseEnabled() {
    return Enabled() && CVarGetInteger(CVAR_SEVEN_DAYS("Base"), 1) != 0;
}

bool RaidsEnabled() {
    return BaseEnabled() && CVarGetInteger(CVAR_SEVEN_DAYS("Raids"), 1) != 0;
}

bool LootEnabled() {
    return CraftingEnabled() && BaseEnabled() && CVarGetInteger(CVAR_SEVEN_DAYS("Loot"), 1) != 0;
}

bool NightsEnabled() {
    return RaidsEnabled() && CVarGetInteger(CVAR_SEVEN_DAYS("Nights"), 1) != 0;
}

bool HasBlueprint(const std::string& recipeId) {
    const auto& b = GetBase().blueprints;
    return std::find(b.begin(), b.end(), recipeId) != b.end();
}

bool TunicColorsEnabled() {
    return Enabled() && CVarGetInteger(CVAR_SEVEN_DAYS("TunicColors"), 1) != 0;
}

const PoolState& GetPool() {
    return sPool;
}

bool IsSevenDaysText(uint16_t textId) {
    return textId >= SEVEN_DAYS_TEXT_BASE && textId < SEVEN_DAYS_TEXT_BASE + SEVEN_DAYS_TEXT_COUNT;
}

// MARK: - Authority

bool Net::Connected() {
    return Anchor::Instance != nullptr && Anchor::Instance->isConnected;
}

// The room owner decides the pool. While it's offline or still on file select,
// the lowest online, save-loaded client acts for it from its cached copy.
uint32_t Net::ActingOwner() {
    if (!Connected()) {
        return 0;
    }
    auto anchor = Anchor::Instance;
    auto ready = [anchor](const AnchorClient& client) {
        return client.online && (client.self ? anchor->IsSaveLoaded() : client.isSaveLoaded);
    };
    auto owner = anchor->clients.find(anchor->roomState.ownerClientId);
    if (owner != anchor->clients.end() && ready(owner->second)) {
        return owner->first;
    }
    for (auto& [clientId, client] : anchor->clients) { // std::map: ascending ids
        if (ready(client)) {
            return clientId;
        }
    }
    return anchor->ownClientId;
}

bool Net::IsOwner() {
    return !Connected() || ActingOwner() == Anchor::Instance->ownClientId;
}

uint32_t Net::OwnId() {
    return Connected() ? Anchor::Instance->ownClientId : 0;
}

void Net::SendTo(uint32_t clientId, nlohmann::json payload) {
    payload["targetClientId"] = clientId;
    Anchor::Instance->SendJsonToRemote(payload);
}

void Net::Broadcast(nlohmann::json payload) {
    if (Connected()) {
        Anchor::Instance->SendJsonToRemote(payload);
    }
}

// MARK: - Pool <-> JSON

static nlohmann::json PoolToJson() {
    nlohmann::json j;
    j["rev"] = sPool.rev;
    j["materials"] = nlohmann::json::array();
    for (uint8_t m = 0; m < MAT_COUNT; m++) {
        j["materials"].push_back(sPool.materials[m]);
    }
    j["kits"] = sPool.kits;
    return j;
}

static void PoolFromJson(const nlohmann::json& j) {
    sPool.rev = j.value("rev", 0u);
    auto mats = j.value("materials", nlohmann::json::array());
    for (uint8_t m = 0; m < MAT_COUNT; m++) {
        sPool.materials[m] = m < mats.size() ? mats[m].get<uint32_t>() : 0;
    }
    sPool.kits = j.value("kits", std::map<std::string, uint32_t>{});
}

static void BroadcastPool(const nlohmann::json& credit) {
    nlohmann::json payload;
    payload["type"] = MATERIALS_STATE;
    payload["pool"] = PoolToJson();
    if (!credit.is_null()) {
        payload["credit"] = credit;
    }
    Broadcast(payload);
}

PoolState& Net::MutablePool() {
    return sPool;
}

void Net::BroadcastPool() {
    SevenDays::BroadcastPool(nlohmann::json());
}

// MARK: - Late join (Anchor team state)

nlohmann::json TeamStateJson() {
    nlohmann::json j;
    j["pool"] = PoolToJson();
    j["base"] = BaseToJson();
    return j;
}

void ApplyTeamStateJson(const nlohmann::json& j) {
    if (!Enabled() || !j.is_object()) {
        return;
    }
    // Two copies disagree: the higher rev wins (the owner re-broadcasts its own
    // copy if it is newer, see MATERIALS_REQUEST / BASE_REQUEST).
    if (j.contains("pool") && j["pool"].value("rev", 0u) > sPool.rev) {
        PoolFromJson(j["pool"]);
    }
    if (j.contains("base") && BaseEnabled()) {
        BaseAdoptIfNewer(j["base"], false);
    }
}

// MARK: - Navi

static void QueueNavi(uint8_t first) {
    if (first >= FIRST_COUNT || (sFirsts & (1u << first))) {
        return;
    }
    sFirsts |= 1u << first;
    sPendingNavi.push_back(SEVEN_DAYS_TEXT_BASE + first);
}

// Raid lines share the firsts bitfield (bits 8+) and the text table (0x08+).
constexpr uint8_t RAID_FIRST_BIT = 8;
constexpr uint16_t RAID_TEXT_OFFSET = 0x08;
static_assert(RAID_FIRST_BIT + RAIDLINE_COUNT <= 32, "firsts bitfield");
static_assert(RAID_TEXT_OFFSET + RAIDLINE_COUNT <= 0x20, "raid lines must stay below the village text ids");

void QueueRaidNavi(uint8_t line) {
    if (line >= RAIDLINE_COUNT || (sFirsts & (1u << (RAID_FIRST_BIT + line)))) {
        return;
    }
    sFirsts |= 1u << (RAID_FIRST_BIT + line);
    sPendingNavi.push_back(SEVEN_DAYS_TEXT_BASE + RAID_TEXT_OFFSET + line);
}

// Loot lines: bits 20+ of the firsts bitfield, text ids 0x28+.
constexpr uint8_t LOOT_FIRST_BIT = 20;
constexpr uint16_t LOOT_TEXT_OFFSET = 0x28;
static_assert(RAID_FIRST_BIT + RAIDLINE_COUNT <= LOOT_FIRST_BIT, "raid and loot firsts overlap");
static_assert(LOOT_FIRST_BIT + LOOTLINE_COUNT <= 32, "firsts bitfield");
static_assert(LOOT_TEXT_OFFSET + LOOTLINE_COUNT <= SEVEN_DAYS_TEXT_COUNT, "loot text ids");

void QueueLootNavi(uint8_t line, bool silent) {
    if (line >= LOOTLINE_COUNT || (sFirsts & (1u << (LOOT_FIRST_BIT + line)))) {
        return;
    }
    sFirsts |= 1u << (LOOT_FIRST_BIT + line);
    if (!silent) {
        sPendingNavi.push_back(SEVEN_DAYS_TEXT_BASE + LOOT_TEXT_OFFSET + line);
    }
}

bool LootLineSaid(uint8_t line) {
    return line < LOOTLINE_COUNT && (sFirsts & (1u << (LOOT_FIRST_BIT + line))) != 0;
}

static void ShowPendingNavi() {
    if (sPendingNavi.empty() || gPlayState == nullptr) {
        return;
    }
    Player* player = GET_PLAYER(gPlayState);
    if (player == nullptr || gPlayState->msgCtx.msgMode != MSGMODE_NONE || Player_InCsMode(gPlayState) ||
        (player->stateFlags1 & (PLAYER_STATE1_DEAD | PLAYER_STATE1_TALKING | PLAYER_STATE1_GETTING_ITEM |
                                PLAYER_STATE1_IN_CUTSCENE)) ||
        gPlayState->transitionTrigger != TRANS_TRIGGER_OFF || gPlayState->pauseCtx.state != 0) {
        return;
    }
    uint16_t textId = sPendingNavi.front();
    sPendingNavi.pop_front();
    Sfx_PlaySfxCentered(NA_SE_VO_NA_HELLO_0);
    Message_StartTextbox(gPlayState, textId, NULL);
}

static void RegisterMessages() {
    static bool registered = false;
    if (registered) {
        return;
    }
    registered = true;
    CustomMessageManager::Instance->AddCustomMessageTable(CUSTOM_MESSAGE_TABLE);
    RegisterVillageMessages(CUSTOM_MESSAGE_TABLE);
    RaidsRegisterMessages(CUSTOM_MESSAGE_TABLE);
    LootRegisterMessages(CUSTOM_MESSAGE_TABLE);
    for (uint8_t m = 0; m < MAT_COUNT; m++) {
        CustomMessageManager::Instance->CreateMessage(
            CUSTOM_MESSAGE_TABLE, SEVEN_DAYS_TEXT_BASE + FIRST_GATHER_BASE + m,
            CustomMessage(GetMaterialInfo(m).firstLine, TEXTBOX_TYPE_BLUE, TEXTBOX_POS_BOTTOM));
    }
    for (uint8_t f = FIRST_CRAFT; f < FIRST_COUNT; f++) {
        CustomMessageManager::Instance->CreateMessage(
            CUSTOM_MESSAGE_TABLE, SEVEN_DAYS_TEXT_BASE + f,
            CustomMessage(sFirstLineText[f - MAT_COUNT], TEXTBOX_TYPE_BLUE, TEXTBOX_POS_BOTTOM));
    }
}

// MARK: - Gathering

static void OnCredited(uint8_t material, uint32_t amount, bool quiet = false) {
    if (material >= MAT_COUNT) {
        return;
    }
    Notification::Emit({
        .prefix = fmt::format("+{}", amount),
        .prefixColor = ImVec4(0.6f, 1.0f, 0.6f, 1.0f),
        .message = GetMaterialInfo(material).name,
        .remainingTime = 3.0f,
        .mute = true,
    });
    Sfx_PlaySfxCentered(NA_SE_SY_GET_ITEM);
    if (!quiet) {
        QueueNavi(FIRST_GATHER_BASE + material);
    } else {
        QueueLootNavi(LOOTLINE_POT); // quiet credits are pots and crates (Loot.cpp): their own line
    }
}

// Owner: dedupe by sourceKey, apply, broadcast. Returns true when it paid.
static bool ApplyGather(const nlohmann::json& payload, uint32_t gatherer) {
    uint8_t material = payload.value("material", (uint8_t)0xFF);
    uint32_t amount = payload.value("amount", 0u);
    uint64_t sourceKey = payload.value("sourceKey", (uint64_t)0);
    double dedupe = payload.value("dedupe", 60.0);
    if (material >= MAT_COUNT || amount == 0 || amount > 50) {
        return false;
    }

    double now = Now();
    if (sSeenSources.size() > 512) {
        std::erase_if(sSeenSources, [now](const auto& entry) { return entry.second <= now; });
    }
    auto seen = sSeenSources.find(sourceKey);
    if (seen != sSeenSources.end() && seen->second > now) {
        return false;
    }
    sSeenSources[sourceKey] = now + dedupe;

    sPool.materials[material] += amount;
    sPool.rev++;

    nlohmann::json credit;
    credit["clientId"] = gatherer;
    credit["material"] = material;
    credit["amount"] = amount;
    credit["quiet"] = payload.value("quiet", false);
    BroadcastPool(credit);
    if (gatherer == OwnId()) {
        OnCredited(material, amount, credit["quiet"].get<bool>());
    }
    return true;
}

// Stable across clients: scene, room, actor, params and the home position (in
// place of the spawn-order index, which other machines don't know for
// non-enemies), folded through EnemySync's PackKey.
static uint64_t StaticSourceKey(Actor* actor, uint32_t salt) {
    uint32_t h = 2166136261u;
    auto mix = [&h](uint32_t v) { h = (h ^ v) * 16777619u; };
    mix((uint32_t)(int32_t)(actor->home.pos.x / 4.0f));
    mix((uint32_t)(int32_t)(actor->home.pos.y / 4.0f));
    mix((uint32_t)(int32_t)(actor->home.pos.z / 4.0f));
    mix(salt);
    uint64_t key = EnemySync::PackKey(actor->room, (uint16_t)(h ^ (h >> 16)), actor->id, (uint16_t)actor->params);
    return key ^ ((uint64_t)(uint8_t)gPlayState->sceneNum << 56);
}

uint64_t SourceKeyFor(Actor* actor, uint32_t salt) {
    return StaticSourceKey(actor, salt);
}

void SendGather(uint8_t material, uint32_t amount, uint64_t sourceKey, uint32_t dedupeSeconds) {
    if (!CraftingEnabled() || material >= MAT_COUNT || amount == 0) {
        return;
    }
    nlohmann::json payload;
    payload["quiet"] = true; // no first-gather line ("Grass fiber!") for a pot
    payload["type"] = GATHER;
    payload["material"] = material;
    payload["amount"] = amount;
    payload["sourceKey"] = sourceKey;
    payload["dedupe"] = dedupeSeconds;
    if (IsOwner()) {
        ApplyGather(payload, OwnId());
    } else {
        SendTo(ActingOwner(), payload);
    }
}

static void Gather(Actor* actor) {
    if (!CraftingEnabled() || gPlayState == nullptr || actor == nullptr) {
        return;
    }
    const GatherSource* source = FindGatherSource(actor->id);
    if (source == nullptr || !IsUnlocked(source->unlock)) {
        return;
    }

    uint64_t sourceKey = 0;
    if (actor->category == ACTORCAT_ENEMY) {
        // Mirrored deaths play out on every client in the scene; EnemySync's key
        // is the one identity they all share.
        sourceKey = EnemySync::KeyForActor(actor);
    }
    if (sourceKey == 0) {
        sourceKey = StaticSourceKey(actor, source->perDay ? (uint32_t)gSaveContext.totalDays : 0);
    }

    nlohmann::json payload;
    payload["type"] = GATHER;
    payload["material"] = source->material;
    payload["amount"] = source->amount;
    payload["sourceKey"] = sourceKey;
    payload["dedupe"] = source->dedupeSeconds;

    if (IsOwner()) {
        ApplyGather(payload, OwnId());
    } else {
        SendTo(ActingOwner(), payload);
    }
}

// MARK: - Crafting

bool CraftInFlight() {
    return sInFlight.reqId != 0;
}

static bool HasInputs(const Recipe& recipe) {
    for (uint8_t i = 0; i < recipe.inputCount; i++) {
        if (sPool.materials[recipe.inputs[i].material] < recipe.inputs[i].amount) {
            return false;
        }
    }
    return true;
}

static bool AmmoFull(uint8_t item) {
    switch (item) {
        case ITEM_STICKS_5:
            return INV_CONTENT(ITEM_STICK) != ITEM_NONE && AMMO(ITEM_STICK) >= CUR_CAPACITY(UPG_STICKS);
        case ITEM_NUTS_5:
            return INV_CONTENT(ITEM_NUT) != ITEM_NONE && AMMO(ITEM_NUT) >= CUR_CAPACITY(UPG_NUTS);
        case ITEM_SEEDS_30:
            return AMMO(ITEM_SLINGSHOT) >= CUR_CAPACITY(UPG_BULLET_BAG);
        case ITEM_ARROWS_SMALL:
            return AMMO(ITEM_BOW) >= CUR_CAPACITY(UPG_QUIVER);
        case ITEM_BOMBS_5:
            return AMMO(ITEM_BOMB) >= CUR_CAPACITY(UPG_BOMB_BAG);
    }
    return false;
}

std::string CraftBlocker(const Recipe& recipe) {
    if (!IsUnlocked(recipe.unlock)) {
        return UnlockName(recipe.unlock);
    }
    if (recipe.blueprint && LootEnabled() && !HasBlueprint(recipe.id)) {
        return "Needs its blueprint";
    }
    if (recipe.kind == RECIPE_CONSUMABLE) {
        if (recipe.requiredItem != ITEM_NONE && INV_CONTENT(recipe.requiredItem) != recipe.requiredItem) {
            return "You don't have the item for this yet";
        }
        if (AmmoFull(recipe.item)) {
            return "You can't carry any more";
        }
    }
    if (recipe.kind == RECIPE_TRADE && gSaveContext.rupees + recipe.outputCount > CUR_CAPACITY(UPG_WALLET)) {
        return "Your wallet is full";
    }
    if (!HasInputs(recipe)) {
        return "Not enough materials";
    }
    if (CraftInFlight()) {
        return "Waiting for the host...";
    }
    return "";
}

static void OnCraftResult(const nlohmann::json& payload) {
    if (payload.contains("pool")) {
        PoolFromJson(payload["pool"]);
    }
    if (payload.value("reqId", 0u) != sInFlight.reqId) {
        return; // stale or timed out
    }
    sInFlight = {};

    const Recipe* recipe = FindRecipe(payload.value("recipe", ""));
    if (recipe == nullptr || gPlayState == nullptr) {
        return;
    }
    if (!payload.value("ok", false)) {
        Notification::Emit({ .prefix = recipe->name,
                             .message = payload.value("reason", "Couldn't craft that"),
                             .remainingTime = 3.0f });
        Sfx_PlaySfxCentered(NA_SE_SY_ERROR);
        return;
    }

    switch (recipe->kind) {
        case RECIPE_CONSUMABLE:
            Item_Give(gPlayState, recipe->item);
            Sfx_PlaySfxCentered(NA_SE_SY_GET_ITEM);
            QueueNavi(FIRST_CRAFT);
            break;
        case RECIPE_KIT:
            Notification::Emit({ .prefix = "Crafted",
                                 .message = fmt::format("{} kit", recipe->name),
                                 .remainingTime = 3.0f,
                                 .mute = true });
            Sfx_PlaySfxCentered(NA_SE_SY_GET_ITEM);
            QueueNavi(FIRST_CRAFT);
            break;
        case RECIPE_TRADE:
            Rupees_ChangeBy(recipe->outputCount);
            Sfx_PlaySfxCentered(NA_SE_SY_GET_RUPY);
            QueueNavi(FIRST_TRADE);
            break;
    }
}

// Owner: check inputs, deduct, answer. Two crafters spending the same wood can't
// both succeed: requests are settled one at a time against the owner's pool.
static void ProcessCraftRequest(const nlohmann::json& payload, uint32_t requester) {
    nlohmann::json result;
    result["type"] = CRAFT_RESULT;
    result["reqId"] = payload.value("reqId", 0u);
    result["recipe"] = payload.value("recipe", "");

    const Recipe* recipe = FindRecipe(payload.value("recipe", ""));
    if (recipe == nullptr) {
        result["ok"] = false;
        result["reason"] = "Unknown recipe";
    } else if (!HasInputs(*recipe)) {
        result["ok"] = false;
        result["reason"] = "Not enough materials";
    } else {
        for (uint8_t i = 0; i < recipe->inputCount; i++) {
            sPool.materials[recipe->inputs[i].material] -= recipe->inputs[i].amount;
        }
        if (recipe->kind == RECIPE_KIT) {
            sPool.kits[recipe->id] += recipe->outputCount;
        }
        sPool.rev++;
        result["ok"] = true;
        BroadcastPool(nullptr);
    }
    result["pool"] = PoolToJson();

    if (requester == OwnId()) {
        OnCraftResult(result);
    } else {
        SendTo(requester, result);
    }
}

bool RequestCraft(const std::string& recipeId) {
    const Recipe* recipe = FindRecipe(recipeId);
    if (recipe == nullptr || !CraftBlocker(*recipe).empty()) {
        return false;
    }
    sInFlight.reqId = sNextReqId++;
    sInFlight.recipe = recipeId;
    sInFlight.sentAt = Now();

    nlohmann::json payload;
    payload["type"] = CRAFT_REQUEST;
    payload["recipe"] = recipeId;
    payload["reqId"] = sInFlight.reqId;
    if (IsOwner()) {
        ProcessCraftRequest(payload, OwnId());
    } else {
        SendTo(ActingOwner(), payload);
    }
    return true;
}

// MARK: - Packets

bool IsPacket(const std::string& type) {
    return type == GATHER || type == CRAFT_REQUEST || type == CRAFT_RESULT || type == MATERIALS_STATE ||
           type == MATERIALS_REQUEST || BaseOwnsPacket(type) || RaidOwnsPacket(type) || LootOwnsPacket(type);
}

static void SendMaterialsRequest() {
    if (!Connected() || IsOwner()) {
        return;
    }
    nlohmann::json payload;
    payload["type"] = MATERIALS_REQUEST;
    payload["pool"] = PoolToJson();
    SendTo(ActingOwner(), payload);
}

void HandlePacket(const nlohmann::json& payload) {
    if (!Enabled()) {
        return;
    }
    std::string type = payload.value("type", "");
    uint32_t from = payload.value("clientId", 0u);

    if (BaseOwnsPacket(type)) {
        BaseHandlePacket(type, payload, from);
    } else if (RaidOwnsPacket(type)) {
        RaidHandlePacket(type, payload, from);
    } else if (LootOwnsPacket(type)) {
        LootHandlePacket(type, payload, from);
    } else if (type == GATHER) {
        if (IsOwner()) {
            ApplyGather(payload, from);
        }
    } else if (type == CRAFT_REQUEST) {
        if (IsOwner()) {
            ProcessCraftRequest(payload, from);
        } else {
            // We lost ownership mid-flight: refuse rather than spend a stale copy.
            nlohmann::json result;
            result["type"] = CRAFT_RESULT;
            result["reqId"] = payload.value("reqId", 0u);
            result["recipe"] = payload.value("recipe", "");
            result["ok"] = false;
            result["reason"] = "The host changed, try again";
            SendTo(from, result);
        }
    } else if (type == CRAFT_RESULT) {
        OnCraftResult(payload);
    } else if (type == MATERIALS_STATE) {
        if (from != ActingOwner()) {
            return;
        }
        PoolFromJson(payload["pool"]);
        if (payload.contains("credit") && payload["credit"].value("clientId", 0u) == OwnId()) {
            OnCredited(payload["credit"].value("material", (uint8_t)0xFF), payload["credit"].value("amount", 0u),
                       payload["credit"].value("quiet", false));
        }
    } else if (type == MATERIALS_REQUEST) {
        if (!IsOwner()) {
            return;
        }
        // Two copies disagree (e.g. the owner was offline): the higher rev wins.
        if (payload.contains("pool") && payload["pool"].value("rev", 0u) > sPool.rev) {
            PoolFromJson(payload["pool"]);
        }
        BroadcastPool(nullptr);
    }
}

// MARK: - Per frame

static void OnFrame() {
    if (gPlayState == nullptr) {
        return;
    }

    // Tunic: our own Link wears our lobby color (DummyPlayer swaps in each
    // remote player's color around its draw).
    RestoreLocalTunicOverride();

    // Ownership moved (owner left, or we just connected): sync up with the new one.
    // The pool and the base travel together: kits are spent by placement.
    uint32_t acting = ActingOwner();
    if (acting != sLastActingOwner) {
        sLastActingOwner = acting;
        if (IsOwner()) {
            BroadcastPool(nullptr);
        } else {
            SendMaterialsRequest();
        }
        if (BaseEnabled()) {
            BaseOnOwnerChanged(IsOwner());
        }
    }

    if (BaseEnabled()) {
        BaseOnFrame();
    }
    if (RaidsEnabled()) {
        RaidsOnFrame();
    }
    if (LootEnabled()) {
        LootOnFrame();
    }
    if (NightsEnabled()) {
        NightsOnFrame();
    }

    ShowPendingNavi();

    if (!CraftingEnabled()) {
        return;
    }

    if (CraftInFlight() && Now() - sInFlight.sentAt > 5.0) {
        sInFlight = {};
        Notification::Emit({ .prefix = "Crafting", .message = "The host didn't answer", .remainingTime = 3.0f });
    }
}

// MARK: - Save

static void InitSave(bool isDebug) {
    sPool = {};
    sFirsts = 0;
    sPendingNavi.clear();
    sSeenSources.clear();
    sInFlight = {};
    sLastActingOwner = UINT32_MAX;
    // Spare boards by the village workbench: Kokiri Forest has no trees to roll
    // into before the Lost Woods, and the first craft should be a barricade.
    // (A loaded save's own pool replaces this.)
    sPool.materials[MAT_WOOD] = 8;
    // A new save starts in the boarded-up village; loading a v2 section replaces it.
    BaseResetSession();
    RaidsResetSession();
    LootResetSession();
    NightsResetSession();
    SeedVillage();
}

static void SaveSection(SaveContext* saveContext, int sectionID, bool fullSave) {
    SaveManager::Instance->SaveData("rev", sPool.rev);
    SaveManager::Instance->SaveArray("materials", MAT_COUNT,
                                     [](size_t i) { SaveManager::Instance->SaveData("", sPool.materials[i]); });
    std::vector<std::pair<std::string, uint32_t>> kits(sPool.kits.begin(), sPool.kits.end());
    SaveManager::Instance->SaveData("kitCount", (uint32_t)kits.size());
    SaveManager::Instance->SaveArray("kits", kits.size(), [&kits](size_t i) {
        SaveManager::Instance->SaveStruct("", [&kits, i]() {
            SaveManager::Instance->SaveData("id", kits[i].first);
            SaveManager::Instance->SaveData("count", kits[i].second);
        });
    });
    SaveManager::Instance->SaveData("firsts", sFirsts);
    // v2 (M5): the base, counters and loot flags, as one JSON object.
    SaveManager::Instance->SaveData("base", BaseToJson());
    MarkMetaCounters(); // file select's details read this file's counters next
}

static void LoadSection() {
    SaveManager::Instance->LoadData("rev", sPool.rev);
    SaveManager::Instance->LoadArray("materials", MAT_COUNT,
                                     [](size_t i) { SaveManager::Instance->LoadData("", sPool.materials[i]); });
    sPool.kits.clear();
    uint32_t kitCount = 0;
    SaveManager::Instance->LoadData("kitCount", kitCount);
    SaveManager::Instance->LoadArray("kits", kitCount, [](size_t i) {
        SaveManager::Instance->LoadStruct("", []() {
            std::string id;
            uint32_t count = 0;
            SaveManager::Instance->LoadData("id", id);
            SaveManager::Instance->LoadData("count", count);
            if (!id.empty()) {
                sPool.kits[id] = count;
            }
        });
    });
    SaveManager::Instance->LoadData("firsts", sFirsts);
}

// v2 = v1 + "base". A v1 save (M4) keeps the village seeded by InitSave.
static void LoadSectionV2() {
    LoadSection();
    nlohmann::json base;
    SaveManager::Instance->LoadData("base", base);
    if (base.is_object()) {
        BaseFromJson(base);
    }
    RaidsResetSession();
    LootResetSession();
    NightsResetSession();
    MarkMetaCounters();
}

// MARK: - Test hooks

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
// For tools/webtest: drive the real GATHER / CRAFT_REQUEST paths without
// steering Link into grass. Inert unless gSevenDays.Enabled is on.
extern "C" {
EMSCRIPTEN_KEEPALIVE
void sevendays_test_gather(int material, int amount, double sourceKey) {
    if (!CraftingEnabled() || material < 0 || material >= MAT_COUNT) {
        return;
    }
    nlohmann::json payload;
    payload["type"] = GATHER;
    payload["material"] = material;
    payload["amount"] = amount;
    payload["sourceKey"] = (uint64_t)sourceKey;
    payload["dedupe"] = 60;
    if (IsOwner()) {
        ApplyGather(payload, OwnId());
    } else {
        SendTo(ActingOwner(), payload);
    }
}

// Set an integer CVar (tests: FileSelectMoreInfo, the M7 switches, mode on/off).
EMSCRIPTEN_KEEPALIVE
void sevendays_test_cvar(const char* name, int value) {
    CVarSetInteger(name, value);
    ShipInit::Init(name);
}

// Save the file (what the pause screen's Save does), so file select's details
// can be checked after a reload.
EMSCRIPTEN_KEEPALIVE
void sevendays_test_save() {
    if (GameInteractor::IsSaveLoaded(true)) {
        SaveManager::Instance->SaveFile(gSaveContext.fileNum);
    }
}

EMSCRIPTEN_KEEPALIVE
int sevendays_test_craft(const char* recipeId) {
    return CraftingEnabled() && RequestCraft(recipeId) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE
const char* sevendays_test_state() {
    static std::string out;
    nlohmann::json j = PoolToJson();
    j["owner"] = IsOwner();
    j["actingOwner"] = ActingOwner();
    j["firsts"] = sFirsts;
    j["rupees"] = gSaveContext.rupees;
    j["sticks"] = AMMO(ITEM_STICK);
    out = j.dump();
    return out.c_str();
}
}
#endif

// MARK: - Registration

static void RegisterSevenDaysM4() {
    bool crafting = CraftingEnabled();

    COND_VB_SHOULD(VB_GRASS_DROP_ITEM, crafting, {
        Actor* actor = va_arg(args, Actor*);
        Gather(actor);
    });
    COND_VB_SHOULD(VB_TREE_DROP_ITEM, crafting, {
        Actor* actor = va_arg(args, Actor*);
        Gather(actor);
    });
    COND_HOOK(OnActorKill, crafting, [](void* refActor) {
        Actor* actor = (Actor*)refActor;
        // Only breaks during play: init-time kills (an already-broken boulder
        // removing itself on scene load) still have their init pending.
        if (actor->init != NULL) {
            return;
        }
        if (actor->id == ACTOR_EN_ISHI && (actor->params & 1) == 0) {
            // Only a rock that was broken: thrown (it left home) or smashed where it
            // sat (AC hit). Obj_Mure2 rock circles despawn their rocks with
            // Actor_Kill when Link walks away, which must not pay out.
            EnIshi* rock = (EnIshi*)actor;
            bool thrown = Math_Vec3f_DistXZ(&actor->world.pos, &actor->home.pos) > 10.0f;
            if (thrown || (rock->collider.base.acFlags & AC_HIT)) {
                Gather(actor);
            }
        } else if (actor->id == ACTOR_OBJ_BOMBIWA) {
            Gather(actor);
        }
    });
    COND_HOOK(OnEnemyDefeat, crafting, [](void* refActor) {
        Actor* actor = (Actor*)refActor;
        if (actor->id == ACTOR_EN_SKB || actor->id == ACTOR_EN_RD) {
            Gather(actor);
        }
    });
    COND_HOOK(OnGameFrameUpdate, Enabled(), OnFrame);
    BaseRegisterHooks(BaseEnabled());
    RaidsRegisterHooks(RaidsEnabled());
    LootRegisterHooks(LootEnabled());
    NightsRegisterHooks(NightsEnabled());

    if (!Enabled()) {
        gSevenDaysTunicColorActive = 0;
    }
}

static void RegisterSevenDaysOnce() {
    RegisterMessages();
    SaveManager::Instance->AddInitFunction(InitSave);
    SaveManager::Instance->AddLoadFunction("sevenDays", 1, LoadSection);
    SaveManager::Instance->AddLoadFunction("sevenDays", 2, LoadSectionV2);
    SaveManager::Instance->AddSaveFunction("sevenDays", 2, SaveSection, true, SECTION_PARENT_NONE);
}

static RegisterShipInitFunc initOnce(RegisterSevenDaysOnce);
static RegisterShipInitFunc initFunc(RegisterSevenDaysM4, { CVAR_SEVEN_DAYS("Enabled"), CVAR_SEVEN_DAYS("Crafting"),
                                                            CVAR_SEVEN_DAYS("TunicColors"), CVAR_SEVEN_DAYS("Base"),
                                                            CVAR_SEVEN_DAYS("Raids"), CVAR_SEVEN_DAYS("Loot"),
                                                            CVAR_SEVEN_DAYS("Nights") });

} // namespace SevenDays
