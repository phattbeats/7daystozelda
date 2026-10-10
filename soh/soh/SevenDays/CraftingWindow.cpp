#include "SevenDays.h"
#include "SevenDaysNet.h"
#include "soh/OTRGlobals.h"
#include "soh/ShipUtils.h"
#include "soh/SohGui/SohGui.hpp"
#include "soh/Enhancements/cosmetics/cosmeticsTypes.h"
#include "soh/Network/Anchor/Anchor.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include <algorithm>
#include <functional>
#include <iterator>

extern "C" {
#include "z64.h"
#include "macros.h"
#include "variables.h"
#include "functions.h"
#include "textures/icon_item_static/icon_item_static.h"
#include "textures/parameter_static/parameter_static.h"
extern PlayState* gPlayState;
void KaleidoScope_MoveCursorToSpecialPos(PlayState* play, u16 specialPos);
uint8_t ResourceMgr_FileExists(const char* resName);
void FrameInterpolation_RecordOpenChild(const void* a, int b);
void FrameInterpolation_RecordCloseChild(void);
}
#include "SevenDaysKaleido.h"

namespace SohGui {
extern std::shared_ptr<SevenDaysCraftingWindow> mSevenDaysCraftingWindow;
}

/**
 * The Workbench lives on the pause menu as a fifth page (Craft, Trade and Base
 * tabs, the game's cursor, stick or D-pad and A), next to Equipment and Select
 * Item. The Tab key and a placed workbench open the pause menu on it. The
 * ImGui window below stays reachable from the Anchor menu.
 */

static constexpr float kButtonHeight = 48.0f;

namespace SevenDays {

void ToggleCraftingWindow() {
    if (SohGui::mSevenDaysCraftingWindow) {
        SohGui::mSevenDaysCraftingWindow->ToggleVisibility();
    }
}

static int sRequestedTab = -1;
static int sRequestedPageTab = -1;

// Opens the pause menu on the Workbench page (tab: 0 Craft, 1 Trade, 2 Base, 3 Buy at a merchant).
void OpenCraftingWindow(int tab) {
    sRequestedPageTab = tab;
    KaleidoSetup_RequestOpen(PAUSE_SEVENDAYS);
}

int TakeRequestedCraftingTab() {
    int tab = sRequestedTab;
    sRequestedTab = -1;
    return tab;
}

static bool InGameplay() {
    return gPlayState != nullptr && GET_PLAYER(gPlayState) != nullptr && gSaveContext.fileNum >= 0 &&
           gSaveContext.fileNum <= 2 && gSaveContext.gameMode == GAMEMODE_NORMAL;
}

static std::string Inputs(const Recipe& recipe) {
    std::string out;
    for (uint8_t i = 0; i < recipe.inputCount; i++) {
        if (!out.empty()) {
            out += ", ";
        }
        out += fmt::format("{} {}", recipe.inputs[i].amount, GetMaterialInfo(recipe.inputs[i].material).name);
    }
    return out;
}

static void DrawRecipe(const Recipe& recipe) {
    std::string blocker = CraftBlocker(recipe);
    bool locked = !IsUnlocked(recipe.unlock);
    std::string label;
    switch (recipe.kind) {
        case RECIPE_CONSUMABLE:
            label = fmt::format("{} x{}", recipe.name, recipe.outputCount);
            break;
        case RECIPE_KIT:
            label = fmt::format("{} kit", recipe.name);
            break;
        case RECIPE_TRADE:
            label = fmt::format("{} -> {} rupees", recipe.name, recipe.outputCount);
            break;
        case RECIPE_BUY:
            label = fmt::format("Buy {} {} for {} rupees", recipe.inputs[0].amount, recipe.name, recipe.outputCount);
            break;
    }

    ImGui::PushID(recipe.id);
    ImGui::BeginDisabled(!blocker.empty());
    if (ImGui::Button(label.c_str(), ImVec2(ImGui::GetFontSize() * 14.0f, kButtonHeight))) {
        RequestCraft(recipe.id);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::TextUnformatted(Inputs(recipe).c_str());
    if (!blocker.empty()) {
        ImGui::TextColored(locked ? ImVec4(1.0f, 0.6f, 0.3f, 1.0f) : ImVec4(1, 1, 1, 0.5f), "%s", blocker.c_str());
    }
    ImGui::EndGroup();
    ImGui::PopID();
}

static void DrawRecipes(RecipeKind a, RecipeKind b) {
    for (auto& recipe : GetRecipes()) {
        if (recipe.kind == a || recipe.kind == b) {
            DrawRecipe(recipe);
        }
    }
}

// Raid interval: the host picks it in the web lobby, changeable later from the Base tab.
struct IntervalChoice {
    uint32_t days;
    const char* label;
};
static const IntervalChoice kIntervals[] = {
    { 1, "Every night" },        { 2, "Every 2 days" }, { 3, "Every 3 days (recommended)" },
    { 5, "Every 5 days" },       { 7, "Every 7 days" },
};

static std::string IntervalName(uint32_t days) {
    return days == 1 ? std::string("every night") : fmt::format("every {} days", days);
}

// The web lobby's "Days between raids" pick, or 0 when there is none.
static uint32_t LobbyRaidInterval() {
#ifdef __EMSCRIPTEN__
    int days = EM_ASM_INT({ return parseInt(window._raidInterval || "0", 10) || 0; });
    for (auto& c : kIntervals) {
        if ((int)c.days == days) {
            return c.days;
        }
    }
#endif
    return 0;
}

// #3856: no in-game popup (a controller couldn't reach it). Once per session, after
// the opening, a new save takes the lobby pick (or the RaidInterval setting), and the
// room's host (or a solo player) also applies a changed lobby pick to an existing save.
static void ApplyLobbyRaidInterval() {
    static bool applied = false;
    if (applied || !RaidsEnabled() || !InGameplay() || !Net::IsOwner()) {
        return;
    }
    if (gPlayState->pauseCtx.state != 0 || gPlayState->csCtx.state != CS_STATE_IDLE ||
        gPlayState->msgCtx.msgMode != MSGMODE_NONE || gPlayState->transitionTrigger != TRANS_TRIGGER_OFF) {
        return;
    }
    applied = true;
    uint32_t lobby = LobbyRaidInterval();
    bool host = !Net::Connected() || Anchor::Instance->roomState.ownerClientId == Net::OwnId();
    if (GetBase().raidInterval == 0) {
        RequestRaidInterval(lobby != 0 ? lobby : RaidInterval());
    } else if (lobby != 0 && host && lobby != GetBase().raidInterval) {
        RequestRaidInterval(lobby);
    }
}

static void DrawRaidIntervalRow() {
    if (!RaidsEnabled()) {
        return;
    }
    uint32_t days = RaidInterval();
    if (!Net::IsOwner()) {
        ImGui::TextColored(ImVec4(1, 1, 1, 0.6f), "Raids come %s (the host decides).", IntervalName(days).c_str());
        return;
    }
    ImGui::TextUnformatted("Raids come");
    for (auto& c : kIntervals) {
        ImGui::SameLine();
        ImGui::PushID((int)c.days);
        bool current = c.days == days;
        if (current) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        }
        std::string label = c.days == 1 ? std::string("1 day") : fmt::format("{} days", c.days);
        if (ImGui::Button(label.c_str()) && !current) {
            RequestRaidInterval(c.days);
        }
        if (current) {
            ImGui::PopStyleColor();
        }
        ImGui::PopID();
    }
}

// #3935: Ore and Iron stay off the materials line until their tier opens (or the
// pool has some), so the line still fits the page.
static bool MaterialShown(uint8_t m) {
    if (m < MAT_LEGACY_COUNT || GetPool().materials[m] > 0) {
        return true;
    }
    return IsUnlocked(m == MAT_ORE ? UNLOCK_HAMMER : UNLOCK_SILVER_GAUNTLETS);
}

// M5: kits in the pool become pieces of the base.
static void DrawBaseTab() {
    const PoolState& pool = GetPool();
    ImGui::TextWrapped("%s", BaseCountsLine().c_str());
    DrawRaidIntervalRow();
    ImGui::Spacing();
    bool any = false;
    for (int t = 0; t < PLACEABLE_COUNT; t++) {
        const PlaceableInfo& info = GetPlaceableInfo((uint8_t)t);
        if (info.kit[0] == '\0') {
            continue;
        }
        auto it = pool.kits.find(info.kit);
        uint32_t count = it != pool.kits.end() ? it->second : 0;
        any = any || count > 0;
        ImGui::PushID(t);
        ImGui::BeginDisabled(count == 0 || InPlacement());
        std::string label = fmt::format("Place {} ({})", info.name, count);
        if (ImGui::Button(label.c_str(), ImVec2(ImGui::GetFontSize() * 14.0f, kButtonHeight))) {
            BeginPlacement((uint8_t)t);
            if (SohGui::mSevenDaysCraftingWindow) {
                SohGui::mSevenDaysCraftingWindow->Hide();
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1, 1, 1, 0.6f), info.maxHp > 0 ? "%u HP" : " ", info.maxHp);
        ImGui::PopID();
    }
    if (!any) {
        ImGui::TextColored(ImVec4(1, 1, 1, 0.6f), "No kits yet: craft them on the Craft tab.");
    }
    ImGui::TextColored(ImVec4(1, 1, 1, 0.6f), "Placing: C-Left/C-Right rotate, A place, B cancel.");
    ImGui::Separator();

    uint16_t nearest = NearestPlaceable(150.0f);
    const Placeable* p = nearest ? FindPlaceable(nearest) : nullptr;
    ImGui::BeginDisabled(p == nullptr || p->type == PLACEABLE_SIGN);
    std::string pack = p ? fmt::format("Pack up the nearby {}", GetPlaceableInfo(p->type).name)
                         : std::string("Pack up (stand next to a piece)");
    if (ImGui::Button(pack.c_str(), ImVec2(ImGui::GetFontSize() * 14.0f, kButtonHeight))) {
        RequestPackUp(nearest);
    }
    ImGui::EndDisabled();
    // #3935: repairs, for materials (a damaged piece packs up into only part of its kit).
    std::string cost = p != nullptr ? RepairCost(nearest) : "";
    ImGui::BeginDisabled(cost.empty());
    std::string repair = cost.empty() ? std::string("Repair (stand next to a damaged piece)")
                                      : fmt::format("Repair the {} ({})", GetPlaceableInfo(p->type).name, cost);
    if (ImGui::Button(repair.c_str(), ImVec2(ImGui::GetFontSize() * 14.0f, kButtonHeight))) {
        RequestRepair(nearest);
    }
    ImGui::EndDisabled();
    if (p != nullptr && GetPlaceableInfo(p->type).maxHp > 0 && !IsRuin(*p)) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1, 1, 1, 0.6f), "%u/%u HP", p->hp, GetPlaceableInfo(p->type).maxHp);
    }
    int upTo = p != nullptr ? UpgradeTarget(nearest) : -1;
    if (upTo >= 0) {
        std::string up = fmt::format("Upgrade to {} ({})", GetPlaceableInfo((uint8_t)upTo).name, UpgradeCost(nearest));
        if (ImGui::Button(up.c_str(), ImVec2(ImGui::GetFontSize() * 14.0f, kButtonHeight))) {
            RequestUpgrade(nearest);
        }
    }
    ImGui::SameLine();
    static bool confirm = false;
    if (!confirm) {
        if (ImGui::Button("Pack up the whole base", ImVec2(0, kButtonHeight))) {
            confirm = true;
        }
    } else {
        if (ImGui::Button("Really? Every kit comes back", ImVec2(0, kButtonHeight))) {
            confirm = false;
            RequestPackUpBase(gSaveContext.linkAge == 0 ? ERA_ADULT : ERA_CHILD);
        }
        ImGui::SameLine();
        if (ImGui::Button("Keep it", ImVec2(0, kButtonHeight))) {
            confirm = false;
        }
    }
}

} // namespace SevenDays

