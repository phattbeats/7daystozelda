#include "Anchor.h"
#include <libultraship/libultraship.h>
#include "soh/SohGui/SohGui.hpp"
#include "soh/SohGui/SohMenu.h"
#include "soh/util.h"
#include "soh/SevenDays/SevenDays.h"
#include <algorithm>
#include <cmath>

namespace SohGui {
extern std::shared_ptr<SohMenu> mSohMenu;
extern std::shared_ptr<AnchorRoomWindow> mAnchorRoomWindow;
} // namespace SohGui

static const char* pvpModes[3] = { "Off", "On", "On + Friendly Fire" };
static std::vector<const char*> teleportModes = { "None", "Team Only", "All" };
static std::vector<const char*> showLocationsModes = { "None", "Team Only", "All" };

// Lobby color presets, the same as the web lobby's swatches (shell.html).
static const struct {
    const char* name;
    Color_RGB8 color;
} sColorSwatches[] = {
    { "Green", { 0x3C, 0xB0, 0x43 } },  { "Red", { 0xD0, 0x31, 0x2D } },   { "Blue", { 0x2A, 0x6F, 0xDB } },
    { "Purple", { 0x8E, 0x44, 0xAD } }, { "Gold", { 0xE8, 0xB9, 0x3B } },  { "Pink", { 0xF0, 0x6E, 0xB4 } },
    { "Cyan", { 0x3B, 0xD6, 0xD6 } },   { "White", { 0xFF, 0xFF, 0xFF } }, { "Black", { 0x22, 0x22, 0x22 } },
};

// A full picker (hex + RGB) and the preset swatches for one Color24 CVar.
static void LobbyColorPicker(const char* label, const char* cvar, Color_RGB8 current) {
    ImGui::PushID(cvar);
    ImGui::Text("%s", label);
    float rgb[3] = { current.r / 255.0f, current.g / 255.0f, current.b / 255.0f };
    ImGui::SetNextItemWidth(std::min(ImGui::GetContentRegionAvail().x, ImGui::GetFontSize() * 9));
    bool changed = ImGui::ColorEdit3("##pick", rgb, ImGuiColorEditFlags_DisplayHex | ImGuiColorEditFlags_NoLabel);
    Color_RGB8 next = { (u8)(rgb[0] * 255.0f + 0.5f), (u8)(rgb[1] * 255.0f + 0.5f), (u8)(rgb[2] * 255.0f + 0.5f) };
    // Swatches on their own row, wrapping in narrow columns.
    float size = ImGui::GetFrameHeight();
    float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    for (size_t i = 0; i < std::size(sColorSwatches); i++) {
        const Color_RGB8& c = sColorSwatches[i].color;
        if (i > 0 && ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + size <= right) {
            ImGui::SameLine();
        }
        if (ImGui::ColorButton(sColorSwatches[i].name, ImVec4(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, 1.0f),
                               ImGuiColorEditFlags_NoAlpha, ImVec2(size, size))) {
            next = c;
            changed = true;
        }
    }
    if (changed) {
        CVarSetColor24(cvar, next);
        Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }
    ImGui::PopID();
}

// Rough perceptual distance (CIE76 in Lab); below ~25 two tunics are hard to tell apart.
static float ColorDistance(Color_RGB8 a, Color_RGB8 b) {
    auto lab = [](Color_RGB8 c, float out[3]) {
        float v[3] = { c.r / 255.0f, c.g / 255.0f, c.b / 255.0f };
        for (float& x : v) {
            x = x > 0.04045f ? powf((x + 0.055f) / 1.055f, 2.4f) : x / 12.92f;
        }
        float X = (v[0] * 0.4124f + v[1] * 0.3576f + v[2] * 0.1805f) / 0.95047f;
        float Y = v[0] * 0.2126f + v[1] * 0.7152f + v[2] * 0.0722f;
        float Z = (v[0] * 0.0193f + v[1] * 0.1192f + v[2] * 0.9505f) / 1.08883f;
        auto f = [](float t) { return t > 0.008856f ? cbrtf(t) : 7.787f * t + 16.0f / 116.0f; };
        out[0] = 116.0f * f(Y) - 16.0f;
        out[1] = 500.0f * (f(X) - f(Y));
        out[2] = 200.0f * (f(Y) - f(Z));
    };
    float la[3], lb[3];
    lab(a, la);
    lab(b, lb);
    return sqrtf((la[0] - lb[0]) * (la[0] - lb[0]) + (la[1] - lb[1]) * (la[1] - lb[1]) +
                 (la[2] - lb[2]) * (la[2] - lb[2]));
}

