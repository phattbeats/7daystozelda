#include "SevenDays.h"
#include "SevenDaysNet.h"
#include "soh/ShipInit.hpp"
#include "soh/frame_interpolation.h"
#include "soh/ShipUtils.h"
#include "soh/SaveManager.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"

#include <algorithm>
#include <cmath>

extern "C" {
#include "z64.h"
#include "macros.h"
#include "variables.h"
#include "functions.h"
extern PlayState* gPlayState;
extern f32 gSevenDaysMoonScale; // z_kankyo.c: Environment_DrawSunAndMoon
extern u8 gSevenDaysMoonRed;
extern s16 gSevenDaysTint[3][3]; // z_kankyo.c: Environment_Update, added to the adj colors
}

/**
 * M7: Majora's Mask-style nights. All of it is presentation, read from the
 * synced base (counters, the raid schedule) and the scene's wave; nothing here
 * sends packets.
 *
 *   - The dawn card: "Dawn of Day 7 · 2 nights until the raid", centered, in the
 *     game's own font, with the counters under it. Raised by the owner's dawn
 *     notice (RAID_NOTICE "dawn"), so every client shows it at the same dawn.
 *   - The final-hours clock: in the last in-game hour before a raid (17:00-18:00
 *     on a raid day, outdoors), a small clock fades in at the bottom of the
 *     screen; it fades out when the raid starts. Wordless.
 *   - Raid nights look red: the scene's ambient, light and fog colors ease
 *     toward red (gSevenDaysTint, added where Environment_Update applies the
 *     envCtx adj colors), the sky gets a red filter, and the moon
 *     (gMoonDL in Environment_DrawSunAndMoon) grows and turns red.
 *   - The raid track: while a wave is fought in this scene, the Mini-Boss Battle
 *     theme from the ROM (gSevenDays.RaidTrack overrides the sequence id); the
 *     scene's own music or night ambience comes back after.
 *   - Counters: a line on the pause screen; the file select's details
 *     (FileSelectMoreInfo) show each file's counters.
 */

