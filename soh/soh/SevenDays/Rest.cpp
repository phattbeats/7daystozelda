#include "SevenDays.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>

extern "C" {
#include "z64.h"
#include "macros.h"
#include "variables.h"
#include "functions.h"
#include "objects/gameplay_keep/gameplay_keep.h"
extern PlayState* gPlayState;
uint8_t ResourceMgr_FileExists(const char* resName);
void func_80853080(Player* thisx, PlayState* play); // back to standing (Player_Action_Idle)
void func_80832564(PlayState* play, Player* thisx);  // drops whatever he carries
s32 Player_PutAwayHeldItem(PlayState* play, Player* thisx);
void Player_Action_Idle(Player* thisx, PlayState* play);
}

/**
 * #3962: Link uses the furniture. A on a chair or the bench sits him on it, a bed
 * puts him to sleep, the rug lays him down and the milk can is drunk from. B, the
 * stick or anything that takes Link over (a hit, a cutscene, a void-out) gets him up.
 *
 * The piece offers A with text id 0xFFFF (no textbox; Player_StartTalking parks Link
 * in csAction 1, which StartRest undoes). While he rests, VB_EXECUTE_PLAYER_ACTION_FUNC
 * skips his action and RestUpdate holds him on the seat and plays the pose instead.
 * The sitting poses are Majora's Mask's (demo_suwari: sitting with his legs hanging),
 * from the MM pack; MM's Link has OoT's skeleton, so they play on him as they are.
 * Anchor sends Link's joints, so co-op partners see the pose too.
 */

using namespace SevenDays;

// Majora's Mask Link animations (art/mm-pack/build_mm_pack.py).
static const ALIGN_ASSET(2) char gMMAnimSit[] = "__OTR__objects/7dtz_mm/anim/gPlayerAnim_demo_suwari1";
static const ALIGN_ASSET(2) char gMMAnimSitLook[] = "__OTR__objects/7dtz_mm/anim/gPlayerAnim_demo_suwari2";
static const ALIGN_ASSET(2) char gMMAnimLieDown[] = "__OTR__objects/7dtz_mm/anim/gPlayerAnim_okiagaru_wait";

static bool MMAnimsLoaded() {
    static int8_t sLoaded = -1;
    if (sLoaded < 0) {
        sLoaded = ResourceMgr_FileExists(gMMAnimSit) && ResourceMgr_FileExists(gMMAnimSitLook) &&
                  ResourceMgr_FileExists(gMMAnimLieDown);
    }
    return sLoaded == 1;
}

enum RestPhase : uint8_t { REST_OFF, REST_IN, REST_HOLD, REST_OUT };

struct RestState {
    RestPhase phase = REST_OFF;
    FurnitureUse use = USE_NONE;
    uint16_t pieceId = 0;
    uint8_t type = 0;
    Vec3f pos = {};      // where Link is held
    Vec3f exitPos = {};  // where he stands up
    s16 yaw = 0;
    int16_t scene = -1;
    int timer = 0;
    int idleLoops = 0;
    u16 health = 0;
    PlayerActionFunc action = nullptr; // his action when he sat down: another one means something took over
};
static RestState sRest;

static std::unordered_map<uint16_t, uint32_t> sMilkDay; // milk can id -> the day it was last drunk from
static uint16_t sBookLine = 0;

bool SevenDays::Resting() {
    return sRest.phase != REST_OFF;
}

// MARK: - Poses

static LinkAnimationHeader* HoldAnim() {
    switch (sRest.use) {
        case USE_SIT:
            if (MMAnimsLoaded()) {
                return (LinkAnimationHeader*)gMMAnimSit;
            }
            return (LinkAnimationHeader*)gPlayerAnim_link_normal_hip_down_free; // sat down hard
        case USE_SLEEP:
            return (LinkAnimationHeader*)gPlayerAnim_clink_op3_wait1; // the opening's sleep in bed
        case USE_LIE:
            if (MMAnimsLoaded()) {
                return (LinkAnimationHeader*)gMMAnimLieDown;
            }
            return (LinkAnimationHeader*)gPlayerAnim_link_normal_back_downB;
        case USE_DRINK:
            return (LinkAnimationHeader*)gPlayerAnim_link_bottle_drink_demo_wait;
        default:
            return nullptr;
    }
}

static void PlayLoop(Player* player, PlayState* play, LinkAnimationHeader* anim, f32 morph) {
    LinkAnimation_Change(play, &player->skelAnime, anim, 1.0f, 0.0f, Animation_GetLastFrame(anim), ANIMMODE_LOOP,
                         morph);
}

