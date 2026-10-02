#include "SevenDays.h"
#include "soh/Network/Anchor/Anchor.h"

extern "C" {
#include "z64.h"
// z_player_lib.c: when active, Player_DrawImpl uses this tunic color (tunic and
// cap share one material) instead of the vanilla/cosmetic one.
extern Color_RGB8 gSevenDaysTunicColor;
extern u8 gSevenDaysTunicColorActive;
}

/**
 * Players recolor the whole tunic with their lobby tunic color (Anchor's
 * per-player `tunic`; older clients send only `color`, which stands in). Our own Link uses our color;
 * each DummyPlayer swaps in its owner's color around its draw, so remote Links
 * show their owner's choice instead of our local cosmetics. The cosmetics editor
 * is left alone: switching gSevenDays.TunicColors off restores it as it was.
 */

namespace SevenDays {

static bool CoopColorsAvailable() {
    return TunicColorsEnabled() && Anchor::Instance != nullptr && Anchor::Instance->isEnabled;
}

void RestoreLocalTunicOverride() {
    if (!CoopColorsAvailable()) {
        gSevenDaysTunicColorActive = 0;
        return;
    }
    gSevenDaysTunicColor = AnchorLocalTunic();
    gSevenDaysTunicColorActive = 1;
}

void ApplyTunicOverrideForClient(uint32_t clientId) {
    if (!CoopColorsAvailable() || !Anchor::Instance->clients.contains(clientId)) {
        gSevenDaysTunicColorActive = 0;
        return;
    }
    gSevenDaysTunicColor = Anchor::Instance->clients[clientId].tunic;
    gSevenDaysTunicColorActive = 1;
}

} // namespace SevenDays
