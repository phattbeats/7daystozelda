#include "SevenDays.h"
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

} // namespace SevenDays

using namespace SevenDays;

void SevenDaysCraftingWindow::Draw() {
    if (!CraftingEnabled() || !InGameplay()) {
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

    if (ImGui::BeginTabBar("SevenDaysTabs")) {
        if (ImGui::BeginTabItem("Craft")) {
            DrawRecipes(RECIPE_CONSUMABLE, RECIPE_KIT);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Trade")) {
            DrawRecipes(RECIPE_TRADE, RECIPE_TRADE);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    if (ImGui::Button("Close", ImVec2(-FLT_MIN, kButtonHeight))) {
        Hide();
    }
}