using namespace SevenDays;

void SevenDaysCraftingWindow::Draw() {
    ApplyLobbyRaidInterval();
    if ((!CraftingEnabled() && !BaseEnabled()) || !InGameplay()) {
        return;
    }

    bool paused = gPlayState->pauseCtx.state != 0 || gPlayState->pauseCtx.debugState != 0;
    ImGuiIO& io = ImGui::GetIO();
    if (!io.WantTextInput && !paused && ImGui::IsKeyPressed(ImGuiKey_Tab, false)) {
        Hide();
        OpenCraftingWindow(-1);
    }

    if (!IsVisible()) {
        return;
    }

    auto vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowViewport(vp->ID);
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(320, 200), ImVec2(vp->WorkSize.x * 0.95f, vp->WorkSize.y * 0.9f));
    bool open = true;
    if (ImGui::Begin("Workbench", &open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking)) {
        DrawElement();
    }
    ImGui::End();
    if (!open) {
        Hide();
    }
}

void SevenDaysCraftingWindow::DrawElement() {
    const PoolState& pool = GetPool();

    for (uint8_t m = 0; m < MAT_COUNT; m++) {
        if (!MaterialShown(m)) {
            continue;
        }
        if (m > 0) {
            ImGui::SameLine(0, 18.0f);
        }
        ImGui::Text("%s %u", GetMaterialInfo(m).name, pool.materials[m]);
    }
    std::string kits;
    for (auto& [id, count] : pool.kits) {
        const Recipe* recipe = FindRecipe(id);
        if (count > 0 && recipe != nullptr) {
            kits += fmt::format("{}{} x{}", kits.empty() ? "" : ", ", recipe->name, count);
        }
    }
    if (!kits.empty()) {
        ImGui::TextColored(ImVec4(1, 1, 1, 0.6f), "Kits: %s", kits.c_str());
    }
    ImGui::Separator();

    int tab = TakeRequestedCraftingTab();
    auto flags = [tab](int i) { return tab == i ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None; };
    if (ImGui::BeginTabBar("SevenDaysTabs")) {
        if (CraftingEnabled()) {
            if (ImGui::BeginTabItem("Craft", nullptr, flags(0))) {
                DrawRecipes(RECIPE_CONSUMABLE, RECIPE_KIT);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Trade", nullptr, flags(1))) {
                DrawRecipes(RECIPE_TRADE, RECIPE_TRADE);
                ImGui::EndTabItem();
            }
        }
        if (BaseEnabled() && ImGui::BeginTabItem("Base", nullptr, flags(2))) {
            DrawBaseTab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    if (ImGui::Button("Close", ImVec2(-FLT_MIN, kButtonHeight))) {
        Hide();
    }
}

// MARK: - The Workbench page on the pause menu

namespace SevenDays {
namespace {

// OPEN_DISPS declares these at block scope; give them C linkage here.
extern "C" {
void FrameInterpolation_RecordOpenChild(const void* a, int b);
void FrameInterpolation_RecordCloseChild(void);
}

enum PageTab : int { PAGE_CRAFT, PAGE_TRADE, PAGE_BASE, PAGE_BUY };

struct PageRow {
    std::string name;
    std::string right; // cost, or how many kits are in the pool
    std::string hint;  // the bottom panel: what A does, or why it can't
    const char* icon = nullptr;
    bool rupee = false; // icon is the HUD rupee (IA8 16x16) instead of a 32x32 item icon
    bool enabled = false;
    int bindType = -1; // #4071: a kit row: a C direction (or D-pad) press binds it to that D-pad direction
    std::function<void()> action;
};

struct PageState {
    int tab = PAGE_CRAFT; // a PageTab
    int row = -1;         // -1: the tab strip
    int top = 0;          // first visible row
    bool confirmPackAll = false;
};

PageState sPage;

constexpr int kVisibleRows = 5;
constexpr s16 kRowTop = 18;
constexpr s16 kRowHeight = 18;

Color_RGB8 kPageDark = { 58, 36, 16 };
Color_RGB8 kPageLight = { 128, 88, 44 };

// #3969: each kit's icon is its own piece, rendered in-game from the model it is built
// with (tools/harness/pha3969) and packed into soh.o2r under objects/7dtz_icons. Without
// the pack the rows keep the vanilla item icons below.
#define ICON(name) const ALIGN_ASSET(2) char gIcon_##name[] = "__OTR__objects/7dtz_icons/" #name
ICON(workbench);
ICON(barricade);
ICON(spikes);
ICON(chest);
ICON(scarecrow);
ICON(guardbaba);
ICON(torch);
ICON(stonewall);
ICON(bombtrap);
ICON(gate);
ICON(ironwall);
ICON(palisade);
ICON(floorplank);
ICON(floorranch);
ICON(floorstone);
ICON(deck);
ICON(step);
ICON(ladder);
ICON(stairs);
ICON(doorswamp);
ICON(doormusic);
ICON(doorpirate);
ICON(packup);
ICON(packall);
#undef ICON

bool IconPackLoaded() {
    static int8_t sLoaded = -1;
    if (sLoaded < 0) {
        sLoaded = ResourceMgr_FileExists(gIcon_workbench) && ResourceMgr_FileExists(gIcon_packall) ? 1 : 0;
    }
    return sLoaded == 1;
}

// The piece's own icon for a kit id, or null.
const char* KitIcon(const std::string& kit) {
    static const std::map<std::string, const char*> sKits = {
        { "workbench", gIcon_workbench },   { "barricade", gIcon_barricade },   { "spikes", gIcon_spikes },
        { "chest", gIcon_chest },           { "scarecrow", gIcon_scarecrow },   { "guardbaba", gIcon_guardbaba },
        { "torch", gIcon_torch },           { "stonewall", gIcon_stonewall },   { "bombtrap", gIcon_bombtrap },
        { "gate", gIcon_gate },             { "ironwall", gIcon_ironwall },     { "palisade", gIcon_palisade },
        { "floorplank", gIcon_floorplank }, { "floorranch", gIcon_floorranch }, { "floorstone", gIcon_floorstone },
        { "deck", gIcon_deck },             { "step", gIcon_step },             { "ladder", gIcon_ladder },
        { "stairs", gIcon_stairs },         { "doorswamp", gIcon_doorswamp },   { "doormusic", gIcon_doormusic },
        { "doorpirate", gIcon_doorpirate },
    };
    if (!IconPackLoaded()) {
        return nullptr;
    }
    auto it = sKits.find(kit);
    if (it != sKits.end()) {
        return it->second;
    }
    // #3962: furniture icons came in a later icon pack; each is checked on its own, so an
    // older pack keeps the vanilla icons for them.
    static std::map<std::string, std::string> sFurniture;
    static std::map<std::string, bool> sFound;
    if (FindPlaceableTypeForKit(kit) >= PLACEABLE_CHAIR_INN) {
        auto f = sFound.find(kit);
        if (f == sFound.end()) {
            std::string path = "__OTR__objects/7dtz_icons/" + kit;
            sFurniture[kit] = path;
            f = sFound.emplace(kit, ResourceMgr_FileExists(path.c_str())).first;
        }
        return f->second ? sFurniture[kit].c_str() : nullptr;
    }
    return nullptr;
}

const char* RecipeIcon(const Recipe& recipe) {
    if (const char* own = KitIcon(recipe.id)) {
        return own;
    }
    static const std::map<std::string, const char*> sIcons = {
        { "sticks", gItemIconDekuStickTex },      { "nuts", gItemIconDekuNutTex },
        { "seeds", gItemIconDekuSeedsTex },       { "arrows", gItemIconBowTex },
        { "bombs", gItemIconBombTex },            { "workbench", gItemIconPoachersSawTex },
        { "barricade", gItemIconShieldDekuTex },  { "torch", gItemIconDinsFireTex },
        { "spikes", gItemIconMaskSkullTex },      { "chest", gMapChestIconTex },
        { "stonewall", gItemIconShieldHylianTex }, { "bombtrap", gItemIconBombchuTex },
        { "gate", gItemIconHookshotTex },         { "scarecrow", gItemIconSlingshotTex },
        { "guardbaba", gItemIconDekuNutTex },     { "ironwall", gItemIconSilverGauntletsTex },
        { "palisade", gItemIconHammerTex },
        // #3945
        { "floorplank", gItemIconBootsKokiriTex },  { "floorranch", gItemIconBottleMilkFullTex },
        { "floorstone", gItemIconGoronsBraceletTex }, { "deck", gItemIconMaskKeatonTex },
        { "step", gItemIconBootsHoverTex },          { "ladder", gItemIconLongshotTex },
        { "stairs", gItemIconBootsIronTex },         { "doorswamp", gItemIconMagicBeanTex },
        { "doormusic", gItemIconOcarinaFairyTex },   { "doorpirate", gItemIconMaskGerudoTex },
        // #3962
        { "chairinn", gItemIconMaskBunnyHoodTex },       { "chairmilkbar", gItemIconMaskZoraTex },
        { "bench", gItemIconDekuStickTex },          { "bedinn", gItemIconWeirdEggTex },
        { "bedmayor", gItemIconWeirdEggTex },      { "dresser", gMapChestIconTex },
        { "drawers", gMapChestIconTex },             { "bookshelf", gItemIconZeldasLetterTex },
        { "painting", gItemIconMaskSpookyTex },      { "milkcan", gItemIconBottleMilkFullTex },
        { "rug", gItemIconMaskGoronTex },            { "barrel", gItemIconBombTex },
        { "barrelromani", gItemIconPocketEggTex },   { "wagonwheel", gItemIconBoomerangTex },
        { "stall", gItemIconMaskKeatonTex },
    };
    auto it = sIcons.find(recipe.id);
    return it != sIcons.end() ? it->second : nullptr;
}

std::string CostLine(const Recipe& recipe) {
    std::string out;
    for (uint8_t i = 0; i < recipe.inputCount; i++) {
        out += fmt::format("{}{} {}", out.empty() ? "" : " ", recipe.inputs[i].amount,
                           GetMaterialInfo(recipe.inputs[i].material).name);
    }
    return out;
}

std::vector<int> AvailableTabs() {
    std::vector<int> tabs;
    if (CraftingEnabled()) {
        tabs.push_back(PAGE_CRAFT);
        tabs.push_back(PAGE_TRADE);
    }
    if (BaseEnabled()) {
        tabs.push_back(PAGE_BASE);
    }
    if (CraftingEnabled() && ActiveMerchant() != nullptr) {
        tabs.push_back(PAGE_BUY); // only while Link stands at the stall he talked to
    }
    return tabs;
}

const char* TabName(int tab) {
    switch (tab) {
        case PAGE_CRAFT:
            return "Craft";
        case PAGE_TRADE:
            return "Trade";
        case PAGE_BUY:
            return "Buy";
        default:
            return "Base";
    }
}

const char* TabHint(int tab) {
    switch (tab) {
        case PAGE_CRAFT:
            return "Uses the shared pool";
        case PAGE_TRADE:
            return "Materials for rupees";
        case PAGE_BUY:
            return "Rupees for materials";
        default:
            return "Place and pack up kits";
    }
}

void ClosePauseMenu(PlayState* play) {
    PauseContext* pauseCtx = &play->pauseCtx;
    Interface_SetDoAction(play, DO_ACTION_NONE);
    pauseCtx->state = 0x12;
    WREG(2) = -6240;
    func_800F64E0(0);
}

std::vector<PageRow> BuildRows(PlayState* play, int tab) {
    std::vector<PageRow> rows;
    if (tab == PAGE_BUY) {
        const Merchant* merchant = ActiveMerchant();
        for (int i = 0; merchant != nullptr && i < 3 && merchant->offers[i] != nullptr; i++) {
            const Recipe* recipe = FindRecipe(merchant->offers[i]);
            if (recipe == nullptr || recipe->kind != RECIPE_BUY) {
                continue;
            }
            PageRow row;
            std::string blocker = CraftBlocker(*recipe);
            row.enabled = blocker.empty();
            row.name = fmt::format("Buy {} {}", recipe->inputs[0].amount, recipe->name);
            row.right = fmt::format("{} Rupees", recipe->outputCount);
            row.rupee = true;
            row.hint = row.enabled ? "Buy (into the pool)" : blocker;
            std::string id = recipe->id;
            row.action = [id]() {
                if (RequestCraft(id)) {
                    Sfx_PlaySfxCentered(NA_SE_SY_DECIDE);
                } else {
                    Sfx_PlaySfxCentered(NA_SE_SY_ERROR);
                }
            };
            rows.push_back(std::move(row));
        }
        return rows;
    }
    if (tab == PAGE_CRAFT || tab == PAGE_TRADE) {
        const PoolState& pool = GetPool();
        // #4018: base kits first, then consumables, so the pieces don't need a scroll.
        std::vector<const Recipe*> order;
        for (const Recipe& recipe : GetRecipes()) {
            order.push_back(&recipe);
        }
        std::stable_partition(order.begin(), order.end(), [](const Recipe* r) { return r->kind == RECIPE_KIT; });
        for (const Recipe* rp : order) {
            const Recipe& recipe = *rp;
            bool trade = recipe.kind == RECIPE_TRADE;
            if (recipe.kind == RECIPE_BUY || trade != (tab == PAGE_TRADE)) {
                continue;
            }
            PageRow row;
            std::string blocker = CraftBlocker(recipe);
            row.enabled = blocker.empty();
            row.right = CostLine(recipe);
            if (recipe.kind == RECIPE_CONSUMABLE) {
                row.name = fmt::format("{} x{}", recipe.name, recipe.outputCount);
                row.icon = RecipeIcon(recipe);
                row.hint = row.enabled ? "Craft" : blocker;
            } else if (recipe.kind == RECIPE_KIT) {
                auto it = pool.kits.find(recipe.id);
                uint32_t have = it != pool.kits.end() ? it->second : 0;
                row.name = have > 0 ? fmt::format("{} ({})", recipe.name, have) : std::string(recipe.name);
                row.icon = RecipeIcon(recipe);
                row.hint = row.enabled ? "Build a kit" : blocker;
            } else {
                row.name = fmt::format("Sell {}", CostLine(recipe));
                row.right = fmt::format("{} Rupees", recipe.outputCount);
                row.rupee = true;
                row.hint = row.enabled ? "Sell" : blocker;
            }
            std::string id = recipe.id;
            row.action = [id]() {
                if (RequestCraft(id)) {
                    Sfx_PlaySfxCentered(NA_SE_SY_DECIDE);
                } else {
                    Sfx_PlaySfxCentered(NA_SE_SY_ERROR);
                }
            };
            rows.push_back(std::move(row));
        }
        return rows;
    }

    // Base: place a kit (the pause menu closes for the ghost), how often raids come, or
    // pack pieces up. #4018: the kits come first, the ones in the pool ahead of the rest.
    const PoolState& pool = GetPool();
    std::vector<PageRow> empty;
    for (int t = 0; t < PLACEABLE_COUNT; t++) {
        const PlaceableInfo& info = GetPlaceableInfo((uint8_t)t);
        if (info.kit[0] == '\0') {
            continue;
        }
        auto it = pool.kits.find(info.kit);
        uint32_t count = it != pool.kits.end() ? it->second : 0;
        PageRow row;
        row.name = fmt::format("Place {}", info.name);
        static const char* const kDirTags[DPAD_KIT_DIRS] = { "Up", "Down", "Left", "Right" };
        int bound = DpadKitDirOf(t);
        row.right = bound >= 0 ? fmt::format("x{} [{}]", count, kDirTags[bound]) : fmt::format("x{}", count);
        row.bindType = t;
        const Recipe* recipe = FindRecipe(info.kit);
        row.icon = recipe != nullptr ? RecipeIcon(*recipe) : nullptr;
        row.enabled = count > 0 && !InPlacement();
        row.hint = count == 0 ? "Build a kit on Craft" : (InPlacement() ? "Already placing" : "Place it");
        row.hint += bound >= 0 ? ". C-dir: move or clear" : ". C-dir: bind to D-Pad";
        uint8_t type = (uint8_t)t;
        row.action = [type, play]() {
            Sfx_PlaySfxCentered(NA_SE_SY_DECIDE);
            ClosePauseMenu(play);
            BeginPlacement(type);
        };
        (count > 0 ? rows : empty).push_back(std::move(row));
    }
    std::move(empty.begin(), empty.end(), std::back_inserter(rows));

    // How often raids come (the host picks).
    if (RaidsEnabled()) {
        uint32_t days = RaidInterval();
        bool picked = GetBase().raidInterval != 0;
        PageRow raids;
        raids.name = days == 1 ? std::string("Raids every night") : fmt::format("Raids every {} days", days);
        raids.icon = gItemIconDekuStickTex;
        raids.enabled = Net::IsOwner();
        raids.right = picked ? "" : "pick one";
        raids.hint = raids.enabled ? "Change how often" : "The host decides";
        raids.action = [days]() {
            size_t next = 0;
            for (size_t i = 0; i < ARRAY_COUNT(kIntervals); i++) {
                if (kIntervals[i].days == days) {
                    next = (i + 1) % ARRAY_COUNT(kIntervals);
                }
            }
            Sfx_PlaySfxCentered(NA_SE_SY_DECIDE);
            RequestRaidInterval(kIntervals[next].days);
        };
        rows.push_back(std::move(raids));
    }

    uint16_t nearest = NearestPlaceable(150.0f);
    const Placeable* p = nearest ? FindPlaceable(nearest) : nullptr;
    PageRow pack;
    pack.name = p ? fmt::format("Pack up {}", GetPlaceableInfo(p->type).name) : std::string("Pack up nearby piece");
    pack.icon = IconPackLoaded() ? gIcon_packup : gItemIconHammerTex; // a crate: back into a kit
    pack.enabled = p != nullptr && p->type != PLACEABLE_SIGN;
    pack.hint = pack.enabled ? "Back into a kit" : "Stand next to a piece";
    pack.action = [nearest]() {
        Sfx_PlaySfxCentered(NA_SE_SY_DECIDE);
        RequestPackUp(nearest);
    };
    rows.push_back(std::move(pack));

    std::string cost = p != nullptr ? RepairCost(nearest) : "";
    PageRow repair;
    repair.name = p != nullptr && !cost.empty() ? fmt::format("Repair {}", GetPlaceableInfo(p->type).name)
                                                : std::string("Repair nearby piece");
    repair.right = p != nullptr && !IsRuin(*p) && GetPlaceableInfo(p->type).maxHp > 0
                       ? fmt::format("{}/{}", p->hp, GetPlaceableInfo(p->type).maxHp)
                       : "";
    repair.icon = gItemIconHammerTex;
    repair.enabled = !cost.empty();
    repair.hint = repair.enabled ? cost : "Stand next to a damaged piece";
    repair.action = [nearest]() {
        Sfx_PlaySfxCentered(NA_SE_SY_DECIDE);
        RequestRepair(nearest);
    };
    rows.push_back(std::move(repair));

    // #3935: the Megaton Hammer rebuilds walls in place.
    int upTo = p != nullptr ? UpgradeTarget(nearest) : -1;
    if (upTo >= 0) {
        PageRow up;
        up.name = fmt::format("Upgrade to {}", GetPlaceableInfo((uint8_t)upTo).name);
        const char* target = KitIcon(GetPlaceableInfo((uint8_t)upTo).kit); // what it becomes
        up.icon = target != nullptr ? target : gItemIconHammerTex;
        up.enabled = true;
        up.hint = UpgradeCost(nearest);
        up.action = [nearest]() {
            Sfx_PlaySfxCentered(NA_SE_SY_DECIDE);
            RequestUpgrade(nearest);
        };
        rows.push_back(std::move(up));
    }

    PageRow all;
    all.name = sPage.confirmPackAll ? "Really pack it all?" : "Pack up the whole base";
    all.icon = IconPackLoaded() ? gIcon_packall : gItemIconHammerTex; // a big crate: everything back
    // Seeded pieces with no kit (the "Day 1" sign) stay when the base is packed up.
    all.enabled = std::any_of(GetBase().placeables.begin(), GetBase().placeables.end(),
                              [](const Placeable& q) { return GetPlaceableInfo(q.type).kit[0] != '\0'; });
    all.hint = sPage.confirmPackAll ? "Again: every kit comes back" : BaseCountsLine();
    all.action = []() {
        if (!sPage.confirmPackAll) {
            sPage.confirmPackAll = true;
            Sfx_PlaySfxCentered(NA_SE_SY_DECIDE);
            return;
        }
        sPage.confirmPackAll = false;
        Sfx_PlaySfxCentered(NA_SE_SY_DECIDE);
        RequestPackUpBase(gSaveContext.linkAge == 0 ? ERA_ADULT : ERA_CHILD);
    };
    rows.push_back(std::move(all));
    return rows;
}

// Text in the game's font, laid out in the current matrix: y up, (x, top) the top-left.
float TextWidth(const std::string& text, float scale) {
    float width = 0.0f;
    for (char c : text) {
        width += Ship_GetCharFontWidth((u8)c) * scale;
    }
    return width;
}

// #4029: every glyph is its own texture, and the renderer ends a draw call whenever the
// texture changes, so drawing text letter by letter cost a draw call per letter (over a
// thousand a frame with the shadows and the page on the side faces): choppy on phones.
// DrawText queues the glyphs; FlushText draws them grouped by letter, one texture load per
// letter, the shadows under the text. Prim colour goes into the vertices: no extra calls.
struct QueuedGlyph {
    u8 c;
    u8 layer; // 0 shadow, 1 text
    s16 x0, x1, y0, y1, tw;
    Color_RGB8 color;
    u8 alpha;
};

std::vector<QueuedGlyph> sGlyphs;

void DrawText(PlayState* play, const std::string& text, float x, float top, float scale, Color_RGB8 color, u8 alpha,
              u8 layer = 1) {
    float cx = x;
    for (char ch : text) {
        float advance = Ship_GetCharFontWidth((u8)ch);
        QueuedGlyph g;
        g.c = (u8)ch;
        g.layer = layer;
        g.x0 = (s16)cx;
        g.x1 = (s16)(cx + advance * scale + 0.5f);
        g.y0 = (s16)top;
        g.y1 = (s16)(top - FONT_CHAR_TEX_HEIGHT * scale);
        g.tw = (s16)(advance * 32.0f);
        g.color = color;
        g.alpha = alpha;
        cx += advance * scale;
        if (ch != ' ') {
            sGlyphs.push_back(g);
        }
    }
}

void DrawShadowText(PlayState* play, const std::string& text, float x, float top, float scale, Color_RGB8 color,
                    u8 alpha) {
    DrawText(play, text, x + 1.0f, top - 1.0f, scale, { 0, 0, 0 }, alpha, 0);
    DrawText(play, text, x, top, scale, color, alpha, 1);
}

void FlushText(PlayState* play) {
    if (sGlyphs.empty()) {
        return;
    }
    std::stable_sort(sGlyphs.begin(), sGlyphs.end(), [](const QueuedGlyph& a, const QueuedGlyph& b) {
        return a.layer != b.layer ? a.layer < b.layer : a.c < b.c;
    });
    OPEN_DISPS(play->state.gfxCtx);
    Vtx* vtx = (Vtx*)Graph_Alloc(play->state.gfxCtx, sGlyphs.size() * 4 * sizeof(Vtx));
    gDPPipeSync(POLY_OPA_DISP++);
    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
    const s16 th = FONT_CHAR_TEX_HEIGHT << 5;
    for (size_t i = 0; i < sGlyphs.size(); i++) {
        const QueuedGlyph& g = sGlyphs[i];
        if (i == 0 || g.c != sGlyphs[i - 1].c || g.layer != sGlyphs[i - 1].layer) {
            gDPLoadTextureBlock_4b(POLY_OPA_DISP++, Ship_GetCharFontTexture(g.c), G_IM_FMT_I, FONT_CHAR_TEX_WIDTH,
                                   FONT_CHAR_TEX_HEIGHT, 0, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP,
                                   G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
        }
        Vtx* v = &vtx[i * 4];
        v[0] = { { { g.x0, g.y0, 0 }, 0, { 0, 0 }, { 255, 255, 255, 255 } } };
        v[1] = { { { g.x1, g.y0, 0 }, 0, { g.tw, 0 }, { 255, 255, 255, 255 } } };
        v[2] = { { { g.x0, g.y1, 0 }, 0, { 0, th }, { 255, 255, 255, 255 } } };
        v[3] = { { { g.x1, g.y1, 0 }, 0, { g.tw, th }, { 255, 255, 255, 255 } } };
        gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, g.color.r, g.color.g, g.color.b, g.alpha);
        gSPVertex(POLY_OPA_DISP++, (uintptr_t)v, 4, 0);
        gSP1Quadrangle(POLY_OPA_DISP++, 0, 2, 3, 1, 0);
    }
    CLOSE_DISPS(play->state.gfxCtx);
    sGlyphs.clear();
}

void DrawIcon(PlayState* play, const char* icon, bool rupee, s16 x, s16 top, bool gray, u8 alpha) {
    if (icon == nullptr && !rupee) {
        return;
    }
    OPEN_DISPS(play->state.gfxCtx);
    Vtx* v = (Vtx*)Graph_Alloc(play->state.gfxCtx, 4 * sizeof(Vtx));
    // The storage chest uses the dungeon map's chest mark (RGBA16 8x8), blown up.
    bool chest = icon == gMapChestIconTex;
    s16 size = rupee ? 16 : (chest ? 8 : 32);
    s16 t = size << 5;
    v[0] = { { { x, top, 0 }, 0, { 0, 0 }, { 255, 255, 255, 255 } } };
    v[1] = { { { (s16)(x + 16), top, 0 }, 0, { t, 0 }, { 255, 255, 255, 255 } } };
    v[2] = { { { x, (s16)(top - 16), 0 }, 0, { 0, t }, { 255, 255, 255, 255 } } };
    v[3] = { { { (s16)(x + 16), (s16)(top - 16), 0 }, 0, { t, t }, { 255, 255, 255, 255 } } };
    gDPPipeSync(POLY_OPA_DISP++);
    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
    if (gray) {
        // The rendered piece icons are darker than the item icons: grey them out lighter.
        u8 level = icon != nullptr && strncmp(icon, "__OTR__objects/7dtz_icons/", 26) == 0 ? 190 : 109;
        gDPSetGrayscaleColor(POLY_OPA_DISP++, level, level, level, 255);
        gSPGrayscale(POLY_OPA_DISP++, true);
    }
    if (rupee) {
        gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 200, 255, 100, alpha);
        gDPLoadTextureBlock(POLY_OPA_DISP++, gRupeeCounterIconTex, G_IM_FMT_IA, G_IM_SIZ_8b, 16, 16, 0,
                            G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK,
                            G_TX_NOLOD, G_TX_NOLOD);
    } else if (chest) {
        gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, alpha);
        gDPLoadTextureBlock(POLY_OPA_DISP++, icon, G_IM_FMT_RGBA, G_IM_SIZ_16b, 8, 8, 0, G_TX_NOMIRROR | G_TX_CLAMP,
                            G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
    } else {
        gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, alpha);
        gDPLoadTextureBlock(POLY_OPA_DISP++, icon, G_IM_FMT_RGBA, G_IM_SIZ_32b, 32, 32, 0,
                            G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK,
                            G_TX_NOLOD, G_TX_NOLOD);
    }
    gSPVertex(POLY_OPA_DISP++, (uintptr_t)v, 4, 0);
    gSP1Quadrangle(POLY_OPA_DISP++, 0, 2, 3, 1, 0);
    if (gray) {
        gSPGrayscale(POLY_OPA_DISP++, false);
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

// The game's four-corner cursor around a box (left, top, width, height), page coordinates.
void SetCursorBox(PauseContext* pauseCtx, s16 left, s16 top, s16 width, s16 height) {
    Vtx* c = pauseCtx->cursorVtx;
    s16 right = left + width - 16;
    s16 bottom = top - height + 16;
    s16 xs[4] = { left, right, left, right };
    s16 ys[4] = { top, top, bottom, bottom };
    for (int corner = 0; corner < 4; corner++) {
        Vtx* q = &c[corner * 4];
        q[0].v.ob[0] = q[2].v.ob[0] = xs[corner];
        q[1].v.ob[0] = q[3].v.ob[0] = xs[corner] + 16;
        q[0].v.ob[1] = q[1].v.ob[1] = ys[corner];
        q[2].v.ob[1] = q[3].v.ob[1] = ys[corner] - 16;
    }
}

float TabCenter(size_t index, size_t count) {
    float span = 200.0f;
    return -span / 2 + span * (index + 0.5f) / count;
}

int TabIndex(const std::vector<int>& tabs) {
    for (size_t i = 0; i < tabs.size(); i++) {
        if (tabs[i] == sPage.tab) {
            return (int)i;
        }
    }
    return 0;
}

// -1, or the D-pad direction (0 up, 1 down, 2 left, 3 right) a bind press names.
int BindPressed(Input* input, bool dpadMovesCursor) {
    static const u16 kC[DPAD_KIT_DIRS] = { BTN_CUP, BTN_CDOWN, BTN_CLEFT, BTN_CRIGHT };
    static const u16 kD[DPAD_KIT_DIRS] = { BTN_DUP, BTN_DDOWN, BTN_DLEFT, BTN_DRIGHT };
    for (int d = 0; d < DPAD_KIT_DIRS; d++) {
        if (CHECK_BTN_ALL(input->press.button, kC[d]) ||
            (!dpadMovesCursor && CHECK_BTN_ALL(input->press.button, kD[d]))) {
            return d;
        }
    }
    return -1;
}

void HandleInput(PlayState* play, const std::vector<int>& tabs, std::vector<PageRow>& rows) {
    PauseContext* pauseCtx = &play->pauseCtx;
    Input* input = &play->state.input[0];
    bool dpad = CVarGetInteger(CVAR_SETTING("DPadOnPause"), 0);
    bool up = pauseCtx->stickRelY > 30 || (dpad && CHECK_BTN_ALL(input->press.button, BTN_DUP));
    bool down = pauseCtx->stickRelY < -30 || (dpad && CHECK_BTN_ALL(input->press.button, BTN_DDOWN));
    bool left = pauseCtx->stickRelX < -30 || (dpad && CHECK_BTN_ALL(input->press.button, BTN_DLEFT));
    bool right = pauseCtx->stickRelX > 30 || (dpad && CHECK_BTN_ALL(input->press.button, BTN_DRIGHT));

    if (pauseCtx->cursorSpecialPos == PAUSE_CURSOR_PAGE_LEFT) {
        if (right) {
            pauseCtx->cursorSpecialPos = 0;
            Sfx_PlaySfxCentered(NA_SE_SY_CURSOR);
        }
        return;
    }
    if (pauseCtx->cursorSpecialPos == PAUSE_CURSOR_PAGE_RIGHT) {
        if (left) {
            pauseCtx->cursorSpecialPos = 0;
            Sfx_PlaySfxCentered(NA_SE_SY_CURSOR);
        }
        return;
    }
    if (pauseCtx->cursorSpecialPos != 0) {
        return;
    }

    int before = sPage.row;
    int tabIndex = TabIndex(tabs);
    if (sPage.row < 0) {
        if (left) {
            if (tabIndex > 0) {
                sPage.tab = tabs[tabIndex - 1];
                sPage.top = 0;
                Sfx_PlaySfxCentered(NA_SE_SY_CURSOR);
            } else {
                KaleidoScope_MoveCursorToSpecialPos(play, PAUSE_CURSOR_PAGE_LEFT);
            }
        } else if (right) {
            if (tabIndex + 1 < (int)tabs.size()) {
                sPage.tab = tabs[tabIndex + 1];
                sPage.top = 0;
                Sfx_PlaySfxCentered(NA_SE_SY_CURSOR);
            } else {
                KaleidoScope_MoveCursorToSpecialPos(play, PAUSE_CURSOR_PAGE_RIGHT);
            }
        } else if (down && !rows.empty()) {
            sPage.row = 0;
        }
    } else {
        if (up) {
            sPage.row--;
        } else if (down && sPage.row + 1 < (int)rows.size()) {
            sPage.row++;
        } else if (left) {
            KaleidoScope_MoveCursorToSpecialPos(play, PAUSE_CURSOR_PAGE_LEFT);
        } else if (right) {
            KaleidoScope_MoveCursorToSpecialPos(play, PAUSE_CURSOR_PAGE_RIGHT);
        } else if (rows[sPage.row].bindType >= 0 && BindPressed(input, dpad) >= 0) {
            // #4071: C-Up/Down/Left/Right (or the D-pad itself, when it isn't moving the cursor)
            // binds the kit to that D-pad direction; the same direction again clears it.
            int dir = BindPressed(input, dpad);
            int type = rows[sPage.row].bindType;
            if (DpadKitType(dir) == type) {
                BindDpadKit(dir, -1);
                Sfx_PlaySfxCentered(NA_SE_SY_CANCEL);
            } else {
                BindDpadKit(dir, type);
                Sfx_PlaySfxCentered(NA_SE_SY_DECIDE);
            }
        } else if (CHECK_BTN_ALL(input->press.button, BTN_A)) {
            PageRow& row = rows[sPage.row];
            if (row.enabled && row.action) {
                row.action();
                return;
            }
            Sfx_PlaySfxCentered(NA_SE_SY_ERROR);
        }
    }
    if (sPage.row != before) {
        sPage.confirmPackAll = false;
        Sfx_PlaySfxCentered(NA_SE_SY_CURSOR);
    }
}

} // namespace
} // namespace SevenDays

extern "C" s32 SevenDaysKaleido_PageOn(void) {
    return SevenDays::Enabled() && (SevenDays::CraftingEnabled() || SevenDays::BaseEnabled());
}

// The page's frame: the blank save frame in workbench browns.
extern "C" void SevenDaysKaleido_InitPageVtx(PlayState* play, Vtx* vtx) {
    using namespace SevenDays;
    Color_RGB8 colors[4] = { kPageDark, kPageLight, kPageLight, kPageDark };
    for (int i = 0; i < 60; i++) {
        int column = i / 20;
        bool rightEdge = (i % 4) == 1 || (i % 4) == 3;
        Color_RGB8 c = colors[column + (rightEdge ? 1 : 0)];
        vtx[i].v.cn[0] = c.r;
        vtx[i].v.cn[1] = c.g;
        vtx[i].v.cn[2] = c.b;
    }
}

extern "C" void SevenDaysKaleido_DrawPage(PlayState* play, s32 current) {
    using namespace SevenDays;
    PauseContext* pauseCtx = &play->pauseCtx;
    u8 alpha = (u8)pauseCtx->alpha;
    s16 dy = pauseCtx->offsetY;

    std::vector<int> tabs = AvailableTabs();
    if (tabs.empty()) {
        return;
    }
    if (sRequestedPageTab >= 0 && current) {
        int want = sRequestedPageTab == 3   ? PAGE_BUY
                   : sRequestedPageTab == 2 ? PAGE_BASE
                                            : (sRequestedPageTab == 1 ? PAGE_TRADE : PAGE_CRAFT);
        sRequestedPageTab = -1;
        if (std::find(tabs.begin(), tabs.end(), want) != tabs.end()) {
            sPage.tab = want;
            sPage.row = -1;
            sPage.top = 0;
        }
    }
    if (std::find(tabs.begin(), tabs.end(), sPage.tab) == tabs.end()) {
        sPage.tab = tabs[0];
        sPage.row = -1;
        sPage.top = 0;
    }

    std::vector<PageRow> rows = BuildRows(play, sPage.tab);
    if (current && pauseCtx->state == 6 && pauseCtx->unk_1E4 == 0) {
        HandleInput(play, tabs, rows);
        if (pauseCtx->state != 6) {
            return; // the pause menu is closing for placement
        }
        rows = BuildRows(play, sPage.tab); // the action may have changed counts or the confirm
    }
    if (sPage.row >= (int)rows.size()) {
        sPage.row = (int)rows.size() - 1;
    }
    if (sPage.row >= 0) {
        sPage.top = std::clamp(sPage.top, sPage.row - kVisibleRows + 1, sPage.row);
    }
    sPage.top = std::max(0, std::min(sPage.top, std::max(0, (int)rows.size() - kVisibleRows)));

    Gfx_SetupDL_42Opa(play->state.gfxCtx);

    // Title, tabs and the shared pool.
    const Merchant* merchant = sPage.tab == PAGE_BUY ? ActiveMerchant() : nullptr;
    std::string title = merchant != nullptr ? merchant->name : "Workbench";
    DrawShadowText(play, title, -TextWidth(title, 1.0f) / 2, 76 + dy, 1.0f, { 255, 255, 255 }, alpha);

    int tabIndex = TabIndex(tabs);
    for (size_t i = 0; i < tabs.size(); i++) {
        std::string name = TabName(tabs[i]);
        float w = TextWidth(name, 0.85f);
        Color_RGB8 color = (int)i == tabIndex ? Color_RGB8{ 255, 255, 0 } : Color_RGB8{ 150, 150, 150 };
        DrawShadowText(play, name, TabCenter(i, tabs.size()) - w / 2, 56 + dy, 0.85f, color, alpha);
    }

    const PoolState& pool = GetPool();
    std::string mats;
    for (uint8_t m = 0; m < MAT_COUNT; m++) {
        if (MaterialShown(m)) {
            mats += fmt::format("{}{} {}", m ? "  " : "", GetMaterialInfo(m).name, pool.materials[m]);
        }
    }
    if (merchant != nullptr) {
        mats += fmt::format("  Rupees {}", gSaveContext.rupees + gSaveContext.rupeeAccumulator);
    }
    DrawShadowText(play, mats, -TextWidth(mats, 0.6f) / 2, 38 + dy, 0.6f, { 220, 220, 200 }, alpha);

    // The list.
    for (int i = sPage.top; i < (int)rows.size() && i < sPage.top + kVisibleRows; i++) {
        const PageRow& row = rows[i];
        s16 top = kRowTop - (i - sPage.top) * kRowHeight + dy;
        DrawIcon(play, row.icon, row.rupee, -106, top - 1, !row.enabled, alpha);
        Color_RGB8 nameColor = row.enabled ? Color_RGB8{ 255, 255, 255 } : Color_RGB8{ 130, 130, 130 };
        float rw = TextWidth(row.right, 0.6f);
        // A long name shrinks to clear the right-hand column instead of running into it.
        float room = 108.0f - rw - 6.0f - -86.0f;
        float nameW = TextWidth(row.name, 1.0f);
        float nameScale = nameW > 0.0f ? std::clamp(room / nameW, 0.55f, 0.75f) : 0.75f;
        DrawShadowText(play, row.name, -86, top - 2 - (0.75f - nameScale) * FONT_CHAR_TEX_HEIGHT / 2, nameScale,
                       nameColor, alpha);
        Color_RGB8 rightColor = row.enabled ? Color_RGB8{ 230, 210, 150 } : Color_RGB8{ 130, 120, 100 };
        DrawShadowText(play, row.right, 108 - rw, top - 4, 0.6f, rightColor, alpha);
    }
    if (sPage.top > 0) {
        DrawText(play, "^", 112, kRowTop + 2 + dy, 0.6f, { 255, 255, 255 }, alpha);
    }
    if (sPage.top + kVisibleRows < (int)rows.size()) {
        DrawText(play, "v", 112, kRowTop - (kVisibleRows - 1) * kRowHeight - 6 + dy, 0.6f, { 255, 255, 255 }, alpha);
    }
    FlushText(play);

    if (!current) {
        return;
    }
    // The cursor: around the tab, or along the row. Green (A) when A does something.
    if (sPage.row < 0) {
        float w = TextWidth(TabName(sPage.tab), 0.85f);
        SetCursorBox(pauseCtx, (s16)(TabCenter(tabIndex, tabs.size()) - w / 2 - 6), 60 + dy, (s16)(w + 12), 20);
        pauseCtx->cursorColorSet = 0;
    } else {
        s16 top = kRowTop - (sPage.row - sPage.top) * kRowHeight + dy;
        SetCursorBox(pauseCtx, -110, top + 1, 224, 20);
        pauseCtx->cursorColorSet = rows[sPage.row].enabled ? 8 : 0;
    }
}

// The bottom panel: what A does on this row, or why it can't.
extern "C" void SevenDaysKaleido_DrawInfo(PlayState* play, s16 top) {
    using namespace SevenDays;
    PauseContext* pauseCtx = &play->pauseCtx;
    if (pauseCtx->state != 6) {
        return;
    }
    std::string line;
    bool canA = false;
    if (sPage.row < 0) {
        line = TabHint(sPage.tab);
    } else {
        std::vector<PageRow> rows = BuildRows(play, sPage.tab);
        if (sPage.row < (int)rows.size()) {
            line = rows[sPage.row].hint;
            canA = rows[sPage.row].enabled;
        }
    }
    // Shrink a long line to fit the panel (about 150 units wide).
    std::string aGlyph = "\x9F";
    float scale = 0.8f;
    float natural = (canA ? TextWidth(aGlyph + " ", 1.0f) : 0.0f) + TextWidth(line, 1.0f);
    if (natural * scale > 150.0f) {
        scale = std::max(0.45f, 150.0f / natural);
    }
    float aw = canA ? TextWidth(aGlyph + " ", scale) : 0.0f;
    float x = -(aw + TextWidth(line, scale)) / 2;
    if (canA) {
        Color_RGB8 aColor = { 80, 150, 255 };
        if (CVarGetInteger(CVAR_COSMETIC("HUD.AButton.Changed"), 0)) {
            aColor = CVarGetColor24(CVAR_COSMETIC("HUD.AButton.Value"), aColor);
        } else if (CVarGetInteger(CVAR_COSMETIC("DefaultColorScheme"), COLORSCHEME_N64) == COLORSCHEME_GAMECUBE) {
            aColor = { 80, 255, 150 };
        }
        DrawText(play, aGlyph, x, top, scale, aColor, 255);
    }
    Color_RGB8 color = canA ? Color_RGB8{ 255, 255, 255 } : Color_RGB8{ 200, 200, 200 };
    DrawText(play, line, x + aw, top, scale, color, 255);
    FlushText(play);
}

extern "C" void SevenDaysKaleido_DrawPageLabel(PlayState* play, s16 top) {
    std::string label = "To Workbench";
    float w = SevenDays::TextWidth(label, 1.0f);
    SevenDays::DrawText(play, label, 1 - w / 2, top, 1.0f, { 255, 200, 0 }, 255);
    SevenDays::FlushText(play);
}

// #4125: a phone tap on the Workbench page, in page space (the units of the tab and row boxes).
// 0 nothing there, 1 the cursor moved (or a tab was tapped), 2 the row was already under the
// cursor: the caller presses A.
extern "C" s32 SevenDaysKaleido_TouchHit(PlayState* play, f32 px, f32 py) {
    using namespace SevenDays;
    PauseContext* pauseCtx = &play->pauseCtx;
    s16 dy = pauseCtx->offsetY;
    std::vector<int> tabs = AvailableTabs();
    if (tabs.empty()) {
        return 0;
    }
    for (size_t i = 0; i < tabs.size(); i++) {
        float w = TextWidth(TabName(tabs[i]), 0.85f);
        float left = TabCenter(i, tabs.size()) - w / 2 - 6;
        float right = left + w + 12;
        float top = 60 + dy;
        if (px >= left && px <= right && py <= top && py >= top - 20) {
            bool same = sPage.tab == tabs[i] && sPage.row < 0 && pauseCtx->cursorSpecialPos == 0;
            pauseCtx->cursorSpecialPos = 0;
            if (!same) {
                sPage.tab = tabs[i];
                sPage.row = -1;
                sPage.top = 0;
                sPage.confirmPackAll = false;
                Sfx_PlaySfxCentered(NA_SE_SY_CURSOR);
            }
            return 1;
        }
    }
    std::vector<PageRow> rows = BuildRows(play, sPage.tab);
    for (int i = sPage.top; i < (int)rows.size() && i < sPage.top + kVisibleRows; i++) {
        s16 top = kRowTop - (i - sPage.top) * kRowHeight + dy;
        float boxTop = top + 1;
        if (px >= -110 && px <= 114 && py <= boxTop && py >= boxTop - 20) {
            if (sPage.row == i && pauseCtx->cursorSpecialPos == 0) {
                return 2;
            }
            pauseCtx->cursorSpecialPos = 0;
            sPage.row = i;
            sPage.confirmPackAll = false;
            Sfx_PlaySfxCentered(NA_SE_SY_CURSOR);
            return 1;
        }
    }
    return 0;
}

// The tap targets in page space: the tabs first, then the visible rows. 0 past the last one.
extern "C" s32 SevenDaysKaleido_TouchRect(PlayState* play, s32 index, f32* x0, f32* y0, f32* x1, f32* y1) {
    using namespace SevenDays;
    PauseContext* pauseCtx = &play->pauseCtx;
    s16 dy = pauseCtx->offsetY;
    std::vector<int> tabs = AvailableTabs();
    if (index < 0 || tabs.empty()) {
        return 0;
    }
    if (index < (int)tabs.size()) {
        float w = TextWidth(TabName(tabs[index]), 0.85f);
        *x0 = TabCenter(index, tabs.size()) - w / 2 - 6;
        *x1 = *x0 + w + 12;
        *y1 = 60 + dy;
        *y0 = *y1 - 20;
        return 1;
    }
    std::vector<PageRow> rows = BuildRows(play, sPage.tab);
    int row = sPage.top + (index - (int)tabs.size());
    if (row >= (int)rows.size() || row >= sPage.top + kVisibleRows) {
        return 0;
    }
    s16 top = kRowTop - (row - sPage.top) * kRowHeight + dy;
    *x0 = -110;
    *x1 = 114;
    *y1 = top + 1;
    *y0 = *y1 - 20;
    return 1;
}

// The row under the cursor (-1 on the tab strip), for the test probe.
extern "C" s32 SevenDaysKaleido_TouchCursor(void) {
    using namespace SevenDays;
    return sPage.row;
}

// #4071: the HUD D-pad asks for the bound kit's icon and count (z_parameter.c).
extern "C" s32 SevenDaysDpad_Bound(s32 dir) {
    return SevenDays::DpadKitType(dir) >= 0;
}

extern "C" s32 SevenDaysDpad_Any(void) {
    return SevenDays::AnyDpadKit();
}

extern "C" s32 SevenDaysDpad_Count(s32 dir) {
    return SevenDays::DpadKitType(dir) >= 0 ? (s32)std::min<uint32_t>(SevenDays::DpadKitCount(dir), 99) : -1;
}

extern "C" void* SevenDaysDpad_Icon(s32 dir) {
    using namespace SevenDays;
    int type = DpadKitType(dir);
    if (type < 0) {
        return nullptr;
    }
    const Recipe* recipe = FindRecipe(GetPlaceableInfo((uint8_t)type).kit);
    return recipe != nullptr ? (void*)RecipeIcon(*recipe) : nullptr;
}
