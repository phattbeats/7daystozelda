#pragma once

#include <nlohmann/json.hpp>
#include <libultraship/libultraship.h>

extern "C" {
#include "z64.h"
}
#include "soh/Enhancements/game-interactor/GameInteractor.h"

// Range checks for values that arrive over the network and then index save-context arrays or shift bit masks.
// A malformed or hostile packet must be dropped here, never reach `gSaveContext.sceneFlags[n]` and friends.
namespace AnchorValidate {

// Reads payload[key] as an integer inside [lo, hi]. Returns false when the field is missing, not a number,
// or out of range; `out` is only written on success.
template <typename T> inline bool Int(const nlohmann::json& payload, const char* key, int64_t lo, int64_t hi, T& out) {
    auto it = payload.find(key);
    if (it == payload.end() || !it->is_number_integer()) {
        return false;
    }
    int64_t v = it->get<int64_t>();
    if (v < lo || v > hi) {
        return false;
    }
    out = static_cast<T>(v);
    return true;
}

// sceneNum == SCENE_ID_MAX means "global flag" (flagType picks the save array).
inline bool FlagTriple(int sceneNum, int flagType, int flag, bool allowGsToken) {
    if (sceneNum < 0 || sceneNum > SCENE_ID_MAX) {
        return false;
    }
    if (flag < 0) {
        return false;
    }
    if (sceneNum != SCENE_ID_MAX) {
        switch (flagType) {
            case FLAG_SCENE_SWITCH:
                return flag < 0x40;
            case FLAG_SCENE_TREASURE:
            case FLAG_SCENE_CLEAR:
            case FLAG_SCENE_COLLECTIBLE:
                return flag < 0x20;
            default:
                return false;
        }
    }
    switch (flagType) {
        case FLAG_EVENT_CHECK_INF:
            return (flag >> 4) < 14;
        case FLAG_ITEM_GET_INF:
            return (flag >> 4) < 4;
        case FLAG_INF_TABLE:
            return (flag >> 4) < 30;
        case FLAG_EVENT_INF:
            return (flag >> 4) < 4;
        case FLAG_RANDOMIZER_INF:
            return flag < RAND_INF_MAX;
        case FLAG_GS_TOKEN:
            return allowGsToken && ((flag & 0x1F00) >> 8) < 24;
        default:
            return false;
    }
}

} // namespace AnchorValidate