// Fairy gradient and tunic: what everyone else sees on our fairy and our Link.
// Changes go out within a second (PuppetFairy.cpp resends the client state).
static void AnchorColorsMenu() {
    ImGui::SeparatorText("Your Colors (everyone sees these)");
    LobbyColorPicker("Fairy core", CVAR_REMOTE_ANCHOR("FairyInner"), AnchorLocalFairyInner());
    LobbyColorPicker("Fairy aura", CVAR_REMOTE_ANCHOR("Color"), AnchorLocalFairyOuter());
    LobbyColorPicker("Tunic", CVAR_REMOTE_ANCHOR("Tunic"), AnchorLocalTunic());
    UIWidgets::CVarCheckbox("My Navi uses my fairy colors", CVAR_REMOTE_ANCHOR("NaviUsesFairyColors"),
                            UIWidgets::CheckboxOptions().DefaultValue(true).Color(THEME_COLOR).Tooltip(
                                "While in a room, your own Navi glows in your fairy colors when idle, as others see "
                                "her. Off: keep the Cosmetics Editor's Navi colors."));

    auto anchor = Anchor::Instance;
    if (anchor == nullptr || !anchor->isConnected) {
        return;
    }
    Color_RGB8 mine = AnchorLocalTunic();
    for (auto& [clientId, client] : anchor->clients) {
        if (client.self || !client.online || ColorDistance(mine, client.tunic) >= 25.0f) {
            continue;
        }
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "%s Your tunic is close to %s's. Pick another so you can "
                           "tell your Links apart.", ICON_FA_EXCLAMATION_TRIANGLE, client.name.c_str());
    }
}

void AnchorMainMenu(WidgetInfo& info) {
    auto anchor = Anchor::Instance;

    std::string anchorTeamId = CVarGetString(CVAR_REMOTE_ANCHOR("TeamId"), "default");
    std::string anchorRoomId = CVarGetString(CVAR_REMOTE_ANCHOR("RoomId"), "");
    std::string anchorName = CVarGetString(CVAR_REMOTE_ANCHOR("Name"), "");

#ifdef __EMSCRIPTEN__
    std::string wsUrl = CVarGetString(CVAR_REMOTE_ANCHOR("WebSocketURL"), "ws://localhost:8080/anchor");
    bool isFormValid = !SohUtils::IsStringEmpty(wsUrl) && !SohUtils::IsStringEmpty(anchorRoomId) &&
                       !SohUtils::IsStringEmpty(anchorName);
#else
    std::string host = CVarGetString(CVAR_REMOTE_ANCHOR("Host"), "anchor.hm64.org");
    uint16_t port = CVarGetInteger(CVAR_REMOTE_ANCHOR("Port"), 43383);
    bool isFormValid = !SohUtils::IsStringEmpty(host) && port > 1024 && port < 65535 &&
                       !SohUtils::IsStringEmpty(anchorRoomId) && !SohUtils::IsStringEmpty(anchorName);
#endif

    ImGui::SeparatorText("Connection Settings");

    ImGui::BeginDisabled(anchor->isEnabled);

#ifdef __EMSCRIPTEN__
    ImGui::Text("WebSocket URL");
    if (UIWidgets::InputString("##WebSocketURL", &wsUrl,
                               UIWidgets::InputOptions()
                                   .Size(ImVec2(ImGui::GetContentRegionAvail().x, 0))
                                   .Color(THEME_COLOR))) {
        CVarSetString(CVAR_REMOTE_ANCHOR("WebSocketURL"), wsUrl.c_str());
        Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }
#else
    ImGui::Text("Host & Port");
    if (UIWidgets::InputString("##Host", &host,
                               UIWidgets::InputOptions()
                                   .Size(ImGui::GetContentRegionAvail() -
                                         ImVec2((ImGui::GetFontSize() * 5 + ImGui::GetStyle().ItemSpacing.x), 0))
                                   .Color(THEME_COLOR))) {
        CVarSetString(CVAR_REMOTE_ANCHOR("Host"), host.c_str());
        Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }

    ImGui::SameLine();
    UIWidgets::PushStyleInput(THEME_COLOR);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 5);
    if (ImGui::InputScalar("##Port", ImGuiDataType_U16, &port)) {
        CVarSetInteger(CVAR_REMOTE_ANCHOR("Port"), port);
        Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }
    UIWidgets::PopStyleInput();