namespace SevenDays {

// OPEN_DISPS declares these at block scope: inside this namespace they name
// SevenDays:: functions, which forward to the real ones (Loot.cpp draws too).
void FrameInterpolation_RecordOpenChild(const void* a, int b) {
    ::FrameInterpolation_RecordOpenChild(a, b);
}
void FrameInterpolation_RecordCloseChild() {
    ::FrameInterpolation_RecordCloseChild();
}

using Net::Now;

constexpr uint16_t FINAL_HOUR = 0xB555; // 17:00
constexpr uint16_t NIGHTFALL = 0xC000;  // 18:00: IS_NIGHT

// MARK: - State

struct DawnCard {
    bool active = false;
    double startedAt = 0;
    std::string title;  // "Dawn of Day 7"
    std::string raid;   // "2 nights until the raid" ("" when none is scheduled)
    std::string counts; // "6 days survived" + "2 raids survived"
    std::string counts2;
};
static DawnCard sCard;
static f32 sClockAlpha = 0.0f;
static f32 sRed = 0.0f;
static bool sOwnSkyFilter = false;
static bool sMusicOn = false;
static bool sMetaMarked = false;
static double sLastOverlay = 0.0; // when the overlay last ran with the pause screen closed

static double CardSeconds() {
    return 6.5;
}

void ShowDawnCard(uint32_t day, uint32_t untilRaid, uint32_t daysSurvived, uint32_t raidsSurvived) {
    sCard.active = true;
    sCard.startedAt = Now();
    sCard.title = fmt::format("Dawn of Day {}", day);
    if (untilRaid == UINT32_MAX) {
        sCard.raid = "";
    } else if (untilRaid == 0) {
        sCard.raid = "the raid is tonight";
    } else {
        sCard.raid = fmt::format("{} night{} until the raid", untilRaid, untilRaid == 1 ? "" : "s");
    }
    sCard.counts = fmt::format("{} day{} survived", daysSurvived, daysSurvived == 1 ? "" : "s");
    sCard.counts2 = fmt::format("{} raid{} survived", raidsSurvived, raidsSurvived == 1 ? "" : "s");
    Sfx_PlaySfxCentered(NA_SE_EV_CHICKEN_CRY_M); // the rooster, as in Clock Town
}

static f32 CardAlpha() {
    if (!sCard.active) {
        return 0.0f;
    }
    double t = Now() - sCard.startedAt;
    if (t >= CardSeconds()) {
        sCard.active = false;
        return 0.0f;
    }
    if (t < 0.6) {
        return (f32)(t / 0.6);
    }
    if (t > CardSeconds() - 1.0) {
        return (f32)((CardSeconds() - t) / 1.0);
    }
    return 1.0f;
}

std::string PauseCountersLine() {
    const BaseState& b = GetBase();
    std::string line = fmt::format("Day {}  -  Days survived: {}  -  Raids survived: {}", CurrentDay(),
                                   b.daysSurvived, b.hordeNightsSurvived);
    return line;
}

struct FileCounters {
    bool valid = false;
    uint32_t days = 0;
    uint32_t raids = 0;
};
static FileCounters sFileCounters[3];

// SaveManager::InitMeta, right after a file was loaded (boot, file select) or saved.
void TakeMetaCounters(int fileNum) {
    bool valid = sMetaMarked || GameInteractor::IsSaveLoaded(true);
    sMetaMarked = false;
    if (fileNum < 0 || fileNum >= 3) {
        return;
    }
    sFileCounters[fileNum] = { valid, valid ? GetBase().daysSurvived : 0, valid ? GetBase().hordeNightsSurvived : 0 };
}

void MarkMetaCounters() {
    sMetaMarked = true;
}

// MARK: - Red nights

static bool RaidNightLook() {
    if (gPlayState == nullptr || !IS_NIGHT || !IsOutdoorScene(gPlayState->sceneNum) || gPlayState->envCtx.indoors) {
        return false;
    }
    return RaidTonight() || RaidWaveHere();
}

static void UpdateRedNight() {
    f32 target = RaidNightLook() ? 1.0f : 0.0f;
    Math_StepToF(&sRed, target, 1.0f / 100.0f); // about five seconds to ease in or out
    EnvironmentContext* env = &gPlayState->envCtx;
    // A blood-moon tint, not a red screen: night fog is close in most fields, so
    // the fog gets the gentlest push. It is added to the final light colors and
    // never written into envCtx's adj fields, which bombs, lightning and bosses
    // step relatively (a share kept there compounded with their steps).
    static const s16 kAmbient[3] = { 40, -12, -18 };
    static const s16 kLight[3] = { 50, -15, -20 };
    static const s16 kFog[3] = { 45, -6, -10 };
    for (int i = 0; i < 3; i++) {
        gSevenDaysTint[0][i] = (s16)(kAmbient[i] * sRed);
        gSevenDaysTint[1][i] = (s16)(kLight[i] * sRed);
        gSevenDaysTint[2][i] = (s16)(kFog[i] * sRed);
    }
    if (sRed > 0.01f) {
        env->customSkyboxFilter = true;
        env->skyboxFilterColor[0] = 160;
        env->skyboxFilterColor[1] = 10;
        env->skyboxFilterColor[2] = 0;
        env->skyboxFilterColor[3] = (u8)(70.0f * sRed);
        sOwnSkyFilter = true;
    } else if (sOwnSkyFilter) {
        env->customSkyboxFilter = false;
        sOwnSkyFilter = false;
    }
    gSevenDaysMoonScale = 1.0f + 1.6f * sRed;
    gSevenDaysMoonRed = (u8)(255.0f * sRed);
}

static void ClearRedNight(bool sceneReset) {
    if (!sceneReset && gPlayState != nullptr && sOwnSkyFilter) {
        gPlayState->envCtx.customSkyboxFilter = false;
    }
    memset(gSevenDaysTint, 0, sizeof(gSevenDaysTint));
    sOwnSkyFilter = false;
    sRed = 0.0f;
    gSevenDaysMoonScale = 1.0f;
    gSevenDaysMoonRed = 0;
}

// MARK: - The raid track

static u16 RaidTrack() {
    return (u16)std::clamp(CVarGetInteger(CVAR_SEVEN_DAYS("RaidTrack"), NA_BGM_MINI_BOSS), 0, 0x7F);
}

// Stop the raid track and forget what the scene last started, so
// Environment_PlaySceneSequence picks the scene's music (or night ambience) again.
// The stop has to land now: Audio_PlayNatureAmbienceSequence refuses to start the
// night ambience while a sequence flagged "no ambience" (the Mini-Boss theme is)
// is the main BGM, which kept the raid track playing all night after a cleared
// wave. func_800F9474 marks the player stopped at once (a queued NA_BGM_STOP
// only lands on the next audio update). It also covers a silent scene, which
// starts nothing that would replace the raid track.
static void ForgetSceneMusic(PlayState* play) {
    if ((func_800FA0B4(SEQ_PLAYER_BGM_MAIN) & 0xFF) == RaidTrack()) {
        func_800F9474(SEQ_PLAYER_BGM_MAIN, 0);
    }
    gSaveContext.seqId = (u8)NA_BGM_DISABLED;
    gSaveContext.natureAmbienceId = NATURE_ID_DISABLED;
}

static void UpdateMusic() {
    bool gameOver = gPlayState->gameOverCtx.state != GAMEOVER_INACTIVE;
    bool want = RaidWaveHere() && !gameOver;
    if (want && !sMusicOn) {
        func_800F5ACC(RaidTrack()); // the mini-boss path: it remembers what was playing
        sMusicOn = true;
    } else if (!want && sMusicOn) {
        sMusicOn = false;
        if (gameOver) {
            // Death already stopped the music for the Game Over fanfare (and a fairy
            // revive keeps the raid track); the respawn's scene load picks the music.
            return;
        }
        // Back to the scene's own music, or its night ambience: let the scene pick.
        ForgetSceneMusic(gPlayState);
        Environment_PlaySceneSequence(gPlayState);
    }
}

// MARK: - Per frame

void NightsOnFrame() {
    if (gPlayState == nullptr || !GameInteractor::IsSaveLoaded(true)) {
        return; // the title's attract demo has a clock too
    }
    UpdateRedNight();
    UpdateMusic();

    // The final-hours clock: the last in-game hour before a raid, outdoors.
    u16 t = gSaveContext.dayTime;
    bool finalHour = RaidTonight() && IsOutdoorScene(gPlayState->sceneNum) && !gPlayState->envCtx.indoors &&
                     t >= FINAL_HOUR && t < NIGHTFALL && !RaidWaveHere();
    Math_StepToF(&sClockAlpha, finalHour ? 1.0f : 0.0f, 1.0f / 30.0f);
}

void NightsResetSession() {
    sCard = {};
    sClockAlpha = 0.0f;
    sMusicOn = false;
    ClearRedNight(true);
}

// MARK: - Drawing (the game's own font, on the overlay list)

static f32 TextWidth(const std::string& s, f32 scale) {
    f32 w = 0.0f;
    for (char c : s) {
        w += (f32)(u16)(Ship_GetCharFontWidth((u8)c) * (R_TEXT_CHAR_SCALE / 100.0f) * scale);
    }
    return w;
}

static void SetupText(GraphicsContext* gfx) {
    OPEN_DISPS(gfx);
    Gfx_SetupDL_39Opa(gfx);
    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
    gDPSetRenderMode(POLY_OPA_DISP++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    CLOSE_DISPS(gfx);
}

static void FillRect(GraphicsContext* gfx, s32 x0, s32 y0, s32 x1, s32 y1, u8 r, u8 g, u8 b, u8 a) {
    OPEN_DISPS(gfx);
    Gfx_SetupDL_57Opa(gfx);
    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, r, g, b, a);
    gDPFillRectangle(POLY_OPA_DISP++, x0, y0, x1, y1);
    CLOSE_DISPS(gfx);
}

static void Text(GraphicsContext* gfx, const std::string& s, s32 x, s32 y, u8 r, u8 g, u8 b, u8 a, f32 scale) {
    SetupText(gfx);
    std::vector<char> buf(s.begin(), s.end());
    buf.push_back('\0');
    Interface_DrawTextLine(gfx, buf.data(), (s16)x, (s16)y, r, g, b, a, scale, true);
}

// Segments joined by a centered dot (the font has no "·").
static void CenteredDotted(GraphicsContext* gfx, const std::vector<std::string>& parts, s32 y, u8 r, u8 g, u8 b, u8 a,
                           f32 scale) {
    const f32 gap = 14.0f * scale;
    f32 total = 0.0f;
    int n = 0;
    for (auto& p : parts) {
        if (!p.empty()) {
            total += TextWidth(p, scale) + (n++ > 0 ? gap : 0.0f);
        }
    }
    f32 x = SCREEN_WIDTH / 2.0f - total / 2.0f;
    n = 0;
    for (auto& p : parts) {
        if (p.empty()) {
            continue;
        }
        if (n++ > 0) {
            s32 cx = (s32)(x + gap / 2.0f), cy = y + (s32)(7.0f * scale);
            s32 d = std::max(1, (s32)(2.0f * scale + 0.5f));
            FillRect(gfx, cx - d / 2, cy - d / 2, cx - d / 2 + d, cy - d / 2 + d, r, g, b, a);
            x += gap;
        }
        Text(gfx, p, (s32)x, y, r, g, b, a, scale);
        x += TextWidth(p, scale);
    }
}

static void DrawDawnCard(GraphicsContext* gfx) {
    f32 alpha = CardAlpha();
    if (alpha <= 0.0f) {
        return;
    }
    u8 a = (u8)(255.0f * alpha);
    // A dark band across the middle, like the three-day cards.
    FillRect(gfx, 0, 92, SCREEN_WIDTH, 146, 0, 0, 0, (u8)(150.0f * alpha));
    FillRect(gfx, 40, 93, SCREEN_WIDTH - 40, 94, 255, 230, 160, (u8)(120.0f * alpha));
    FillRect(gfx, 40, 144, SCREEN_WIDTH - 40, 145, 255, 230, 160, (u8)(120.0f * alpha));
    CenteredDotted(gfx, { sCard.title, sCard.raid }, 100, 255, 255, 255, a, 1.0f);
    CenteredDotted(gfx, { sCard.counts, sCard.counts2 }, 124, 255, 220, 140, a, 0.75f);
}

static void DrawFinalHoursClock(GraphicsContext* gfx) {
    if (sClockAlpha <= 0.0f) {
        return;
    }
    u8 a = (u8)(255.0f * sClockAlpha);
    const s32 cx = SCREEN_WIDTH / 2, cy = 200;
    const f32 radius = 20.0f;
    u16 t = gSaveContext.dayTime;
    f32 progress = std::clamp((f32)(t - FINAL_HOUR) / (f32)(NIGHTFALL - FINAL_HOUR), 0.0f, 1.0f);
    // The face: a dark disc of squares, then twelve ticks (five minutes each) that
    // burn red as the hour runs out, and the minute hand.
    for (int dy = -radius - 3; dy <= radius + 3; dy += 2) {
        s32 half = (s32)sqrtf(std::max(0.0f, (radius + 3) * (radius + 3) - (f32)(dy * dy)));
        FillRect(gfx, cx - half, cy + dy, cx + half, cy + dy + 2, 10, 10, 30, (u8)(170.0f * sClockAlpha));
    }
    for (int i = 0; i < 12; i++) {
        s16 angle = (s16)(i * 0x10000 / 12);
        s32 x = cx + (s32)(Math_SinS(angle) * radius), y = cy - (s32)(Math_CosS(angle) * radius);
        bool burnt = progress * 12.0f > (f32)i;
        if (burnt) {
            FillRect(gfx, x - 2, y - 2, x + 2, y + 2, 255, 50, 30, a);
        } else {
            FillRect(gfx, x - 1, y - 1, x + 2, y + 2, 255, 230, 150, a);
        }
    }
    s16 hand = (s16)(progress * 0x10000);
    for (int k = 2; k <= 15; k += 2) {
        s32 x = cx + (s32)(Math_SinS(hand) * k), y = cy - (s32)(Math_CosS(hand) * k);
        FillRect(gfx, x - 1, y - 1, x + 1, y + 1, 255, 255, 255, a);
    }
    FillRect(gfx, cx - 2, cy - 2, cx + 2, cy + 2, 255, 200, 60, a);
    // The time under the face, the way the three-day clock shows it.
    s32 minutes = (s32)((u32)t * 24 * 60 / 0x10000);
    std::string time = fmt::format("{}:{:02}", ((minutes / 60) + 11) % 12 + 1, minutes % 60);
    Text(gfx, time, cx - (s32)(TextWidth(time, 0.6f) / 2.0f), cy + (s32)radius + 4, 255, 230, 160, a, 0.6f);
}

static void DrawPauseLine(PlayState* play, GraphicsContext* gfx) {
    PauseContext* pause = &play->pauseCtx;
    if (pause->state != 6 || pause->debugState != 0) {
        return; // only once the pause screen has opened (PAUSE_STATE_MAIN)
    }
    const BaseState& b = GetBase();
    CenteredDotted(gfx,
                   { fmt::format("Day {}", CurrentDay()), fmt::format("Days survived: {}", b.daysSurvived),
                     fmt::format("Raids survived: {}", b.hordeNightsSurvived) },
                   4, 255, 230, 160, 255, 0.7f);
}

} // namespace SevenDays

