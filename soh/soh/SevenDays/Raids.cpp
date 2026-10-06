#include "SevenDays.h"
#include "SevenDaysNet.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/custom-message/CustomMessageManager.h"
#include "soh/ShipInit.hpp"
#include "soh/Network/Anchor/Anchor.h"
#include "soh/Network/Anchor/EnemySync.h"
#include "soh/Network/Anchor/HordeNight.h"
#include "soh/Notification/Notification.h"
#include "soh/Enhancements/mods.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_map>

extern "C" {
#include "z64.h"
#include "macros.h"
#include "variables.h"
#include "functions.h"
extern PlayState* gPlayState;
}

/**
 * M6: raids on the base. Extends HordeNight (soh/Network/Anchor/HordeNight.cpp),
 * which stands down while gSevenDays.Raids is on and lends its cap, living-player
 * set and generalized ShambleToward.
 *
 * Who decides what:
 *   - The room owner keeps the night schedule and story beats in BaseState
 *     (counters: daysSurvived, hordeNightsSurvived, nextRaidDay, story,
 *     nightsFailed), counts dawns on its own clock, settles raids on an empty base,
 *     applies the losing-a-night penalty and pays survivors.
 *   - The scene's enemy authority (EnemySync::IsLocalAuthority; solo = always)
 *     runs the wave: gamestage budget, ring spawns out of every player's view,
 *     routing to the workbench, the stuck check and barricade damage (BASE_HP to
 *     the owner, which sequences BASE_DELTA). Its HORDE_EVENT carries the night,
 *     gamestage, wave status and its clock so same-scene peers hold with it.
 *
 * Packets (JSON):
 *   HORDE_EVENT   authority -> same-scene peers  raid, status, night, gamestage, budget, alive, dayTime, types
 *   RAID_STORY    any client -> owner            event: sword | gohma | duskCleared
 *   RAID_SCRIPT   owner -> room                  to (dayTime), why: dusk | raid | dawn
 *   RAID_REPORT   authority -> owner             scene, base (this scene holds the base): a raid is fought there
 *   RAID_LOST     any client -> owner            everyone went down during a raid
 *   RAID_NOTICE   owner -> room                  prefix, message, navi[] (dawn reports, penalties)
 *
 * The raid clock (each client, for its own scene): after the prologue, frozen
 * outdoor scenes get about half the field's time speed; dungeons and interiors
 * stay frozen; a raid night holds the clock until the wave is cleared or about
 * four minutes pass. The prologue's nights are scripted (RAID_SCRIPT), the way
 * the Sun's Song sets the time.
 */