#endif

    ImGui::Text("Name");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (UIWidgets::InputString("##Name", &anchorName, UIWidgets::InputOptions().Color(THEME_COLOR))) {
        CVarSetString(CVAR_REMOTE_ANCHOR("Name"), anchorName.c_str());
        Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }
    ImGui::Text("Room ID");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (UIWidgets::InputString("##RoomId", &anchorRoomId,
                               UIWidgets::InputOptions().IsSecret(anchor->isEnabled).Color(THEME_COLOR))) {
        CVarSetString(CVAR_REMOTE_ANCHOR("RoomId"), anchorRoomId.c_str());
        Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }
    ImGui::Text("Team ID (Items & Flags Shared)");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (UIWidgets::InputString("##TeamId", &anchorTeamId, UIWidgets::InputOptions().Color(THEME_COLOR))) {
        CVarSetString(CVAR_REMOTE_ANCHOR("TeamId"), anchorTeamId.c_str());
        Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }
    ImGui::Spacing();

    if (UIWidgets::Button("Restore Defaults", UIWidgets::ButtonOptions()
                                                  .Size(ImVec2(ImGui::GetContentRegionAvail().x / 2, 0))
                                                  .Color(UIWidgets::Colors::Red))) {
#ifdef __EMSCRIPTEN__
        CVarSetString(CVAR_REMOTE_ANCHOR("WebSocketURL"), "ws://localhost:8080/anchor");
#else
        CVarSetString(CVAR_REMOTE_ANCHOR("Host"), "anchor.hm64.org");
        CVarSetInteger(CVAR_REMOTE_ANCHOR("Port"), 43383);
#endif
        CVarSetString(CVAR_REMOTE_ANCHOR("TeamId"), "default");
        CVarSetString(CVAR_REMOTE_ANCHOR("RoomId"), "");
        CVarSetString(CVAR_REMOTE_ANCHOR("Name"), "");
        Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }

#ifndef __EMSCRIPTEN__
    ImGui::SameLine();

    if (UIWidgets::Button("Global Room", UIWidgets::ButtonOptions()
                                             .Color(UIWidgets::Colors::Blue)
                                             .Tooltip("Always-online public room so you don't have to experience "
                                                      "Hyrule alone. PVP and syncing are disabled."))) {
        CVarSetString(CVAR_REMOTE_ANCHOR("Host"), "anchor.hm64.org");
        CVarSetInteger(CVAR_REMOTE_ANCHOR("Port"), 43383);
        CVarSetString(CVAR_REMOTE_ANCHOR("TeamId"), "default");
        CVarSetString(CVAR_REMOTE_ANCHOR("RoomId"), "soh-global");
        Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }
#endif

    ImGui::EndDisabled();

    ImGui::Spacing();

    ImGui::BeginDisabled(!isFormValid);
    const char* buttonLabel = anchor->isEnabled ? "Disable" : "Enable";
    UIWidgets::PushStyleButton(anchor->isEnabled ? UIWidgets::ColorValues.at(UIWidgets::Colors::Red)
                                                 : UIWidgets::ColorValues.at(UIWidgets::Colors::Green));
    if (ImGui::Button(buttonLabel, ImVec2(-1.0f, 0.0f))) {
        if (anchor->isEnabled) {
            CVarClear(CVAR_REMOTE_ANCHOR("Enabled"));
            Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
            anchor->Disable();
        } else {
            CVarSetInteger(CVAR_REMOTE_ANCHOR("Enabled"), 1);
            Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
            anchor->Enable();
        }
    }
    UIWidgets::PopStyleButton();
    ImGui::EndDisabled();
    ImGui::Spacing();

    AnchorColorsMenu();
    ImGui::Spacing();

    if (!anchor->isEnabled) {
        return;
    }

    if (!anchor->isConnected) {
        ImGui::Text("Connecting...");
        return;
    }

    ImGui::SeparatorText("Current Room");
    ImGui::Text("%s Connected", ICON_FA_CHECK);

    UIWidgets::PushStyleButton(THEME_COLOR);
    if (ImGui::Button("Request Team State")) {
        anchor->SendPacket_RequestTeamState();
    }
    UIWidgets::Tooltip("Try this if you are missing items or flags that your team members have collected");
    UIWidgets::PopStyleButton();

    ImGui::SameLine();

    UIWidgets::WindowButton("Toggle Anchor Room Window", CVAR_WINDOW("AnchorRoom"), SohGui::mAnchorRoomWindow);
    if (!SohGui::mAnchorRoomWindow->IsVisible()) {
        SohGui::mAnchorRoomWindow->DrawElement();
    }
}