using namespace SevenDays;

// Called at the end of Play_DrawOverlayElements (z_play.c), after the HUD, the
// pause screen and the message box: the card and the clock sit on top of them.
extern "C" void SevenDays_DrawOverlay(PlayState* play) {
    if (!NightsEnabled() || play == nullptr || !GameInteractor::IsSaveLoaded(true)) {
        return;
    }
    if (!sCard.active && sClockAlpha <= 0.0f && play->pauseCtx.state != 6) {
        return;
    }
    GraphicsContext* gfx = play->state.gfxCtx;
    // Draw on the overlay list: the helpers (Interface_DrawTextLine) write POLY_OPA.
    Gfx* savedOpa = gfx->polyOpa.p;
    gfx->polyOpa.p = gfx->overlay.p;
    if (play->pauseCtx.state == 0) {
        // Cutscenes and text boxes own the screen: the clock steps aside for both,
        // the card for cutscenes (a dawn often comes with a Navi line under it).
        bool cutscene = play->csCtx.state != CS_STATE_IDLE || Player_InCsMode(play);
        if (!cutscene && play->msgCtx.msgMode == MSGMODE_NONE) {
            DrawFinalHoursClock(gfx);
        }
        // A card hidden by a cutscene or Navi's dawn line waits instead of running out unseen.
        double now = Now();
        if (cutscene && sCard.active && sLastOverlay > 0.0 && now - sLastOverlay < 0.25) {
            sCard.startedAt += now - sLastOverlay;
        }
        sLastOverlay = now;
        if (!cutscene) {
            DrawDawnCard(gfx);
        }
    } else {
        DrawPauseLine(play, gfx);
    }
    gfx->overlay.p = gfx->polyOpa.p;
    gfx->polyOpa.p = savedOpa;
}

