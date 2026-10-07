#include "soh/Network/Anchor/Anchor.h"
#include "soh/Network/Anchor/PushBlockSync.h"
#include <nlohmann/json.hpp>
#include <libultraship/libultraship.h>
#include "soh/Enhancements/game-interactor/GameInteractor.h"

#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

extern "C" {
#include "variables.h"
#include "functions.h"
#include "src/overlays/actors/ovl_Obj_Oshihiki/z_obj_oshihiki.h"
extern PlayState* gPlayState;

void ObjOshihiki_SetupOnActor(ObjOshihiki* objOshihiki, PlayState* play);
void ObjOshihiki_OnScene(ObjOshihiki* objOshihiki, PlayState* play);
void ObjOshihiki_OnActor(ObjOshihiki* objOshihiki, PlayState* play);
void ObjOshihiki_SetupPush(ObjOshihiki* objOshihiki, PlayState* play);
}

/**
 * PUSH_BLOCK / PUSH_BLOCK_REQUEST
 *
 * Every client runs its own copy of a push block (ObjOshihiki), so without this each
 * player had to push the Deku Tree block under the Skulltula themselves (#4020).
 *
 * A block is identified by { sceneNum, room, params, origin } where origin is the
 * spawn position (its home before the first push). Both clients load the same room
 * data, so the key matches across machines.
 *
 * PUSH_BLOCK kind "push" { pos = home before the push, yaw, dir }: sent the frame a
 *   local push starts. The receiver snaps the block to pos if it drifted, then starts
 *   the same push, so it slides (and falls off a ledge, if it does) on its own screen.
 * PUSH_BLOCK kind "rest" { pos = home after the push }: sent when a local push (and any
 *   fall after it) comes to rest, and in answer to a PUSH_BLOCK_REQUEST. The receiver
 *   snaps the block there once it is at rest, so both screens agree even if a push was
 *   missed.
 * PUSH_BLOCK_REQUEST { sceneNum, room }: sent on room entry. Partners in that room
 *   answer with a "rest" for every block that is no longer at its origin. Leaving a room
 *   unloads its blocks back to their origins (vanilla), so only a partner still standing
 *   in the room has anything to report.
 *
 * Blocks that are busy or not spawned yet keep the packet pending for a few seconds.
 */