void AnchorAdminMenu(WidgetInfo& info) {
    auto anchor = Anchor::Instance;
    bool isGlobalRoom = (std::string("soh-global") == CVarGetString(CVAR_REMOTE_ANCHOR("RoomId"), ""));

    if (!anchor->isEnabled || !anchor->isConnected || anchor->roomState.ownerClientId != anchor->ownClientId ||
        isGlobalRoom) {
        return;
    }

    ImGui::SeparatorText("Room Settings (Admin Only)");

    UIWidgets::PushStyleButton(THEME_COLOR);
    if (ImGui::Button("Clear All Team State")) {
        std::set<std::string> teams;
        for (auto& [clientId, client] : Anchor::Instance->clients) {
            teams.insert(client.teamId);
        }
        for (auto& team : teams) {
            anchor->SendPacket_ClearTeamState(team);
        }
    }
    UIWidgets::PopStyleButton();

    if (UIWidgets::CVarCombobox("PvP Mode:", CVAR_REMOTE_ANCHOR("RoomSettings.PvpMode"), pvpModes,
                                UIWidgets::ComboboxOptions()
                                    .DefaultIndex(1)
                                    .LabelPosition(UIWidgets::LabelPositions::Above)
                                    .Color(THEME_COLOR))) {
        anchor->SendPacket_UpdateRoomState();
    }
    if (UIWidgets::CVarCombobox("Show Locations For:", CVAR_REMOTE_ANCHOR("RoomSettings.ShowLocationsMode"),
                                showLocationsModes,
                                UIWidgets::ComboboxOptions()
                                    .DefaultIndex(1)
                                    .LabelPosition(UIWidgets::LabelPositions::Above)
                                    .Color(THEME_COLOR))) {
        anchor->SendPacket_UpdateRoomState();
    }
    if (UIWidgets::CVarCombobox("Allow Teleporting To:", CVAR_REMOTE_ANCHOR("RoomSettings.TeleportMode"), teleportModes,
                                UIWidgets::ComboboxOptions()
                                    .DefaultIndex(1)
                                    .LabelPosition(UIWidgets::LabelPositions::Above)
                                    .Color(THEME_COLOR))) {
        anchor->SendPacket_UpdateRoomState();
    }
    if (UIWidgets::CVarCheckbox("Sync Items & Flags", CVAR_REMOTE_ANCHOR("RoomSettings.SyncItemsAndFlags"),
                                UIWidgets::CheckboxOptions().DefaultValue(true).Color(THEME_COLOR))) {
        anchor->SendPacket_UpdateRoomState();
    }
}

void AnchorInstructionsMenu(WidgetInfo& info) {
    auto anchor = Anchor::Instance;

    ImGui::SeparatorText("Usage Instructions");

    ImGui::TextWrapped("1. All players involved should start at the file select screen");

    ImGui::TextWrapped("2. Come up with a unique Room ID (this is basically your password) and enter it, along with "
                       "your desired player name and team ID and click Enable");

    ImGui::TextWrapped("3. The host should configure the randomizer settings and generate a seed, then share the newly "
                       "generated JSON spoiler file with other players.");

    ImGui::TextWrapped("4. All players should load the same JSON spoiler file (drag it into SoH window), make sure "
                       "seed icons match, then create a new file.");

    ImGui::TextWrapped("5. All players should now load into their game. IMPORTANT! If using an existing save/seed "
                       "ensure the player with the most progress loads the file first.");

    ImGui::TextWrapped("6. After everyone has loaded in, verify on the network tab that it doesn't warn about anyone "
                       "being on a wrong version or seed.");

    ImGui::Spacing();

    ImGui::TextWrapped(
        "Note: Team ID is used to group players together in the same team, sharing items and flags. Make sure all "
        "players who want to share progress use the same Team ID. All players with the same Team ID should be using "
        "the same randomizer seed, while players on different teams can use different seeds.");
}