// FileSelectMoreInfo (z_file_choose.c DrawMoreInfo): this file's counters.
extern "C" void SevenDays_DrawFileInfo(GraphicsContext* gfx, s16 fileIndex, u8 alpha, s16 x, s16 y) {
    if (CVarGetInteger(CVAR_SEVEN_DAYS("Enabled"), 0) == 0 || CVarGetInteger(CVAR_SEVEN_DAYS("Nights"), 1) == 0) {
        return;
    }
    if (fileIndex < 0 || fileIndex >= 3 || !SaveManager::Instance->fileMetaInfo[fileIndex].valid ||
        !sFileCounters[fileIndex].valid) {
        return;
    }
    // In the name bar, right of the name: "Days 2 · Raids 1".
    const FileCounters& c = sFileCounters[fileIndex];
    std::string days = fmt::format("Days {}", c.days), raids = fmt::format("Raids {}", c.raids);
    const f32 scale = 0.5f;
    Text(gfx, days, x, y, 255, 230, 160, alpha, scale);
    s32 dx = x + (s32)TextWidth(days, scale) + 4;
    FillRect(gfx, dx - 1, y + 3, dx + 1, y + 5, 255, 230, 160, alpha);
    Text(gfx, raids, dx + 4, y, 255, 230, 160, alpha, scale);
}