static void PlayOnce(Player* player, PlayState* play, LinkAnimationHeader* anim, f32 morph) {
    LinkAnimation_Change(play, &player->skelAnime, anim, 1.0f, 0.0f, Animation_GetLastFrame(anim), ANIMMODE_ONCE,
                         morph);
}

// A point in a piece's frame (its +z is away from Link as he placed it) in the world.
static Vec3f PiecePoint(Actor* piece, f32 lx, f32 ly, f32 lz) {
    s16 rot = piece->shape.rot.y;
    f32 c = Math_CosS(rot), s = Math_SinS(rot);
    return { piece->world.pos.x + lx * c + lz * s, piece->world.pos.y + ly, piece->world.pos.z - lx * s + lz * c };
}

// MARK: - Start and stop

bool SevenDays::StartRest(Actor* piece, uint8_t type) {
    PlayState* play = gPlayState;
    if (play == nullptr || piece == nullptr || Resting()) {
        return false;
    }
    Player* player = GET_PLAYER(play);
    FurnitureUse use = FurnitureUseOf(type);
    const Placeable* p = FindPlaceable((uint16_t)piece->params);
    if (p == nullptr || (use != USE_SIT && use != USE_SLEEP && use != USE_LIE && use != USE_DRINK)) {
        return false;
    }

    // Undo the talk hold the A press started (Player_StartTalking with text id 0xFFFF).
    player->csAction = 0;
    player->prevCsAction = 0;
    player->csActor = nullptr;
    player->cv.haltActorsDuringCsAction = false;
    player->talkActor = nullptr;
    player->focusActor = nullptr;
    player->stateFlags1 &= ~(PLAYER_STATE1_TALKING | PLAYER_STATE1_IN_CUTSCENE);
    piece->flags &= ~ACTOR_FLAG_TALK;

    const PlaceableInfo& info = GetPlaceableInfo(type);
    f32 seat = (f32)FurnitureSeatHeight(type);
    sRest = {};
    sRest.use = use;
    sRest.type = type;
    sRest.pieceId = p->id;
    sRest.scene = play->sceneNum;
    switch (use) {
        case USE_SIT: {
            // On the seat facing out (-z); on the bench, wherever along it Link stood.
            f32 lx = 0.0f;
            if (type == PLACEABLE_BENCH) {
                f32 dx = player->actor.world.pos.x - piece->world.pos.x, dz = player->actor.world.pos.z - piece->world.pos.z;
                f32 c = Math_CosS(piece->shape.rot.y), s = Math_SinS(piece->shape.rot.y);
                lx = std::clamp(dx * c - dz * s, -(f32)info.halfX + 16.0f, (f32)info.halfX - 16.0f);
            }
            sRest.pos = PiecePoint(piece, lx, seat, 2.0f);
            sRest.exitPos = PiecePoint(piece, lx, 0.0f, -(info.halfZ + 14.0f));
            sRest.yaw = piece->shape.rot.y + 0x8000;
            break;
        }
        case USE_SLEEP:
            // On the mattress, head towards the bed's head (+z); he gets up on its left.
            sRest.pos = PiecePoint(piece, 0.0f, seat, 0.0f);
            sRest.exitPos = PiecePoint(piece, -(info.halfX + 14.0f), 0.0f, 0.0f);
            sRest.yaw = piece->shape.rot.y;
            break;
        case USE_LIE:
            sRest.pos = PiecePoint(piece, 0.0f, seat, 0.0f);
            sRest.exitPos = sRest.pos;
            sRest.yaw = piece->shape.rot.y;
            break;
        case USE_DRINK: {
            uint32_t today = CurrentDay();
            auto it = sMilkDay.find(p->id);
            if (it != sMilkDay.end() && it->second == today) {
                return false; // the piece shows TEXT_MILK_EMPTY instead (Placeables.cpp)
            }
            sMilkDay[p->id] = today;
            // In front of the can, facing it.
            sRest.pos = player->actor.world.pos;
            sRest.exitPos = sRest.pos;
            sRest.yaw = Math_Vec3f_Yaw(&player->actor.world.pos, &piece->world.pos);
            break;
        }
        default:
            break;
    }

    func_80832564(play, player); // put down whatever he carries
    Player_PutAwayHeldItem(play, player);
    func_80853080(player, play); // a clean standing state to come back to
    sRest.action = player->actionFunc;
    sRest.health = gSaveContext.health;
    sRest.phase = REST_IN;
    if (use == USE_DRINK) {
        PlayOnce(player, play, (LinkAnimationHeader*)gPlayerAnim_link_bottle_drink_demo_start, -6.0f);
    } else {
        PlayLoop(player, play, HoldAnim(), -8.0f);
        sRest.phase = REST_HOLD;
    }
    player->actor.speedXZ = 0.0f;
    player->actor.velocity = { 0.0f, 0.0f, 0.0f };
    return true;
}