// 7 Days to Zelda (soh/SevenDays). Works solo too; in a room the owner decides the pool.
void SevenDaysMenu(WidgetInfo& info) {
    ImGui::SeparatorText("7 Days to Zelda");
    UIWidgets::CVarCheckbox("Enable 7 Days to Zelda", CVAR_SEVEN_DAYS("Enabled"),
                            UIWidgets::CheckboxOptions().Color(THEME_COLOR).Tooltip(
                                "Master switch. Off: plain co-op, nothing below runs."));
    bool off = !SevenDays::Enabled();
    UIWidgets::CVarCheckbox("Materials and crafting", CVAR_SEVEN_DAYS("Crafting"),
                            UIWidgets::CheckboxOptions({ { .disabled = off } })
                                .DefaultValue(true)
                                .Color(THEME_COLOR)
                                .Tooltip("Gather Fiber, Stone, Wood, Bone and Rot into the room's shared pool and "
                                         "craft on the pause menu's Workbench page (R from Equipment; Tab or the Craft button opens "
                                         "it)."));
    UIWidgets::CVarCheckbox("Tunic in my lobby tunic color", CVAR_SEVEN_DAYS("TunicColors"),
                            UIWidgets::CheckboxOptions({ { .disabled = off } })
                                .DefaultValue(true)
                                .Color(THEME_COLOR)
                                .Tooltip("Every player's Link wears their own lobby tunic color (tunic and cap), set under Network > Anchor."));
    UIWidgets::CVarCheckbox("Bases and the village", CVAR_SEVEN_DAYS("Base"),
                            UIWidgets::CheckboxOptions({ { .disabled = off } })
                                .DefaultValue(true)
                                .Color(THEME_COLOR)
                                .Tooltip("Build barricades, spike strips, workbenches and storage chests from kits "
                                         "(the Workbench page's Base tab). A new save starts in a boarded-up Kokiri "
                                         "village."));
    UIWidgets::CVarCheckbox("Raids on the base", CVAR_SEVEN_DAYS("Raids"),
                            UIWidgets::CheckboxOptions({ { .disabled = off || !SevenDays::BaseEnabled() } })
                                .DefaultValue(true)
                                .Color(THEME_COLOR)
                                .Tooltip("The dead come for the workbench every third night once Gohma falls. "
                                         "Outdoor scenes get a clock; dungeons and interiors stay frozen. Takes over "
                                         "from Horde Night while on."));
    UIWidgets::CVarCheckbox("Loot: caches, pots and blueprints", CVAR_SEVEN_DAYS("Loot"),
                            UIWidgets::CheckboxOptions({ { .disabled = off || !SevenDays::BaseEnabled() } })
                                .DefaultValue(true)
                                .Color(THEME_COLOR)
                                .Tooltip("Supply caches in dungeons and grottos (each opens once per save), material "
                                         "rolls from pots and crates, blueprints that unlock the strong recipes, and "
                                         "rewards for bosses and every 10 Gold Skulltula tokens."));
    UIWidgets::CVarCheckbox("Majora-style nights", CVAR_SEVEN_DAYS("Nights"),
                            UIWidgets::CheckboxOptions({ { .disabled = off || !SevenDays::RaidsEnabled() } })
                                .DefaultValue(true)
                                .Color(THEME_COLOR)
                                .Tooltip("The dawn card, the final-hours clock before a raid, red raid nights with a "
                                         "red moon, the raid track, and days/raids survived on the pause screen and "
                                         "in file select details."));
    if (SevenDays::BaseEnabled()) {
        ImGui::TextWrapped("%s", SevenDays::BaseCountsLine().c_str());
    }
    if (SevenDays::CraftingEnabled()) {
        if (ImGui::Button("Open Workbench window")) {
            SevenDays::ToggleCraftingWindow();
        }
    }
}

#if defined(ENABLE_REMOTE_CONTROL) || defined(__EMSCRIPTEN__)
void RegisterAnchorMenu() {
    WidgetPath path = { "Network", "Anchor", SECTION_COLUMN_1 };
    SohGui::mSohMenu->AddWidget(path, "AnchorMainMenu", WIDGET_CUSTOM)
        .CustomFunction(AnchorMainMenu)
        .HideInSearch(true);
    path.column = SECTION_COLUMN_2;
    SohGui::mSohMenu->AddWidget(path, "SevenDaysMenu", WIDGET_CUSTOM)
        .CustomFunction(SevenDaysMenu)
        .HideInSearch(true);
    SohGui::mSohMenu->AddWidget(path, "AnchorAdminMenu", WIDGET_CUSTOM)
        .CustomFunction(AnchorAdminMenu)
        .HideInSearch(true);
    SohGui::mSohMenu->AddWidget(path, "AnchorInstructionsMenu", WIDGET_CUSTOM)
        .CustomFunction(AnchorInstructionsMenu)
        .HideInSearch(true);
}

static RegisterMenuInitFunc menuInitFunc(RegisterAnchorMenu);
#endif