namespace {

constexpr u32 PENDING_TICKS = 120;
constexpr f32 SAME_POS = 1.0f;

struct BlockInfo {
    Vec3f origin;
    bool localMove; // a local push is in progress; send "rest" when it settles
};

struct PendingMove {
    s16 sceneNum;
    s16 room;
    u16 params;
    Vec3f origin;
    bool push; // "push", else "rest"
    Vec3f pos;
    s16 yaw;
    f32 dir;
    u32 expiresTick;
};

std::unordered_map<Actor*, BlockInfo> sBlocks;
std::vector<PendingMove> sPending;
u32 sTick = 0;
s16 sLastScene = -1;
s16 sLastRoom = -1;

bool NearXZ(const Vec3f& a, const Vec3f& b) {
    return fabsf(a.x - b.x) <= SAME_POS && fabsf(a.z - b.z) <= SAME_POS;
}

bool Near(const Vec3f& a, const Vec3f& b) {
    return NearXZ(a, b) && fabsf(a.y - b.y) <= SAME_POS;
}

bool AtRest(ObjOshihiki* block) {
    return block->actionFunc == ObjOshihiki_OnScene || block->actionFunc == ObjOshihiki_OnActor;
}

nlohmann::json VecJson(const Vec3f& v) {
    return nlohmann::json::array({ v.x, v.y, v.z });
}

Vec3f JsonVec(const nlohmann::json& j) {
    return Vec3f{ j[0].get<f32>(), j[1].get<f32>(), j[2].get<f32>() };
}

ObjOshihiki* FindBlock(const PendingMove& move) {
    if (gPlayState == NULL || gPlayState->sceneNum != move.sceneNum) {
        return nullptr;
    }
    for (auto& [actor, info] : sBlocks) {
        if (actor->room == move.room && (u16)actor->params == move.params && Near(info.origin, move.origin)) {
            return (ObjOshihiki*)actor;
        }
    }
    return nullptr;
}

void Snap(ObjOshihiki* block, const Vec3f& pos) {
    Actor* actor = &block->dyna.actor;
    actor->world.pos = pos;
    actor->home.pos = pos;
    actor->prevPos = pos;
    block->pushDist = 0.0f;
    block->pushSpeed = 0.0f;
    block->dyna.unk_150 = 0.0f;
    // Same as Init: settles on whatever floor is under the new spot.
    ObjOshihiki_SetupOnActor(block, gPlayState);
}

// True once the move is applied (or can never apply); false keeps it pending.
bool TryApply(const PendingMove& move) {
    ObjOshihiki* block = FindBlock(move);
    if (block == nullptr || !AtRest(block)) {
        return false;
    }
    Actor* actor = &block->dyna.actor;

    if (!move.push) {
        if (!Near(actor->home.pos, move.pos)) {
            Snap(block, move.pos);
        }
        return true;
    }

    if (!NearXZ(actor->home.pos, move.pos)) {
        Snap(block, move.pos);
    }
    block->dyna.unk_158 = move.yaw;
    actor->world.rot.y = move.yaw;
    block->yawSin = Math_SinS(move.yaw);
    block->yawCos = Math_CosS(move.yaw);
    block->direction = move.dir;
    block->dyna.unk_150 = move.dir;
    block->pushDist = 0.0f;
    block->pushSpeed = 0.0f;
    ObjOshihiki_SetupPush(block, gPlayState);
    return true;
}

void SendMove(Actor* actor, const BlockInfo& info, bool push, const Vec3f& pos, s16 yaw, f32 dir,
              int64_t onlyClientId) {
    Anchor* anchor = Anchor::Instance;
    if (anchor == nullptr || !anchor->IsSaveLoaded() || gPlayState == NULL) {
        return;
    }

    nlohmann::json payload;
    payload["type"] = Anchor::PUSH_BLOCK;
    payload["quiet"] = true;
    payload["sceneNum"] = gPlayState->sceneNum;
    payload["room"] = actor->room;
    payload["params"] = (u16)actor->params;
    payload["origin"] = VecJson(info.origin);
    payload["kind"] = push ? "push" : "rest";
    payload["pos"] = VecJson(pos);
    payload["yaw"] = yaw;
    payload["dir"] = dir;

    for (auto& [clientId, client] : anchor->clients) {
        if (client.sceneNum == gPlayState->sceneNum && client.online && client.isSaveLoaded && !client.self &&
            (onlyClientId < 0 || clientId == (uint32_t)onlyClientId)) {
            payload["targetClientId"] = clientId;
            anchor->SendJsonToRemote(payload);
        }
    }
}

void OnBlockUpdate(Actor* actor) {
    ObjOshihiki* block = (ObjOshihiki*)actor;
    auto it = sBlocks.find(actor);
    if (it == sBlocks.end()) {
        // First update after spawn: home is still the spawn spot.
        it = sBlocks.emplace(actor, BlockInfo{ actor->home.pos, false }).first;
    }
    BlockInfo& info = it->second;

    // SetupPush ran inside this update: the local player started a push. (A remote push
    // calls SetupPush from the packet handler, and Update clears the flag before we look.)
    if (block->stateFlags & PUSHBLOCK_SETUP_PUSH) {
        info.localMove = true;
        SendMove(actor, info, true, actor->home.pos, block->dyna.unk_158, block->direction, -1);
    } else if (info.localMove && (block->stateFlags & (PUSHBLOCK_SETUP_ON_SCENE | PUSHBLOCK_SETUP_ON_ACTOR))) {
        info.localMove = false;
        SendMove(actor, info, false, actor->home.pos, block->dyna.unk_158, 0.0f, -1);
    }
}

} // namespace

void Anchor::SendPacket_PushBlockRequest(int16_t room) {
    if (!IsSaveLoaded() || gPlayState == NULL) {
        return;
    }

    nlohmann::json payload;
    payload["type"] = PUSH_BLOCK_REQUEST;
    payload["quiet"] = true;
    payload["sceneNum"] = gPlayState->sceneNum;
    payload["room"] = room;

    for (auto& [clientId, client] : clients) {
        if (client.sceneNum == gPlayState->sceneNum && client.online && client.isSaveLoaded && !client.self) {
            payload["targetClientId"] = clientId;
            SendJsonToRemote(payload);
        }
    }
}

void Anchor::HandlePacket_PushBlock(nlohmann::json payload) {
    if (!IsSaveLoaded() || gPlayState == NULL) {
        return;
    }

    PendingMove move;
    move.sceneNum = payload["sceneNum"].get<s16>();
    if (move.sceneNum != gPlayState->sceneNum) {
        return;
    }
    move.room = payload["room"].get<s16>();
    move.params = payload["params"].get<u16>();
    move.origin = JsonVec(payload["origin"]);
    move.push = payload["kind"].get<std::string>() == "push";
    move.pos = JsonVec(payload["pos"]);
    move.yaw = payload["yaw"].get<s16>();
    move.dir = payload["dir"].get<f32>();
    move.expiresTick = sTick + PENDING_TICKS;

    // Later packets for the same block queue behind this one, so moves apply in order.
    for (const PendingMove& p : sPending) {
        if (p.room == move.room && p.params == move.params && Near(p.origin, move.origin)) {
            sPending.push_back(move);
            return;
        }
    }
    if (!TryApply(move)) {
        sPending.push_back(move);
    }
}

