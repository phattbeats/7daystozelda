#include "SevenDays.h"
#include "SevenDaysNet.h"
#include "soh/OTRGlobals.h"
#include "soh/SohGui/SohGui.hpp"

extern "C" {
#include "z64.h"
#include "variables.h"
extern PlayState* gPlayState;
}

namespace SohGui {
extern std::shared_ptr<SevenDaysCraftingWindow> mSevenDaysCraftingWindow;
}

/**
 * Crafting v0: an ImGui window (like AnchorRoomWindow). Opens from the Tab key,
 * the on-screen Craft button, or the Anchor menu; a placed Workbench opens it in
 * M5. Buttons are sized for the touch layout.
 */

static constexpr float kButtonHeight = 48.0f;

namespace SevenDays {

void ToggleCraftingWindow() {
    if (SohGui::mSevenDaysCraftingWindow) {
        SohGui::mSevenDaysCraftingWindow->ToggleVisibility();
    }
}

static int sRequestedTab = -1;

void OpenCraftingWindow(int tab) {
    sRequestedTab = tab;
    if (SohGui::mSevenDaysCraftingWindow && !SohGui::mSevenDaysCraftingWindow->IsVisible()) {
        SohGui::mSevenDaysCraftingWindow->Show();
    }
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

// Raid interval: picked once on a new save, changeable later from the Base tab.
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

// The owner (or a solo player) chooses on a new save, before the first night.
static void DrawRaidIntervalPicker() {
    if (!RaidsEnabled() || !InGameplay() || GetBase().raidInterval != 0 || !Net::IsOwner()) {
        return;
    }
    // Not over the opening's narration: wait until Link is free to move.
    if (gPlayState->csCtx.state != CS_STATE_IDLE || gPlayState->msgCtx.msgMode != MSGMODE_NONE ||
        gPlayState->transitionTrigger != TRANS_TRIGGER_OFF) {
        return;
    }
    auto vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowViewport(vp->ID);
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowFocus();
    if (ImGui::Begin("How often do raids come?", nullptr,
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse |
                         ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::TextUnformatted("The dead raid the village at night.");
        ImGui::TextUnformatted("Pick how many days pass between raids.");
        ImGui::TextColored(ImVec4(1, 1, 1, 0.6f), "The first raid comes after the Deku Tree, and it's an easy one.");
        ImGui::Spacing();
        for (auto& c : kIntervals) {
            if (ImGui::Button(c.label, ImVec2(ImGui::GetFontSize() * 16.0f, kButtonHeight))) {
                RequestRaidInterval(c.days);
            }
        }
        ImGui::TextColored(ImVec4(1, 1, 1, 0.6f), "You can change it later on the Workbench's Base tab.");
    }
    ImGui::End();
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
    DrawRaidIntervalPicker();
    if ((!CraftingEnabled() && !BaseEnabled()) || !InGameplay()) {
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Tab, false)) {
        ToggleVisibility();
    }

    auto vp = ImGui::GetMainViewport();
    if (!IsVisible()) {
        // A thumb-sized button on the right edge for touch players.
        ImGui::SetNextWindowViewport(vp->ID);
        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - 8.0f, vp->WorkPos.y + vp->WorkSize.y * 0.35f),
                                ImGuiCond_Always, ImVec2(1.0f, 0.5f));
        ImGui::SetNextWindowBgAlpha(0.35f);
        ImGui::Begin("##SevenDaysCraftButton", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoNav |
                         ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoDocking |
                         ImGuiWindowFlags_NoSavedSettings);
        if (ImGui::Button(ICON_FA_WRENCH " Craft", ImVec2(0, kButtonHeight))) {
            ToggleVisibility();
        }
        ImGui::End();
        return;
    }

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