static void StopRest(Player* player, PlayState* play, bool standUp) {
    if (standUp) {
        // Back on his feet beside the piece, on whatever floor is there.
        Vec3f probe = { sRest.exitPos.x, sRest.exitPos.y + 60.0f, sRest.exitPos.z };
        CollisionPoly* poly = nullptr;
        s32 bgId = BGCHECK_SCENE;
        f32 floorY = BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, &probe);
        Vec3f at = sRest.exitPos;
        if (floorY > BGCHECK_Y_MIN && fabsf(floorY - at.y) < 80.0f) {
            at.y = floorY;
        }
        player->actor.world.pos = player->actor.prevPos = player->actor.home.pos = at;
        player->fallStartHeight = (s16)at.y;
        func_80853080(player, play);
    }
    sRest = {};
}

// MARK: - Every frame, instead of Link's action

static void Heal(PlayState* play, s16 amount) {
    if (gSaveContext.health < gSaveContext.healthCapacity) {
        Health_ChangeBy(play, amount);
    }
}

static void RestUpdate(Player* player, PlayState* play, Input* input) {
    // Something else took Link over (a hit, a cutscene, a void-out), or he is somewhere else.
    bool pieceGone = sRest.use != USE_DRINK && SpawnedPlaceableActor(sRest.pieceId) == nullptr; // packed up or broken
    if (player->actionFunc != sRest.action || play->sceneNum != sRest.scene || gSaveContext.health == 0 || pieceGone) {
        StopRest(player, play, player->actionFunc == sRest.action && play->sceneNum == sRest.scene);
        return;
    }
    bool hurt = gSaveContext.health < sRest.health;
    sRest.health = gSaveContext.health;
    bool getUp = hurt || CHECK_BTN_ANY(input->press.button, BTN_B) ||
                 (input->rel.stick_x * input->rel.stick_x + input->rel.stick_y * input->rel.stick_y > 40 * 40);

    bool done = LinkAnimation_Update(play, &player->skelAnime);
    sRest.timer++;
    switch (sRest.use) {
        case USE_SIT:
            if (getUp) {
                StopRest(player, play, true);
                return;
            }
            // Now and then he looks around.
            if (done && MMAnimsLoaded()) {
                bool looking = player->skelAnime.animation == (void*)gMMAnimSitLook;
                if (looking || ++sRest.idleLoops >= 4) {
                    sRest.idleLoops = 0;
                    LinkAnimationHeader* next =
                        looking ? (LinkAnimationHeader*)gMMAnimSit : (LinkAnimationHeader*)gMMAnimSitLook;
                    PlayLoop(player, play, next, -6.0f);
                }
            }
            break;
        case USE_SLEEP:
        case USE_LIE:
            if (getUp) {
                StopRest(player, play, true);
                return;
            }
            // A good rest: a quarter heart every couple of seconds in bed, slower on the rug.
            if (sRest.timer % (sRest.use == USE_SLEEP ? 40 : 80) == 0) {
                Heal(play, 4);
            }
            break;
        case USE_DRINK:
            // Start, a few gulps, then the end; Lon Lon milk's five hearts, like a bottle of it.
            if (sRest.phase == REST_IN && done) {
                PlayLoop(player, play, (LinkAnimationHeader*)gPlayerAnim_link_bottle_drink_demo_wait, 0.0f);
                sRest.phase = REST_HOLD;
                sRest.timer = 0;
            } else if (sRest.phase == REST_HOLD && sRest.timer >= 30) {
                Heal(play, 0x50);
                PlayOnce(player, play, (LinkAnimationHeader*)gPlayerAnim_link_bottle_drink_demo_end, 0.0f);
                sRest.phase = REST_OUT;
            } else if (sRest.phase == REST_OUT && done) {
                StopRest(player, play, true);
                return;
            }
            if (hurt) {
                StopRest(player, play, true);
                return;
            }
            break;
        default:
            StopRest(player, play, true);
            return;
    }

    // Held in place: no falling, no sliding, no turning.
    player->actor.world.pos = player->actor.prevPos = sRest.pos;
    player->actor.speedXZ = 0.0f;
    player->actor.velocity = { 0.0f, 0.0f, 0.0f };
    player->actor.shape.rot.y = player->actor.world.rot.y = player->yaw = sRest.yaw;
    player->fallStartHeight = (s16)sRest.pos.y;
}