void Anchor::HandlePacket_PushBlockRequest(nlohmann::json payload) {
    if (!IsSaveLoaded() || gPlayState == NULL || payload["sceneNum"].get<s16>() != gPlayState->sceneNum) {
        return;
    }
    s16 room = payload["room"].get<s16>();
    int64_t from = payload["clientId"].get<uint32_t>();

    for (auto& [actor, info] : sBlocks) {
        ObjOshihiki* block = (ObjOshihiki*)actor;
        if (actor->room == room && AtRest(block) && !Near(actor->home.pos, info.origin)) {
            SendMove(actor, info, false, actor->home.pos, block->dyna.unk_158, 0.0f, from);
        }
    }
}

void RegisterPushBlockHooks(bool isConnected) {
    sBlocks.clear();
    sPending.clear();
    sLastScene = -1;
    sLastRoom = -1;

    COND_ID_HOOK(OnActorUpdate, ACTOR_OBJ_OSHIHIKI, isConnected,
                 [](void* refActor) { OnBlockUpdate(static_cast<Actor*>(refActor)); });
    COND_ID_HOOK(OnActorDestroy, ACTOR_OBJ_OSHIHIKI, isConnected,
                 [](void* refActor) { sBlocks.erase(static_cast<Actor*>(refActor)); });
}

void PushBlockTick() {
    sTick++;
    if (gPlayState == NULL || Anchor::Instance == nullptr || !Anchor::Instance->IsSaveLoaded()) {
        sPending.clear();
        return;
    }

    if (gPlayState->sceneNum != sLastScene) {
        sPending.clear();
    }

    // Oldest first, one move per block per frame, so a "push" starts before its "rest" is
    // looked at (and the rest then waits for the push to settle).
    std::vector<PendingMove> kept;
    std::vector<PendingMove> touched;
    for (const PendingMove& move : sPending) {
        bool behind = false;
        for (const PendingMove& t : touched) {
            if (t.room == move.room && t.params == move.params && Near(t.origin, move.origin)) {
                behind = true;
                break;
            }
        }
        touched.push_back(move);
        if (behind) {
            kept.push_back(move);
        } else if (sTick <= move.expiresTick && !TryApply(move)) {
            kept.push_back(move);
        }
    }
    sPending.swap(kept);

    s16 room = gPlayState->roomCtx.curRoom.num;
    if (gPlayState->sceneNum != sLastScene || room != sLastRoom) {
        sLastScene = gPlayState->sceneNum;
        sLastRoom = room;
        if (room >= 0) {
            Anchor::Instance->SendPacket_PushBlockRequest(room);
        }
    }
}

#ifdef __EMSCRIPTEN__
extern "C" {

// #4020 tests: every tracked block, and a push as Link's grab starts one (block index,
// world yaw, +1 push / -1 pull). The block runs its own OnScene check next frame.
EMSCRIPTEN_KEEPALIVE
const char* anchor_test_push_blocks(int index, int yaw, int dir) {
    static std::string out;
    nlohmann::json j = nlohmann::json::array();
    int i = 0;
    for (auto& [actor, info] : sBlocks) {
        ObjOshihiki* block = (ObjOshihiki*)actor;
        if (i == index && dir != 0) {
            block->dyna.unk_158 = (s16)yaw;
            block->dyna.unk_150 = (f32)dir;
        }
        j.push_back({ { "room", actor->room },
                      { "params", (u16)actor->params },
                      { "origin", VecJson(info.origin) },
                      { "pos", VecJson(actor->world.pos) },
                      { "rest", AtRest(block) } });
        i++;
    }
    j = { { "blocks", j }, { "pending", sPending.size() }, { "room", gPlayState ? gPlayState->roomCtx.curRoom.num : -1 } };
    out = j.dump();
    return out.c_str();
}

// #4020 tests: swap to a room the way a door does, then put Link at (x, y, z), so a
// test can reach a block behind locked doors.
EMSCRIPTEN_KEEPALIVE
void anchor_test_load_room(int room, double x, double y, double z) {
    if (gPlayState == NULL || gPlayState->roomCtx.status != 0 || room == gPlayState->roomCtx.curRoom.num) {
        return;
    }
    func_8009728C(gPlayState, &gPlayState->roomCtx, room);
    if (func_800973FC(gPlayState, &gPlayState->roomCtx)) {
        func_80097534(gPlayState, &gPlayState->roomCtx);
    }
    Player* player = GET_PLAYER(gPlayState);
    player->actor.world.pos = player->actor.home.pos = player->actor.prevPos = { (f32)x, (f32)y, (f32)z };
}
}
#endif
