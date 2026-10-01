#include "soh/Network/Anchor/Anchor.h"
#include "soh/Notification/Notification.h"
#include <nlohmann/json.hpp>
#include <libultraship/libultraship.h>

extern "C" {
#include "macros.h"
#include "functions.h"
#include "variables.h"
extern PlayState* gPlayState;
}

/**
 * HORDE_EVENT
 *
 * The enemy authority announces the start and end of a horde night to its
 * same-scene peers (their own clocks may disagree with the authority's; the
 * enemies themselves arrive through ENEMY_SPAWN regardless). See HordeNight.h.
 */

void Anchor::SendPacket_HordeEvent(bool started, int32_t horde) {
    if (!IsSaveLoaded() || gPlayState == NULL) {
        return;
    }

    nlohmann::json payload;
    payload["type"] = HORDE_EVENT;
    payload["started"] = started;
    payload["horde"] = horde;
    payload["sceneNum"] = gPlayState->sceneNum;

    for (auto& [clientId, client] : clients) {
        if (client.sceneNum == gPlayState->sceneNum && client.online && client.isSaveLoaded && !client.self) {
            payload["targetClientId"] = clientId;
            SendJsonToRemote(payload);
        }
    }
}

void Anchor::HandlePacket_HordeEvent(nlohmann::json payload) {
    if (!IsSaveLoaded() || gPlayState == NULL || payload.value("sceneNum", (s16)-1) != gPlayState->sceneNum) {
        return;
    }
    int32_t horde = payload.value("horde", 1);
    if (payload.value("started", false)) {
        Notification::Emit({
            .prefix = "Horde night",
            .message = horde > 1 ? fmt::format("#{}. The dead are restless.", horde) : "The dead are restless.",
            .remainingTime = 6.0f,
        });
        Sfx_PlaySfxCentered(NA_SE_EN_REDEAD_AIM);
    } else {
        Notification::Emit({
            .prefix = "Dawn",
            .message = "The horde retreats.",
            .remainingTime = 5.0f,
        });
    }
}