void SevenDays::RestRegisterHooks(bool enabled) {
    COND_VB_SHOULD(VB_EXECUTE_PLAYER_ACTION_FUNC, enabled, {
        Player* player = va_arg(args, Player*);
        Input* input = va_arg(args, Input*);
        if (!Resting() || gPlayState == nullptr || player != GET_PLAYER(gPlayState)) {
            return;
        }
        *should = false;
        RestUpdate(player, gPlayState, input);
    });
    // After Player_UpdateCommon's own movement: he stays exactly on the seat.
    COND_HOOK(OnPlayerUpdate, enabled, []() {
        if (!Resting() || gPlayState == nullptr) {
            return;
        }
        Player* player = GET_PLAYER(gPlayState);
        player->actor.world.pos = sRest.pos;
        player->actor.velocity.x = player->actor.velocity.y = player->actor.velocity.z = 0.0f;
    });
    COND_HOOK(OnSceneInit, enabled, [](int16_t sceneNum) { sRest = {}; });
    COND_HOOK(OnLoadGame, enabled, [](int32_t fileNum) {
        sRest = {};
        sMilkDay.clear();
    });
}

// MARK: - The bookshelf's lines

// clang-format off
static const char* sBookLines[] = {
    "\"Keeping the Dead at Bay,\" chapter one:^Walls first, beds second. A tired guard is a dead guard.",
    "A field journal, in a shaky hand:^\"They come from the dark side of the field. Torches kept the east wall clear all night.\"",
    "\"The Hylian Carpenter's Almanac\":^A floor on the walls makes a second storey. Up high, the dead can't reach you.",
    "A Kokiri picture book. A little Deku Baba is wearing a crown.^Someone has drawn a sword through it.",
    "\"Milk and Its Virtues,\" by Talon of Lon Lon Ranch:^A long drink of fresh milk will put you back on your feet. Once a day, mind!",
    "A traveller's notes on Termina:^\"The folk there dread a falling moon. Here it's the dead in the grass. Every land has its night.\"",
};
// clang-format on
constexpr uint16_t BOOK_LINE_COUNT = sizeof(sBookLines) / sizeof(sBookLines[0]);

void SevenDays::OnBookRead() {
    sBookLine = (uint16_t)((sBookLine + 1) % BOOK_LINE_COUNT);
}

const char* SevenDays::BookLine() {
    return sBookLines[sBookLine % BOOK_LINE_COUNT];
}

bool SevenDays::MilkCanEmpty(uint16_t id) {
    auto it = sMilkDay.find(id);
    return it != sMilkDay.end() && it->second == CurrentDay();
}

// MARK: - Test hooks

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
extern "C" {
// The animation Link is playing, when it is a resource path (all of Link's are).
static const char* AnimName(Player* player) {
    const char* a = player != nullptr ? (const char*)player->skelAnime.animation : nullptr;
    return a != nullptr && strncmp(a, "__OTR__", 7) == 0 ? a : "";
}

// #3962 tests: the rest state (phase, use, piece, health, where Link is held, his animation).
EMSCRIPTEN_KEEPALIVE
const char* sevendays_test_rest_state() {
    static std::string out;
    Player* player = gPlayState != nullptr ? GET_PLAYER(gPlayState) : nullptr;
    out = fmt::format("{{\"phase\":{},\"use\":{},\"piece\":{},\"health\":{},\"pos\":[{:.1f},{:.1f},{:.1f}],"
                      "\"yaw\":{},\"anim\":\"{}\"}}",
                      (int)sRest.phase, (int)sRest.use, sRest.pieceId, gSaveContext.health,
                      player ? player->actor.world.pos.x : 0.0f, player ? player->actor.world.pos.y : 0.0f,
                      player ? player->actor.world.pos.z : 0.0f, player ? player->actor.shape.rot.y : 0,
                      AnimName(player));
    return out.c_str();
}

EMSCRIPTEN_KEEPALIVE
int sevendays_test_rest_on(int id) {
    if (gPlayState == nullptr) {
        return 0;
    }
    Actor* piece = SpawnedPlaceableActor((uint16_t)id);
    const Placeable* p = FindPlaceable((uint16_t)id);
    return piece != nullptr && p != nullptr && SevenDays::StartRest(piece, p->type) ? 1 : 0;
}
}
#endif
