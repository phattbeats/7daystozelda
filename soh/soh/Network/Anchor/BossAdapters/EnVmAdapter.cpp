#include "soh/Network/Anchor/BossAdapters/ActorSyncAdapter.h"
#include "soh/Network/Anchor/EnemySync.h"
// Pull the C++ side of global.h in under proper linkage before the extern "C"
// overlay header includes it (BossRush.cpp pattern).
#include "soh/OTRGlobals.h"

extern "C" {
#include "functions.h"
#include "macros.h"
#include "variables.h"
#include "src/overlays/actors/ovl_En_Vm/z_en_vm.h"
}

/**
 * Beamos (EN_VM, PHA-4179).
 *
 * The laser is pure Update state: beamScale (length/width), beamRot (aim), the
 * head yaw, the beam-phase counter unk_260 (>= 3 draws the ground impact flame)
 * and unk_21C (3 hides the eye limb). A suppressed mirror never runs Update, so
 * it drew a Beamos with no beam. These extras carry that state; the draw
 * callbacks then place the beam from the streamed head pose and clip it against
 * the mirror's own collision. The beam's AT quad vertices already ride the
 * generic quad stream, so the beam hurts the partner exactly where it is drawn,
 * and the beam / ground-impact sounds and sparks come from EnemyFxSync.
 */

static void EnVm_SerializeExtras(Actor* actor, nlohmann::json& x) {
    EnVm* v = (EnVm*)actor;
    x["bs"] = {
        (int)(v->beamScale.x * 1000.0f),
        (int)(v->beamScale.y * 1000.0f),
        (int)(v->beamScale.z * 10.0f),
    };
    x["br"] = { v->beamRot.x, v->beamRot.y, v->beamRot.z };
    x["hy"] = v->headRotY;
    x["ph"] = v->unk_260;
    x["md"] = v->unk_21C;
    x["ts"] = v->beamTexScroll;
}

static void EnVm_DeserializeExtras(Actor* actor, const nlohmann::json& x) {
    EnVm* v = (EnVm*)actor;
    try {
        if (x.contains("bs") && x["bs"].is_array() && x["bs"].size() == 3) {
            v->beamScale.x = x["bs"][0].get<int>() / 1000.0f;
            v->beamScale.y = x["bs"][1].get<int>() / 1000.0f;
            v->beamScale.z = x["bs"][2].get<int>() / 10.0f;
        }
        if (x.contains("br") && x["br"].is_array() && x["br"].size() == 3) {
            v->beamRot.x = x["br"][0].get<int16_t>();
            v->beamRot.y = x["br"][1].get<int16_t>();
            v->beamRot.z = x["br"][2].get<int16_t>();
        }
        if (x.contains("hy")) {
            v->headRotY = x["hy"].get<int16_t>();
        }
        if (x.contains("ph")) {
            v->unk_260 = x["ph"].get<int16_t>();
        }
        if (x.contains("md")) {
            v->unk_21C = x["md"].get<int32_t>();
        }
        if (x.contains("ts")) {
            v->beamTexScroll = x["ts"].get<int16_t>();
        }
    } catch (const std::exception& ex) {
        SPDLOG_WARN("[EnemySync] EnVm extras parse error: {}", ex.what());
    }
}

void RegisterEnVmAdapter() {
    ActorSyncAdapter adapter;
    adapter.SerializeExtras = EnVm_SerializeExtras;
    adapter.DeserializeExtras = EnVm_DeserializeExtras;
    EnemySync::RegisterAdapter(ACTOR_EN_VM, adapter);
}