namespace SevenDays {

using Net::ActingOwner;
using Net::Broadcast;
using Net::Connected;
using Net::IsOwner;
using Net::Now;
using Net::OwnId;
using Net::SendTo;

static const std::string RAID_STORY = "RAID_STORY";
static const std::string RAID_SCRIPT = "RAID_SCRIPT";
static const std::string RAID_REPORT = "RAID_REPORT";
static const std::string RAID_LOST = "RAID_LOST";
static const std::string RAID_NOTICE = "RAID_NOTICE";
static const std::string HORDE_EVENT_TYPE = "HORDE_EVENT";

constexpr uint16_t DUSK_TIME = 0xC400; // about 18:25: night (> 0xC000)
constexpr uint16_t DAWN_TIME = 0x4800; // about 6:45: day (>= 0x4555)
constexpr uint16_t SCRIPT_SPEED = 400; // the Sun's Song's fast-forward speed
constexpr f32 RETARGET_DIST = 300.0f;
constexpr f32 RING_MIN = 600.0f, RING_MAX = 900.0f;
constexpr double STUCK_WINDOW = 5.0;
constexpr f32 STUCK_GAIN = 20.0f;
constexpr double DUSK_MAX_SECONDS = 120.0;
// PHA-4006: the torch ward. The ritual: the ring's torches turn blue one after another,
// sweeping round from Link's side, then the red night clears.
constexpr double WARD_CHECK_SECONDS = 2.0;
constexpr double WARD_SWEEP_SECONDS = 4.0;
constexpr double WARD_REMIND_SECONDS = 120.0;

// MARK: - Settings

static int32_t Interval() {
    return (int32_t)RaidInterval();
}
static double HoldSeconds() {
    return (double)std::max(0, CVarGetInteger(CVAR_SEVEN_DAYS("RaidHoldSeconds"), 240));
}
static uint16_t RaidClockSpeed() {
    return (uint16_t)std::clamp(CVarGetInteger(CVAR_SEVEN_DAYS("RaidClockSpeed"), 5), 0, 30);
}

// MARK: - Enemies and the budget

struct RaiderDef {
    int16_t actorId;
    int16_t params;
    uint8_t cost;
    uint8_t enemy; // RaidEnemy (Navi's per-type line)
    f32 drainPerSecond; // barricade HP per second while pressing against one
};
// Costs from the spec: Stalchild 1, Wolfos 3, ReDead 4, Gibdo 5 (Keese 1, big Stalchild 2).
// ReDead/Gibdo params as HordeNight: TEMP switch flag 0x3F, standing ReDead / Gibdo.
static const RaiderDef kStalchild = { ACTOR_EN_SKB, 0, 1, RAIDENEMY_STALCHILD, 3.0f };
static const RaiderDef kBigStalchild = { ACTOR_EN_SKB, 10, 2, RAIDENEMY_STALCHILD, 5.0f };
static const RaiderDef kKeese = { ACTOR_EN_FIREFLY, 2 /* KEESE_NORMAL_FLY */, 1, RAIDENEMY_KEESE, 1.5f };
static const RaiderDef kWolfos = { ACTOR_EN_WF, (int16_t)0xFF00, 3, RAIDENEMY_WOLFOS, 6.0f };
static const RaiderDef kRedead = { ACTOR_EN_RD, 0x3F01, 4, RAIDENEMY_REDEAD, 8.0f };
static const RaiderDef kGibdo = { ACTOR_EN_RD, 0x3FFE, 5, RAIDENEMY_GIBDO, 10.0f };

struct Weighted {
    const RaiderDef* def;
    int weight;
};

static std::vector<Weighted> Composition(int32_t raidNo, bool prologue) {
    if (prologue) {
        // Gohma's night is a new player's first raid: Stalchildren only (Keese reach
        // a player anywhere, and Deku Babas are rooted and can't march).
        return { { &kStalchild, 100 } };
    }
    if (raidNo <= 2) {
        // The second raid ramps up with Keese and the odd Wolfos; ReDeads wait a night.
        return { { &kStalchild, 60 }, { &kKeese, 25 }, { &kWolfos, 15 } };
    }
    if (raidNo == 3) {
        return { { &kStalchild, 50 }, { &kKeese, 15 }, { &kRedead, 25 }, { &kWolfos, 10 } };
    }
    if (raidNo == 4) {
        return { { &kStalchild, 35 }, { &kKeese, 10 }, { &kRedead, 25 }, { &kGibdo, 12 }, { &kWolfos, 18 } };
    }
    return { { &kStalchild, 25 }, { &kBigStalchild, 10 }, { &kKeese, 10 },
             { &kRedead, 25 },    { &kGibdo, 15 },        { &kWolfos, 15 } };
}

static bool IsRaiderType(int16_t id) {
    return id == ACTOR_EN_SKB || id == ACTOR_EN_RD || id == ACTOR_EN_WF || id == ACTOR_EN_FIREFLY;
}

static f32 DrainFor(Actor* a) {
    switch (a->id) {
        case ACTOR_EN_SKB:
            return a->params >= 10 ? kBigStalchild.drainPerSecond : kStalchild.drainPerSecond;
        case ACTOR_EN_FIREFLY:
            return kKeese.drainPerSecond;
        case ACTOR_EN_WF:
            return kWolfos.drainPerSecond;
        case ACTOR_EN_RD:
            return a->params < 0 ? kGibdo.drainPerSecond : kRedead.drainPerSecond;
    }
    return 0.0f;
}

static int CountReadyPlayers() {
    if (!Connected()) {
        return 1;
    }
    int n = 0;
    for (auto& [id, c] : Anchor::Instance->clients) {
        if (c.online && (c.self ? Anchor::Instance->IsSaveLoaded() : c.isSaveLoaded)) {
            n++;
        }
    }
    return std::max(n, 1);
}

// gamestage = 3 x dungeons cleared + heart containers + 2 x players + horde nights survived
int32_t Gamestage() {
    static const uint8_t kDungeonRewards[] = { QUEST_KOKIRI_EMERALD,    QUEST_GORON_RUBY,      QUEST_ZORA_SAPPHIRE,
                                               QUEST_MEDALLION_FOREST,  QUEST_MEDALLION_FIRE,  QUEST_MEDALLION_WATER,
                                               QUEST_MEDALLION_SPIRIT,  QUEST_MEDALLION_SHADOW };
    int dungeons = 0;
    for (uint8_t q : kDungeonRewards) {
        dungeons += CHECK_QUEST_ITEM(q) ? 1 : 0;
    }
    if (!CHECK_QUEST_ITEM(QUEST_KOKIRI_EMERALD) && Flags_GetEventChkInf(EVENTCHKINF_OBTAINED_KOKIRI_EMERALD_DEKU_TREE_DEAD)) {
        dungeons++; // the Deku Tree is dead but the emerald is still in flight
    }
    int hearts = std::max(0, gSaveContext.healthCapacity / 16 - 3);
    return 3 * dungeons + hearts + 2 * CountReadyPlayers() + (int32_t)GetBase().hordeNightsSurvived;
}

static int32_t RaidNumber() {
    const BaseState& b = GetBase();
    return (int32_t)(b.hordeNightsSurvived + b.nightsFailed) + 1;
}

bool RaidTonight() {
    const BaseState& b = GetBase();
    if (CVarGetInteger(CVAR_SEVEN_DAYS("RaidForce"), 0) != 0) {
        return true;
    }
    return b.nextRaidDay != 0 && CurrentDay() >= b.nextRaidDay;
}

uint32_t NightsUntilRaid() {
    const BaseState& b = GetBase();
    if (b.nextRaidDay == 0) {
        return UINT32_MAX;
    }
    return b.nextRaidDay > CurrentDay() ? b.nextRaidDay - CurrentDay() : 0;
}

static bool PrologueOver() {
    return (GetBase().story & STORY_FIRST_RAID) != 0;
}

// Gohma's night is easy-ish: a few Stalchildren whatever the gamestage. The second
// raid buys at 1x gamestage, the third on at the spec's 1.5x.
static int32_t WaveBudget(int32_t gamestage, int32_t raidNo, bool prologue) {
    if (prologue) {
        return 2 + CountReadyPlayers();
    }
    return raidNo <= 2 ? gamestage : (int32_t)(gamestage * 1.5f);
}

uint32_t RaidInterval() {
    uint32_t picked = GetBase().raidInterval;
    return picked != 0 ? picked : (uint32_t)std::max(1, CVarGetInteger(CVAR_SEVEN_DAYS("RaidInterval"), 3));
}

void RequestRaidInterval(uint32_t days) {
    if (!Net::IsOwner() || days == 0) {
        return;
    }
    BaseState& b = Net::MutableBase();
    b.raidInterval = days;
    // A shorter interval can pull the next raid in; a longer one starts after it.
    if (b.nextRaidDay > CurrentDay() + days) {
        b.nextRaidDay = CurrentDay() + days;
    }
    Net::CommitBase();
}

// MARK: - Session state

enum WaveStatus : uint8_t {
    WAVE_NONE,
    WAVE_INCOMING, // announced, first spawns pending
    WAVE_ASSAULT,  // spawning / fighting
    WAVE_CLEARED,  // budget spent and every raider down
    WAVE_WARDED,   // PHA-4006: held back by the torch ring, nothing spawns while it burns
};
static const char* StatusName(uint8_t s) {
    switch (s) {
        case WAVE_INCOMING:
            return "incoming";
        case WAVE_ASSAULT:
            return "assault";
        case WAVE_CLEARED:
            return "cleared";
        case WAVE_WARDED:
            return "warded";
    }
    return "none";
}
static uint8_t StatusFromName(const std::string& s) {
    if (s == "incoming") {
        return WAVE_INCOMING;
    }
    if (s == "assault") {
        return WAVE_ASSAULT;
    }
    if (s == "cleared") {
        return WAVE_CLEARED;
    }
    if (s == "warded") {
        return WAVE_WARDED;
    }
    return WAVE_NONE;
}

enum RaidKind : uint8_t {
    KIND_NONE,
    KIND_DUSK, // the Kokiri Sword's dusk: 2-3 Stalchildren near the bridge, a scare
    KIND_RAID, // a raid night
};

struct Director {
    bool active = false;
    uint8_t kind = KIND_NONE;
    int16_t scene = -1;
    uint32_t night = 0;
    int32_t gamestage = 0;
    int32_t budget = 0;
    int32_t spent = 0;
    int32_t raidNo = 0;
    bool prologue = false;
    bool baseHere = false;
    Vec3f center = {};
    uint8_t status = WAVE_NONE;
    double startedAt = 0;
    double lastStatusSent = -100;
    double clearedAt = 0;
    int32_t spawnTimer = 0;
    int32_t spawned = 0;
    int32_t relocated = 0;
    uint32_t types = 0; // RaidEnemy bits seen this wave
    std::vector<Vec3f> samples;
    bool storyCleared = false; // the dusk's "cleared" sent to the owner
    double lastWardCheck = 0;
};
static Director sDir;
static std::vector<Vec3f> sSpawnLog; // tests: where this wave's raiders came up

struct Tracker {
    Vec3f lastPos = {};
    f32 lastGoalDist = 0.0f;
    double windowStart = 0;
};
static std::unordered_map<Actor*, Tracker> sTrack;
static std::unordered_map<uint16_t, f32> sPendingDrain; // placeable id -> HP owed

// A peer's view of the wave in its scene (from the authority's HORDE_EVENT).
struct PeerWave {
    int16_t scene = -1;
    uint8_t status = WAVE_NONE;
    double heardAt = -100;
    uint32_t night = 0;
    int32_t gamestage = 0;
};
static PeerWave sPeer;

// PHA-4006: this client's view of the ward's ritual (authority and peers alike).
struct WardRitual {
    double since = -1;      // when the ring closed here (-1: not warded)
    Vec3f center = {};      // the workbench
    s16 fromYaw = 0;        // the sweep starts on Link's side of the ring
    bool announced = false; // the sweep finished: chime and notice
    double remindedAt = 0;
};
static WardRitual sWard;

// The clock.
static uint16_t sVanillaIncrement = 0; // the scene's own time speed (envCtx.timeIncrement at load)
static int32_t sLastWrittenIncrement = -1;
static bool sTransition = false;
static uint16_t sTransitionTo = 0;
static int sPrevNight = -1; // owner's dawn/dusk edges (-1: not sampled yet)

// Owner, per night (not saved: a reload mid-night just forgets them).
// Tonight's record (fought at the base, lost, gamestage) lives in BaseState, keyed by
// the day, so it is saved and reaches whoever is owner at dawn (PHA-3935).
static BaseState& Night() {
    BaseState& b = Net::MutableBase();
    if (b.nightDay != CurrentDay()) {
        b.nightDay = CurrentDay();
        b.nightFought = false;
        b.nightFailed = false;
        b.nightGamestage = 0;
        b.nightWarded = false;
    }
    return b;
}

// Story triggers (any client).
static double sFreeSince = -1;
static double sLastStorySent = -100;
static int sLastGameOver = 0;

// MARK: - Helpers

static bool RaidAuthority() {
    if (!Connected()) {
        return true;
    }
    return EnemySync::SyncEnabled() && EnemySync::MirroringEnabled() && EnemySync::IsLocalAuthority();
}

static bool PlayerFree() {
    if (gPlayState == nullptr) {
        return false;
    }
    Player* player = GET_PLAYER(gPlayState);
    return player != nullptr && gPlayState->msgCtx.msgMode == MSGMODE_NONE && !Player_InCsMode(gPlayState) &&
           gPlayState->csCtx.state == CS_STATE_IDLE &&
           !(player->stateFlags1 & (PLAYER_STATE1_DEAD | PLAYER_STATE1_TALKING | PLAYER_STATE1_GETTING_ITEM |
                                    PLAYER_STATE1_IN_CUTSCENE)) &&
           gPlayState->transitionTrigger == TRANS_TRIGGER_OFF && gPlayState->transitionMode == TRANS_MODE_OFF &&
           gPlayState->pauseCtx.state == 0;
}

static void Emit(const std::string& prefix, const std::string& message, float secs = 5.0f) {
    Notification::Emit({ .prefix = prefix, .message = message, .remainingTime = secs });
}

static bool BaseInThisScene(Vec3f* outCenter) {
    if (gPlayState == nullptr) {
        return false;
    }
    const BaseCenter& c = GetBase().center[CurrentEraNow()];
    if (!c.valid || c.scene != gPlayState->sceneNum) {
        return false;
    }
    if (outCenter != nullptr) {
        *outCenter = { c.pos[0], c.pos[1], c.pos[2] };
    }
    return true;
}

// Is this point in some living player's view? Local Link: the camera's cone with
// line of sight. Other players: their facing cone (we don't have their camera).
static bool SeenByAnyPlayer(const Vec3f& point) {
    if (gPlayState == nullptr) {
        return true;
    }
    Vec3f target = { point.x, point.y + 40.0f, point.z };
    auto seenFrom = [&](Vec3f eye, f32 dirX, f32 dirY, f32 dirZ, f32 cosCone, f32 range) {
        f32 vx = target.x - eye.x, vy = target.y - eye.y, vz = target.z - eye.z;
        f32 d = sqrtf(vx * vx + vy * vy + vz * vz);
        if (d > range) {
            return false;
        }
        if (d > 1.0f && (vx * dirX + vy * dirY + vz * dirZ) / d < cosCone) {
            return false;
        }
        Vec3f hit;
        CollisionPoly* poly = nullptr;
        return !BgCheck_AnyLineTest1(&gPlayState->colCtx, &eye, &target, &hit, &poly, true);
    };
    Player* local = GET_PLAYER(gPlayState);
    Camera* cam = GET_ACTIVE_CAM(gPlayState);
    if (cam != nullptr && local != nullptr && !(local->stateFlags1 & PLAYER_STATE1_DEAD)) {
        f32 fx = cam->at.x - cam->eye.x, fy = cam->at.y - cam->eye.y, fz = cam->at.z - cam->eye.z;
        f32 fl = sqrtf(fx * fx + fy * fy + fz * fz);
        if (fl > 0.01f && seenFrom(cam->eye, fx / fl, fy / fl, fz / fl, 0.64f /* ~50 degrees */, 1800.0f)) {
            return true;
        }
    }
    for (Actor* p : HordeNight::LivingPlayers()) {
        if (local != nullptr && p == &local->actor) {
            continue;
        }
        Vec3f eye = { p->world.pos.x, p->world.pos.y + 50.0f, p->world.pos.z };
        if (seenFrom(eye, Math_SinS(p->shape.rot.y), 0.0f, Math_CosS(p->shape.rot.y), 0.5f /* 60 degrees */,
                     1500.0f)) {
            return true;
        }
    }
    return false;
}

// A floor point near (x, z): raycast down from above refY, solid scene floor, not
// water, not too steep, not too far above or below refY.
static bool FloorAt(f32 x, f32 z, f32 refY, f32 maxRise, Vec3f* out) {
    Player* local = GET_PLAYER(gPlayState);
    Vec3f probe = { x, refY + maxRise, z };
    CollisionPoly* poly = nullptr;
    s32 bgId = BGCHECK_SCENE;
    f32 floorY = BgCheck_EntityRaycastFloor4(&gPlayState->colCtx, &poly, &bgId, &local->actor, &probe);
    if (floorY <= BGCHECK_Y_MIN || poly == nullptr || bgId != BGCHECK_SCENE || fabsf(floorY - refY) > maxRise) {
        return false;
    }
    if (COLPOLY_GET_NORMAL(poly->normal.y) < 0.75f) {
        return false;
    }
    f32 waterY;
    WaterBox* waterBox;
    if (WaterBox_GetSurface1(gPlayState, &gPlayState->colCtx, x, z, &waterY, &waterBox) && waterY > floorY - 5.0f) {
        return false;
    }
    if (SurfaceType_IsWallDamage(&gPlayState->colCtx, poly, bgId) ||
        func_80041EA4(&gPlayState->colCtx, poly, bgId) == 12 /* void out */) {
        return false;
    }
    *out = { x, floorY, z };
    return true;
}

// PHA-3935: a torch keeps raid spawns TORCH_RADIUS away (spec, "Torch").
static bool NearTorch(const Vec3f& point) {
    for (auto& [id, actor] : SpawnedPlaceables()) {
        const Placeable* p = FindPlaceable(id);
        if (p != nullptr && p->type == PLACEABLE_TORCH && !IsRuin(*p) &&
            Math_Vec3f_DistXZ(const_cast<Vec3f*>(&point), &actor->world.pos) < TORCH_RADIUS) {
            return true;
        }
    }
    return false;
}

// Spawn points on a ring 600-900 from the workbench (or, with no base in this
// scene, around a living player), none within a torch's reach.
static void SampleRing() {
    sDir.samples.clear();
    Vec3f c = sDir.center;
    if (!sDir.baseHere) {
        auto players = HordeNight::LivingPlayers();
        if (players.empty()) {
            return;
        }
        c = players[(size_t)(Rand_ZeroOne() * players.size()) % players.size()]->world.pos;
    }
    for (int i = 0; i < 48 && sDir.samples.size() < 16; i++) {
        s16 angle = (s16)(i * (0x10000 / 48) + (s16)(Rand_ZeroOne() * 0x400));
        f32 dist = RING_MIN + Rand_ZeroOne() * (RING_MAX - RING_MIN);
        Vec3f p;
        if (FloorAt(c.x + Math_SinS(angle) * dist, c.z + Math_CosS(angle) * dist, c.y, 300.0f, &p) && !NearTorch(p)) {
            sDir.samples.push_back(p);
        }
    }
}

// An unseen sample (jittered), or false.
static bool PickSpawnPoint(Vec3f* out, const Vec3f* avoid) {
    if (sDir.samples.empty()) {
        SampleRing();
    }
    if (sDir.samples.empty()) {
        return false;
    }
    size_t n = sDir.samples.size();
    size_t start = (size_t)(Rand_ZeroOne() * n) % n;
    for (size_t k = 0; k < n; k++) {
        const Vec3f& s = sDir.samples[(start + k) % n];
        if (avoid != nullptr && Math_Vec3f_DistXZ(const_cast<Vec3f*>(&s), const_cast<Vec3f*>(avoid)) < 150.0f) {
            continue;
        }
        Vec3f p;
        if (!FloorAt(s.x + Rand_CenteredFloat(60.0f), s.z + Rand_CenteredFloat(60.0f), s.y, 120.0f, &p)) {
            p = s;
        }
        if (!SeenByAnyPlayer(p) && !NearTorch(p)) {
            *out = p;
            return true;
        }
    }
    return false;
}

// PHA-4006: every floor point of the spawn ring is within a torch's reach, so the
// dead have nowhere to come up. Checked on a grid finer than SampleRing's spread.
static bool WardComplete() {
    if (!sDir.baseHere) {
        return false;
    }
    // NearTorch's test, with the torches looked up once instead of for every grid point.
    std::vector<Vec3f> torches;
    for (auto& [id, actor] : SpawnedPlaceables()) {
        const Placeable* p = FindPlaceable(id);
        if (p != nullptr && p->type == PLACEABLE_TORCH && !IsRuin(*p)) {
            torches.push_back(actor->world.pos);
        }
    }
    if (torches.empty()) {
        return false;
    }
    const Vec3f& c = sDir.center;
    int floors = 0;
    for (int i = 0; i < 72; i++) {
        s16 angle = (s16)(i * (0x10000 / 72));
        for (f32 dist = RING_MIN; dist <= RING_MAX; dist += 50.0f) {
            Vec3f p;
            if (!FloorAt(c.x + Math_SinS(angle) * dist, c.z + Math_CosS(angle) * dist, c.y, 300.0f, &p)) {
                continue;
            }
            bool covered = false;
            for (Vec3f& t : torches) {
                covered = covered || Math_Vec3f_DistXZ(&p, &t) < TORCH_RADIUS;
            }
            if (!covered) {
                return false;
            }
            floors++;
        }
    }
    return floors > 0;
}

static void BeginRitual(const Vec3f& center) {
    sWard = {};
    sWard.since = Now();
    sWard.center = center;
    Player* player = GET_PLAYER(gPlayState);
    sWard.fromYaw = player != nullptr ? Math_Vec3f_Yaw(&sWard.center, &player->actor.world.pos) : 0;
}

bool TorchWardLit(Actor* torch) {
    if (sWard.since < 0 || gPlayState == nullptr || !RaidWardedHere()) {
        return false;
    }
    // The ring's torches: those that cover some of the spawn band.
    f32 d = Math_Vec3f_DistXZ(&sWard.center, &torch->world.pos);
    if (d < RING_MIN - TORCH_RADIUS || d > RING_MAX + TORCH_RADIUS) {
        return false;
    }
    uint16_t turn = (uint16_t)(Math_Vec3f_Yaw(&sWard.center, &torch->world.pos) - sWard.fromYaw);
    return Now() - sWard.since >= WARD_SWEEP_SECONDS * turn / 0x10000;
}

// M10: the nearest scarecrow within 400 of a raider draws it away from the workbench
// and the players. Broken scarecrows leave the piece list, so routing reverts then.
// It has to be reachable: on about the raider's level (like EngagedPlayer) and not
// behind the scene's walls or up a ledge. The base's own pieces can be in the way:
// those get broken through. Otherwise the players and the workbench stay the target.
static constexpr f32 DECOY_RANGE = 400.0f;

// Cached once per frame: the scarecrows in the scene, and each raider's answer (the
// lookup runs for every raider in RouteRaiders and again in its perception hook).
static uint32_t sDecoyFrame = UINT32_MAX;
static std::vector<Actor*> sDecoys;
static std::vector<std::pair<Actor*, Actor*>> sDecoyFor;

static void RefreshDecoys() {
    if (sDecoyFrame == gPlayState->gameplayFrames) {
        return;
    }
    sDecoyFrame = gPlayState->gameplayFrames;
    sDecoys.clear();
    sDecoyFor.clear();
    for (const Placeable& p : GetBase().placeables) {
        if (p.type != PLACEABLE_SCARECROW || IsRuin(p)) {
            continue;
        }
        Actor* actor = SpawnedPlaceableActor(p.id);
        if (actor != nullptr && actor->update != nullptr) {
            sDecoys.push_back(actor);
        }
    }
}

static bool DecoyReachable(Actor* a, Actor* decoy) {
    if (fabsf(decoy->world.pos.y - a->world.pos.y) >= 120.0f) {
        return false;
    }
    Vec3f from = { a->world.pos.x, a->world.pos.y + 30.0f, a->world.pos.z };
    Vec3f to = { decoy->world.pos.x, decoy->world.pos.y + 30.0f, decoy->world.pos.z };
    Vec3f hit;
    CollisionPoly* poly = nullptr;
    s32 bgId = BGCHECK_SCENE;
    if (!BgCheck_EntityLineTest1(&gPlayState->colCtx, &from, &to, &hit, &poly, true, false, false, true, &bgId)) {
        return true;
    }
    return bgId != BGCHECK_SCENE; // a barricade (dyna): break through it
}

static Actor* NearestDecoy(Actor* a) {
    RefreshDecoys();
    if (sDecoys.empty()) {
        return nullptr;
    }
    for (auto& [raider, decoy] : sDecoyFor) {
        if (raider == a) {
            return decoy;
        }
    }
    Actor* best = nullptr;
    f32 bestD = DECOY_RANGE;
    for (Actor* actor : sDecoys) {
        f32 d = Math_Vec3f_DistXZ(&a->world.pos, &actor->world.pos);
        if (d < bestD && DecoyReachable(a, actor)) {
            best = actor;
            bestD = d;
        }
    }
    sDecoyFor.emplace_back(a, best);
    return best;
}

// A player within 300 on (about) the same level: the raider fights them instead.
// One on a ledge above or below can't be reached by walking straight at them.
static Actor* EngagedPlayer(Actor* a) {
    f32 d;
    Actor* p = HordeNight::NearestLivingPlayer(a->world.pos, &d);
    if (p != nullptr && d < RETARGET_DIST && fabsf(p->world.pos.y - a->world.pos.y) < 120.0f) {
        return p;
    }
    return nullptr;
}

static Vec3f GoalFor(Actor* a) {
    if (sDir.baseHere) {
        return sDir.center;
    }
    Actor* p = HordeNight::NearestLivingPlayer(a->world.pos, nullptr);
    return p != nullptr ? p->world.pos : a->world.pos;
}

static int CountRaiders() {
    int n = 0;
    for (Actor* a = gPlayState->actorCtx.actorLists[ACTORCAT_ENEMY].head; a != nullptr; a = a->next) {
        n += IsRaiderType(a->id) && a->colChkInfo.health > 0 && a->update != nullptr;
    }
    return n;
}

// MARK: - HORDE_EVENT (extended)

static void SendWaveEvent(const char* statusOverride = nullptr) {
    if (!Connected() || gPlayState == nullptr) {
        return;
    }
    nlohmann::json payload;
    payload["type"] = HORDE_EVENT_TYPE;
    payload["raid"] = true;
    payload["started"] = sDir.status != WAVE_CLEARED;
    payload["horde"] = sDir.raidNo;
    payload["night"] = sDir.night;
    payload["gamestage"] = sDir.gamestage;
    payload["budget"] = sDir.budget;
    payload["budgetLeft"] = std::max(0, sDir.budget - sDir.spent);
    payload["alive"] = CountRaiders();
    payload["kind"] = sDir.kind;
    payload["status"] = statusOverride != nullptr ? statusOverride : StatusName(sDir.status);
    payload["dayTime"] = gSaveContext.dayTime;
    payload["types"] = sDir.types;
    payload["sceneNum"] = gPlayState->sceneNum;
    for (auto& [clientId, client] : Anchor::Instance->clients) {
        if (client.sceneNum == gPlayState->sceneNum && client.online && client.isSaveLoaded && !client.self) {
            SendTo(clientId, payload);
        }
    }
    sDir.lastStatusSent = Now();
}

static void SendToOwner(nlohmann::json payload);

static void AnnounceStart() {
    if (sDir.kind == KIND_DUSK) {
        Emit("Dusk", "Something claws its way up by the bridge...", 6.0f);
        QueueRaidNavi(RAIDLINE_DUSK);
    } else {
        Emit(fmt::format("Raid! Night {}", sDir.night),
             sDir.baseHere ? fmt::format("Gamestage {}. The dead are coming for the workbench.", sDir.gamestage)
                           : fmt::format("Gamestage {}. The dead are coming.", sDir.gamestage),
             7.0f);
        QueueRaidNavi(RAIDLINE_START);
    }
    Sfx_PlaySfxCentered(NA_SE_EN_REDEAD_AIM);
}

static void QueueEnemyLines(uint32_t types) {
    for (uint8_t t = 0; t < 5; t++) {
        if (types & (1u << t)) {
            QueueRaidNavi(RAIDLINE_ENEMY + t);
        }
    }
}

void RaidHandleHordeEvent(const nlohmann::json& payload) {
    if (gPlayState == nullptr || RaidAuthority()) {
        return; // we run this scene's wave ourselves
    }
    uint8_t status = StatusFromName(payload.value("status", ""));
    uint8_t prev = sPeer.scene == gPlayState->sceneNum ? sPeer.status : WAVE_NONE;
    sPeer.scene = gPlayState->sceneNum;
    sPeer.status = status;
    sPeer.heardAt = Now();
    sPeer.night = payload.value("night", 0u);
    sPeer.gamestage = payload.value("gamestage", 0);
    uint8_t kind = payload.value("kind", (uint8_t)KIND_RAID);

    // Same clock as the authority while its wave runs (it holds the night), and at its dawn.
    bool dawn = payload.value("status", "") == "dawn";
    if (payload.contains("dayTime") && (status != WAVE_NONE || dawn)) {
        uint16_t t = payload.value("dayTime", (uint16_t)gSaveContext.dayTime);
        int16_t diff = (int16_t)(t - gSaveContext.dayTime);
        if (!sTransition && (diff > 0x300 || diff < -0x300)) {
            gSaveContext.dayTime = t;
            gSaveContext.skyboxTime = t;
        }
    }
    if (status == WAVE_WARDED && prev != WAVE_WARDED) {
        Vec3f center;
        if (BaseInThisScene(&center)) {
            BeginRitual(center);
        }
    } else if (prev == WAVE_WARDED && status != WAVE_WARDED && status != WAVE_NONE) {
        sWard = {};
        Emit("The ward is broken!", "A torch went out. The dead are coming!", 7.0f);
        Sfx_PlaySfxCentered(NA_SE_EN_REDEAD_AIM);
    } else if ((status == WAVE_INCOMING || status == WAVE_ASSAULT) && prev == WAVE_NONE) {
        if (kind == KIND_DUSK) {
            Emit("Dusk", "Something claws its way up by the bridge...", 6.0f);
            QueueRaidNavi(RAIDLINE_DUSK);
        } else {
            Emit(fmt::format("Raid! Night {}", sPeer.night),
                 fmt::format("Gamestage {}. The dead are coming.", sPeer.gamestage), 7.0f);
            QueueRaidNavi(RAIDLINE_START);
        }
        Sfx_PlaySfxCentered(NA_SE_EN_REDEAD_AIM);
    } else if (status == WAVE_CLEARED && prev != WAVE_CLEARED && prev != WAVE_NONE) {
        bool first = !(GetBase().story & STORY_FIRST_RAID_DONE);
        Emit("Wave cleared", kind == KIND_DUSK ? "The bridge is quiet again."
                             : first          ? "The village held. Dawn is coming."
                                              : "Hold on until dawn.",
             5.0f);
    }
    QueueEnemyLines(payload.value("types", 0u));
}

// MARK: - The wave (enemy authority)

static void StartWave(uint8_t kind) {
    sDir = {};
    sSpawnLog.clear();
    sTrack.clear();
    sPendingDrain.clear();
    sDir.active = true;
    sDir.kind = kind;
    sDir.scene = gPlayState->sceneNum;
    sDir.night = CurrentDay();
    sDir.gamestage = Gamestage();
    sDir.raidNo = RaidNumber();
    sDir.prologue = kind == KIND_RAID && !(GetBase().story & STORY_FIRST_RAID_DONE);
    sDir.baseHere = BaseInThisScene(&sDir.center);
    sDir.startedAt = Now();
    sDir.spawnTimer = HordeNight::SpawnFrames() * 2;
    if (kind == KIND_DUSK) {
        sDir.budget = 2; // two Stalchildren: a scare a three-heart Link can win
    } else {
        sDir.budget = WaveBudget(sDir.gamestage, sDir.raidNo, sDir.prologue);
    }

    // Raiders already here = we inherited a wave (authority handover, scene re-entry).
    bool inherited = CountRaiders() > 0;
    sDir.status = inherited ? WAVE_ASSAULT : WAVE_INCOMING;
    sDir.lastWardCheck = Now();
    if (!inherited && kind == KIND_RAID && !sDir.prologue && WardComplete()) {
        // PHA-4006: the ring was ready before dark. No "Raid!": the ritual instead.
        sDir.status = WAVE_WARDED;
        BeginRitual(sDir.center);
    } else if (!inherited) {
        AnnounceStart();
    }
    SendWaveEvent();
    if (kind == KIND_RAID) {
        nlohmann::json report;
        report["type"] = RAID_REPORT;
        report["scene"] = sDir.scene;
        report["base"] = sDir.baseHere;
        report["gamestage"] = sDir.gamestage;
        report["warded"] = sDir.status == WAVE_WARDED;
        SendToOwner(report);
    }
    ESYNC_LOG("[Raids] start kind={} night={} gamestage={} budget={} baseHere={}", kind, sDir.night, sDir.gamestage,
              sDir.budget, sDir.baseHere);
}

static void EndWave(bool dawn) {
    if (!sDir.active) {
        return;
    }
    if (dawn && gPlayState != nullptr && gPlayState->sceneNum == sDir.scene) {
        // Horde-only types leave at dawn (Stalchildren burrow on their own). Actor_Kill
        // goes through EnemySync's kill hook, which replicates the removal.
        for (Actor* a = gPlayState->actorCtx.actorLists[ACTORCAT_ENEMY].head; a != nullptr; a = a->next) {
            if (a->id == ACTOR_EN_RD || a->id == ACTOR_EN_WF || a->id == ACTOR_EN_FIREFLY) {
                Actor_Kill(a);
            }
        }
        SendWaveEvent("dawn");
    }
    sDir.active = false;
    sDir.status = WAVE_NONE;
    sTrack.clear();
    sPendingDrain.clear();
    sWard = {};
}

static bool TrySpawnRaider() {
    const RaiderDef* def = nullptr;
    Vec3f pos;
    if (sDir.kind == KIND_DUSK) {
        // Near the Hyrule Field bridge (the village's barricaded bridge path), where
        // the player can see them claw up: a scare, not an ambush.
        def = &kStalchild;
        s16 angle = (s16)(Rand_ZeroOne() * 0x10000);
        f32 r = 60.0f + Rand_ZeroOne() * 120.0f;
        if (!FloorAt(-1060.0f + Math_SinS(angle) * r, -290.0f + Math_CosS(angle) * r, -80.0f, 200.0f, &pos)) {
            return false;
        }
    } else {
        int32_t left = sDir.budget - sDir.spent;
        auto table = Composition(sDir.raidNo, sDir.prologue);
        int total = 0;
        for (auto& w : table) {
            total += w.def->cost <= left ? w.weight : 0;
        }
        if (total == 0) {
            sDir.spent = sDir.budget; // nothing affordable is left
            return true;
        }
        int roll = (int)(Rand_ZeroOne() * total);
        for (auto& w : table) {
            if (w.def->cost > left) {
                continue;
            }
            if (roll < w.weight) {
                def = w.def;
                break;
            }
            roll -= w.weight;
        }
        if (def == nullptr || !PickSpawnPoint(&pos, nullptr)) {
            return false;
        }
    }
    f32 y = pos.y + (def->actorId == ACTOR_EN_FIREFLY ? 50.0f : 0.0f);
    Vec3f goal = sDir.baseHere ? sDir.center : pos;
    s16 yaw = sDir.baseHere ? Math_Vec3f_Yaw(&pos, &goal) : (s16)(Rand_ZeroOne() * 0x10000);
    Actor* a = Actor_Spawn(&gPlayState->actorCtx, gPlayState, def->actorId, pos.x, y, pos.z, 0, yaw, 0, def->params,
                           false);
    if (a == nullptr) {
        return false;
    }
    sDir.spent += sDir.kind == KIND_DUSK ? 1 : def->cost;
    sDir.spawned++;
    sSpawnLog.push_back(pos);
    if (!(sDir.types & (1u << def->enemy))) {
        sDir.types |= 1u << def->enemy;
        if (sDir.kind == KIND_RAID) {
            QueueRaidNavi(RAIDLINE_ENEMY + def->enemy);
        }
        SendWaveEvent();
    }
    ESYNC_LOG("[Raids] spawned id={} params={:#x} at ({:.0f},{:.0f},{:.0f}) spent={}/{}", def->actorId,
              (uint16_t)def->params, pos.x, pos.y, pos.z, sDir.spent, sDir.budget);
    return true;
}

// Barricade damage: every raider's body against every destructible piece's box.
// The pieces are looked up once per frame, not once per raider (100+ pieces).
struct DrainPiece {
    uint16_t id;
    Actor* actor;
    const PlaceableInfo* info;
};
static void DrainBarricades() {
    std::vector<DrainPiece> pieces;
    for (auto& [id, actor] : SpawnedPlaceables()) {
        const Placeable* p = FindPlaceable(id);
        // Spikes and low floors are walked over (spikes bite back); PHA-3945.
        if (p != nullptr && GetPlaceableInfo(p->type).maxHp != 0 && !IsWalkOverType(p->type) && !IsRuin(*p)) {
            pieces.push_back({ id, actor, &GetPlaceableInfo(p->type) });
        }
    }
    if (pieces.empty()) {
        return;
    }
    const f32 dt = 1.0f / 20.0f; // game logic runs at 20 Hz
    const f32 drainScale = sDir.prologue ? 0.5f : 1.0f; // the first raid's walls hold twice as long
    for (Actor* a = gPlayState->actorCtx.actorLists[ACTORCAT_ENEMY].head; a != nullptr; a = a->next) {
        if (!IsRaiderType(a->id) || a->colChkInfo.health == 0 || a->update == nullptr) {
            continue;
        }
        for (auto& [id, actor, infoPtr] : pieces) {
            const PlaceableInfo& info = *infoPtr;
            f32 dx = a->world.pos.x - actor->world.pos.x, dz = a->world.pos.z - actor->world.pos.z;
            f32 dy = a->world.pos.y - actor->world.pos.y;
            if (dy < -30.0f || dy > info.height + 60.0f) {
                continue;
            }
            // Into the piece's local frame.
            f32 c = Math_CosS(actor->shape.rot.y), s = Math_SinS(actor->shape.rot.y);
            f32 lx = dx * c - dz * s, lz = dx * s + dz * c;
            // Body radius + the wall push-out margin. Raiders crowd a scarecrow about 60 out
            // (its collider plus theirs) instead of pressing in like at a wall, so it needs
            // a longer reach or a Stalchild-only crowd stands there forever (PHA-3915).
            const f32 r = infoPtr == &GetPlaceableInfo(PLACEABLE_SCARECROW) ? 64.0f : 32.0f;
            if (fabsf(lx) < info.halfX + r && fabsf(lz) < info.halfZ + r) {
                f32& owed = sPendingDrain[id];
                owed += DrainFor(a) * drainScale * dt;
                if (owed >= 4.0f) {
                    int amount = (int)owed;
                    owed -= amount;
                    DamagePlaceable(id, amount);
                    ESYNC_LOG("[Raids] barricade {} hit for {} by actor {}", id, amount, a->id);
                }
            }
        }
    }
}

static bool TouchingBarricade(Actor* a) {
    for (auto& [id, actor] : SpawnedPlaceables()) {
        // Cheap distance reject first: the lookup is linear in the piece count.
        f32 d = Math_Vec3f_DistXZ(&a->world.pos, &actor->world.pos);
        if (d > 200.0f) {
            continue;
        }
        const Placeable* p = FindPlaceable(id);
        if (p == nullptr || GetPlaceableInfo(p->type).maxHp == 0 || IsRuin(*p) || IsFloorType(p->type)) {
            continue;
        }
        // PHA-3945: a floor up on the walls is out of reach.
        f32 dy = a->world.pos.y - actor->world.pos.y;
        if (dy < -30.0f || dy > GetPlaceableInfo(p->type).height + 60.0f) {
            continue;
        }
        if (d < GetPlaceableInfo(p->type).halfX + 45.0f) {
            return true;
        }
    }
    return false;
}

// Home-point routing for leashed raiders (ReDeads, Gibdos, Keese) and the stuck check.
static void RouteRaiders() {
    double now = Now();
    for (Actor* a = gPlayState->actorCtx.actorLists[ACTORCAT_ENEMY].head; a != nullptr; a = a->next) {
        if (!IsRaiderType(a->id) || a->colChkInfo.health == 0 || a->update == nullptr) {
            continue;
        }
        Actor* decoy = NearestDecoy(a);
        Actor* engagedWith = decoy == nullptr ? EngagedPlayer(a) : nullptr;
        bool engaged = engagedWith != nullptr;
        Vec3f goal = decoy != nullptr ? decoy->world.pos : engaged ? engagedWith->world.pos : GoalFor(a);

        if (a->id == ACTOR_EN_RD) {
            HordeNight::ShambleToward(a, goal, HordeNight::HORDE_SHAMBLE_SPEED);
        } else if (a->id == ACTOR_EN_FIREFLY) {
            HordeNight::ShambleToward(a, goal, 3.0f);
            a->home.pos.y = goal.y + 40.0f; // fly in at head height
        }

        // Stuck check: under 20 units closer to its goal (the workbench, or the player
        // it fights) in 5 seconds -> move it, out of sight, to another ring sample. Not
        // while it is in melee range, presses a barricade, or stands at the workbench.
        if (a->id == ACTOR_EN_FIREFLY) {
            continue; // fliers circle their (moving) home and cross walls: never "stuck"
        }
        Tracker& t = sTrack[a];
        f32 goalDist = Math_Vec3f_DistXZ(&a->world.pos, &goal);
        if (t.windowStart == 0) {
            t = { a->world.pos, goalDist, now };
            continue;
        }
        if (now - t.windowStart < STUCK_WINDOW) {
            continue;
        }
        bool stuck = (t.lastGoalDist - goalDist) < STUCK_GAIN && goalDist > (engaged ? 120.0f : 150.0f) &&
                     !TouchingBarricade(a);
        t = { a->world.pos, goalDist, now };
        if (!stuck || SeenByAnyPlayer(a->world.pos)) {
            continue;
        }
        Vec3f to;
        if (PickSpawnPoint(&to, &a->world.pos)) {
            ESYNC_LOG("[Raids] stuck actor {} at ({:.0f},{:.0f}) -> ({:.0f},{:.0f})", a->id, a->world.pos.x,
                      a->world.pos.z, to.x, to.z);
            to.y += a->id == ACTOR_EN_FIREFLY ? 50.0f : 0.0f;
            a->world.pos = a->prevPos = a->home.pos = to;
            a->shape.rot.y = a->world.rot.y = Math_Vec3f_Yaw(&to, &goal);
            t = { to, Math_Vec3f_DistXZ(&to, &goal), now };
            sDir.relocated++;
        }
    }
}

// Raiders that walk (Stalchildren, Wolfos) steer by yawTowardsPlayer: point it at
// the workbench unless a player is within 300. Runs as an ID hook, after EnemySync's
// nearest-player perception has written the same fields.
static void OnRaiderPerception(void* actorRef, bool* should) {
    Actor* a = (Actor*)actorRef;
    if (!*should || !sDir.active || gPlayState == nullptr || gPlayState->sceneNum != sDir.scene ||
        !RaidAuthority()) {
        return;
    }
    Actor* nearest = HordeNight::NearestLivingPlayer(a->world.pos, nullptr);
    if (a->id == ACTOR_EN_SKB && nearest != nullptr) {
        // Stalchildren burrow when home is 800+ from the player; a raider's home
        // travels with its quarry so it never gives up.
        a->home.pos = nearest->world.pos;
    }
    Actor* decoy = NearestDecoy(a);
    if (decoy == nullptr && (!sDir.baseHere || EngagedPlayer(a) != nullptr)) {
        return; // a player is close (or there's no base here): the players are the target
    }
    Vec3f goal = decoy != nullptr ? decoy->world.pos : sDir.center;
    f32 xz = Actor_WorldDistXZToPoint(a, &goal);
    if (a->id == ACTOR_EN_WF) {
        xz = std::min(xz, 200.0f); // close enough to come out of the ground and run at it
    }
    a->yawTowardsPlayer = Actor_WorldYawTowardPoint(a, &goal);
    a->xzDistToPlayer = xz;
    a->yDistToPlayer = goal.y - a->world.pos.y;
    a->xyzDistToPlayerSq = SQ(a->xzDistToPlayer) + SQ(a->yDistToPlayer);
}

// A raider thrown out of the scene (a bomb trap's blast can launch a dying Stalchild
// tens of thousands of units off) never lands to finish dying and stays listed forever.
static void ClearStrayRaiders() {
    const CollisionContext& col = gPlayState->colCtx;
    const f32 margin = 500.0f;
    for (Actor* a = gPlayState->actorCtx.actorLists[ACTORCAT_ENEMY].head; a != nullptr; a = a->next) {
        if (!IsRaiderType(a->id) || a->update == nullptr) {
            continue;
        }
        const Vec3f& p = a->world.pos;
        if (p.x < col.minBounds.x - margin || p.x > col.maxBounds.x + margin || p.z < col.minBounds.z - margin ||
            p.z > col.maxBounds.z + margin || p.y < col.minBounds.y - margin) {
            ESYNC_LOG("[Raids] stray actor {} out of the scene at ({:.0f},{:.0f},{:.0f}) hp {}: removed", a->id, p.x,
                      p.y, p.z, a->colChkInfo.health);
            Actor_Kill(a);
        }
    }
}

static void WaveTick() {
    bool sceneOk = IsOutdoorScene(gPlayState->sceneNum);
    uint8_t want = KIND_NONE;
    if (sceneOk && RaidAuthority() && IS_NIGHT) {
        if ((GetBase().story & STORY_DUSK_ACTIVE) && gPlayState->sceneNum == SCENE_KOKIRI_FOREST) {
            want = KIND_DUSK;
        } else if (RaidTonight()) {
            want = KIND_RAID;
        }
    }
    if (want == KIND_NONE) {
        // Dawn in this scene ends the wave; losing authority or the scene stops it quietly.
        EndWave(sceneOk && IS_DAY && gPlayState->sceneNum == sDir.scene && RaidAuthority());
        return;
    }
    if (!sDir.active || sDir.kind != want || sDir.scene != gPlayState->sceneNum) {
        EndWave(false);
        StartWave(want);
        return;
    }
    if (gPlayState->pauseCtx.state != 0 || gPlayState->csCtx.state != CS_STATE_IDLE ||
        gPlayState->transitionTrigger != TRANS_TRIGGER_OFF) {
        return;
    }

    RouteRaiders();
    DrainBarricades();

    int alive = CountRaiders();
    bool budgetSpent = sDir.spent >= sDir.budget;
    // PHA-4006: closing the ring holds back whatever is left of the wave once the field
    // is clear; a torch broken or packed up opens it again and the raid comes on.
    if (sDir.kind == KIND_RAID && !sDir.prologue && Now() - sDir.lastWardCheck > WARD_CHECK_SECONDS &&
        (sDir.status == WAVE_WARDED || (alive == 0 && !budgetSpent && sDir.status != WAVE_CLEARED))) {
        sDir.lastWardCheck = Now();
        bool ring = WardComplete();
        if (ring != (sDir.status == WAVE_WARDED)) {
            if (ring) {
                sDir.status = WAVE_WARDED;
                BeginRitual(sDir.center);
            } else {
                sDir.status = sDir.spawned > 0 ? WAVE_ASSAULT : WAVE_INCOMING;
                sDir.startedAt = Now(); // a fresh hold for the raid that is coming after all
                sDir.spawnTimer = HordeNight::SpawnFrames();
                sWard = {};
                Emit("The ward is broken!", "A torch went out. The dead are coming!", 7.0f);
                Sfx_PlaySfxCentered(NA_SE_EN_REDEAD_AIM);
                QueueRaidNavi(RAIDLINE_START);
            }
            SendWaveEvent();
            nlohmann::json report;
            report["type"] = RAID_REPORT;
            report["scene"] = sDir.scene;
            report["base"] = sDir.baseHere;
            report["warded"] = ring;
            SendToOwner(report);
            ESYNC_LOG("[Raids] ward {} (spawned {}/{} budget)", ring ? "closed" : "broken", sDir.spent, sDir.budget);
        }
    }
    if (sDir.status == WAVE_WARDED) {
        if (Now() - sDir.lastStatusSent > 5.0) {
            SendWaveEvent();
        }
        return;
    }
    if (sDir.status != WAVE_CLEARED) {
        if (budgetSpent && alive == 0 && sDir.spawned > 0) {
            sDir.status = WAVE_CLEARED;
            sDir.clearedAt = Now();
            Emit("Wave cleared", sDir.kind == KIND_DUSK ? "The bridge is quiet again."
                                 : sDir.prologue        ? "The village held. Dawn is coming."
                                                        : "Hold on until dawn.");
            Sfx_PlaySfxCentered(NA_SE_SY_CORRECT_CHIME);
            SendWaveEvent();
        } else if (!budgetSpent) {
            if (sDir.spawnTimer > 0) {
                sDir.spawnTimer--;
            } else if (gPlayState->actorCtx.actorLists[ACTORCAT_ENEMY].length >= HordeNight::MaxAlive(sDir.raidNo) ||
                       (sDir.prologue && alive >= 2)) {
                sDir.spawnTimer = HordeNight::SpawnFrames() / 2; // the first raid: two at a time
            } else if (TrySpawnRaider()) {
                sDir.status = WAVE_ASSAULT;
                sDir.spawnTimer = HordeNight::SpawnFrames() * (sDir.prologue ? 2 : 1);
            } else {
                sDir.spawnTimer = 5;
            }
        }
    }

    // The dusk ends at dawn: once its Stalchildren are down (or after a while) the
    // owner scripts the morning.
    if (sDir.kind == KIND_DUSK && !sDir.storyCleared &&
        (sDir.status == WAVE_CLEARED || Now() - sDir.startedAt > DUSK_MAX_SECONDS)) {
        sDir.storyCleared = true;
        nlohmann::json story;
        story["type"] = RAID_STORY;
        story["event"] = "duskCleared";
        SendToOwner(story);
    }
    // Clearing the first raid wins the night outright: the owner brings the dawn.
    if (sDir.kind == KIND_RAID && sDir.prologue && !sDir.storyCleared && sDir.status == WAVE_CLEARED &&
        Now() - sDir.clearedAt > 4.0) {
        sDir.storyCleared = true;
        nlohmann::json story;
        story["type"] = RAID_STORY;
        story["event"] = "firstRaidCleared";
        SendToOwner(story);
    }

    if (Now() - sDir.lastStatusSent > 5.0) {
        SendWaveEvent();
    }
}

// MARK: - The clock

bool RaidWaveHere() {
    if (gPlayState == nullptr) {
        return false;
    }
    if (sDir.active && sDir.kind == KIND_RAID && gPlayState->sceneNum == sDir.scene) {
        return sDir.status == WAVE_INCOMING || sDir.status == WAVE_ASSAULT;
    }
    return sPeer.scene == gPlayState->sceneNum && Now() - sPeer.heardAt < 12.0 &&
           (sPeer.status == WAVE_INCOMING || sPeer.status == WAVE_ASSAULT);
}

static bool HoldingNight() {
    if (gPlayState == nullptr) {
        return false;
    }
    if (sDir.active && sDir.kind == KIND_RAID && gPlayState->sceneNum == sDir.scene) {
        return (sDir.status == WAVE_INCOMING || sDir.status == WAVE_ASSAULT) && Now() - sDir.startedAt < HoldSeconds();
    }
    // A peer holds with the authority while its wave reports in.
    return sPeer.scene == gPlayState->sceneNum && Now() - sPeer.heardAt < 12.0 &&
           (sPeer.status == WAVE_INCOMING || sPeer.status == WAVE_ASSAULT) && RaidsEnabled();
}

bool RaidWardedHere() {
    if (gPlayState == nullptr) {
        return false;
    }
    if (sDir.active && sDir.kind == KIND_RAID && gPlayState->sceneNum == sDir.scene) {
        return sDir.status == WAVE_WARDED;
    }
    return sPeer.scene == gPlayState->sceneNum && Now() - sPeer.heardAt < 12.0 && sPeer.status == WAVE_WARDED;
}

// PHA-4006: the end of the ritual's sweep, and a reminder now and then while it holds.
static void WardTick() {
    if (sWard.since < 0) {
        return;
    }
    if (!RaidWardedHere()) {
        sWard = {};
        return;
    }
    double now = Now();
    if (!sWard.announced && now - sWard.since > WARD_SWEEP_SECONDS + 0.3) {
        sWard.announced = true;
        sWard.remindedAt = now;
        Sfx_PlaySfxCentered(NA_SE_SY_CORRECT_CHIME);
        Emit("Warded", "The raid can't reach you while the ring burns.", 9.0f);
    } else if (sWard.announced && now - sWard.remindedAt > WARD_REMIND_SECONDS && PlayerFree()) {
        sWard.remindedAt = now;
        Emit("Warded", "The ring holds. The dead keep their distance.", 5.0f);
    }
}

static void StartTransition(uint16_t to) {
    if (gPlayState != nullptr && IsOutdoorScene(gPlayState->sceneNum) && !gPlayState->envCtx.indoors &&
        gSaveContext.sunsSongState == SUNSSONG_INACTIVE) {
        sTransition = true;
        sTransitionTo = to;
        gTimeIncrement = SCRIPT_SPEED;
        sLastWrittenIncrement = SCRIPT_SPEED;
    } else {
        // Indoors / dungeons: the time just changes, seen on the way out.
        gSaveContext.dayTime = gSaveContext.skyboxTime = to;
        gSaveContext.nightFlag = (to > 0xC000 || to < 0x4555) ? 1 : 0;
    }
}

static void ClockTick() {
    if (gPlayState == nullptr || gSaveContext.sunsSongState != SUNSSONG_INACTIVE) {
        return;
    }
    if (sTransition) {
        uint16_t left = (uint16_t)(sTransitionTo - gSaveContext.dayTime);
        if (left <= SCRIPT_SPEED * 2 || left > 0xF000) {
            gSaveContext.dayTime = gSaveContext.skyboxTime = sTransitionTo;
            sTransition = false;
            sLastWrittenIncrement = -1; // re-apply the scene's speed below
        } else {
            gTimeIncrement = SCRIPT_SPEED;
            return;
        }
    }
    if (!IsOutdoorScene(gPlayState->sceneNum)) {
        return; // dungeons, interiors and the Market keep their own (frozen) clock
    }
    uint16_t want = sVanillaIncrement;
    if (want == 0 && PrologueOver()) {
        want = RaidClockSpeed(); // about half Hyrule Field's 10: a day lasts ~8 minutes
    }
    if (HoldingNight() && IS_NIGHT) {
        want = 0;
    }
    // Only write on a change of our own mind, so scene actors that drive the clock
    // themselves (the Hyrule Field drawbridge at dusk) aren't fought every frame.
    if ((int32_t)want != sLastWrittenIncrement) {
        gTimeIncrement = want;
        sLastWrittenIncrement = want;
    }
}

// MARK: - Owner: the schedule, dawn, penalties

static void SendToOwner(nlohmann::json payload) {
    if (IsOwner()) {
        RaidHandlePacket(payload.value("type", ""), payload, OwnId());
    } else {
        SendTo(ActingOwner(), payload);
    }
}

static void ShowNotice(const nlohmann::json& n) {
    std::string prefix = n.value("prefix", ""), message = n.value("message", "");
    if (n.contains("dawn") && NightsEnabled()) {
        // M7: the dawn's title is the Majora-style card; the report stays a toast.
        const auto& d = n["dawn"];
        ShowDawnCard(d.value("day", 1u), d.value("until", UINT32_MAX), d.value("days", 0u), d.value("raids", 0u));
        prefix = "";
    }
    if (prefix.empty()) {
        if (!message.empty()) {
            Emit("", message, n.value("secs", 7.0f) + 2.0f);
        }
    } else if (prefix.size() + message.size() > 48 && !message.empty()) {
        // A toast is one line: the title on its own, then the report.
        Emit(prefix, "", n.value("secs", 7.0f));
        Emit("", message, n.value("secs", 7.0f) + 2.0f);
    } else {
        Emit(prefix, message, n.value("secs", 7.0f));
    }
    for (auto& line : n.value("navi", nlohmann::json::array())) {
        QueueRaidNavi(line.get<uint8_t>());
    }
}

static void Notice(const std::string& prefix, const std::string& message, std::vector<uint8_t> navi) {
    nlohmann::json n;
    n["type"] = RAID_NOTICE;
    n["prefix"] = prefix;
    n["message"] = message;
    n["navi"] = navi;
    Broadcast(n);
    ShowNotice(n);
}

static void Script(uint16_t to, const std::string& why) {
    nlohmann::json s;
    s["type"] = RAID_SCRIPT;
    s["to"] = to;
    s["why"] = why;
    Broadcast(s);
    RaidHandlePacket(RAID_SCRIPT, s, OwnId());
}

// The raid on paper: wave budget minus the base's defense rating is the damage,
// spread over the pieces nearest the routes in (the outermost); the pool takes a cut.
static std::string SettleEmptyBase(int32_t gamestage, std::vector<uint8_t>& navi) {
    BaseState& b = Net::MutableBase();
    int era = CurrentEraNow();
    const BaseCenter& c = b.center[era];
    if (!c.valid) {
        return "";
    }
    int32_t budget = WaveBudget(gamestage, RaidNumber(), !(b.story & STORY_FIRST_RAID_DONE));
    int32_t defense = 0;
    std::vector<Placeable*> pieces;
    for (auto& p : b.placeables) {
        if (p.era != era || p.scene != c.scene) {
            continue;
        }
        if (p.type == PLACEABLE_SPIKES) {
            defense += 3; // the spikes bite back
        } else if (GetPlaceableInfo(p.type).maxHp > 0) {
            defense += p.hp / 25;
            pieces.push_back(&p);
        }
    }
    int32_t damage = std::max(0, budget - defense) * 10; // HP
    std::sort(pieces.begin(), pieces.end(), [&](Placeable* x, Placeable* y) {
        f32 dx = x->pos[0] - c.pos[0], dz = x->pos[2] - c.pos[2], ex = y->pos[0] - c.pos[0], ez = y->pos[2] - c.pos[2];
        return dx * dx + dz * dz > ex * ex + ez * ez;
    });
    int32_t left = damage;
    std::vector<uint16_t> broken;
    for (size_t i = 0; i < pieces.size() && left > 0 && i < 4; i++) {
        // Spread: each of the outer pieces takes an even share, the rest rolls inward.
        int32_t share = std::max<int32_t>(left / (int32_t)std::max<size_t>(1, std::min<size_t>(4, pieces.size()) - i), 1);
        int32_t take = std::min<int32_t>(share, pieces[i]->hp);
        left -= take;
        if (take >= pieces[i]->hp) {
            broken.push_back(pieces[i]->id); // broken below, with what stood on it
        } else {
            pieces[i]->hp -= (uint16_t)take;
        }
    }
    pieces.clear(); // breaking pieces moves the rest of the list around
    std::map<std::string, int> lostByName;
    int lost = 0;
    for (uint16_t id : broken) {
        for (uint8_t type : BreakPiece(id)) {
            lostByName[GetPlaceableInfo(type).name]++;
            lost++;
        }
    }
    // The pool takes a cut: 2% per 10 damage, at most a quarter.
    uint32_t cutPct = (uint32_t)std::min(25, damage / 5);
    PoolState& pool = Net::MutablePool();
    if (cutPct > 0) {
        for (uint8_t m = 0; m < MAT_COUNT; m++) {
            pool.materials[m] -= pool.materials[m] * cutPct / 100;
        }
        pool.rev++;
    }
    navi.push_back(RAIDLINE_AWAY);
    if (damage == 0) {
        return "The base was raided while you were away. It held.";
    }
    std::string names;
    for (auto& [name, n] : lostByName) {
        names += fmt::format("{}{}{}", names.empty() ? "" : ", ", name, n > 1 ? fmt::format(" x{}", n) : "");
    }
    if (lost == 0) {
        return fmt::format("The base was raided: the walls took a beating, {}% of the stores taken.", cutPct);
    }
    return fmt::format("The base was raided: {} piece{} lost ({}), {}% of the stores taken.", lost, lost == 1 ? "" : "s",
                       names, cutPct);
}

static void OwnerDawn() {
    BaseState& b = Night();
    uint32_t night = CurrentDay(); // the night that just ended belongs to this day
    const bool nightFought = b.nightFought, nightFailed = b.nightFailed, nightWarded = b.nightWarded;
    const int32_t nightGamestage = b.nightGamestage;
    bool wasRaid = RaidTonight();
    bool wasDusk = (b.story & STORY_DUSK_ACTIVE) != 0;
    b.daysSurvived++;
    b.story &= ~STORY_DUSK_ACTIVE;
    std::vector<uint8_t> navi;
    std::string report;
    if (wasRaid) {
        int32_t gs = nightGamestage > 0 ? nightGamestage : Gamestage();
        if (!nightFought) {
            report = SettleEmptyBase(gs, navi);
        }
        if (!nightFailed) {
            b.hordeNightsSurvived++;
            // Survivors earn materials by gamestage.
            PoolState& pool = Net::MutablePool();
            uint32_t wood = (uint32_t)std::max(1, gs / 2), bone = (uint32_t)std::max(1, gs / 3);
            pool.materials[MAT_WOOD] += wood;
            pool.materials[MAT_BONE] += bone;
            pool.rev++;
            std::string reward = fmt::format("{}: +{} Wood, +{} Bone.", nightWarded ? "Warded" : "Survived", wood, bone);
            if (nightWarded) {
                report = "The dead never came. " + reward; // a toast is one line
                navi.push_back(RAIDLINE_WARD);
            } else {
                report = report.empty() ? reward : report + " " + reward;
            }
        } else {
            b.nightsFailed++;
        }
        b.story |= STORY_FIRST_RAID_DONE;
        b.nextRaidDay = night + (uint32_t)Interval();
        navi.push_back(RAIDLINE_DAWN);
    }
    if (CVarGetInteger(CVAR_SEVEN_DAYS("RaidForce"), 0) != 0 && wasRaid) {
        CVarSetInteger(CVAR_SEVEN_DAYS("RaidForce"), 0); // a forced night is one night
    }
    b.nightDay = 0; // a new night starts clean
    b.nightFought = b.nightFailed = b.nightWarded = false;
    b.nightGamestage = 0;
    Net::CommitBase();
    Net::BroadcastPool();
    uint32_t until = b.nextRaidDay > CurrentDay() ? b.nextRaidDay - CurrentDay() : 0;
    std::string title = fmt::format("Dawn of Day {}", CurrentDay());
    if (b.nextRaidDay != 0) {
        title += until == 0 ? " - the raid is tonight" : fmt::format(" - {} night{} until the raid", until, until == 1 ? "" : "s");
    }
    if (report.empty() && wasDusk) {
        report = "The night things went back into the ground.";
    }
    nlohmann::json n;
    n["type"] = RAID_NOTICE;
    n["prefix"] = title;
    n["message"] = report;
    n["navi"] = navi;
    n["dawn"] = { { "day", CurrentDay() },
                  { "until", b.nextRaidDay != 0 ? until : UINT32_MAX },
                  { "days", b.daysSurvived },
                  { "raids", b.hordeNightsSurvived } };
    Broadcast(n);
    ShowNotice(n);
    ESYNC_LOG("[Raids] dawn: day {} raid={} fought={} failed={} -> {}", CurrentDay(), wasRaid, !report.empty(),
              b.nightsFailed, report);
}

static void OwnerDusk() {
    if (RaidTonight()) {
        Night().nightGamestage = Gamestage();
        Net::CommitBase();
    }
}

static void OwnerRaidLost() {
    BaseState& b = Night();
    if (b.nightFailed) {
        return;
    }
    b.nightFailed = true;
    for (auto& p : b.placeables) {
        if (GetPlaceableInfo(p.type).maxHp > 0 && !IsRuin(p)) {
            p.hp -= p.hp / 2; // half its remaining HP
        }
    }
    PoolState& pool = Net::MutablePool();
    for (uint8_t m = 0; m < MAT_COUNT; m++) {
        pool.materials[m] -= pool.materials[m] / 4; // 25%, rounded down
    }
    pool.rev++;
    Net::CommitBase();
    Net::BroadcastPool();
    Notice("The night is lost", "Walls at half strength, a quarter of the stores gone.",
           { RAIDLINE_LOST });
}

bool RaidOwnsPacket(const std::string& type) {
    return type == RAID_STORY || type == RAID_SCRIPT || type == RAID_REPORT || type == RAID_LOST ||
           type == RAID_NOTICE;
}

void RaidHandlePacket(const std::string& type, const nlohmann::json& payload, uint32_t from) {
    if (!RaidsEnabled()) {
        return;
    }
    if (type == RAID_SCRIPT) {
        if (from != ActingOwner() && from != OwnId()) {
            return;
        }
        StartTransition(payload.value("to", (uint16_t)DUSK_TIME));
        std::string why = payload.value("why", "");
        if (why == "raid") {
            QueueRaidNavi(RAIDLINE_EVE);
            Emit("Dusk", "With the Deku Tree gone, the dead come for the village tonight.", 7.0f);
        } else if (why == "dusk") {
            Emit("Dusk", "The sun goes down over the forest...", 5.0f);
        }
        return;
    }
    if (type == RAID_NOTICE) {
        if (from == ActingOwner() && from != OwnId()) {
            ShowNotice(payload);
        }
        return;
    }
    if (!IsOwner()) {
        return;
    }
    BaseState& b = Net::MutableBase();
    if (type == RAID_STORY) {
        std::string event = payload.value("event", "");
        if (event == "sword" && !(b.story & (STORY_DUSK_DONE | STORY_FIRST_RAID))) {
            b.story |= STORY_DUSK_DONE | STORY_DUSK_ACTIVE;
            Net::CommitBase();
            Script(DUSK_TIME, "dusk");
        } else if (event == "gohma" && !(b.story & STORY_FIRST_RAID)) {
            b.story |= STORY_FIRST_RAID;
            b.story &= ~STORY_DUSK_ACTIVE;
            b.nextRaidDay = CurrentDay(); // that night
            Night().nightGamestage = Gamestage();
            Net::CommitBase();
            Script(DUSK_TIME, "raid");
        } else if (event == "duskCleared" && (b.story & STORY_DUSK_ACTIVE)) {
            Script(DAWN_TIME, "dawn");
        } else if (event == "firstRaidCleared" && RaidTonight() && !(b.story & STORY_FIRST_RAID_DONE) &&
                   !Night().nightFailed) {
            Script(DAWN_TIME, "dawn");
        }
    } else if (type == RAID_REPORT) {
        BaseState& n = Night();
        bool changed = false;
        if (payload.value("base", false) && !n.nightFought) {
            n.nightFought = true;
            changed = true;
        }
        if (n.nightGamestage == 0 && payload.value("gamestage", 0) != 0) {
            n.nightGamestage = payload.value("gamestage", 0);
            changed = true;
        }
        if (payload.contains("warded") && payload.value("warded", false) != n.nightWarded) {
            n.nightWarded = payload.value("warded", false);
            changed = true;
        }
        if (changed) {
            Net::CommitBase();
        }
    } else if (type == RAID_LOST) {
        if (RaidTonight()) { // a raid night (the Kokiri Sword's dusk is a scare, not a horde)
            OwnerRaidLost();
        }
    }
}

// MARK: - Per frame

static void StoryTriggers() {
    const BaseState& b = GetBase();
    if (gPlayState->sceneNum != SCENE_KOKIRI_FOREST || !PlayerFree()) {
        sFreeSince = -1;
        return;
    }
    double now = Now();
    if (sFreeSince < 0) {
        sFreeSince = now;
    }
    if (now - sFreeSince < 3.0 || now - sLastStorySent < 5.0) {
        return;
    }
    const char* event = nullptr;
    if (!(b.story & STORY_FIRST_RAID) && IsUnlocked(UNLOCK_DEKU_TREE)) {
        event = "gohma"; // Gohma's death: the first raid comes that night
    } else if (!(b.story & (STORY_DUSK_DONE | STORY_FIRST_RAID)) &&
               CHECK_OWNED_EQUIP(EQUIP_TYPE_SWORD, EQUIP_INV_SWORD_KOKIRI)) {
        event = "sword"; // the Kokiri Sword's chest: the first dusk
    }
    if (event != nullptr) {
        sLastStorySent = now;
        nlohmann::json story;
        story["type"] = RAID_STORY;
        story["event"] = event;
        SendToOwner(story);
    }
}

// PHA-3935: Navi warns the evening before every raid (spec, "Warning"). The first raid
// has its own staged line (RAIDLINE_EVE); this covers every one after it.
constexpr uint16_t EVE_TIME = 0xB000; // about 16:30
static uint32_t sEveWarnedDay = 0;
static void EveWarning() {
    const BaseState& b = GetBase();
    if (!NightsEnabled() || !(b.story & STORY_FIRST_RAID_DONE) || sEveWarnedDay == CurrentDay() ||
        gSaveContext.dayTime < EVE_TIME || !RaidTonight() || sDir.active || sPeer.status != WAVE_NONE ||
        !IsOutdoorScene(gPlayState->sceneNum)) {
        return;
    }
    sEveWarnedDay = CurrentDay();
    QueueNaviText(TEXT_RAID_EVE_EACH + (uint16_t)(RaidNumber() % 3));
}

// PHA-3935 (M9): on a raid night the other towns' folk bar themselves indoors too:
// the townsfolk, carpenters and the Cucco girl, never a quest NPC or a guard.
static bool IsTownScene(int16_t scene) {
    switch (scene) {
        case SCENE_KAKARIKO_VILLAGE:
        case SCENE_MARKET_NIGHT:
        case SCENE_MARKET_ENTRANCE_NIGHT:
        case SCENE_BACK_ALLEY_NIGHT:
        case SCENE_LON_LON_RANCH:
            return true;
    }
    return false;
}

static int16_t sHidNoticeScene = -1;
static int sTownsfolkHidden = 0; // tests

static void TownsHide() {
    int16_t scene = gPlayState->sceneNum;
    if (!IS_NIGHT || !RaidTonight() || !IsTownScene(scene) || IS_RANDO) {
        if (!IS_NIGHT) {
            sHidNoticeScene = -1;
        }
        return;
    }
    int hid = 0;
    for (Actor* a = gPlayState->actorCtx.actorLists[ACTORCAT_NPC].head; a != nullptr; a = a->next) {
        if (a->update != nullptr &&
            (a->id == ACTOR_EN_HY || a->id == ACTOR_EN_DAIKU_KAKARIKO || a->id == ACTOR_EN_NIW_GIRL)) {
            Actor_Kill(a);
            hid++;
        }
    }
    sTownsfolkHidden += hid;
    if (sHidNoticeScene != scene && PlayerFree()) {
        sHidNoticeScene = scene;
        Emit("Raid night", fmt::format("{} has barred its doors until dawn.", scene == SCENE_LON_LON_RANCH ? "The ranch"
                                                                                : scene == SCENE_KAKARIKO_VILLAGE ? "Kakariko"
                                                                                : "Castle Town"),
             5.0f);
    }
}

void RaidsOnFrame() {
    if (gPlayState == nullptr || !GameInteractor::IsSaveLoaded(true)) {
        sPrevNight = -1; // the title screen's attract demo has a clock too: never count it
        return;
    }
    StoryTriggers();
    EveWarning();

    // Owner: dawn and dusk on its own clock (scripted ones included). A vanilla
    // cutscene sets its own sky (a new file's intro dream is a night in Hyrule
    // Field, then Link wakes up at home in the morning), so the clock is only
    // sampled in play: a night seen before a cutscene still dawns after it.
    if (gPlayState->csCtx.state == CS_STATE_IDLE && gSaveContext.cutsceneIndex < 0xFFF0) {
        int night = IS_NIGHT ? 1 : 0;
        if (sPrevNight >= 0 && night != sPrevNight && IsOwner()) {
            if (night == 0) {
                OwnerDawn();
            } else {
                OwnerDusk();
            }
        }
        sPrevNight = night;
    }

    // The Kokiri stay indoors on the village's dark nights.
    if (IS_NIGHT && gPlayState->sceneNum == SCENE_KOKIRI_FOREST && (sDir.active || sPeer.status != WAVE_NONE)) {
        for (Actor* a = gPlayState->actorCtx.actorLists[ACTORCAT_NPC].head; a != nullptr; a = a->next) {
            if (a->id == ACTOR_EN_KO) {
                Actor_Kill(a);
            }
        }
    }
    TownsHide();

    if (RaidAuthority()) {
        ClearStrayRaiders();
    }
    WaveTick();
    WardTick();
    ClockTick();

    // Losing a night: everyone down during a raid (co-op only reaches vanilla
    // game over when nobody is left standing).
    int gameOver = gPlayState->gameOverCtx.state != GAMEOVER_INACTIVE ? 1 : 0;
    bool raidHere = (sDir.active && sDir.kind == KIND_RAID &&
                     (sDir.status == WAVE_INCOMING || sDir.status == WAVE_ASSAULT)) ||
                    (sPeer.scene == gPlayState->sceneNum && Now() - sPeer.heardAt < 12.0 &&
                     (sPeer.status == WAVE_INCOMING || sPeer.status == WAVE_ASSAULT));
    if (gameOver && !sLastGameOver && raidHere) {
        nlohmann::json lost;
        lost["type"] = RAID_LOST;
        SendToOwner(lost);
    }
    sLastGameOver = gameOver;
}

void RaidsResetSession() {
    sDir = {};
    sPeer = {};
    sWard = {};
    sTrack.clear();
    sPendingDrain.clear();
    sTransition = false;
    sPrevNight = -1;
    sFreeSince = -1;
    sLastStorySent = -100;
    sLastGameOver = 0;
    sLastWrittenIncrement = -1;
}

// MARK: - Navi's raid lines

void RaidsRegisterMessages(const char* table) {
    // clang-format off
    static const char* lines[RAIDLINE_COUNT] = {
        /* EVE   */ "Link, listen! Every third night the dead rise and come for wherever we sleep.^Build, Link!",
        /* START */ "They're coming for the workbench! Defend it, Link!^Barricades take hits. Keep them standing!",
        /* DAWN  */ "We made it to morning! Surviving a raid pays in materials.^But if everyone falls, the walls crumble and the stores are lost!",
        /* AWAY  */ "Link! The base was raided while we were away!^Walls with no one behind them don't hold for long.",
        /* LOST  */ "Ow... We lost the night. The walls are cracked and some of the stores are gone.^Let's rebuild!",
        /* DUSK  */ "Link, it's getting dark... Something is moving down by the bridge!",
        /* STAL  */ "Stalchildren! Bones that walk... Hit them before they swarm the boards!",
        /* KEESE */ "Keese! They fly right over the walls. Swat them down!",
        /* WOLF  */ "A Wolfos! Watch its claws, and strike when it lunges!",
        /* REDEAD*/ "ReDead! Their scream freezes you. Don't let them get close!",
        /* GIBDO */ "A Gibdo! It's as tough as it is slow. Keep your distance!",
        /* WARD  */ "Link... Link! Nothing came. Not one of them, all night!^Those fires... A ring of flame the dead won't cross. That's an old Sheikah warding! I thought it was just a story!^How did you even figure that out?! Nobody knows that!",
    };
    // clang-format on
    for (uint8_t i = 0; i < RAIDLINE_COUNT; i++) {
        AddText(
            table, SEVEN_DAYS_TEXT_BASE + 0x08 + i, CustomMessage(lines[i], TEXTBOX_TYPE_BLUE, TEXTBOX_POS_BOTTOM));
    }
    // clang-format off
    static const char* eve[3] = {
        "Hey! Listen! The sun's going down, and tonight the dead come for the base.^Check the walls, Link!",
        "Link, it's almost dark... It's a raid night. They'll head straight for the workbench!",
        "Listen! Tonight's a raid night. We've held off [[raids]] so far. Let's make it one more!",
    };
    // clang-format on
    for (uint16_t i = 0; i < 3; i++) {
        AddText(table, TEXT_RAID_EVE_EACH + i,
                                                      CustomMessage(eve[i], TEXTBOX_TYPE_BLUE, TEXTBOX_POS_BOTTOM));
    }
}

// MARK: - Registration

void RaidsRegisterHooks(bool enabled) {
    COND_ID_HOOK(ShouldActorUpdate, ACTOR_EN_SKB, enabled, OnRaiderPerception);
    COND_ID_HOOK(ShouldActorUpdate, ACTOR_EN_WF, enabled, OnRaiderPerception);
    COND_HOOK(OnActorDestroy, enabled, [](void* actor) { sTrack.erase((Actor*)actor); });
    COND_HOOK(OnSceneSpawnActors, enabled, []() {
        // A new scene: the wave and the transition belong to the old one.
        if (gPlayState != nullptr) {
            sVanillaIncrement = gPlayState->envCtx.timeIncrement;
        }
        sDir = {};
        sTrack.clear();
        sPendingDrain.clear();
        sPeer = {};
        sWard = {};
        sLastWrittenIncrement = -1;
        if (sTransition) {
            sTransition = false;
            gSaveContext.dayTime = gSaveContext.skyboxTime = sTransitionTo;
        }
    });
    if (!enabled) {
        if (sLastWrittenIncrement >= 0 && gPlayState != nullptr) {
            gTimeIncrement = gPlayState->envCtx.timeIncrement; // back to the scene's own speed
        }
        RaidsResetSession();
    } else if (gPlayState != nullptr) {
        sVanillaIncrement = gPlayState->envCtx.timeIncrement;
    }
}

// MARK: - Test hooks

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
// For tools/webtest and the live playthrough. Shortcuts (they skip story, never
// the raid itself): "gohma" marks the Deku Tree beaten, "sword" gives the Kokiri
// Sword, "force" makes tonight a raid night, "dusk"/"dawn" script the time,
// "fast" fast-forwards the clock to the next dawn, "lost" reports a lost night.
extern "C" {
EMSCRIPTEN_KEEPALIVE
const char* sevendays_test_raid_state() {
    static std::string out;
    nlohmann::json j;
    const BaseState& b = GetBase();
    j["enabled"] = RaidsEnabled();
    j["day"] = CurrentDay();
    j["nextRaidDay"] = b.nextRaidDay;
    j["raidInterval"] = b.raidInterval;
    j["interval"] = RaidInterval();
    j["story"] = b.story;
    j["hordeNightsSurvived"] = b.hordeNightsSurvived;
    j["nightsFailed"] = b.nightsFailed;
    j["raidTonight"] = RaidTonight();
    j["dayTime"] = gSaveContext.dayTime;
    j["night"] = IS_NIGHT;
    j["timeIncrement"] = gTimeIncrement;
    j["vanillaIncrement"] = sVanillaIncrement;
    j["transition"] = sTransition;
    j["holding"] = HoldingNight();
    j["authority"] = RaidAuthority();
    j["owner"] = IsOwner();
    j["gamestage"] = gPlayState != nullptr ? Gamestage() : 0;
    j["health"] = gSaveContext.health;
    j["healthCapacity"] = gSaveContext.healthCapacity;
    j["wave"] = { { "active", sDir.active },     { "kind", sDir.kind },         { "status", StatusName(sDir.status) },
                  { "night", sDir.night },       { "gamestage", sDir.gamestage }, { "budget", sDir.budget },
                  { "spent", sDir.spent },       { "spawned", sDir.spawned },   { "relocated", sDir.relocated },
                  { "baseHere", sDir.baseHere }, { "samples", sDir.samples.size() }, { "prologue", sDir.prologue },
                  { "elapsed", sDir.active ? Now() - sDir.startedAt : 0.0 }, { "types", sDir.types } };
    j["warded"] = gPlayState != nullptr && RaidWardedHere();
    j["ward"] = { { "since", sWard.since < 0 ? -1.0 : Now() - sWard.since }, { "announced", sWard.announced },
                  { "complete", gPlayState != nullptr && sDir.active && WardComplete() } };
    j["nightWarded"] = b.nightWarded;
    j["peer"] = { { "status", StatusName(sPeer.status) }, { "scene", sPeer.scene }, { "age", Now() - sPeer.heardAt } };
    j["spawnLog"] = nlohmann::json::array();
    for (auto& p : sSpawnLog) {
        j["spawnLog"].push_back({ (int)p.x, (int)p.y, (int)p.z });
    }
    j["eveWarnedDay"] = sEveWarnedDay;
    j["townsfolkHidden"] = sTownsfolkHidden;
    j["rocks"] = nlohmann::json::array(); // PHA-3935 tests: boulders and rocks to break
    if (gPlayState != nullptr) {
        for (int cat = 0; cat < ACTORCAT_MAX; cat++) {
            for (Actor* a = gPlayState->actorCtx.actorLists[cat].head; a != nullptr; a = a->next) {
                if (a->id == ACTOR_OBJ_BOMBIWA || a->id == ACTOR_OBJ_HAMISHI || a->id == ACTOR_EN_ISHI) {
                    j["rocks"].push_back({ a->id, a->params, (int)a->world.pos.x, (int)a->world.pos.y,
                                           (int)a->world.pos.z });
                }
            }
        }
    }
    j["npcs"] = nlohmann::json::array();
    if (gPlayState != nullptr) {
        for (Actor* a = gPlayState->actorCtx.actorLists[ACTORCAT_NPC].head; a != nullptr; a = a->next) {
            j["npcs"].push_back(a->id);
        }
    }
    j["raiders"] = nlohmann::json::array();
    if (gPlayState != nullptr) {
        Vec3f c = sDir.center;
        for (Actor* a = gPlayState->actorCtx.actorLists[ACTORCAT_ENEMY].head; a != nullptr; a = a->next) {
            if (IsRaiderType(a->id)) {
                j["raiders"].push_back({ { "id", a->id },
                                         { "params", a->params },
                                         { "hp", a->colChkInfo.health },
                                         { "pos", { (int)a->world.pos.x, (int)a->world.pos.y, (int)a->world.pos.z } },
                                         { "toBench", (int)Math_Vec3f_DistXZ(&a->world.pos, &c) } });
            }
        }
        j["gameOver"] = gPlayState->gameOverCtx.state;
    }
    out = j.dump();
    return out.c_str();
}

EMSCRIPTEN_KEEPALIVE
void sevendays_test_raid(const char* cmdC) {
    if (!RaidsEnabled() || gPlayState == nullptr) {
        return;
    }
    std::string cmd = cmdC;
    if (cmd == "gohma") {
        Flags_SetEventChkInf(EVENTCHKINF_OBTAINED_KOKIRI_EMERALD_DEKU_TREE_DEAD);
        Flags_SetEventChkInf(EVENTCHKINF_USED_DEKU_TREE_BLUE_WARP);
        gSaveContext.inventory.questItems |= gBitFlags[QUEST_KOKIRI_EMERALD];
        // Mido lets nobody into the Deku Tree without a Deku Shield: every player has one by now.
        Item_Give(gPlayState, ITEM_SHIELD_DEKU);
        Inventory_ChangeEquipment(EQUIP_TYPE_SHIELD, EQUIP_VALUE_SHIELD_DEKU);
        // Gohma's Heart Container: a fourth heart, filled.
        if (gSaveContext.healthCapacity < 0x40) {
            gSaveContext.healthCapacity = 0x40;
        }
        gSaveContext.health = gSaveContext.healthCapacity;
    } else if (cmd == "sword") {
        Item_Give(gPlayState, ITEM_SWORD_KOKIRI); // what the chest gives, equipped on B as the chest does
        gSaveContext.equips.buttonItems[0] = ITEM_SWORD_KOKIRI;
        Inventory_ChangeEquipment(EQUIP_TYPE_SWORD, EQUIP_VALUE_SWORD_KOKIRI);
        Interface_LoadItemIcon1(gPlayState, 0);
    } else if (cmd == "force") {
        CVarSetInteger(CVAR_SEVEN_DAYS("RaidForce"), 1);
    } else if (cmd == "dusk") {
        StartTransition(DUSK_TIME);
    } else if (cmd == "dawn" || cmd == "fast") {
        StartTransition(DAWN_TIME);
    } else if (cmd == "calm") {
        // Spend the wave's budget and send its raiders home: the night stays a raid
        // night (red sky, held clock) with nobody left to fight.
        sDir.spent = sDir.budget;
        for (Actor* a = gPlayState->actorCtx.actorLists[ACTORCAT_ENEMY].head; a != nullptr; a = a->next) {
            if (IsRaiderType(a->id)) {
                Actor_Kill(a);
            }
        }
    } else if (cmd == "fling") {
        // PHA-3969 tests: what a bomb blast did to a dying Stalchild, thrown out of the world.
        for (Actor* a = gPlayState->actorCtx.actorLists[ACTORCAT_ENEMY].head; a != nullptr; a = a->next) {
            if (IsRaiderType(a->id) && a->update != nullptr) {
                a->world.pos = a->prevPos = { 62000.0f, -25030.0f, a->world.pos.z };
                a->colChkInfo.health = 0;
                break;
            }
        }
    } else if (cmd.rfind("say:", 0) == 0) {
        // M9 tests: open a vanilla text id here, as an NPC would (the world lines replace it).
        Message_StartTextbox(gPlayState, (u16)std::stoul(cmd.substr(4), nullptr, 16), nullptr);
    } else if (cmd.rfind("warp:", 0) == 0) {
        gPlayState->nextEntranceIndex = (s16)std::stoul(cmd.substr(5), nullptr, 16);
        gPlayState->transitionTrigger = TRANS_TRIGGER_START;
        gPlayState->transitionType = TRANS_TYPE_FADE_BLACK;
        gSaveContext.nextCutsceneIndex = 0xFFEF; // no entrance cutscene
    } else if (cmd.rfind("raids:", 0) == 0) {
        const_cast<BaseState&>(GetBase()).hordeNightsSurvived = (uint32_t)std::stoul(cmd.substr(6));
    } else if (cmd.rfind("tier:", 0) == 0) {
        // PHA-3935 tests: the tool that opens a tier, without the dungeon.
        std::string t = cmd.substr(5);
        if (t == "bomb") {
            Item_Give(gPlayState, ITEM_BOMB_BAG_20);
        } else if (t == "hookshot") {
            Item_Give(gPlayState, ITEM_HOOKSHOT);
        } else if (t == "hammer") {
            Item_Give(gPlayState, ITEM_HAMMER);
        } else if (t == "silver") {
            Item_Give(gPlayState, ITEM_BRACELET);
            Item_Give(gPlayState, ITEM_GAUNTLETS_SILVER);
        }
    } else if (cmd.rfind("aim:", 0) == 0) {
        // PHA-3935 tests: point first-person aim at a world point ("aim:x,y,z").
        float x = 0, y = 0, z = 0;
        sscanf(cmd.c_str() + 4, "%f,%f,%f", &x, &y, &z);
        Player* player = GET_PLAYER(gPlayState);
        Vec3f eye = player->actor.world.pos;
        eye.y += 50.0f;
        Vec3f at = { x, y, z };
        player->actor.shape.rot.y = player->actor.world.rot.y = player->yaw = player->actor.focus.rot.y =
            Math_Vec3f_Yaw(&eye, &at);
        player->actor.focus.rot.x = Math_Vec3f_Pitch(&eye, &at);
    } else if (cmd == "equip:hookshot") {
        // PHA-3935 tests: the Hookshot on C-Left.
        gSaveContext.equips.buttonItems[1] = ITEM_HOOKSHOT;
        gSaveContext.equips.cButtonSlots[0] = SLOT_HOOKSHOT;
        Interface_LoadItemIcon1(gPlayState, 1);
    } else if (cmd == "bomb") {
        // PHA-3935 tests: a lit bomb 50 ahead of Link (boulders for Stone and Ore).
        Player* player = GET_PLAYER(gPlayState);
        Vec3f at = player->actor.world.pos;
        at.x += Math_SinS(player->actor.shape.rot.y) * 50.0f;
        at.z += Math_CosS(player->actor.shape.rot.y) * 50.0f;
        Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_EN_BOM, at.x, at.y, at.z, 0, 0, 0, 0, false);
    } else if (cmd == "age") {
        SwitchAge(); // PHA-3935 tests: the seven-year jump without the Master Sword
    } else if (cmd.rfind("bp:", 0) == 0) {
        BaseState& b = Net::MutableBase();
        std::string id = cmd.substr(3);
        if (IsOwner() && std::find(b.blueprints.begin(), b.blueprints.end(), id) == b.blueprints.end()) {
            b.blueprints.push_back(id);
            Net::CommitBase();
        }
    } else if (cmd == "heal") {
        gSaveContext.health = gSaveContext.healthCapacity; // a bottled fairy, for scripted fights
    } else if (cmd == "lost") {
        nlohmann::json lost;
        lost["type"] = RAID_LOST;
        SendToOwner(lost);
    }
}
}
#endif

} // namespace SevenDays