namespace SevenDays {

void NightsRegisterHooks(bool enabled) {
    COND_HOOK(OnSceneInit, enabled, [](int16_t sceneNum) {
        // A new scene: fresh environment and its own music. OnSceneInit runs before
        // Play_Init plays the scene's sequence, so forgetting the raid track here
        // makes the new scene start its own, even through a "continue the music"
        // door (Link's house from Kokiri Forest).
        if (sMusicOn && gPlayState != nullptr) {
            ForgetSceneMusic(gPlayState);
        }
        sMusicOn = false;
        ClearRedNight(true);
        sClockAlpha = 0.0f;
    });
    if (!enabled) {
        if (sMusicOn && gPlayState != nullptr) {
            sMusicOn = false;
            ForgetSceneMusic(gPlayState);
            Environment_PlaySceneSequence(gPlayState);
        }
        ClearRedNight(gPlayState == nullptr);
        sCard = {};
        sClockAlpha = 0.0f;
    }
}

} // namespace SevenDays

// MARK: - Test hooks

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
// "card" shows the dawn card with today's numbers; "time:0xB600" sets the time of
// day (the clock then runs on its own); "meta" re-reads file select's counters.
extern "C" {
EMSCRIPTEN_KEEPALIVE
const char* sevendays_test_nights_state() {
    static std::string out;
    nlohmann::json j;
    j["enabled"] = NightsEnabled();
    j["card"] = { { "active", sCard.active },
                  { "alpha", CardAlpha() },
                  { "title", sCard.title },
                  { "raid", sCard.raid },
                  { "counts", sCard.counts + " / " + sCard.counts2 } };
    j["clockAlpha"] = sClockAlpha;
    j["red"] = sRed;
    j["moonScale"] = gSevenDaysMoonScale;
    j["moonRed"] = gSevenDaysMoonRed;
    j["music"] = sMusicOn;
    j["raidWave"] = gPlayState != nullptr && RaidWaveHere();
    j["raidTonight"] = RaidTonight();
    j["untilRaid"] = NightsUntilRaid();
    j["dayTime"] = gSaveContext.dayTime;
    j["pauseLine"] = PauseCountersLine();
    j["pauseState"] = gPlayState != nullptr ? gPlayState->pauseCtx.state : -1;
    j["seq"] = func_800FA0B4(SEQ_PLAYER_BGM_MAIN);
    j["files"] = nlohmann::json::array();
    for (int i = 0; i < 3; i++) {
        j["files"].push_back({ { "valid", SaveManager::Instance->fileMetaInfo[i].valid != 0 },
                               { "sd", sFileCounters[i].valid },
                               { "days", sFileCounters[i].days },
                               { "raids", sFileCounters[i].raids } });
    }
    if (gPlayState != nullptr) {
        auto& e = gPlayState->envCtx;
        j["env"] = { { "ambient", { gSevenDaysTint[0][0], gSevenDaysTint[0][1], gSevenDaysTint[0][2] } },
                     { "fog", { gSevenDaysTint[2][0], gSevenDaysTint[2][1], gSevenDaysTint[2][2] } },
                     { "adjAmbient", { e.adjAmbientColor[0], e.adjAmbientColor[1], e.adjAmbientColor[2] } },
                     { "skyFilter", e.customSkyboxFilter } };
    }
    out = j.dump();
    return out.c_str();
}

EMSCRIPTEN_KEEPALIVE
void sevendays_test_nights(const char* cmdC) {
    if (!NightsEnabled() || gPlayState == nullptr) {
        return;
    }
    std::string cmd = cmdC;
    if (cmd == "card") {
        const BaseState& b = GetBase();
        ShowDawnCard(CurrentDay(), NightsUntilRaid(), b.daysSurvived, b.hordeNightsSurvived);
    } else if (cmd.rfind("time:", 0) == 0) {
        gSaveContext.dayTime = gSaveContext.skyboxTime = (u16)std::stoi(cmd.substr(5), nullptr, 0);
    }
}
}
#endif
