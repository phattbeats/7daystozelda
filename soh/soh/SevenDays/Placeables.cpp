#include "SevenDays.h"
#include "soh/ActorDB.h"
#include "soh/frame_interpolation.h"
#include "soh/Enhancements/custom-message/CustomMessageManager.h"
#include "soh/Enhancements/custom-message/CustomMessageTypes.h"
#include "soh/Network/Anchor/Anchor.h"
#include "soh/Network/Anchor/EnemySync.h"
#include "soh/Network/Anchor/HordeNight.h"

#include <algorithm>
#include <array>
#include <cmath>

extern "C" {
#include "z64.h"
#include "macros.h"
#include "variables.h"
#include "functions.h"
#include "objects/gameplay_keep/gameplay_keep.h"
#include "objects/gameplay_dangeon_keep/gameplay_dangeon_keep.h"
#include "objects/object_kibako2/object_kibako2.h"
#include "objects/object_trap/object_trap.h"
#include "objects/object_dekubaba/object_dekubaba.h"
#include "objects/object_ka/object_ka.h"
#include "objects/object_syokudai/object_syokudai.h"
#include "objects/object_pu_box/object_pu_box.h"
#include "objects/object_ingate/object_ingate.h"
#include "objects/object_bombf/object_bombf.h"
#include "objects/object_umajump/object_umajump.h"
#include "objects/object_shop_dungen/object_shop_dungen.h"
#include "objects/object_box/object_box.h"
extern PlayState* gPlayState;
uint8_t ResourceMgr_FileExists(const char* resName);
}

// #3904: models cut from Majora's Mask by art/mm-pack/build_mm_pack.py. They are
// not in oot.o2r or in the repo; the deploy appends them to the server's soh.o2r.
// Each draw checks the pack is there and falls back to an OoT model when it isn't.
static const ALIGN_ASSET(2) char gMMPracticeLogDL[] = "__OTR__objects/7dtz_mm/maruta/gMMPracticeLogDL";
static const ALIGN_ASSET(2) char gMMSmithyHammerDL[] = "__OTR__objects/7dtz_mm/kgy/gMMSmithyHammerDL";
static const ALIGN_ASSET(2) char gMMSmithyBladeDL[] = "__OTR__objects/7dtz_mm/kgy/gMMSmithyBladeDL";
static const ALIGN_ASSET(2) char gMMInnDeskDL[] = "__OTR__objects/7dtz_mm/gMMInnDeskDL";

static bool MMPackLoaded() {
    static int8_t sLoaded = -1;
    if (sLoaded < 0) {
        sLoaded = ResourceMgr_FileExists(gMMPracticeLogDL) && ResourceMgr_FileExists(gMMSmithyHammerDL) &&
                  ResourceMgr_FileExists(gMMSmithyBladeDL) && ResourceMgr_FileExists(gMMInnDeskDL);
    }
    return sLoaded == 1;
}

// #3945: floors, stairs and doors, from the same pack (a newer build of it).
static const ALIGN_ASSET(2) char gMMPiratePanelDL[] = "__OTR__objects/7dtz_mm/taru/gMMPiratePanelDL";
static const ALIGN_ASSET(2) char gMMRanchPlankDL[] = "__OTR__objects/7dtz_mm/gMMRanchPlankDL";
static const ALIGN_ASSET(2) char gMMStonePlatformDL[] = "__OTR__objects/7dtz_mm/raillift/gMMStonePlatformDL";
static const ALIGN_ASSET(2) char gMMFestivalDeckDL[] = "__OTR__objects/7dtz_mm/tokei_turret/gMMFestivalDeckDL";
static const ALIGN_ASSET(2) char gMMLadderDL[] = "__OTR__objects/7dtz_mm/ladder/gMMLadderDL";
static const ALIGN_ASSET(2) char gMMInnStairsDL[] = "__OTR__objects/7dtz_mm/gMMInnStairsDL";
static const ALIGN_ASSET(2) char gMMSwampDoorDL[] = "__OTR__objects/7dtz_mm/dor03/gMMSwampDoorDL";
static const ALIGN_ASSET(2) char gMMMusicBoxDoorDL[] = "__OTR__objects/7dtz_mm/wdor05/gMMMusicBoxDoorDL";
static const ALIGN_ASSET(2) char gMMPirateDoorDL[] = "__OTR__objects/7dtz_mm/kaizoku_obj/gMMPirateDoorDL";

// #3962: furniture, from the same pack (a newer build of it).
static const ALIGN_ASSET(2) char gMMInnChairDL[] = "__OTR__objects/7dtz_mm/gMMInnChairDL";
static const ALIGN_ASSET(2) char gMMMilkBarChairDL[] = "__OTR__objects/7dtz_mm/mbar_obj/gMMMilkBarChairDL";
static const ALIGN_ASSET(2) char gMMInnBenchDL[] = "__OTR__objects/7dtz_mm/gMMInnBenchDL";
static const ALIGN_ASSET(2) char gMMInnBedDL[] = "__OTR__objects/7dtz_mm/gMMInnBedDL";
static const ALIGN_ASSET(2) char gMMMayorBedDL[] = "__OTR__objects/7dtz_mm/gMMMayorBedDL";
static const ALIGN_ASSET(2) char gMMInnDresserDL[] = "__OTR__objects/7dtz_mm/gMMInnDresserDL";
static const ALIGN_ASSET(2) char gMMDrawersDL[] = "__OTR__objects/7dtz_mm/kin2_obj/gMMDrawersDL";
static const ALIGN_ASSET(2) char gMMBookshelfDL[] = "__OTR__objects/7dtz_mm/kin2_obj/gMMBookshelfDL";
static const ALIGN_ASSET(2) char gMMPaintingDL[] = "__OTR__objects/7dtz_mm/kin2_obj/gMMPaintingDL";
static const ALIGN_ASSET(2) char gMMMilkCanDL[] = "__OTR__objects/7dtz_mm/gMMMilkCanDL";
static const ALIGN_ASSET(2) char gMMRugDL[] = "__OTR__objects/7dtz_mm/gMMRugDL";
static const ALIGN_ASSET(2) char gMMBarrelDL[] = "__OTR__objects/7dtz_mm/taru/gMMBarrelDL";
static const ALIGN_ASSET(2) char gMMRomaniBarrelDL[] = "__OTR__objects/7dtz_mm/gMMRomaniBarrelDL";
static const ALIGN_ASSET(2) char gMMWagonWheelDL[] = "__OTR__objects/7dtz_mm/gMMWagonWheelDL";
static const ALIGN_ASSET(2) char gMMFestivalStallDL[] = "__OTR__objects/7dtz_mm/tokei_turret/gMMFestivalStallDL";

static bool MMFurniturePackLoaded() {
    static int8_t sLoaded = -1;
    if (sLoaded < 0) {
        static const char* const kAll[] = { gMMInnChairDL,   gMMMilkBarChairDL, gMMInnBenchDL,    gMMInnBedDL,
                                            gMMMayorBedDL,   gMMInnDresserDL,   gMMDrawersDL,     gMMBookshelfDL,
                                            gMMPaintingDL,   gMMMilkCanDL,      gMMRugDL,         gMMBarrelDL,
                                            gMMRomaniBarrelDL, gMMWagonWheelDL, gMMFestivalStallDL };
        sLoaded = 1;
        for (const char* res : kAll) {
            sLoaded = sLoaded && ResourceMgr_FileExists(res);
        }
    }
    return sLoaded == 1;
}

static bool MMBuildPackLoaded() {
    static int8_t sLoaded = -1;
    if (sLoaded < 0) {
        static const char* const kAll[] = { gMMPiratePanelDL, gMMRanchPlankDL,   gMMStonePlatformDL,
                                            gMMFestivalDeckDL, gMMLadderDL,      gMMInnStairsDL,
                                            gMMSwampDoorDL,   gMMMusicBoxDoorDL, gMMPirateDoorDL };
        sLoaded = 1;
        for (const char* res : kAll) {
            sLoaded = sLoaded && ResourceMgr_FileExists(res);
        }
    }
    return sLoaded == 1;
}

/**
 * M5 placeable actors, registered through ActorDB::AddEntry (no vanilla actor
 * table changes):
 *
 *   SevenDays_Placeable  one actor for every placeable type (params = stable id).
 *                        Drawn with OoT's own display lists (the horse-jump fence,
 *                        the treasure chest, the rectangular sign, the spike, the
 *                        wooden torch stand, push blocks, a bomb flower, Ingo's gate)
 *                        and, since #3904, a few Majora's Mask models from the
 *                        server's soh.o2r (palisade logs, the workbench's desk and
 *                        hammer). It has no collision of its own: see
 *                        SevenDays_BaseCollision.
 *   SevenDays_BaseCollision  #3916: the pieces' boxes (8 vertices, 12 triangles
 *                        each; #3945: a ramp for the stairs, a slab on posts for
 *                        the deck), merged into one CollisionHeader per 640-unit
 *                        chunk of the base and registered with DynaPoly_SetBgActor,
 *                        so Link and enemies collide with them like scenery. A base
 *                        takes a handful of the scene's 50 dyna slots however many
 *                        pieces it has; chunking keeps each bounding sphere small,
 *                        so a floor or wall check only walks the nearby pieces.
 *   SevenDays_Ghost      placement mode's translucent ghost. ACTORCAT_SWITCH so it
 *                        updates before Link and can take the buttons it uses.
 */

using namespace SevenDays;

struct PlaceableActor {
    Actor actor;
    ColliderCylinder spikeCollider;
    uint16_t id;
    uint8_t type;
    uint8_t talking;
    int16_t atCooldown;
    int16_t hitFlash;
    int16_t shake; // M6: frames left of the "took a hit" shake
    // M10: the scarecrow and the Guard Baba are skeletons from the ROM's own objects.
    SkelAnime skel;
    ColliderCylinder biteCollider;
    bool hasSkel;
    uint8_t babaState; // BABA_*
    int16_t babaTimer;
    int16_t babaYaw;   // where its head points
    f32 babaLunge;     // 0 upright .. 1 fully extended
    f32 babaAim;       // direction of the lunge, world units from the stalk's base
    bool babaBit;      // this lunge already landed
    // #3935: the torch's light, the bomb-flower trap and the gate.
    LightNode* lightNode;
    LightInfo lightInfo;
    ColliderCylinder blastCollider;
    int16_t trapFuse;    // frames until the bomb goes off (0: not lit)
    int16_t trapRegrow;  // frames until the bomb has grown back (0: armed)
    int16_t blastFrames; // frames left of the blast's attack collider
    f32 gateOpen;        // 0 shut .. 1 swung open
    bool gatePassable;   // left out of the base collision while a player walks through
    bool ruin;           // the child base after the seven-year jump: drawn broken, does nothing
    bool ghost;          // the placement preview (SevenDays_Ghost), which only draws
    bool wardLit;        // #4006: a torch of the ward's ring, burning blue
};

enum { BABA_IDLE, BABA_WINDUP, BABA_LUNGE, BABA_RECOVER };
constexpr f32 BABA_SIZE = 1.3f;
constexpr f32 BABA_SENSE = 120.0f; // starts a bite on a raider this close to the stalk
constexpr f32 TRAP_SENSE = 70.0f;   // a raider this close lights the bomb-flower trap
constexpr int16_t TRAP_FUSE = 12;
constexpr int16_t TRAP_REGROW = 200; // 10 s at 20 Hz, then it is armed again
constexpr f32 GATE_REACH = 70.0f;    // a player this far in front of or behind the gate opens it

static int16_t sPlaceableId = -1;
static int sTrapBlasts = 0; // tests: bomb-flower traps set off
static int16_t sGhostId = -1;
// #3969 tests: the icon studio draws one piece in front of the camera on a flat backdrop.
static int16_t sStudioId = -1;
static Actor* sStudio = nullptr;
static int sStudioType = -1;      // a PLACEABLE_* type, STUDIO_SMALL_CRATE, STUDIO_LARGE_CRATE, or -1: off
static bool sStudioWhite = false; // black or white backdrop: a shot on each gives the icon's alpha
static bool sStudioRuin = false;  // tests: the piece as a ruin of the child base
enum { STUDIO_SMALL_CRATE = 100, STUDIO_LARGE_CRATE = 101 };
constexpr s16 kStudioTurn = 0x2000; // the studio shows pieces three-quarters on
static s16 sStudioTurn = kStudioTurn; // tests may turn a piece to its best side
static int16_t sCollisionId = -1;

int16_t SevenDays::PlaceableActorId() {
    return sPlaceableId;
}

bool SevenDays::PlaceableActorIsRuin(Actor* actor) {
    return ((PlaceableActor*)actor)->ruin;
}

int16_t SevenDays::GhostActorId() {
    return sGhostId;
}

// MARK: - Collision: one shape per type, built once

// Surface 0: wood-ish floor sound, normal walls. Surface 1 (#3945): the same with
// wall type 2, a ladder (wall flags 1 | 2): Link climbs it when he walks into it.
static SurfaceType sSurfaces[2];

struct ShapeCollision {
    std::vector<Vec3s> verts;
    std::vector<CollisionPoly> polys;
};
static ShapeCollision sShapes[PLACEABLE_COUNT];
static CollisionHeader sHeaderTemplate;

// A convex solid from its corners and triangles. Each triangle is wound so that its
// normal, (B-A)x(C-A) as DynaPoly recomputes it, points away from the solid's centre.
static void AddConvex(ShapeCollision& shape, const std::vector<Vec3s>& v, const std::vector<std::array<u16, 3>>& tris,
                      uint16_t ladderFaces = 0) {
    u16 base = (u16)shape.verts.size();
    Vec3f mid = { 0, 0, 0 };
    for (const Vec3s& p : v) {
        mid.x += p.x / (f32)v.size(), mid.y += p.y / (f32)v.size(), mid.z += p.z / (f32)v.size();
    }
    shape.verts.insert(shape.verts.end(), v.begin(), v.end());
    for (size_t i = 0; i < tris.size(); i++) {
        u16 a = tris[i][0], b = tris[i][1], c = tris[i][2];
        Vec3f A = { (f32)v[a].x, (f32)v[a].y, (f32)v[a].z };
        Vec3f B = { (f32)v[b].x, (f32)v[b].y, (f32)v[b].z };
        Vec3f C = { (f32)v[c].x, (f32)v[c].y, (f32)v[c].z };
        Vec3f ab = { B.x - A.x, B.y - A.y, B.z - A.z }, ac = { C.x - A.x, C.y - A.y, C.z - A.z };
        Vec3f n = { ab.y * ac.z - ab.z * ac.y, ab.z * ac.x - ab.x * ac.z, ab.x * ac.y - ab.y * ac.x };
        if (n.x * (A.x - mid.x) + n.y * (A.y - mid.y) + n.z * (A.z - mid.z) < 0) {
            std::swap(b, c);
            n = { -n.x, -n.y, -n.z };
        }
        f32 len = sqrtf(n.x * n.x + n.y * n.y + n.z * n.z);
        CollisionPoly p;
        memset(&p, 0, sizeof(p));
        p.type = (ladderFaces >> i) & 1; // the surface index
        p.flags_vIA = base + a;
        p.flags_vIB = base + b;
        p.vIC = base + c;
        p.normal.x = (s16)(n.x / len * 0x7FFF);
        p.normal.y = (s16)(n.y / len * 0x7FFF);
        p.normal.z = (s16)(n.z / len * 0x7FFF);
        p.dist = (s16)(-(n.x * A.x + n.y * A.y + n.z * A.z) / len);
        shape.polys.push_back(p);
    }
}

// A box; ladderFaces bit 0 marks its -z face, bit 1 its +z face as climbable.
static void AddBox(ShapeCollision& shape, s16 x0, s16 x1, s16 y0, s16 y1, s16 z0, s16 z1, int ladderFaces = 0) {
    std::vector<Vec3s> v = { { x0, y0, z0 }, { x1, y0, z0 }, { x1, y0, z1 }, { x0, y0, z1 },
                             { x0, y1, z0 }, { x1, y1, z0 }, { x1, y1, z1 }, { x0, y1, z1 } };
    std::vector<std::array<u16, 3>> tris = {
        { 0, 4, 5 }, { 0, 5, 1 }, // -z
        { 3, 2, 6 }, { 3, 6, 7 }, // +z
        { 0, 1, 2 }, { 0, 2, 3 }, // bottom
        { 4, 7, 6 }, { 4, 6, 5 }, // top
        { 0, 3, 7 }, { 0, 7, 4 }, // -x
        { 1, 5, 6 }, { 1, 6, 2 }, // +x
    };
    AddConvex(shape, v, tris, (ladderFaces & 1 ? 0x3 : 0) | (ladderFaces & 2 ? 0xC : 0));
}

static void BuildShape(ShapeCollision& shape, uint8_t type) {
    const PlaceableInfo& info = GetPlaceableInfo(type);
    s16 hx = info.halfX, hz = info.halfZ, h = info.height;
    switch (type) {
        case PLACEABLE_DECK: {
            // The planks on top and the four corner posts: Link walks under it.
            const s16 slab = 8, post = 5, at = hx - post;
            AddBox(shape, -hx, hx, h - slab, h, -hz, hz);
            for (int i = 0; i < 4; i++) {
                s16 px = (i & 1) ? at : -at, pz = (i & 2) ? at : -at;
                AddBox(shape, px - post, px + post, 0, h - slab, pz - post, pz + post);
            }
            break;
        }
        case PLACEABLE_STAIRS: {
            // A ramp rising towards +z (away from Link as he places it).
            std::vector<Vec3s> v = { { (s16)-hx, 0, (s16)-hz }, { hx, 0, (s16)-hz }, { hx, 0, hz },
                                     { (s16)-hx, 0, hz },       { hx, h, hz },       { (s16)-hx, h, hz } };
            AddConvex(shape, v,
                      { { 0, 1, 2 }, { 0, 2, 3 },   // bottom
                        { 0, 1, 4 }, { 0, 4, 5 },   // the slope
                        { 3, 2, 4 }, { 3, 4, 5 },   // the back, under the top step
                        { 1, 2, 4 }, { 0, 3, 5 } }); // the sides
            break;
        }
        case PLACEABLE_LADDER:
            AddBox(shape, -hx, hx, 0, h, -hz, hz, 0x3); // both faces climb
            break;
        case PLACEABLE_CHAIR_INN:
        case PLACEABLE_CHAIR_MILKBAR:
        case PLACEABLE_BENCH:
        case PLACEABLE_BED_INN:
        case PLACEABLE_BED_MAYOR:
            // #3962: only up to the seat or the mattress, which Link sits or lies on.
            AddBox(shape, -hx, hx, 0, FurnitureSeatHeight(type), -hz, hz);
            break;
        default:
            AddBox(shape, -hx, hx, 0, h, -hz, hz);
            break;
    }
}

static void BuildSurfaces() {
    sSurfaces[0].data[0] = 0x00000000;
    sSurfaces[0].data[1] = 0x000007C0; // wood-ish floor sound, normal walls
    sSurfaces[1].data[0] = 2 << 21;    // wall type 2: a ladder
    sSurfaces[1].data[1] = 0x000007C0;
    CollisionHeader& hdr = sHeaderTemplate;
    memset(&hdr, 0, sizeof(hdr));
    hdr.surfaceTypeList = sSurfaces;
    hdr.cameraDataList = nullptr;
    hdr.numWaterBoxes = 0;
    hdr.waterBoxes = nullptr;
}

// MARK: - SevenDays_BaseCollision: the pieces' boxes, one header per chunk

constexpr f32 CHUNK_SIZE = 640.0f;
constexpr int CHUNK_MAX = 24; // a BASE_RADIUS base spans at most 4x4 chunks

struct CollisionChunk {
    bool used = false;
    int cx = 0, cz = 0;
    Actor* actor = nullptr; // the SevenDays_BaseCollision actor (params = slot)
    s32 bgId = BG_ACTOR_MAX;
    std::vector<Vec3s> verts;
    std::vector<CollisionPoly> polys;
    CollisionHeader header;
};
static CollisionChunk sChunks[CHUNK_MAX];
static bool sCollisionDirty = false;

// Fill a chunk's header with the boxes of its pieces, relative to its actor.
static void FillChunk(CollisionChunk& ch, const std::vector<Actor*>& pieces) {
    ch.verts.clear();
    ch.polys.clear();
    Vec3f origin = ch.actor->world.pos;
    s16 minX = 0x7FFF, minY = 0x7FFF, minZ = 0x7FFF, maxX = -0x7FFF, maxY = -0x7FFF, maxZ = -0x7FFF;
    for (Actor* a : pieces) {
        const ShapeCollision& box = sShapes[((PlaceableActor*)a)->type];
        // The same transform DynaPoly_ExpandSRT applied to a piece's own header.
        MtxF mtx;
        SkinMatrix_SetTranslateRotateYXZScale(&mtx, 1.0f, 1.0f, 1.0f, 0, a->shape.rot.y, 0, a->world.pos.x - origin.x,
                                              a->world.pos.y - origin.y, a->world.pos.z - origin.z);
        u16 base = (u16)ch.verts.size();
        for (const Vec3s& v : box.verts) {
            Vec3f in = { (f32)v.x, (f32)v.y, (f32)v.z }, out;
            SkinMatrix_Vec3fMtxFMultXYZ(&mtx, &in, &out);
            Vec3s o = { (s16)lroundf(out.x), (s16)lroundf(out.y), (s16)lroundf(out.z) };
            minX = std::min(minX, o.x), minY = std::min(minY, o.y), minZ = std::min(minZ, o.z);
            maxX = std::max(maxX, o.x), maxY = std::max(maxY, o.y), maxZ = std::max(maxZ, o.z);
            ch.verts.push_back(o);
        }
        // A rotation about Y keeps each triangle's winding: ExpandSRT recomputes
        // the normals from the moved vertices.
        for (const CollisionPoly& p : box.polys) {
            CollisionPoly q = p;
            q.flags_vIA = p.flags_vIA + base;
            q.flags_vIB = p.flags_vIB + base;
            q.vIC = p.vIC + base;
            ch.polys.push_back(q);
        }
    }
    CollisionHeader& hdr = ch.header;
    hdr = sHeaderTemplate;
    hdr.minBounds = { minX, minY, minZ };
    hdr.maxBounds = { maxX, maxY, maxZ };
    hdr.numVertices = (u16)ch.verts.size();
    hdr.vtxList = ch.verts.data();
    hdr.numPolygons = (u16)ch.polys.size();
    hdr.polyList = ch.polys.data();
    hdr.surfaceTypeList = sSurfaces;
}

static bool ChunkAlive(const CollisionChunk& ch) {
    return ch.used && ch.actor != nullptr && ch.actor->update != nullptr;
}

// Regroup every live piece into chunks and rebuild the headers that changed
// hands. Runs when a piece spawns or goes away, never per frame.
static void RebuildBaseCollision(PlayState* play) {
    if (!sCollisionDirty) {
        return;
    }
    sCollisionDirty = false;
    // Forget chunks whose actor is not in this scene's list (a reset that
    // skipped Collision_Destroy must not leave a dangling pointer).
    bool seen[CHUNK_MAX] = {};
    for (Actor* a = play->actorCtx.actorLists[ACTORCAT_BG].head; a != nullptr; a = a->next) {
        if (a->id == sCollisionId && a->params >= 0 && a->params < CHUNK_MAX && sChunks[a->params].actor == a) {
            seen[a->params] = true;
        }
    }
    for (int i = 0; i < CHUNK_MAX; i++) {
        if (!seen[i]) {
            sChunks[i].used = false;
            sChunks[i].actor = nullptr;
            sChunks[i].bgId = BG_ACTOR_MAX;
        }
    }
    std::vector<Actor*> byChunk[CHUNK_MAX];
    for (Actor* a = play->actorCtx.actorLists[ACTORCAT_BG].head; a != nullptr; a = a->next) {
        if (a->id != sPlaceableId || a->update == nullptr || ((PlaceableActor*)a)->type >= PLACEABLE_COUNT ||
            ((PlaceableActor*)a)->gatePassable || ((PlaceableActor*)a)->ruin) {
            continue;
        }
        int cx = (int)floorf(a->world.pos.x / CHUNK_SIZE), cz = (int)floorf(a->world.pos.z / CHUNK_SIZE);
        int slot = -1, freeSlot = -1;
        for (int i = 0; i < CHUNK_MAX; i++) {
            if (ChunkAlive(sChunks[i]) || !byChunk[i].empty()) {
                if (sChunks[i].cx == cx && sChunks[i].cz == cz) {
                    slot = i;
                    break;
                }
            } else if (freeSlot < 0) {
                freeSlot = i;
            }
        }
        if (slot < 0) {
            // Out of chunks (never for a BASE_RADIUS base): share the last one.
            slot = freeSlot >= 0 ? freeSlot : CHUNK_MAX - 1;
            if (freeSlot >= 0) {
                sChunks[slot].cx = cx;
                sChunks[slot].cz = cz;
            }
        }
        byChunk[slot].push_back(a);
    }
    int polys = 0, chunks = 0;
    for (int i = 0; i < CHUNK_MAX; i++) {
        CollisionChunk& ch = sChunks[i];
        if (byChunk[i].empty()) {
            if (ChunkAlive(ch)) {
                Actor_Kill(ch.actor);
            }
            ch.used = false;
            ch.actor = nullptr;
            continue;
        }
        if (!ChunkAlive(ch)) {
            ch.used = true;
            ch.bgId = BG_ACTOR_MAX;
            ch.actor = Actor_Spawn(&play->actorCtx, play, sCollisionId, (ch.cx + 0.5f) * CHUNK_SIZE,
                                   byChunk[i][0]->world.pos.y, (ch.cz + 0.5f) * CHUNK_SIZE, 0, 0, 0, (s16)i, false);
            if (ch.actor == nullptr) {
                ch.used = false;
                continue;
            }
            ch.actor->room = -1;
        }
        FillChunk(ch, byChunk[i]);
        if (ch.bgId >= BG_ACTOR_MAX) {
            ch.bgId = DynaPoly_SetBgActor(play, &play->colCtx.dyna, ch.actor, &ch.header);
        } else {
            // Same slot, new geometry: DynaPoly_Setup re-expands it next frame.
            func_8003EE6C(play, &play->colCtx.dyna);
        }
        polys += ch.header.numPolygons;
        chunks++;
    }
    ESYNC_LOG("[SevenDays] base collision: {} polys in {} chunks (dyna max {})", polys, chunks,
                play->colCtx.dyna.polyListMax);
}

static void Collision_Init(Actor* thisx, PlayState* play) {
    DynaPolyActor_Init((DynaPolyActor*)thisx, 0);
    thisx->room = -1;
    Actor_SetScale(thisx, 1.0f);
}

static void Collision_Destroy(Actor* thisx, PlayState* play) {
    if (thisx->params < 0 || thisx->params >= CHUNK_MAX) {
        return;
    }
    CollisionChunk& ch = sChunks[thisx->params];
    if (ch.actor != thisx) {
        return;
    }
    if (ch.bgId < BG_ACTOR_MAX) {
        DynaPoly_DeleteBgActor(play, &play->colCtx.dyna, ch.bgId);
    }
    ch.used = false;
    ch.actor = nullptr;
    ch.bgId = BG_ACTOR_MAX;
    // A scene change takes the pieces with it; a lone kill (an actor-list purge)
    // gets rebuilt from whatever pieces are left.
    sCollisionDirty = true;
}

static void Collision_Update(Actor* thisx, PlayState* play) {
    RebuildBaseCollision(play);
}

// #3916: grow the dynamic collision lists where a base can stand
// (BgCheck_Allocate, z_bgcheck.c). DYNA_BUDGET polys/vertices/nodes (8192: about 210 KB of the
// play arena, #4062 for 256 pieces) next to the scene's own movers.
extern "C" s32 SevenDays_DynaBudget(s16 sceneNum) {
    return BaseEnabled() && IsOutdoorScene(sceneNum) ? SevenDays::DYNA_BUDGET : 0;
}

SevenDays::CollisionCost SevenDays::PieceCollisionCost(uint8_t type) {
    if (type >= PLACEABLE_COUNT || type == PLACEABLE_GATE) {
        return { 0, 0 }; // a gate swings open for players: it is not in the chunks (gatePassable)
    }
    return { (int)sShapes[type].polys.size(), (int)sShapes[type].verts.size() };
}

void SevenDays::CollisionInUse(int& polys, int& verts, int& chunks) {
    polys = verts = chunks = 0;
    for (const CollisionChunk& ch : sChunks) {
        if (ChunkAlive(ch)) {
            polys += ch.header.numPolygons;
            verts += ch.header.numVertices;
            chunks++;
        }
    }
}

// MARK: - Drawing

static void DrawDL(PlayState* play, Gfx* dl, float tx, float ty, float tz, float sx, float sy, float sz,
                   float rz = 0.0f) {
    OPEN_DISPS(play->state.gfxCtx);
    Matrix_Push();
    Matrix_Translate(tx, ty, tz, MTXMODE_APPLY);
    if (rz != 0.0f) {
        Matrix_RotateZ(rz, MTXMODE_APPLY);
    }
    Matrix_Scale(sx, sy, sz, MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_OPA_DISP++, dl);
    Matrix_Pop();
    CLOSE_DISPS(play->state.gfxCtx);
}

// The treasure chest is a skeleton (base + lid limbs), posed closed like En_Box's
// frame 0. One shared pose serves every chest and the ghost: nothing animates it.
// A ruin's chest has a pose of its own: the lid hangs open, long since looted.
static SkelAnime sChestSkel[2];
static Vec3s sChestJoints[2][5];
static Vec3s sChestMorph[2][5];
static bool sChestReady = false;

static void ChestPostLimbDraw(PlayState* play, s32 limbIndex, Gfx** dList, Vec3s* rot, void* arg) {
    if (limbIndex == 1 || limbIndex == 3) {
        OPEN_DISPS(play->state.gfxCtx);
        gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(POLY_OPA_DISP++,
                       limbIndex == 1 ? (Gfx*)gTreasureChestChestFrontDL : (Gfx*)gTreasureChestChestSideAndLidDL);
        CLOSE_DISPS(play->state.gfxCtx);
    }
}

static void DrawChest(PlayState* play, bool ruin) {
    if (!sChestReady) {
        AnimationHeader* anim = (AnimationHeader*)gTreasureChestAnim_00024C;
        for (int i = 0; i < 2; i++) {
            f32 frame = i == 0 ? 0.0f : Animation_GetLastFrame(anim) * 0.6f;
            SkelAnime_Init(play, &sChestSkel[i], (SkeletonHeader*)gTreasureChestSkel, anim, sChestJoints[i],
                           sChestMorph[i], 5);
            Animation_Change(&sChestSkel[i], anim, 0.0f, frame, frame, ANIMMODE_ONCE, 0.0f);
            SkelAnime_Update(&sChestSkel[i]);
        }
        sChestReady = true;
    }
    SkelAnime& skel = sChestSkel[ruin ? 1 : 0];
    OPEN_DISPS(play->state.gfxCtx);
    Matrix_Push();
    Matrix_RotateY(M_PI, MTXMODE_APPLY); // En_Box turns the model around too
    Matrix_Scale(0.01f, 0.01f, 0.01f, MTXMODE_APPLY);
    // Segment 8 is En_Box's render-mode hook; an empty list keeps the opaque mode.
    Gfx* empty = (Gfx*)Graph_Alloc(play->state.gfxCtx, sizeof(Gfx));
    gSPEndDisplayList(empty);
    gSPSegment(POLY_OPA_DISP++, 0x08, (uintptr_t)empty);
    gDPSetEnvColor(POLY_OPA_DISP++, 0, 0, 0, 255);
    CLOSE_DISPS(play->state.gfxCtx);
    SkelAnime_DrawOpa(play, skel.skeleton, skel.jointTable, nullptr, ChestPostLimbDraw, nullptr);
    Matrix_Pop();
}

// A model drawn about the Y axis too (yaw in binary angle units).
static void DrawDLYaw(PlayState* play, Gfx* dl, float tx, float ty, float tz, s16 yaw, float sx, float sy,
                      float sz, float rz = 0.0f) {
    Matrix_Push();
    Matrix_Translate(tx, ty, tz, MTXMODE_APPLY);
    Matrix_RotateY(BINANG_TO_RAD(yaw), MTXMODE_APPLY);
    DrawDL(play, dl, 0.0f, 0.0f, 0.0f, sx, sy, sz, rz);
    Matrix_Pop();
}

// Palisade wall: five Majora's Mask practice logs (630 tall, 182 wide at scale 1)
// stood side by side, about 96 units tall. Below half HP one log has fallen
// against its neighbour and another has snapped off. A ruin is stumps: one log lies
// fallen across them and one is gone.
static void DrawPalisade(PlayState* play, float hpFrac, bool ruin) {
    if (!MMPackLoaded()) {
        // No MM pack: the horse-jump fence stretched to the wall's height.
        DrawDL(play, (Gfx*)gJumpableHorseFenceDL, 0.0f, 0.0f, 0.0f, 0.0375f, ruin ? 0.05f : 0.12f, 0.1f);
        return;
    }
    static const float kHeight[5] = { 1.0f, 0.95f, 1.04f, 0.97f, 1.01f };
    static const float kStump[5] = { 0.38f, 0.0f, 0.5f, 0.0f, 0.3f };
    static const s16 kYaw[5] = { 0x0000, 0x3000, 0x6800, 0x9C00, 0xD000 };
    const float s = 96.0f / 630.0f;
    for (int i = 0; i < 5; i++) {
        float x = -48.0f + 24.0f * i, sy = s * kHeight[i], rz = 0.0f;
        if (ruin) {
            if (i == 3) {
                continue; // rotted away
            }
            if (i == 1) {
                // Fallen flat along the wall, at the stumps' feet.
                DrawDL(play, (Gfx*)gMMPracticeLogDL, -44.0f, 8.0f, 18.0f, s, s * 0.9f, s, -1.5f);
                continue;
            }
            sy = s * kStump[i];
        } else if (hpFrac < 0.5f && i == 1) {
            sy *= 0.55f; // snapped off
        } else if (hpFrac < 0.5f && i == 3) {
            rz = -0.3f; // leaning on the log beside it
        }
        DrawDLYaw(play, (Gfx*)gMMPracticeLogDL, x, 0.0f, 0.0f, kYaw[i], s, sy, s, rz);
    }
}

// Workbench: the Stock Pot Inn's desk with drawers (44 x 29 x 29 in the room, at
// -435..-391, 210..239, 360..389), scaled 1.2x, with Gabora's smithing hammer and a
// red-hot sword blank from the Mountain Village smithy lying on top. #3856: it was
// 1.8x (52 tall), over child Link's head; 1.2x puts the top at 35, an adult's hip and a
// child's chest, and a child still clambers up onto it.
static void DrawWorkbench(PlayState* play, bool ruin) {
    if (!MMPackLoaded()) {
        // No MM pack: the dungeon shop's wooden shelves at half size.
        DrawDL(play, (Gfx*)gShopDungenWoodenShelvesDL, 0.0f, 0.0f, 9.0f, 0.5f, 0.5f, 0.5f);
        return;
    }
    const float s = 1.2f, top = 29.0f * s;
    const float k = s / 1.8f; // the tools were placed on the 1.8x desk
    DrawDL(play, (Gfx*)gMMInnDeskDL, 413.0f * s, -210.0f * s, -374.5f * s, s, s, s);
    if (ruin) {
        return; // someone walked off with the hammer and the blade
    }
    OPEN_DISPS(play->state.gfxCtx);
    // Segments 8 and 9 are En_Kgy's render-mode hooks, which the hammer calls too: empty
    // lists keep the opaque mode (left to chance, whatever drew before decides).
    Gfx* empty = (Gfx*)Graph_Alloc(play->state.gfxCtx, sizeof(Gfx));
    gSPEndDisplayList(empty);
    gSPSegment(POLY_OPA_DISP++, 0x08, (uintptr_t)empty);
    gSPSegment(POLY_OPA_DISP++, 0x09, (uintptr_t)empty);
    // The hammer stands with its handle along +y and its head across x (315..3034)
    // at 0.01. Laid flat: handle along x, head pointing back, resting on the top.
    Matrix_Push();
    Matrix_Translate(-10.0f * k, top + 4.6f * k, -2.0f * k, MTXMODE_APPLY);
    Matrix_RotateX(M_PI / 2, MTXMODE_APPLY);
    Matrix_RotateZ(-M_PI / 2, MTXMODE_APPLY);
    Matrix_Scale(0.006f * k, 0.006f * k, 0.006f * k, MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_OPA_DISP++, (Gfx*)gMMSmithyHammerDL);
    Matrix_Pop();
    // The blade (x 426..1727, its width along y) laid flat in front of the hammer.
    Matrix_Push();
    Matrix_Translate(-21.0f * k, top + 1.6f * k, 12.0f * k, MTXMODE_APPLY);
    Matrix_RotateX(M_PI / 2, MTXMODE_APPLY);
    Matrix_Scale(0.02f * k, 0.02f * k, 0.02f * k, MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_OPA_DISP++, (Gfx*)gMMSmithyBladeDL);
    Matrix_Pop();
    CLOSE_DISPS(play->state.gfxCtx);
}

// MARK: - #3945: floors, stairs and doors

// Without the pack: OoT's push block (8000 units square at scale 1) stretched over the box.
static void DrawFallbackBox(PlayState* play, uint8_t type) {
    const PlaceableInfo& info = GetPlaceableInfo(type);
    DrawDL(play, (Gfx*)gBlockSmallDL, 0.0f, 0.0f, 0.0f, info.halfX / 4000.0f, info.height / 8000.0f,
           info.halfZ / 4000.0f);
}

// The whole-piece wash Placeable_Draw set (damage, ruin moss), if any. Models that wash
// themselves (wood, iron, the tamed Baba) put it back when they are done instead of
// switching grayscale off under the rest of the piece.
static bool sPieceTint = false;
static u8 sPieceTintColor[4];

static void SetPieceTint(PlayState* play, bool on, u8 r = 0, u8 g = 0, u8 b = 0, u8 lerp = 0) {
    sPieceTint = on;
    sPieceTintColor[0] = r;
    sPieceTintColor[1] = g;
    sPieceTintColor[2] = b;
    sPieceTintColor[3] = lerp;
    OPEN_DISPS(play->state.gfxCtx);
    if (on) {
        gDPSetGrayscaleColor(POLY_OPA_DISP++, r, g, b, lerp);
    }
    gSPGrayscale(POLY_OPA_DISP++, on);
    CLOSE_DISPS(play->state.gfxCtx);
}

static void RestorePieceTint(PlayState* play) {
    SetPieceTint(play, sPieceTint, sPieceTintColor[0], sPieceTintColor[1], sPieceTintColor[2], sPieceTintColor[3]);
}

// The Pirates' Fortress panel is grey, weathered wood: washed a little warmer, unless the
// piece is already tinted (damaged, a ruin, the ghost).
static void WoodWash(PlayState* play, bool on) {
    if (!on) {
        RestorePieceTint(play);
        return;
    }
    OPEN_DISPS(play->state.gfxCtx);
    gDPSetGrayscaleColor(POLY_OPA_DISP++, 205, 160, 110, 150);
    gSPGrayscale(POLY_OPA_DISP++, true);
    CLOSE_DISPS(play->state.gfxCtx);
}

// The panel (1200 square and 80 thick at scale 1, standing up from y = 0) as a w x d x t
// slab: flat (d along z, t thick) or upright (d tall, t along z), its bottom at y.
static void DrawPanel(PlayState* play, f32 x, f32 y, f32 z, s16 yaw, f32 w, f32 d, f32 t, bool flat) {
    OPEN_DISPS(play->state.gfxCtx);
    Matrix_Push();
    Matrix_Translate(x, flat ? y + t / 2 : y, z, MTXMODE_APPLY);
    Matrix_RotateY(BINANG_TO_RAD(yaw), MTXMODE_APPLY);
    if (flat) {
        Matrix_RotateX(M_PI / 2, MTXMODE_APPLY);
        Matrix_Scale(w / 1200.0f, d / 1200.0f, t / 80.0f, MTXMODE_APPLY);
        Matrix_Translate(0.0f, -600.0f, 0.0f, MTXMODE_APPLY);
    } else {
        Matrix_Scale(w / 1200.0f, d / 1200.0f, t / 80.0f, MTXMODE_APPLY);
    }
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_OPA_DISP++, (Gfx*)gMMPiratePanelDL);
    Matrix_Pop();
    CLOSE_DISPS(play->state.gfxCtx);
}

// Wooden step: a 60 x 52 x 60 box of panels, low enough for Link to hop onto; a
// palisade with a floor on it is one more hop up. (Majora's Mask's own "wooden step"
// turned out to be a bent walkway plank, so it is built from the planks instead.)
// A ruin's has caved in: the top is gone and one side lies flat beside it.
static void DrawStep(PlayState* play, bool wash, bool ruin) {
    WoodWash(play, wash);
    for (int i = 0; i < 4; i++) {
        s16 yaw = (s16)(i * 0x4000);
        f32 sx = Math_SinS(yaw) * 28.0f, sz = Math_CosS(yaw) * 28.0f;
        if (ruin && i == 0) {
            DrawPanel(play, 0.0f, 0.0f, 58.0f, 0, 60.0f, 48.0f, 4.0f, true);
            continue;
        }
        DrawPanel(play, sx, 0.0f, sz, yaw, 60.0f, 48.0f, 4.0f, false);
    }
    if (!ruin) {
        DrawPanel(play, 0.0f, 48.0f, 0.0f, 0, 60.0f, 60.0f, 4.0f, true);
    }
    WoodWash(play, false);
}

// A ruin's plank floor has rotted through: the middle third is gone and the boards left
// either side have warped up off the ground.
static void DrawRuinedPlankFloor(PlayState* play) {
    for (int side = -1; side <= 1; side += 2) {
        Matrix_Push();
        Matrix_Translate(0.0f, 0.0f, side * 40.0f, MTXMODE_APPLY);
        Matrix_RotateX(side * 0.12f, MTXMODE_APPLY);
        DrawPanel(play, 0.0f, 0.0f, 0.0f, 0, 120.0f, 40.0f, 8.0f, true);
        Matrix_Pop();
    }
}

// Ranch floor: three planks from the Romani Ranch house (40 x 6 x 164 in the room, at
// 600..640, 57..63, -100..64), side by side and cut to 120 long.
// A ruin's has lost its middle plank, and one of the others has slewed round.
static void DrawRanchFloor(PlayState* play, bool ruin) {
    OPEN_DISPS(play->state.gfxCtx);
    for (int i = -1; i <= 1; i++) {
        if (ruin && i == 0) {
            continue;
        }
        Matrix_Push();
        Matrix_Translate(i * 40.0f, 0.0f, 0.0f, MTXMODE_APPLY);
        if (ruin && i == 1) {
            Matrix_RotateY(0.35f, MTXMODE_APPLY);
        }
        Matrix_Scale(1.0f, 1.0f, 120.0f / 164.0f, MTXMODE_APPLY);
        Matrix_Translate(-620.0f, -57.0f, 18.0f, MTXMODE_APPLY);
        gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(POLY_OPA_DISP++, (Gfx*)gMMRanchPlankDL);
        Matrix_Pop();
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

// The Stock Pot Inn's lobby stairs (240 run x 210 rise x 127 wide in the room, rising
// towards -x, at -30..210, 0..210, -150..-23), turned to rise towards +z and scaled to a
// storey. Drawn a second time mirrored across the middle: that closes the open side,
// puts a banister on both sides and gives the ramp an underside.
// A ruin's has lost one half: the open side shows, and one banister.
static void DrawStairs(PlayState* play, bool ruin) {
    OPEN_DISPS(play->state.gfxCtx);
    for (int mirror = 0; mirror < (ruin ? 1 : 2); mirror++) {
        Matrix_Push();
        Matrix_RotateY(M_PI / 2, MTXMODE_APPLY);
        Matrix_Scale(0.5f, (f32)STOREY_HEIGHT / 210.0f, 0.5f, MTXMODE_APPLY);
        if (mirror) {
            Matrix_Scale(1.0f, 1.0f, -1.0f, MTXMODE_APPLY);
        }
        Matrix_Translate(-90.0f, 0.0f, 86.5f, MTXMODE_APPLY);
        gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(POLY_OPA_DISP++, (Gfx*)gMMInnStairsDL);
        Matrix_Pop();
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

// The 12-rung ladder (300 wide, 1950 tall, 40 deep at scale 1) cut down to a storey.
static void DrawLadder(PlayState* play) {
    OPEN_DISPS(play->state.gfxCtx);
    // Segment 0x0C is Bg_Ladder's render-mode hook (called at +0x10): empty lists keep
    // the opaque mode.
    Gfx* empty = (Gfx*)Graph_Alloc(play->state.gfxCtx, 3 * sizeof(Gfx));
    for (int i = 0; i < 3; i++) {
        gSPEndDisplayList(&empty[i]);
    }
    gSPSegment(POLY_OPA_DISP++, 0x0C, (uintptr_t)empty);
    CLOSE_DISPS(play->state.gfxCtx);
    DrawDL(play, (Gfx*)gMMLadderDL, 0.0f, 0.0f, -2.0f, 0.1f, (f32)STOREY_HEIGHT / 1950.0f, 0.1f);
}

// Doors: two leaves of a Majora's Mask door (one 6000 x 10000 leaf at scale 1, hinged
// at x = 0 and lying down, its height along +z), 60 wide and 100 tall each, hinged at
// the wall's ends like the player gate; they swing out to 90 degrees.
// A ruin's: one leaf hangs half open and sags, the other lies flat on the ground.
static void DrawDoor(PlayState* play, Gfx* leaf, f32 open, bool ruin) {
    s16 swing = (s16)((ruin ? 0.4f : open) * 0x4000);
    OPEN_DISPS(play->state.gfxCtx);
    for (int side = 0; side < 2; side++) {
        Matrix_Push();
        Matrix_Translate(side == 0 ? -60.0f : 60.0f, ruin && side == 1 ? 1.0f : 0.0f, 0.0f, MTXMODE_APPLY);
        Matrix_RotateY((side == 0 ? -swing : (s16)(0x8000 + swing)) * (M_PI / 0x8000), MTXMODE_APPLY);
        if (ruin && side == 0) {
            Matrix_RotateZ(-0.12f, MTXMODE_APPLY); // off its top hinge
        }
        if (!ruin || side == 0) {
            Matrix_RotateX(-M_PI / 2, MTXMODE_APPLY); // stand it up (the model lies flat)
        }
        Matrix_Scale(0.01f, 0.01f, 0.01f, MTXMODE_APPLY);
        gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(POLY_OPA_DISP++, leaf);
        Matrix_Pop();
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

// MARK: - #3962: furniture

// A model cut from a Majora's Mask room or object, moved so that its footprint is centred
// on the origin with its bottom at y = 0 (native `at` is that point in the model's own
// coordinates), turned by `yaw` and scaled in the piece's frame. Pieces face -z, towards
// Link as he places them: a chair's back and a bed's head are at +z.
static void DrawCut(PlayState* play, const char* dl, Vec3f at, s16 yaw, f32 sx, f32 sy, f32 sz) {
    OPEN_DISPS(play->state.gfxCtx);
    Matrix_Push();
    Matrix_Scale(sx, sy, sz, MTXMODE_APPLY);
    Matrix_RotateY(BINANG_TO_RAD(yaw), MTXMODE_APPLY);
    Matrix_Translate(-at.x, -at.y, -at.z, MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_OPA_DISP++, (Gfx*)dl);
    Matrix_Pop();
    CLOSE_DISPS(play->state.gfxCtx);
}

// Without the pack: the push block stretched over the box, like the floors.
static void DrawFurniture(PlayState* play, uint8_t type) {
    if (!MMFurniturePackLoaded()) {
        DrawFallbackBox(play, type);
        return;
    }
    const f32 s = 1.3f; // the house furniture at Link's size
    switch (type) {
        case PLACEABLE_CHAIR_INN:
            // Stock Pot Inn room 2 (25 x 44 x 24 at -432..-407, 210..254, -69..-45), back towards +x.
            DrawCut(play, gMMInnChairDL, { -419.5f, 210.0f, -57.0f }, -0x4000, s, s, s);
            break;
        case PLACEABLE_CHAIR_MILKBAR:
            // object_mbar_obj (154 x 420 x 168 at scale 1), back towards -z.
            DrawCut(play, gMMMilkBarChairDL, { 0.0f, 0.0f, 0.0f }, -0x8000, s * 0.1f, s * 0.1f, s * 0.1f);
            break;
        case PLACEABLE_BENCH: {
            // The inn lobby's bench (room 0: 35 x 30 x 150 at 285..320, 0..30, 120..270), rails
            // towards +x, cut down to a floor tile's length. The lobby's lists call segment 8.
            OPEN_DISPS(play->state.gfxCtx);
            Gfx* empty = (Gfx*)Graph_Alloc(play->state.gfxCtx, sizeof(Gfx));
            gSPEndDisplayList(empty);
            gSPSegment(POLY_OPA_DISP++, 0x08, (uintptr_t)empty);
            CLOSE_DISPS(play->state.gfxCtx);
            DrawCut(play, gMMInnBenchDL, { 302.5f, 0.0f, 195.0f }, -0x4000, 120.0f / 150.0f, s, s);
            break;
        }
        case PLACEABLE_BED_INN:
            // Room 2 (72 x 24 x 108 at -591..-519, 210..234, -236..-128), pillow towards -z.
            DrawCut(play, gMMInnBedDL, { -555.0f, 210.0f, -182.0f }, -0x8000, s, s, s);
            break;
        case PLACEABLE_BED_MAYOR:
            // Mayor's Residence room 3 (105 x 36 x 72 at 570..675, 0..36, -51..21), head towards +x.
            DrawCut(play, gMMMayorBedDL, { 622.5f, 0.0f, -15.0f }, -0x4000, s, s, s);
            break;
        case PLACEABLE_DRESSER:
            // Room 2 (30 x 45 x 15 at -465..-435, 210..255, -240..-225), against the wall at -z.
            DrawCut(play, gMMInnDresserDL, { -450.0f, 210.0f, -232.5f }, -0x8000, s, s, s);
            break;
        case PLACEABLE_DRAWERS:
            // object_kin2_obj (300 x 450 x 200, its back at z = 0).
            DrawCut(play, gMMDrawersDL, { 0.0f, 0.0f, 100.0f }, -0x8000, s * 0.1f, s * 0.1f, s * 0.1f);
            break;
        case PLACEABLE_BOOKSHELF:
            // object_kin2_obj (120 x 120 x 30, its back at z = 0), a palisade tall.
            DrawCut(play, gMMBookshelfDL, { 0.0f, 0.0f, 15.0f }, -0x8000, 0.8f, 0.8f, 0.8f);
            break;
        case PLACEABLE_PAINTING:
            // object_kin2_obj (450 x 563 x 40, its back at z = 0), stood on the floor.
            DrawCut(play, gMMPaintingDL, { 0.0f, 0.0f, 20.0f }, -0x8000, s * 0.1f, s * 0.1f, s * 0.1f);
            break;
        case PLACEABLE_MILKCAN:
            // Romani Ranch house room 1 (39 x 53 x 34 at 1099..1138, 0..53, -180..-146).
            DrawCut(play, gMMMilkCanDL, { 1118.5f, 0.0f, -163.0f }, 0, 1.0f, 1.0f, 1.0f);
            break;
        case PLACEABLE_RUG: {
            // Hung on the ranch house wall in room 2 (5 thick in x, 23 x 23 at 784..789,
            // 308..331, -110..-87): laid flat on the floor and spread to 92 across.
            OPEN_DISPS(play->state.gfxCtx);
            Matrix_Push();
            Matrix_Translate(0.0f, 0.6f, 0.0f, MTXMODE_APPLY);
            Matrix_Scale(4.0f, 0.2f, 4.0f, MTXMODE_APPLY);
            Matrix_RotateZ(M_PI / 2, MTXMODE_APPLY); // its face (+x) up
            Matrix_Translate(-786.5f, -319.5f, 98.5f, MTXMODE_APPLY);
            gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPDisplayList(POLY_OPA_DISP++, (Gfx*)gMMRugDL);
            Matrix_Pop();
            CLOSE_DISPS(play->state.gfxCtx);
            break;
        }
        case PLACEABLE_BARREL:
            // object_taru's barrel (600 x 600 x 540).
            DrawCut(play, gMMBarrelDL, { 0.0f, 0.0f, 0.0f }, 0, 0.1f, 0.1f, 0.1f);
            break;
        case PLACEABLE_BARREL_ROMANI:
            // Ranch house room 0 (57 x 40 x 49 at -392..-335, 0..40, -198..-149).
            DrawCut(play, gMMRomaniBarrelDL, { -363.5f, 0.0f, -173.5f }, 0, s, s, s);
            break;
        case PLACEABLE_WAGONWHEEL:
            // Ranch house room 0 (65 x 65 x 8 at -65..0, 15..80, 90..98), stood up on its rim.
            DrawCut(play, gMMWagonWheelDL, { -32.5f, 15.0f, 94.0f }, 0, 1.0f, 1.0f, 1.0f);
            break;
        case PLACEABLE_STALL:
            // The carnival tower's base (1360 square, 800 tall): its cloth walls and posts.
            DrawCut(play, gMMFestivalStallDL, { 0.0f, 0.0f, 0.0f }, 0, 120.0f / 1360.0f, 70.0f / 800.0f,
                    120.0f / 1360.0f);
            break;
    }
}

// The model of each type, in the actor's local space (origin on the floor).
static void DrawBaba(PlayState* play, PlaceableActor* self, f32 lunge, s16 yaw);

static const s16 kBabaUp[3] = { -0x5555, -0x4000, -0x4000 };
static const s16 kBabaOut[3] = { -0xAAA, -0x1555, -0xE38 };

static void BabaAngles(f32 lunge, s16 out[3]) {
    for (int i = 0; i < 3; i++) {
        out[i] = (s16)(kBabaUp[i] + (kBabaOut[i] - kBabaUp[i]) * lunge);
    }
}

// A ruin's Baba wilted: the stalk arches over from the base and its tip lies on the
// ground (head end first, like the angles above).
static const s16 kBabaWilt[3] = { 0x2400, 0x0400, -0x2000 };

// Where the head sits relative to the stalk's base: up, and forward along its yaw.
// EnDekubaba draws its sections from the head down, so the sines come out negative.
static void BabaHeadAt(const s16 ang[3], f32* y, f32* z) {
    *y = *z = 0.0f;
    for (int i = 0; i < 3; i++) {
        *y -= 20.0f * BABA_SIZE * Math_SinS(ang[i]);
        *z += 20.0f * BABA_SIZE * Math_CosS(ang[i]);
    }
}

static void BabaHead(f32 lunge, f32* y, f32* z) {
    s16 ang[3];
    BabaAngles(lunge, ang);
    BabaHeadAt(ang, y, z);
}

static void DrawTorchFlame(PlayState* play, PlaceableActor* self);
static void DrawBombFlower(PlayState* play, PlaceableActor* self);
static void DrawGate(PlayState* play, f32 open);

static void DrawModel(PlayState* play, uint8_t type, float hpFrac, PlaceableActor* self = nullptr) {
    bool ruin = self != nullptr && self->ruin;
    switch (type) {
        case PLACEABLE_TORCH:
            DrawDL(play, (Gfx*)gWoodenTorchDL, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f);
            if (self != nullptr && !self->ruin) {
                DrawTorchFlame(play, self);
            }
            break;
        case PLACEABLE_STONEWALL:
            // Two push blocks (80 units at 0.01), 60 wide, deep and tall each.
            DrawDL(play, (Gfx*)gBlockSmallDL, -30.0f, 0.0f, 0.0f, 0.0075f, 0.0075f, 0.0075f);
            if (hpFrac >= 0.5f) {
                DrawDL(play, (Gfx*)gBlockSmallDL, 30.0f, 0.0f, 0.0f, 0.0075f, 0.0075f, 0.0075f);
            } else {
                DrawDL(play, (Gfx*)gBlockSmallDL, 30.0f, 0.0f, 0.0f, 0.0075f, 0.005f, 0.0075f); // knocked down
            }
            break;
        case PLACEABLE_IRONWALL:
            // The stone wall's blocks, washed steel blue-grey (ruins keep their moss), with an
            // iron grate across.
            {
                if (!ruin) {
                    OPEN_DISPS(play->state.gfxCtx);
                    gDPSetGrayscaleColor(POLY_OPA_DISP++, 120, 130, 150, 255);
                    gSPGrayscale(POLY_OPA_DISP++, true);
                    CLOSE_DISPS(play->state.gfxCtx);
                }
                DrawDL(play, (Gfx*)gBlockSmallDL, -30.0f, 0.0f, 0.0f, 0.0075f, 0.0075f, 0.0075f);
                DrawDL(play, (Gfx*)gBlockSmallDL, 30.0f, 0.0f, 0.0f, 0.0075f, hpFrac >= 0.5f ? 0.0075f : 0.005f,
                       0.0075f);
                RestorePieceTint(play);
            }
            break;
        case PLACEABLE_BOMBTRAP:
            DrawBombFlower(play, self);
            break;
        case PLACEABLE_GATE:
            DrawGate(play, self != nullptr ? self->gateOpen : 0.0f);
            break;
        case PLACEABLE_SCARECROW:
            if (self != nullptr && self->hasSkel) {
                OPEN_DISPS(play->state.gfxCtx);
                Matrix_Push();
                Matrix_Scale(0.01f, 0.01f, 0.01f, MTXMODE_APPLY);
                SkelAnime_DrawSkeletonOpa(play, &self->skel, nullptr, nullptr, &self->actor);
                Matrix_Pop();
                CLOSE_DISPS(play->state.gfxCtx);
            }
            break;
        case PLACEABLE_GUARDBABA:
            DrawBaba(play, self, self != nullptr ? self->babaLunge : 0.0f,
                     self != nullptr ? (s16)(self->babaYaw - self->actor.shape.rot.y) : 0);
            break;
        case PLACEABLE_BARRICADE:
            // The horse-jump fence (Hyrule Field / Lon Lon), stretched up to chest
            // height. Below half HP it sags and leans: it already took a beating.
            if (hpFrac >= 0.5f) {
                DrawDL(play, (Gfx*)gJumpableHorseFenceDL, 0.0f, 0.0f, 0.0f, 0.0375f, 0.06f, 0.1f);
            } else {
                DrawDL(play, (Gfx*)gJumpableHorseFenceDL, 0.0f, -4.0f, 0.0f, 0.0375f, 0.05f, 0.1f, -0.08f);
            }
            break;
        case PLACEABLE_PALISADE:
            DrawPalisade(play, hpFrac, ruin);
            break;
        case PLACEABLE_SPIKES:
            // A ruin's has lost its middle spike, and another lies knocked flat.
            for (int i = -1; i <= 1; i++) {
                if (ruin && i == 0) {
                    continue;
                }
                DrawDL(play, (Gfx*)gUnusedSpikeDL, i * 30.0f, 4.0f, 0.0f, 0.004f, 0.004f, 0.004f,
                       ruin && i == 1 ? -1.3f : 0.0f);
            }
            break;
        case PLACEABLE_WORKBENCH:
            DrawWorkbench(play, ruin);
            break;
        case PLACEABLE_CHEST:
            DrawChest(play, ruin);
            break;
        case PLACEABLE_SIGN:
            DrawDL(play, (Gfx*)gSignRectangularDL, 0.0f, 0.0f, 0.0f, 0.01f, 0.01f, 0.01f);
            break;
        case PLACEABLE_FLOOR_PLANK:
        case PLACEABLE_FLOOR_RANCH:
        case PLACEABLE_FLOOR_STONE:
        case PLACEABLE_DECK:
        case PLACEABLE_STEP:
        case PLACEABLE_LADDER:
        case PLACEABLE_STAIRS:
            if (!MMBuildPackLoaded()) {
                DrawFallbackBox(play, type);
            } else if (type == PLACEABLE_FLOOR_PLANK) {
                // Only wash pieces that aren't tinted already (damaged, ruins, the ghost).
                WoodWash(play, self != nullptr && hpFrac >= 0.5f);
                if (ruin) {
                    DrawRuinedPlankFloor(play);
                } else {
                    DrawPanel(play, 0.0f, 0.0f, 0.0f, 0, 120.0f, 120.0f, 8.0f, true);
                }
                WoodWash(play, false);
            } else if (type == PLACEABLE_FLOOR_RANCH) {
                DrawRanchFloor(play, ruin);
            } else if (type == PLACEABLE_FLOOR_STONE) {
                // A Woodfall Temple platform (1000 square, 200 thick, its top at y = 0).
                DrawDL(play, (Gfx*)gMMStonePlatformDL, 0.0f, 24.0f, 0.0f, 0.12f, 0.12f, 0.12f);
            } else if (type == PLACEABLE_DECK) {
                // The top of the Clock Town carnival tower (1360 square, 800 tall): planks on four posts.
                // A ruin's posts have snapped: it sits at half height.
                DrawDL(play, (Gfx*)gMMFestivalDeckDL, 0.0f, 0.0f, 0.0f, 120.0f / 1360.0f,
                       (f32)STOREY_HEIGHT / 800.0f * (ruin ? 0.5f : 1.0f), 120.0f / 1360.0f);
            } else if (type == PLACEABLE_STEP) {
                DrawStep(play, self != nullptr && hpFrac >= 0.5f, ruin);
            } else if (type == PLACEABLE_LADDER) {
                DrawLadder(play);
            } else {
                DrawStairs(play, ruin);
            }
            break;
        case PLACEABLE_DOOR_SWAMP:
        case PLACEABLE_DOOR_MUSIC:
        case PLACEABLE_DOOR_PIRATE: {
            f32 open = self != nullptr ? self->gateOpen : 0.0f;
            if (!MMBuildPackLoaded()) {
                DrawGate(play, open);
            } else {
                DrawDoor(play,
                         (Gfx*)(type == PLACEABLE_DOOR_SWAMP   ? gMMSwampDoorDL
                                : type == PLACEABLE_DOOR_MUSIC ? gMMMusicBoxDoorDL
                                                               : gMMPirateDoorDL),
                         open, ruin);
            }
            break;
        }
        case PLACEABLE_CHAIR_INN:
        case PLACEABLE_CHAIR_MILKBAR:
        case PLACEABLE_BENCH:
        case PLACEABLE_BED_INN:
        case PLACEABLE_BED_MAYOR:
        case PLACEABLE_DRESSER:
        case PLACEABLE_DRAWERS:
        case PLACEABLE_BOOKSHELF:
        case PLACEABLE_PAINTING:
        case PLACEABLE_MILKCAN:
        case PLACEABLE_RUG:
        case PLACEABLE_BARREL:
        case PLACEABLE_BARREL_ROMANI:
        case PLACEABLE_WAGONWHEEL:
        case PLACEABLE_STALL:
            DrawFurniture(play, type);
            break;
    }
}

// The Deku Baba's stalk and head drawn from home outward, head in front (+z in
// the frame yawed by `yaw`). Without an actor (the placement ghost) it is just the
// stalk and leaves; a ruin's is the stalk wilted over, headless, in the ruin's moss.
static void DrawBaba(PlayState* play, PlaceableActor* self, f32 lunge, s16 yaw) {
    const f32 size = BABA_SIZE, sc = 0.01f * size;
    bool ruin = self != nullptr && self->ruin;
    s16 ang[3];
    if (ruin) {
        memcpy(ang, kBabaWilt, sizeof(ang));
    } else {
        BabaAngles(lunge, ang);
    }
    f32 headY, headZ;
    BabaHeadAt(ang, &headY, &headZ);
    // Tamed: a warm gold-green wash over the usual jungle green (the ghost keeps its tint).
    bool tamed = self != nullptr && !self->ghost && !ruin;
    OPEN_DISPS(play->state.gfxCtx);
    if (tamed) {
        gDPSetGrayscaleColor(POLY_OPA_DISP++, 255, 230, 90, 70);
        gSPGrayscale(POLY_OPA_DISP++, true);
    }
    Matrix_Push();
    Matrix_RotateY(yaw * (M_PI / 0x8000), MTXMODE_APPLY);

    Matrix_Push();
    Matrix_Scale(sc, sc, sc, MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_OPA_DISP++, (Gfx*)gDekuBabaBaseLeavesDL);
    Matrix_Pop();

    static Gfx* stemDLists[] = { (Gfx*)gDekuBabaStemTopDL, (Gfx*)gDekuBabaStemMiddleDL,
                                 (Gfx*)gDekuBabaStemBaseDL };
    f32 y = headY, z = headZ;
    for (int i = 0; i < 3; i++) {
        y += 20.0f * size * Math_SinS(ang[i]);
        z -= 20.0f * size * Math_CosS(ang[i]);
        Matrix_Push();
        Matrix_Translate(0.0f, y, z, MTXMODE_APPLY);
        Matrix_RotateX(ang[i] * (M_PI / 0x8000), MTXMODE_APPLY);
        Matrix_Scale(sc, sc, sc, MTXMODE_APPLY);
        gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(POLY_OPA_DISP++, stemDLists[i]);
        Matrix_Pop();
    }
    if (self != nullptr && self->hasSkel && !ruin) {
        Matrix_Push();
        Matrix_Translate(0.0f, headY, headZ, MTXMODE_APPLY);
        Matrix_Scale(sc, sc, sc, MTXMODE_APPLY);
        SkelAnime_DrawSkeletonOpa(play, &self->skel, nullptr, nullptr, &self->actor);
        Matrix_Pop();
    }
    Matrix_Pop();
    CLOSE_DISPS(play->state.gfxCtx);
    if (tamed) {
        RestorePieceTint(play);
    }
}

// ObjSyokudai's flame: the scrolling fire billboard over the stand, facing the camera.
static void DrawTorchFlame(PlayState* play, PlaceableActor* self) {
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    gSPSegment(POLY_XLU_DISP++, 0x08,
               (uintptr_t)Gfx_TwoTexScroll(play->state.gfxCtx, 0, 0, 0, 0x20, 0x40, 1, 0,
                                ((sStudioType >= 0 ? 6 : play->gameplayFrames) * -20) & 0x1FF, 0x20, 0x80));
    if (self->wardLit) {
        gDPSetPrimColor(POLY_XLU_DISP++, 0x80, 0x80, 170, 255, 255, 255); // blue fire
        gDPSetEnvColor(POLY_XLU_DISP++, 0, 100, 255, 0);
    } else {
        gDPSetPrimColor(POLY_XLU_DISP++, 0x80, 0x80, 255, 255, 0, 255);
        gDPSetEnvColor(POLY_XLU_DISP++, 255, 0, 0, 0);
    }
    Matrix_Push();
    Matrix_Translate(0.0f, 52.0f, 0.0f, MTXMODE_APPLY);
    s16 toCamera = sStudioType >= 0 ? (s16)-sStudioTurn // the studio camera looks down -z
                                    : (s16)(Camera_GetCamDirYaw(GET_ACTIVE_CAM(play)) - self->actor.shape.rot.y + 0x8000);
    Matrix_RotateY(toCamera *
                       (M_PI / 0x8000),
                   MTXMODE_APPLY);
    Matrix_Scale(0.0027f, 0.0027f, 0.0027f, MTXMODE_APPLY);
    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_XLU_DISP++, (Gfx*)gEffFire1DL);
    Matrix_Pop();
    CLOSE_DISPS(play->state.gfxCtx);
}

// EnBombf's flower: leaves, then the bomb on top as a billboard. The bomb shrinks
// away when it goes off and grows back while the trap rearms.
static void DrawBombFlower(PlayState* play, PlaceableActor* self) {
    f32 bomb = 1.0f;
    u8 flash = 0;
    if (self != nullptr) {
        if (self->ruin) {
            bomb = 0.0f; // a dud: the bomb never grew back
        } else if (self->trapRegrow > 0) {
            bomb = 1.0f - (f32)self->trapRegrow / TRAP_REGROW;
        }
        if (self->trapFuse > 0 && (self->trapFuse & 2)) {
            flash = 150;
        }
    }
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    Matrix_Push();
    Matrix_Scale(0.01f, 0.01f, 0.01f, MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_OPA_DISP++, (Gfx*)gBombFlowerLeavesDL);
    gSPDisplayList(POLY_OPA_DISP++, (Gfx*)gBombFlowerBaseLeavesDL);
    if (bomb > 0.05f) {
        Matrix_Translate(0.0f, 1000.0f, 0.0f, MTXMODE_APPLY);
        Matrix_Scale(bomb, bomb, bomb, MTXMODE_APPLY);
        gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 200, 255, 200, 255);
        gDPPipeSync(POLY_OPA_DISP++);
        gDPSetEnvColor(POLY_OPA_DISP++, flash, 20, 10, 0);
        gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        // EnBombf_NewMtxDList: the bomb's own matrix, turned to face the camera.
        Gfx* dl = (Gfx*)Graph_Alloc(play->state.gfxCtx, 2 * sizeof(Gfx));
        Gfx* head = dl;
        Matrix_ReplaceRotation(&play->billboardMtxF);
        gSPMatrix(head++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPEndDisplayList(head++);
        gSPSegment(POLY_OPA_DISP++, 0x08, (uintptr_t)dl);
        gSPDisplayList(POLY_OPA_DISP++, (Gfx*)gBombFlowerBombAndSparkDL);
    }
    Matrix_Pop();
    CLOSE_DISPS(play->state.gfxCtx);
}

// Two leaves of Ingo's ranch gate (one 800 x 1200 panel at 0.01, hinged at x = 0),
// 60 wide and 90 tall each, hinged at the wall's ends; they swing out to 90 degrees.
static void DrawGate(PlayState* play, f32 open) {
    const f32 sc = 0.075f;
    s16 swing = (s16)(open * 0x4000);
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    for (int side = 0; side < 2; side++) {
        Matrix_Push();
        Matrix_Translate(side == 0 ? -60.0f : 60.0f, 0.0f, 0.0f, MTXMODE_APPLY);
        Matrix_RotateY((side == 0 ? -swing : (s16)(0x8000 + swing)) * (M_PI / 0x8000), MTXMODE_APPLY);
        Matrix_Scale(sc, sc, sc, MTXMODE_APPLY);
        gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(POLY_OPA_DISP++, (Gfx*)gIngoGateDL);
        Matrix_Pop();
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

// A unit box (x/z in -1..1, y in 0..1) for the ghost's translucent volume.
static Vtx sCubeVtx[8];
static bool sCubeBuilt = false;
static void BuildCube() {
    if (sCubeBuilt) {
        return;
    }
    sCubeBuilt = true;
    static const s16 c[8][3] = { { -1, 0, -1 }, { 1, 0, -1 }, { 1, 0, 1 }, { -1, 0, 1 },
                                 { -1, 1, -1 }, { 1, 1, -1 }, { 1, 1, 1 }, { -1, 1, 1 } };
    for (int i = 0; i < 8; i++) {
        memset(&sCubeVtx[i], 0, sizeof(Vtx));
        sCubeVtx[i].v.ob[0] = c[i][0];
        sCubeVtx[i].v.ob[1] = c[i][1];
        sCubeVtx[i].v.ob[2] = c[i][2];
        sCubeVtx[i].v.cn[0] = sCubeVtx[i].v.cn[1] = sCubeVtx[i].v.cn[2] = sCubeVtx[i].v.cn[3] = 255;
    }
}

static void DrawGhostVolume(PlayState* play, const PlaceableInfo& info, bool valid) {
    BuildCube();
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    Matrix_Push();
    Matrix_Scale((f32)info.halfX + 2.0f, (f32)info.height + 2.0f, (f32)info.halfZ + 2.0f, MTXMODE_APPLY);
    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gDPPipeSync(POLY_XLU_DISP++);
    gDPSetCombineMode(POLY_XLU_DISP++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetRenderMode(POLY_XLU_DISP++, G_RM_ZB_XLU_SURF, G_RM_ZB_XLU_SURF2);
    gSPClearGeometryMode(POLY_XLU_DISP++, G_CULL_BOTH | G_LIGHTING | G_TEXTURE_GEN | G_FOG);
    if (valid) {
        gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, 80, 255, 120, 90);
    } else {
        gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, 255, 60, 60, 110);
    }
    gSPVertex(POLY_XLU_DISP++, (uintptr_t)sCubeVtx, 8, 0);
    gSP2Triangles(POLY_XLU_DISP++, 4, 7, 6, 0, 4, 6, 5, 0); // top
    gSP2Triangles(POLY_XLU_DISP++, 0, 4, 5, 0, 0, 5, 1, 0); // -z
    gSP2Triangles(POLY_XLU_DISP++, 3, 2, 6, 0, 3, 6, 7, 0); // +z
    gSP2Triangles(POLY_XLU_DISP++, 0, 3, 7, 0, 0, 7, 4, 0); // -x
    gSP2Triangles(POLY_XLU_DISP++, 1, 5, 6, 0, 1, 6, 2, 0); // +x
    Matrix_Pop();
    CLOSE_DISPS(play->state.gfxCtx);
}

// MARK: - SevenDays_Placeable

static ColliderCylinderInit sSpikeCylinderInit = {
    {
        COLTYPE_METAL,
        AT_ON | AT_TYPE_PLAYER, // hurts enemies, never players (see BystanderInReach)
        AC_NONE,
        OC1_NONE,
        OC2_NONE,
        COLSHAPE_CYLINDER,
    },
    {
        ELEMTYPE_UNK2,
        { DMG_SLASH_KOKIRI, 0x00, 0x01 }, // half a heart's worth: a Kokiri Sword slash
        { 0x00000000, 0x00, 0x00 },
        TOUCH_ON | TOUCH_SFX_NONE,
        BUMP_NONE,
        OCELEM_NONE,
    },
    { 45, 18, 0, { 0, 0, 0 } },
};

static ColliderCylinderInit sBiteCylinderInit = {
    {
        COLTYPE_NONE,
        AT_ON | AT_TYPE_PLAYER, // bites raiders, never players (see BystanderInReach)
        AC_NONE,
        OC1_NONE,
        OC2_NONE,
        COLSHAPE_CYLINDER,
    },
    {
        ELEMTYPE_UNK2,
        { DMG_SLASH_KOKIRI, 0x00, 0x01 }, // half a heart's worth, like the spikes
        { 0x00000000, 0x00, 0x00 },
        TOUCH_ON | TOUCH_SFX_NONE,
        BUMP_NONE,
        OCELEM_NONE,
    },
    { 32, 40, -20, { 0, 0, 0 } },
};

static ColliderCylinderInit sBlastCylinderInit = {
    {
        COLTYPE_NONE,
        AT_ON | AT_TYPE_PLAYER, // blows up raiders, never players (see BystanderInReach)
        AC_NONE,
        OC1_NONE,
        OC2_NONE,
        COLSHAPE_CYLINDER,
    },
    {
        ELEMTYPE_UNK0,
        { 0x00000008, 0x00, 0x08 }, // a bomb's blast (DMG_EXPLOSIVE)
        { 0x00000000, 0x00, 0x00 },
        TOUCH_ON | TOUCH_SFX_NONE,
        BUMP_NONE,
        OCELEM_NONE,
    },
    { 110, 80, -10, { 0, 0, 0 } },
};

// #3962: the furniture Link uses straight away offers A with no textbox (0xFFFF).
constexpr uint16_t TEXT_NONE_USE = 0xFFFF;

static uint16_t TextFor(uint8_t type) {
    switch (type) {
        case PLACEABLE_SIGN:
            return TEXT_SIGN_DAY;
        case PLACEABLE_WORKBENCH:
            return TEXT_WORKBENCH;
        case PLACEABLE_CHEST:
            return TEXT_CHEST;
        case PLACEABLE_BOOKSHELF:
            return TEXT_BOOKSHELF;
        case PLACEABLE_PAINTING:
            return TEXT_PAINTING;
    }
    switch (FurnitureUseOf(type)) {
        case USE_STORAGE:
            return TEXT_CHEST; // the same stores as the chest
        case USE_SIT:
        case USE_SLEEP:
        case USE_LIE:
        case USE_DRINK:
            return TEXT_NONE_USE;
        default:
            return 0;
    }
}

static void Placeable_Init(Actor* thisx, PlayState* play) {
    PlaceableActor* self = (PlaceableActor*)thisx;
    self->id = (uint16_t)thisx->params;
    const Placeable* p = FindPlaceable(self->id);
    if (p == nullptr) {
        self->type = 0xFF;
        Actor_Kill(thisx);
        return;
    }
    self->type = p->type;
    self->ruin = IsRuin(*p);
    self->talking = 0;
    self->atCooldown = 0;
    self->hitFlash = 0;
    self->shake = 0;
    self->wardLit = false;
    thisx->room = -1; // scene-wide: survives walking between rooms
    Actor_SetScale(thisx, 1.0f);
    thisx->shape.rot = thisx->world.rot = { 0, p->rot, 0 };

    // Re-snap to the floor under it (seeded pieces carry an approximate y). #3945:
    // not a piece stacked on another, whose collision may not be built yet.
    Vec3f probe = { p->pos[0], p->pos[1] + 80.0f, p->pos[2] };
    CollisionPoly* poly = nullptr;
    s32 bgId = BGCHECK_SCENE;
    f32 floorY = p->stacked ? BGCHECK_Y_MIN : BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, &probe);
    if (floorY > BGCHECK_Y_MIN && bgId == BGCHECK_SCENE && fabsf(floorY - p->pos[1]) < 200.0f) {
        thisx->world.pos.y = thisx->home.pos.y = floorY;
    }

    sCollisionDirty = true;

    uint16_t textId = self->ruin ? 0 : TextFor(self->type);
    if (textId != 0) {
        thisx->textId = textId;
        thisx->flags |= ACTOR_FLAG_ATTENTION_ENABLED | ACTOR_FLAG_FRIENDLY;
        thisx->targetMode = 0;
    }
    if (self->type == PLACEABLE_SPIKES && !self->ruin) {
        Collider_InitCylinder(play, &self->spikeCollider);
        Collider_SetCylinder(play, &self->spikeCollider, thisx, &sSpikeCylinderInit);
    }
    self->hasSkel = false;
    self->babaState = BABA_IDLE;
    self->babaTimer = 0;
    self->babaYaw = p->rot;
    self->babaLunge = 0.0f;
    self->babaBit = false;
    if (self->type == PLACEABLE_SCARECROW) {
        SkelAnime_InitFlex(play, &self->skel, (FlexSkeletonHeader*)object_ka_Skel_0065B0,
                           (AnimationHeader*)object_ka_Anim_000214, nullptr, nullptr, 0);
        self->skel.playSpeed = 0.0f;
        self->hasSkel = true;
    } else if (self->type == PLACEABLE_GUARDBABA && !self->ruin) {
        SkelAnime_Init(play, &self->skel, (SkeletonHeader*)gDekuBabaSkel, (AnimationHeader*)gDekuBabaPauseChompAnim,
                       nullptr, nullptr, 0);
        self->skel.playSpeed = 0.0f;
        self->hasSkel = true;
        Collider_InitCylinder(play, &self->biteCollider);
        Collider_SetCylinder(play, &self->biteCollider, thisx, &sBiteCylinderInit);
    }
    self->lightNode = nullptr;
    self->trapFuse = self->trapRegrow = self->blastFrames = 0;
    self->gateOpen = 0.0f;
    self->gatePassable = false;
    if (self->ruin) {
        // a burnt-out torch, a dud bomb flower, a wilted Baba: nothing to set up
    } else if (self->type == PLACEABLE_TORCH) {
        Lights_PointGlowSetInfo(&self->lightInfo, thisx->world.pos.x, thisx->world.pos.y + 70.0f, thisx->world.pos.z,
                                255, 255, 180, 250);
        self->lightNode = LightContext_InsertLight(play, &play->lightCtx, &self->lightInfo);
    } else if (self->type == PLACEABLE_BOMBTRAP) {
        Collider_InitCylinder(play, &self->blastCollider);
        Collider_SetCylinder(play, &self->blastCollider, thisx, &sBlastCylinderInit);
    }
    Actor_SetFocus(thisx, (f32)GetPlaceableInfo(self->type).height);
    OnPlaceableSpawned(self->id, thisx);
}

static void Placeable_Destroy(Actor* thisx, PlayState* play) {
    PlaceableActor* self = (PlaceableActor*)thisx;
    if (self->type == 0xFF) {
        return;
    }
    sCollisionDirty = true;
    if (self->type == PLACEABLE_SPIKES && !self->ruin) {
        Collider_DestroyCylinder(play, &self->spikeCollider);
    }
    if (self->type == PLACEABLE_GUARDBABA && !self->ruin) {
        Collider_DestroyCylinder(play, &self->biteCollider);
    }
    if (self->type == PLACEABLE_BOMBTRAP && !self->ruin) {
        Collider_DestroyCylinder(play, &self->blastCollider);
    }
    if (self->lightNode != nullptr) {
        LightContext_RemoveLight(play, &play->lightCtx, self->lightNode);
        self->lightNode = nullptr;
    }
    if (self->hasSkel) {
        SkelAnime_Free(&self->skel, play);
        self->hasSkel = false;
    }
    OnPlaceableDestroyed(self->id, thisx);
}

// The nearest living enemy within `range` of the stalk's base, or null.
static Actor* NearestEnemyTo(PlayState* play, const Vec3f& at, f32 range) {
    Actor* best = nullptr;
    f32 bestD = range;
    for (Actor* a = play->actorCtx.actorLists[ACTORCAT_ENEMY].head; a != nullptr; a = a->next) {
        if (a->update == nullptr || a->colChkInfo.health == 0 || a->id == sPlaceableId) {
            continue;
        }
        f32 d = Math_Vec3f_DistXZ(const_cast<Vec3f*>(&at), &a->world.pos);
        if (d < bestD && fabsf(a->world.pos.y - at.y) < 100.0f) {
            best = a;
            bestD = d;
        }
    }
    return best;
}

// The spikes' and the bite's AT is AT_TYPE_PLAYER, the only kind raiders take damage
// from, but a sword-like hit also lands on anything else that takes a player's sword:
// another player's Link in PvP, grass, bushes, signs, Cuccos. Hold the hit while any
// of those is within reach of it, so it only ever lands on raiders.
static bool BystanderInReach(PlayState* play, const ColliderCylinder& c) {
    // Our own Link (ACTORCAT_PLAYER) never takes it. Remote players are DummyPlayers,
    // which Anchor moves into the NPC list.
    static const uint8_t kCategories[] = { ACTORCAT_PROP, ACTORCAT_NPC };
    Vec3f at = { (f32)c.dim.pos.x, (f32)c.dim.pos.y, (f32)c.dim.pos.z };
    for (uint8_t cat : kCategories) {
        for (Actor* a = play->actorCtx.actorLists[cat].head; a != nullptr; a = a->next) {
            if (a->update == nullptr) {
                continue;
            }
            // Remote players only take hits with PvP on against their team: DummyPlayer
            // clears this flag exactly then.
            if (a->update == DummyPlayer_Update && (a->flags & ACTOR_FLAG_LOCK_ON_DISABLED)) {
                continue;
            }
            f32 dy = a->world.pos.y - at.y;
            // Reach: the hit's cylinder plus a Link-sized (or grass-tuft-sized) body.
            if (Math_Vec3f_DistXZ(&at, &a->world.pos) < c.dim.radius + 20.0f && dy > -60.0f &&
                dy < c.dim.height + 10.0f) {
                return true;
            }
        }
    }
    return false;
}

// Guard Baba: rooted; turns to the nearest raider, winds up, lunges and bites once
// (half a heart), then recovers. Every client animates it from the enemies it sees;
// only the scene's enemy authority puts the bite's collider out, like the spikes.
static void BabaUpdate(PlaceableActor* self, PlayState* play) {
    Actor* thisx = &self->actor;
    bool authority = Anchor::Instance == nullptr || !Anchor::Instance->isConnected || EnemySync::IsLocalAuthority();
    SkelAnime_Update(&self->skel);
    if (self->hitFlash > 0) {
        self->hitFlash--;
    }
    switch (self->babaState) {
        case BABA_IDLE: {
            Math_StepToF(&self->babaLunge, 0.0f, 0.1f);
            Actor* t = NearestEnemyTo(play, thisx->world.pos, BABA_SENSE);
            if (t == nullptr) {
                Math_ApproachS(&self->babaYaw, thisx->shape.rot.y + (s16)(Math_SinS(play->gameplayFrames * 0x180) * 0x1400),
                               6, 0x300);
                break;
            }
            s16 want = Math_Vec3f_Yaw(&thisx->world.pos, &t->world.pos);
            Math_ApproachS(&self->babaYaw, want, 2, 0xE38);
            if (ABS((s16)(want - self->babaYaw)) < 0x1000) {
                self->babaState = BABA_WINDUP;
                self->babaTimer = 8;
                self->babaBit = false;
            }
            break;
        }
        case BABA_WINDUP:
            Math_StepToF(&self->babaLunge, -0.15f, 0.05f); // rears back
            if (--self->babaTimer <= 0) {
                self->babaState = BABA_LUNGE;
                self->babaTimer = 0;
                Animation_PlayOnce(&self->skel, (AnimationHeader*)gDekuBabaPauseChompAnim);
                self->skel.playSpeed = 1.5f;
                Audio_PlayActorSound2(thisx, NA_SE_EN_DEKU_JR_ATTACK);
            }
            break;
        case BABA_LUNGE:
            Math_StepToF(&self->babaLunge, 1.0f, 0.3f);
            if (self->babaLunge > 0.5f && !self->babaBit && authority) {
                if (self->biteCollider.base.atFlags & AT_HIT) {
                    self->biteCollider.base.atFlags &= ~AT_HIT;
                    self->babaBit = true;
                    self->hitFlash = 6;
                    Audio_PlayActorSound2(thisx, NA_SE_EN_DEKU_ATTACK);
                } else {
                    Collider_UpdateCylinder(thisx, &self->biteCollider);
                    f32 hy, hz;
                    BabaHead(self->babaLunge, &hy, &hz);
                    self->biteCollider.dim.pos.x = (s16)(thisx->world.pos.x + Math_SinS(self->babaYaw) * hz);
                    self->biteCollider.dim.pos.y = (s16)(thisx->world.pos.y + hy);
                    self->biteCollider.dim.pos.z = (s16)(thisx->world.pos.z + Math_CosS(self->babaYaw) * hz);
                    if (!BystanderInReach(play, self->biteCollider)) {
                        CollisionCheck_SetAT(play, &play->colChkCtx, &self->biteCollider.base);
                    }
                }
            }
            if (++self->babaTimer > 9) {
                self->babaState = BABA_RECOVER;
                self->babaTimer = 25; // one bite per lunge, then a breather
            }
            break;
        case BABA_RECOVER:
            Math_StepToF(&self->babaLunge, 0.0f, 0.08f);
            if (--self->babaTimer <= 0) {
                self->babaState = BABA_IDLE;
            }
            break;
    }
}

// Torch: flickers like ObjSyokudai's lit torches.
static void TorchUpdate(PlaceableActor* self, PlayState* play) {
    // #4006: the ward's ritual reaches this torch: it flares up blue.
    bool ward = SevenDays::TorchWardLit(&self->actor);
    if (ward && !self->wardLit) {
        Audio_PlayActorSound2(&self->actor, NA_SE_EV_FLAME_IGNITION);
    }
    self->wardLit = ward;
    u8 brightness = (u8)(Rand_ZeroOne() * 127.0f) + 128;
    if (ward) {
        Lights_PointSetColorAndRadius(&self->lightInfo, brightness / 3, brightness * 3 / 4, 255, 320);
    } else {
        Lights_PointSetColorAndRadius(&self->lightInfo, brightness, brightness, 0, 250);
    }
    func_8002F974(&self->actor, NA_SE_EV_TORCH - SFX_FLAG);
}

// Bomb-flower trap: a raider within TRAP_SENSE lights the fuse; the blast hurts every
// enemy within 110 for a bomb's damage, then the bomb grows back. Every client plays
// it from the enemies it sees; only the scene's enemy authority puts the blast out.
static void TrapUpdate(PlaceableActor* self, PlayState* play) {
    Actor* thisx = &self->actor;
    bool authority = Anchor::Instance == nullptr || !Anchor::Instance->isConnected || EnemySync::IsLocalAuthority();
    if (self->blastFrames > 0) {
        self->blastFrames--;
        if (authority) {
            // Like the spikes and the Baba: a player-type hit, so it holds off while another
            // player (PvP), grass or an NPC is in the blast.
            Collider_UpdateCylinder(thisx, &self->blastCollider);
            if (!BystanderInReach(play, self->blastCollider)) {
                CollisionCheck_SetAT(play, &play->colChkCtx, &self->blastCollider.base);
            }
        }
    }
    if (self->trapRegrow > 0) {
        self->trapRegrow--;
        return;
    }
    if (self->trapFuse > 0) {
        if (--self->trapFuse == 0) {
            static Vec3f zero = { 0.0f, 0.0f, 0.0f };
            static Vec3f bomb2Accel = { 0.0f, 0.1f, 0.0f };
            Vec3f effPos = thisx->world.pos;
            effPos.y += 10.0f;
            EffectSsBomb2_SpawnLayered(play, &effPos, &zero, &bomb2Accel, 100, 19);
            effPos.y = thisx->world.pos.y;
            EffectSsBlast_SpawnWhiteShockwave(play, &effPos, &zero, &zero);
            Audio_PlayActorSound2(thisx, NA_SE_IT_BOMB_EXPLOSION);
            Camera_AddQuake(&play->mainCamera, 2, 0xB, 8);
            self->blastFrames = 3;
            self->trapRegrow = TRAP_REGROW;
            sTrapBlasts++;
        }
        return;
    }
    if (NearestEnemyTo(play, thisx->world.pos, TRAP_SENSE) != nullptr) {
        self->trapFuse = TRAP_FUSE;
        Audio_PlayActorSound2(thisx, NA_SE_IT_BOMB_IGNIT);
    }
}

// Player gate (and #3945's doors): swings open while a player stands in front of or
// behind it, and its box leaves the base collision so they walk through. Raiders have to
// break it.
static void GateUpdate(PlaceableActor* self, PlayState* play) {
    Actor* thisx = &self->actor;
    const PlaceableInfo& info = GetPlaceableInfo(self->type);
    f32 c = Math_CosS(thisx->shape.rot.y), s = Math_SinS(thisx->shape.rot.y);
    bool near = false;
    for (Actor* p : HordeNight::LivingPlayers()) {
        f32 dx = p->world.pos.x - thisx->world.pos.x, dz = p->world.pos.z - thisx->world.pos.z;
        f32 lx = dx * c - dz * s, lz = dx * s + dz * c;
        if (fabsf(lx) < info.halfX + 10.0f && fabsf(lz) < GATE_REACH && fabsf(p->world.pos.y - thisx->world.pos.y) < 80.0f) {
            near = true;
            break;
        }
    }
    bool wasOpen = self->gateOpen > 0.0f;
    Math_StepToF(&self->gateOpen, near ? 1.0f : 0.0f, near ? 0.12f : 0.06f);
    if (!wasOpen && self->gateOpen > 0.0f) {
        Audio_PlayActorSound2(thisx, NA_SE_EV_WOODDOOR_OPEN);
    }
    bool passable = self->gateOpen > 0.3f;
    if (passable != self->gatePassable) {
        self->gatePassable = passable;
        sCollisionDirty = true;
    }
}

static void Placeable_Update(Actor* thisx, PlayState* play) {
    PlaceableActor* self = (PlaceableActor*)thisx;
    const PlaceableInfo& info = GetPlaceableInfo(self->type);
    RebuildBaseCollision(play);

    if (thisx->textId != 0) {
        if (self->type == PLACEABLE_MILKCAN && !self->talking) {
            // #3962: once a day; until tomorrow it just says it's empty.
            thisx->textId = MilkCanEmpty(self->id) ? TEXT_MILK_EMPTY : TEXT_NONE_USE;
        }
        if (self->talking) {
            if (Actor_TextboxIsClosing(thisx, play) || play->msgCtx.msgMode == MSGMODE_NONE) {
                self->talking = 0;
                OnPlaceableInteract(self->type);
            }
        } else if (Actor_ProcessTalkRequest(thisx, play)) {
            if (thisx->textId == TEXT_NONE_USE) {
                StartRest(thisx, self->type); // #3962: sit, sleep, lie down or drink
            } else {
                self->talking = 1;
            }
        } else if (!InPlacement() && !Resting()) {
            // Offer "Check" on A when Link is close.
            func_8002F2CC(thisx, play, (f32)std::max(info.halfX, info.halfZ) + 40.0f);
        }
    }

    if (self->shake > 0) {
        self->shake--;
    }
    if (self->ruin) {
        return;
    }

    if (self->type == PLACEABLE_SCARECROW && self->hasSkel) {
        SkelAnime_Update(&self->skel); // playSpeed 0: holds the first frame
    } else if (self->type == PLACEABLE_GUARDBABA) {
        BabaUpdate(self, play);
    } else if (self->type == PLACEABLE_TORCH) {
        TorchUpdate(self, play);
    } else if (self->type == PLACEABLE_BOMBTRAP) {
        TrapUpdate(self, play);
    } else if (IsDoorType(self->type)) {
        GateUpdate(self, play);
    }

    if (self->type == PLACEABLE_SPIKES) {
        // Only the scene's enemy authority hurts enemies; mirrors see the result.
        bool authority = Anchor::Instance == nullptr || !Anchor::Instance->isConnected || EnemySync::IsLocalAuthority();
        if (self->atCooldown > 0) {
            self->atCooldown--;
        } else if (authority) {
            if (self->spikeCollider.base.atFlags & AT_HIT) {
                self->spikeCollider.base.atFlags &= ~AT_HIT;
                self->atCooldown = 20; // one bite per enemy crossing, not one per frame
                self->hitFlash = 6;
                Audio_PlayActorSound2(thisx, NA_SE_IT_SWORD_STRIKE);
            } else {
                Collider_UpdateCylinder(thisx, &self->spikeCollider);
                if (!BystanderInReach(play, self->spikeCollider)) {
                    CollisionCheck_SetAT(play, &play->colChkCtx, &self->spikeCollider.base);
                }
            }
        }
        if (self->hitFlash > 0) {
            self->hitFlash--;
        }
    }
}

// A placed piece with its wear: the damage wash, a ruin's moss and keel, a hit's judder.
// The icon studio draws ruins through here too.
static void DrawWorn(PlayState* play, PlaceableActor* self, float hpFrac) {
    const PlaceableInfo& info = GetPlaceableInfo(self->type);
    if (self->ruin) {
        hpFrac = 0.1f; // the broken look: a crate knocked askew, a block knocked down
    }

    bool tint = (info.maxHp > 0 && hpFrac < 0.5f) || self->ruin;
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    CLOSE_DISPS(play->state.gfxCtx);
    if (self->ruin) {
        // Seven years of moss and rot.
        SetPieceTint(play, true, 105, 125, 65, 215);
    } else if (tint) {
        // Damaged wood darkens.
        SetPieceTint(play, true, 120, 80, 50, 140);
    }

    if (self->ruin) {
        // Sunk into the ground and keeled over, each piece its own way (seeded by its id,
        // so it lies the same on every client and every visit): 15-20 degrees, sunk 15-20
        // or a third of its height if that's less. Some pieces go further (below); each
        // also has a broken shape of its own in DrawModel.
        uint32_t h = self->id * 2654435761u;
        f32 sink = std::min(15.0f + (f32)((h >> 8) % 6), info.height * 0.35f);
        f32 lean = 0.26f + 0.09f * (f32)((h >> 12) & 0xFF) / 255.0f;
        switch (self->type) {
            case PLACEABLE_TORCH:
            case PLACEABLE_LADDER:
                // Rotted through at the foot: the torch has fallen flat, the ladder lies
                // propped at a slant.
                sink = 3.0f;
                lean = self->type == PLACEABLE_TORCH ? 1.45f : 0.9f;
                break;
            case PLACEABLE_BARRICADE:
                lean += 0.25f; // half pushed over
                break;
            case PLACEABLE_SCARECROW:
                sink = 8.0f;
                lean = 0.7f; // slumped on its pole
                break;
            case PLACEABLE_GUARDBABA:
                sink = 2.0f; // the wilted stalk is low already
                break;
            case PLACEABLE_FLOOR_PLANK:
            case PLACEABLE_FLOOR_RANCH:
            case PLACEABLE_FLOOR_STONE:
                lean *= 0.5f; // heaved by roots, not a ramp
                break;
        }
        f32 dir = BINANG_TO_RAD((s16)(h >> 16));
        Matrix_Push();
        Matrix_Translate(0.0f, -sink, 0.0f, MTXMODE_APPLY);
        Matrix_RotateY(dir, MTXMODE_APPLY);
        Matrix_RotateX(lean, MTXMODE_APPLY);
        Matrix_RotateY(-dir, MTXMODE_APPLY);
        DrawModel(play, self->type, hpFrac, self);
        Matrix_Pop();
    } else if (self->shake > 0) {
        // Took a hit: a short sideways judder, on every client (BASE_DELTA hp).
        Matrix_Push();
        Matrix_Translate(Math_SinS(self->shake * 0x3000) * self->shake * 0.6f, 0.0f, 0.0f, MTXMODE_APPLY);
        DrawModel(play, self->type, hpFrac, self);
        Matrix_Pop();
    } else {
        DrawModel(play, self->type, hpFrac, self);
    }

    if (tint) {
        SetPieceTint(play, false);
    }
}

// #4062: the pieces draw before the enemies, Link's gear and the effects (actor list order), so a
// big base must not spend the display-list room they need. Below this share of the opaque or
// translucent pool, a piece skips its draw for the frame; the engine's own reserve in Actor_Draw
// is the hard stop behind it.
extern "C" s32 Graph_GfxRoomBelowPercent(GraphicsContext* gfxCtx, s32 percent);
extern "C" void Graph_GfxNoteActorSkip(s32 placeable);
extern "C" const char* Graph_GfxStatsJson(void);
extern "C" void Graph_GfxStatsReset(void);
extern "C" void Graph_GfxForceOverflow(s32 frames);
static constexpr s32 kPieceDrawFloorPercent = 35;

static void Placeable_Draw(Actor* thisx, PlayState* play) {
    PlaceableActor* self = (PlaceableActor*)thisx;
    if (!self->ghost && Graph_GfxRoomBelowPercent(play->state.gfxCtx, kPieceDrawFloorPercent)) {
        Graph_GfxNoteActorSkip(1);
        return;
    }
    const Placeable* p = FindPlaceable(self->id);
    const PlaceableInfo& info = GetPlaceableInfo(self->type);
    DrawWorn(play, self, (p != nullptr && info.maxHp > 0) ? (float)p->hp / (float)info.maxHp : 1.0f);
}

// MARK: - SevenDays_Ghost (placement mode)

// The ghost is a PlaceableActor too, so the pieces whose look lives on the actor (the
// scarecrow's and the Baba's skeletons, the torch's flame) preview whole.
static void Ghost_Init(Actor* thisx, PlayState* play) {
    PlaceableActor* self = (PlaceableActor*)thisx;
    thisx->room = -1;
    Actor_SetScale(thisx, 1.0f);
    self->type = (uint8_t)thisx->params;
    self->ghost = true;
    self->wardLit = false;
    self->hasSkel = false;
    if (self->type == PLACEABLE_SCARECROW) {
        SkelAnime_InitFlex(play, &self->skel, (FlexSkeletonHeader*)object_ka_Skel_0065B0,
                           (AnimationHeader*)object_ka_Anim_000214, nullptr, nullptr, 0);
        self->hasSkel = true;
    } else if (self->type == PLACEABLE_GUARDBABA) {
        SkelAnime_Init(play, &self->skel, (SkeletonHeader*)gDekuBabaSkel, (AnimationHeader*)gDekuBabaPauseChompAnim,
                       nullptr, nullptr, 0);
        self->hasSkel = true;
    }
    if (self->hasSkel) {
        self->skel.playSpeed = 0.0f;
    }
}

static void Ghost_Destroy(Actor* thisx, PlayState* play) {
    PlaceableActor* self = (PlaceableActor*)thisx;
    if (self->hasSkel) {
        SkelAnime_Free(&self->skel, play);
        self->hasSkel = false;
    }
    OnGhostDestroyed(thisx);
}

static void Ghost_Update(Actor* thisx, PlayState* play) {
    PlaceableActor* self = (PlaceableActor*)thisx;
    PlacementUpdate(thisx, play);
    self->babaYaw = thisx->shape.rot.y; // the head points the way the piece faces
    if (self->hasSkel) {
        SkelAnime_Update(&self->skel); // playSpeed 0: holds the first frame
    }
}

static void Ghost_Draw(Actor* thisx, PlayState* play) {
    uint8_t type = (uint8_t)thisx->params;
    if (type >= PLACEABLE_COUNT) {
        return;
    }
    bool valid = PlacementGhostValid();
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    CLOSE_DISPS(play->state.gfxCtx);
    SetPieceTint(play, true, valid ? 150 : 255, valid ? 255 : 90, valid ? 170 : 90, 200);
    // Flicker the solid model so the ghost reads as see-through.
    if ((play->gameplayFrames & 1) == 0) {
        PlaceableActor* self = (PlaceableActor*)thisx;
        bool onActor = type == PLACEABLE_SCARECROW || type == PLACEABLE_GUARDBABA || type == PLACEABLE_TORCH;
        DrawModel(play, type, 1.0f, onActor ? self : nullptr);
    }
    SetPieceTint(play, false);
    DrawGhostVolume(play, GetPlaceableInfo(type), valid);
}

// MARK: - SevenDays_IconStudio (tests)

// #3969: the Workbench icons are the pieces' own models. The studio draws one through
// its own orthographic camera, turned three-quarters and tipped toward it, on a black or
// white backdrop; tools/harness/pha3969 shoots both and keeps the difference as alpha. The
// backdrop writes the front of the depth range over the whole screen, so nothing the game
// draws after it shows through.
constexpr f32 kStudioHeight = 4000.0f; // over Link, out of everyone's way
constexpr f32 kStudioDist = 150.0f;    // the studio camera to the piece
constexpr f32 kStudioFar = 400.0f;     // the camera's depth range: the backdrop sits near its front
constexpr f32 kStudioHalfH = 70.0f;    // half the height the camera sees
constexpr f32 kStudioSize = 56.0f;     // the piece's largest side, scaled to this
constexpr f32 kStudioLift = 6.0f;      // the piece a little above the middle of the shot

static Vtx sStudioQuad[4] = {
    { { { -1, -1, 0 }, 0, { 0, 0 }, { 255, 255, 255, 255 } } },
    { { { 1, -1, 0 }, 0, { 0, 0 }, { 255, 255, 255, 255 } } },
    { { { 1, 1, 0 }, 0, { 0, 0 }, { 255, 255, 255, 255 } } },
    { { { -1, 1, 0 }, 0, { 0, 0 }, { 255, 255, 255, 255 } } },
};

static void Studio_Init(Actor* thisx, PlayState* play) {
    Ghost_Init(thisx, play); // the skeletons, as the preview has them
    ((PlaceableActor*)thisx)->ghost = false;
}

static void Studio_Destroy(Actor* thisx, PlayState* play) {
    PlaceableActor* self = (PlaceableActor*)thisx;
    if (self->hasSkel) {
        SkelAnime_Free(&self->skel, play);
        self->hasSkel = false;
    }
    if (sStudio == thisx) {
        sStudio = nullptr;
    }
}

static void Studio_Update(Actor* thisx, PlayState* play) {
    PlaceableActor* self = (PlaceableActor*)thisx;
    if (sStudio != thisx) {
        Actor_Kill(thisx);
        return;
    }
    Player* player = GET_PLAYER(play);
    thisx->world.pos = { player->actor.world.pos.x, player->actor.world.pos.y + kStudioHeight,
                         player->actor.world.pos.z };
    thisx->shape.rot = { 0, 0, 0 };
    self->babaYaw = 0;
    self->ruin = sStudioRuin;
    if (self->hasSkel) {
        SkelAnime_Update(&self->skel);
    }
}

// How big and how tall each subject draws, before the studio scales it to kStudioSize.
static void StudioExtent(int type, f32* size, f32* height) {
    if (type == STUDIO_SMALL_CRATE || type == STUDIO_LARGE_CRATE) {
        *size = *height = type == STUDIO_SMALL_CRATE ? 40.0f : 100.0f;
        return;
    }
    const PlaceableInfo& info = GetPlaceableInfo((uint8_t)type);
    *height = info.height;
    switch (type) {
        case PLACEABLE_GUARDBABA:
            *height = 80.0f; // the head stands over the stalk
            break;
        case PLACEABLE_TORCH:
            *height = 85.0f; // the flame
            break;
        case PLACEABLE_BOMBTRAP:
            *height = 30.0f; // the bomb on its leaves
            break;
    }
    *size = std::max({ 2.0f * info.halfX, 2.0f * info.halfZ, *height });
}

static void Studio_Draw(Actor* thisx, PlayState* play) {
    PlaceableActor* self = (PlaceableActor*)thisx;
    Vec3f pos = thisx->world.pos;
    View* view = &play->view;
    GraphicsContext* gfx = play->state.gfxCtx;

    // The studio camera: on +z of the piece, looking at it.
    Mtx* projection = (Mtx*)Graph_Alloc(gfx, sizeof(Mtx));
    Mtx* viewing = (Mtx*)Graph_Alloc(gfx, sizeof(Mtx));
    f32 aspect = (f32)(view->viewport.rightX - view->viewport.leftX) /
                 (f32)(view->viewport.bottomY - view->viewport.topY);
    guOrtho(projection, -kStudioHalfH * aspect, kStudioHalfH * aspect, -kStudioHalfH, kStudioHalfH, 1.0f, kStudioFar,
            1.0f);
    // Aimed a little under the piece: clear of the hearts, the buttons and the minimap.
    guLookAt(viewing, pos.x, pos.y - kStudioLift, pos.z + kStudioDist, pos.x, pos.y - kStudioLift, pos.z, 0.0f, 1.0f,
             0.0f);
    OPEN_DISPS(gfx);
    gSPPerspNormalize(POLY_OPA_DISP++, 0xFFFF);
    gSPMatrix(POLY_OPA_DISP++, projection, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);
    gSPMatrix(POLY_OPA_DISP++, viewing, G_MTX_NOPUSH | G_MTX_MUL | G_MTX_PROJECTION);
    gSPFogPosition(POLY_OPA_DISP++, 996, 1000); // no fog in the studio
    gSPPerspNormalize(POLY_XLU_DISP++, 0xFFFF);
    gSPMatrix(POLY_XLU_DISP++, projection, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);
    gSPMatrix(POLY_XLU_DISP++, viewing, G_MTX_NOPUSH | G_MTX_MUL | G_MTX_PROJECTION);
    gSPFogPosition(POLY_XLU_DISP++, 996, 1000);

    // The backdrop: a flat card behind the piece. It draws over everything (no depth test)
    // and leaves its own, near, depth behind it.
    u8 shade = sStudioWhite ? 255 : 0;
    Matrix_Translate(pos.x, pos.y, pos.z - 80.0f, MTXMODE_NEW);
    Matrix_Scale(400.0f, 400.0f, 1.0f, MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(gfx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gDPPipeSync(POLY_OPA_DISP++);
    gSPClearGeometryMode(POLY_OPA_DISP++, G_CULL_BOTH | G_LIGHTING | G_TEXTURE_GEN | G_FOG);
    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetRenderMode(POLY_OPA_DISP++, G_RM_OPA_SURF | Z_UPD, G_RM_OPA_SURF2 | Z_UPD);
    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, shade, shade, shade, 255);
    gSPVertex(POLY_OPA_DISP++, (uintptr_t)sStudioQuad, 4, 0);
    gSP2Triangles(POLY_OPA_DISP++, 0, 1, 2, 0, 0, 2, 3, 0);
    CLOSE_DISPS(gfx);

    // The piece: turned three-quarters and tipped toward the camera, as if seen from above.
    // Billboards (the bomb, the flame) face the studio camera, which looks straight down -z.
    MtxF billboard = play->billboardMtxF;
    SkinMatrix_Clear(&play->billboardMtxF);
    f32 size, height;
    StudioExtent(sStudioType, &size, &height);
    f32 sc = kStudioSize / size;
    Matrix_Translate(pos.x, pos.y, pos.z, MTXMODE_NEW);
    Matrix_RotateX(height < 30.0f ? 0.8f : 0.45f, MTXMODE_APPLY); // flat pieces from higher up
    Matrix_RotateY(BINANG_TO_RAD(sStudioTurn), MTXMODE_APPLY);
    Matrix_Scale(sc, sc, sc, MTXMODE_APPLY);
    Matrix_Translate(0.0f, -height / 2.0f, 0.0f, MTXMODE_APPLY);
    Gfx_SetupDL_25Opa(gfx);
    if (sStudioType == STUDIO_SMALL_CRATE) {
        DrawDL(play, (Gfx*)gSmallWoodenBoxDL, 0.0f, 0.0f, 0.0f, 0.1f, 0.1f, 0.1f);
    } else if (sStudioType == STUDIO_LARGE_CRATE) {
        DrawDL(play, (Gfx*)gLargeCrateDL, 0.0f, 0.0f, 0.0f, 0.1f, 0.1f, 0.1f);
    } else if (sStudioRuin) {
        DrawWorn(play, self, 1.0f);
    } else {
        DrawModel(play, (uint8_t)sStudioType, 1.0f, self);
    }
    play->billboardMtxF = billboard;

    // Back to the game's camera and fog for whatever draws next.
    OPEN_DISPS(gfx);
    POLY_OPA_DISP = Play_SetFog(play, POLY_OPA_DISP);
    POLY_XLU_DISP = Play_SetFog(play, POLY_XLU_DISP);
    gSPPerspNormalize(POLY_OPA_DISP++, view->normal);
    gSPMatrix(POLY_OPA_DISP++, view->projectionPtr, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);
    gSPMatrix(POLY_OPA_DISP++, view->viewingPtr, G_MTX_NOPUSH | G_MTX_MUL | G_MTX_PROJECTION);
    gSPPerspNormalize(POLY_XLU_DISP++, view->normal);
    gSPMatrix(POLY_XLU_DISP++, view->projectionPtr, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);
    gSPMatrix(POLY_XLU_DISP++, view->viewingPtr, G_MTX_NOPUSH | G_MTX_MUL | G_MTX_PROJECTION);
    CLOSE_DISPS(gfx);
}

// MARK: - Registration and spawning

void SevenDays::RegisterPlaceableActors() {
    if (sPlaceableId >= 0 || ActorDB::Instance == nullptr) {
        return;
    }
    BuildSurfaces();
    for (int t = 0; t < PLACEABLE_COUNT; t++) {
        BuildShape(sShapes[t], (uint8_t)t);
    }
    ActorDBInit placeable;
    placeable.name = "SevenDays_Placeable";
    placeable.desc = "7 Days to Zelda placeable (barricade, spike strip, workbench, storage chest, sign, scarecrow, Guard Baba, "
                       "torch, stone wall, bomb-flower trap, player gate, iron wall, palisade, floors, deck, step, ladder, "
                       "stairs, doors)";
    placeable.category = ACTORCAT_BG;
    placeable.flags = ACTOR_FLAG_UPDATE_CULLING_DISABLED;
    placeable.objectId = OBJECT_GAMEPLAY_KEEP;
    placeable.instanceSize = sizeof(PlaceableActor);
    placeable.init = Placeable_Init;
    placeable.destroy = Placeable_Destroy;
    placeable.update = Placeable_Update;
    placeable.draw = Placeable_Draw;
    sPlaceableId = (int16_t)ActorDB::Instance->AddEntry(placeable).entry.id;

    ActorDBInit ghost;
    ghost.name = "SevenDays_Ghost";
    ghost.desc = "7 Days to Zelda placement ghost";
    ghost.category = ACTORCAT_SWITCH; // updates before Link (ACTORCAT_PLAYER)
    ghost.flags = ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED;
    ghost.objectId = OBJECT_GAMEPLAY_KEEP;
    ghost.instanceSize = sizeof(PlaceableActor);
    ghost.init = Ghost_Init;
    ghost.destroy = Ghost_Destroy;
    ghost.update = Ghost_Update;
    ghost.draw = Ghost_Draw;
    sGhostId = (int16_t)ActorDB::Instance->AddEntry(ghost).entry.id;

    ActorDBInit studio;
    studio.name = "SevenDays_IconStudio";
    studio.desc = "7 Days to Zelda tests: draws one piece in front of the camera for its Workbench icon";
    studio.category = ACTORCAT_BG;
    studio.flags = ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED;
    studio.objectId = OBJECT_GAMEPLAY_KEEP;
    studio.instanceSize = sizeof(PlaceableActor);
    studio.init = Studio_Init;
    studio.destroy = Studio_Destroy;
    studio.update = Studio_Update;
    studio.draw = Studio_Draw;
    sStudioId = (int16_t)ActorDB::Instance->AddEntry(studio).entry.id;

    ActorDBInit collision;
    collision.name = "SevenDays_BaseCollision";
    collision.desc = "7 Days to Zelda: the collision of a chunk of the base's pieces";
    collision.category = ACTORCAT_BG;
    collision.flags = ACTOR_FLAG_UPDATE_CULLING_DISABLED;
    collision.objectId = OBJECT_GAMEPLAY_KEEP;
    collision.instanceSize = sizeof(DynaPolyActor);
    collision.init = Collision_Init;
    collision.destroy = Collision_Destroy;
    collision.update = Collision_Update;
    collision.draw = nullptr;
    sCollisionId = (int16_t)ActorDB::Instance->AddEntry(collision).entry.id;
}

Actor* SevenDays::SpawnPlaceableActor(const Placeable& p) {
    RegisterPlaceableActors();
    if (gPlayState == nullptr || sPlaceableId < 0) {
        return nullptr;
    }
    Actor* actor = Actor_Spawn(&gPlayState->actorCtx, gPlayState, sPlaceableId, p.pos[0], p.pos[1], p.pos[2], 0, p.rot,
                               0, (s16)p.id, false);
    if (actor != nullptr) {
        actor->room = -1;
    }
    return actor;
}

void SevenDays::OnPlaceableHit(uint16_t id, Actor* actor) {
    if (actor == nullptr || actor->id != sPlaceableId || actor->update == nullptr) {
        return;
    }
    PlaceableActor* self = (PlaceableActor*)actor;
    if (self->shake == 0) {
        Audio_PlayActorSound2(actor, NA_SE_EV_WOOD_HIT);
    }
    self->shake = 12;
}

void SevenDays::OnPlaceableInteract(uint8_t type) {
    if (type == PLACEABLE_WORKBENCH) {
        OpenCraftingWindow(0);
    } else if (type == PLACEABLE_CHEST || FurnitureUseOf(type) == USE_STORAGE) {
        OpenCraftingWindow(2);
    } else if (type == PLACEABLE_BOOKSHELF) {
        OnBookRead(); // another book next time
    }
}

// MARK: - The village's new lines

struct VillageLine {
    uint16_t textId;
    const char* text;
    TextBoxType box;
};
// clang-format off
static const VillageLine sVillageLines[] = {
    // Fado (EnKo, first talk / again)
    { 0x10D7, "Link, have you heard them? There are things in the grass after dark.^They scratch at the boards all night. Mido says the barricades will hold... I hope he's right.", TEXTBOX_TYPE_BLACK },
    { 0x10D8, "Don't stay out in the grass after dark. The things out there don't care that we're Kokiri.", TEXTBOX_TYPE_BLACK },
    // Saria's greeting (EnSa in the village, first talk / again)
    { 0x1002, "Hi, Link! Did you see the barricades? Everyone helped board up the village after the noises started.^There's a workbench by your ladder. If a board breaks, we can build another one!", TEXTBOX_TYPE_BLACK },
    { 0x1003, "The barricades at the bridge and up by the Lost Woods keep the night things out...^...mostly. Let's keep them standing, Link!", TEXTBOX_TYPE_BLACK },
    // A Kokiri boy (EnKo type 0)
    { 0x1004, "We boarded up the way to the bridge and the Lost Woods. Nothing gets in at night now. Probably.", TEXTBOX_TYPE_BLACK },
};
// clang-format on

void SevenDays::RegisterVillageMessages(const char* table) {
    for (auto& line : sVillageLines) {
        AddText(table, line.textId,
                                                      CustomMessage(line.text, line.box, TEXTBOX_POS_BOTTOM));
    }
    AddText(
        table, TEXT_SIGN_DAY,
        CustomMessage("Day [[day]]^The boards hold. Build at the workbench. Survive the nights.", TEXTBOX_TYPE_WOODEN,
                      TEXTBOX_POS_BOTTOM));
    AddText(
        table, TEXT_WORKBENCH,
        CustomMessage("A sturdy workbench. Time to build something!", TEXTBOX_TYPE_BLACK, TEXTBOX_POS_BOTTOM));
    AddText(
        table, TEXT_CHEST,
        CustomMessage("The village storage chest. Everything the village has gathered is in here.", TEXTBOX_TYPE_BLACK,
                      TEXTBOX_POS_BOTTOM));
    // #3962: furniture
    AddText(table, TEXT_BOOKSHELF, CustomMessage("[[book]]", TEXTBOX_TYPE_BLACK, TEXTBOX_POS_BOTTOM));
    AddText(table, TEXT_PAINTING,
            CustomMessage("A masked imp, painted in a faraway land.^Its eyes seem to follow you around the room...",
                          TEXTBOX_TYPE_BLACK, TEXTBOX_POS_BOTTOM));
    AddText(table, TEXT_MILK_EMPTY,
            CustomMessage("The milk can is empty.^It'll be full again tomorrow.", TEXTBOX_TYPE_BLACK,
                          TEXTBOX_POS_BOTTOM));
}

// MARK: - M9: the world reacts (towns talk about the nights)

// Idle lines elsewhere in Hyrule, replaced while the 7 Days base is on. Each has a
// line for before the first raid is survived and one for after; [[raids]] ("2 raids"),
// [[days]] ("5 days"), [[base]] and [[next]] are filled in when the line is shown.
// Flavor only: every replaced vanilla text was checked in the ROM's message table
// to have no choice, event, item, ocarina or clock code, and none carries a quest
// hint, a direction or the time. The line shows in a second box after the NPC's own
// words (AfterVanilla), so it must not repeat them (#4005).
struct WorldLine {
    int16_t scene;  // -1: any scene
    uint16_t textId;
    const char* before; // no raid survived yet
    const char* after;  // one or more raids survived
};
// clang-format off
static const WorldLine sWorldLines[] = {
    // Kakariko: the carpenters (EnDaikuKakariko) and Impa's house guard (EnHeishi4: 0x507A, the text 0x5079 chains to)
    { SCENE_KAKARIKO_VILLAGE, 0x5075, "Boss says we're building houses, but half my lumber went to boarding windows.^Folks say something comes out of the field at night.", "[[raids]] now, the field's come knocking. We nail the shutters at dusk and pray they hold." },
    { SCENE_KAKARIKO_VILLAGE, 0x5076, "Hear that? Scratching, out past the gate, every night. Lady Impa says keep the lamps lit.", "Impa's got the watchtower manned every night now. If you see red in the sky, kid, get indoors." },
    { SCENE_KAKARIKO_VILLAGE, 0x5074, "Lady Impa had us shore up the watchtower first. She says she can see the whole field from up there.", "The lookout on the watchtower counted torches out in the field last night. Not ours." },
    { SCENE_KAKARIKO_VILLAGE, 0x506B, "Anju's been bringing her Cuccos in every evening. Says they won't stop squawking at the dark.", "Anju counts her Cuccos every dawn now. Lost two to the last bad night." },
    { SCENE_KAKARIKO_VILLAGE, 0x506A, "We're out of nails. Every house in the village wants its doors barred.", "Barred every door twice. Still hear them on the roofs some nights." },
    { SCENE_KAKARIKO_VILLAGE, 0x507A, "Lady Impa climbs the watchtower every night now. She says she's counting something out in the field.", "Lady Impa has the watchtower lit every night since the raids began. She says the forest is holding." },
    // Hyrule Castle Town: townsfolk (EnHy) and the gate guards (EnHeishi4); night scenes share these
    { SCENE_MARKET_DAY, 0x701E, "The guards say the castle is safe. The guards say a lot of things.", "The guards came back from the field with their spears snapped in half." },
    { SCENE_MARKET_DAY, 0x7020, "Heard something howl from the field last night. That was no dog.", "Wolfos at the drawbridge, they say. Next red night they'll be at the market." },
    { SCENE_MARKET_DAY, 0x7022, "Lock your door at night, dearie. Things come up out of the ground in that field.", "I don't sleep any more. Not since the sky went red that night." },
    { SCENE_MARKET_DAY, 0x7015, "My cousin says the Kokiri boarded up their forest. Since when do the Kokiri need walls?", "They say the forest kids have held off [[raids]]. A whole village of children!" },
    { SCENE_MARKET_DAY, 0x7055, "Loitering? I'm keeping watch, kid. Somebody has to, with what's out in that field.", "[[days]] of this. The King should send soldiers to that forest." },
    { SCENE_MARKET_DAY, 0x700E, "Every night feels longer lately.", "Dawn takes forever on the red nights. I count every minute of them." },
    { SCENE_MARKET_ENTRANCE_DAY, 0x7002, "The drawbridge closes at dusk, and these days we mean it. The field isn't safe at night.", "The patrols report [[raids]] on the forest. We raise the bridge early now." },
    { SCENE_MARKET_ENTRANCE_DAY, 0x7003, "Something's been testing the drawbridge at night.", "Red sky, red moon... Nobody should be out on nights like these." },
    { SCENE_HYRULE_CASTLE, 0x7002, "Rumor in the barracks says the night creatures are out in force.", "Rumor in the barracks: a forest base has held off [[raids]]." },
    // #3935: the castle gate's guard (EnHeishi2) and Impa in the courtyard (DemoIm's repeat line)
    { SCENE_HYRULE_CASTLE, 0x7006, "Not with things crawling out of the field at night!", "And certainly not the dead. We've counted [[raids]] on the forest." },
    { -1, 0x708E, "The field is no place for a child after dark these days. Go home quickly.", "A forest village has held off [[raids]], I hear. The Sheikah are watching it." },
    // #3935: Talon (EnTa: awake at the ranch; asleep in Kakariko as an adult) and Anju (EnNiwLady)
    { -1, 0x2055, "Somebody's gotta mend the fences before the night things find 'em!", "Mended the fences twice since the raids started. Only napped through one of 'em!" },
    { -1, 0x5015, "Z Z Z... Bar the stable, Ingo...", "Z Z Z... [[raids]]... the walls held..." },
    { -1, 0x503D, "They're jumpy enough with all that scratching at night.", "They haven't laid a single egg since the sky went red." },
    { -1, 0x5047, "Out there all alone, with the nights the way they are...", "I hope he had walls around him on the red nights." },
    // Lon Lon Ranch and the castle: Malon (EnMa1, the Epona lines that carry no song or event)
    { -1, 0x2048, "Epona's jumpy lately, fairy boy. Dad says we're going to barricade the ranch. Something's been scaring the horses at night!", "We put boards over the stable doors. Epona still stamps all night when the sky goes red." },
    { -1, 0x204A, "Oh, Epona! She likes you, fairy boy. Mr. Ingo is nailing up the corral gates. He says the night things don't like fences.", "Oh, Epona! The fence held last raid night, and Mr. Ingo even smiled. A little." },
    // Zora's Domain (EnZo)
    { SCENE_ZORAS_DOMAIN, 0x400A, "He says the surface's troubles are not ours. I'm not so sure.", "Even he asked about the forest's walls. Word travels fast down the river." },
    { SCENE_ZORAS_DOMAIN, 0x4011, "Lately, strange things wash down the river after dark.", "The river runs strange on the red nights. We stay deep in the Domain until dawn." },
    { SCENE_ZORAS_DOMAIN, 0x402D, "She says you're braver than the night things.", "They say you've lasted [[days]] out there. Impressive, for a land-dweller." },
    { SCENE_ZORAS_DOMAIN, 0x402E, "Now if only the nights were quiet...", "And you've held off [[raids]]. Is there anything you can't do?" },
    // Goron City and Death Mountain Trail (EnGo2)
    { SCENE_GORON_CITY, 0x3015, "And now things crawl up the mountain at night too!", "We rolled boulders across the trail at night. Nothing gets into Goron City, goro!" },
    { SCENE_GORON_CITY, 0x3027, "Now if only the nights were quiet, Big Brother...", "Big Brother, you saved our food and you keep beating back the nights! [[raids]], goro!" },
    { SCENE_DEATH_MOUNTAIN_TRAIL, 0x3026, "The trail's dangerous at night, brother. Things come up from Kakariko.", "We stacked rocks at the trail's bend. Even Stalchildren can't climb those!" },
    { SCENE_DEATH_MOUNTAIN_TRAIL, 0x3027, "The volcano smokes, and the field howls. What a time, goro!", "The night things never come this high. You should move your base up here, brother!" },
    // Lake Hylia: the lab scientist (EnMk) and the Zora at the lake (EnZo)
    { SCENE_LAKESIDE_LABORATORY, 0x4018, "Fascinating! The night creatures rise at the same hour every few days. A pattern! I must record it.", "My notes: [[raids]], [[days]]. The next red night should come [[when]]. Science!" },
    { SCENE_LAKE_HYLIA, 0x4021, "Things wash up here after dark now.", "On the red nights the lake glows strange. I stay under the water until dawn." },
    // Gerudo Valley and Fortress (EnGe1: the gate guard to a kid, the valley floor, the fortress greeting)
    { SCENE_GERUDO_VALLEY, 0x6069, "Night creatures or not, that's no place for a kid.", "We cut the bridge on red nights and fix it at dawn." },
    { SCENE_GERUDO_VALLEY, 0x601A, "At least the night things don't climb these cliffs.", "Hiding from the red nights too? Smart." },
    { SCENE_GERUDOS_FORTRESS, 0x6001, "Hylian creatures at night? Ha! Let them try our walls.", "Even we post double guards on red nights now. Don't tell anyone." },
    // #3935: the Training Ground's gate guard (EnGe1), unqualified / qualified
    { SCENE_GERUDOS_FORTRESS, 0x6070, "Not even if the dead come knocking.", "The red nights changed nothing!" },
    { SCENE_GERUDOS_FORTRESS, 0x6072, "Some of that treasure buys walls.", "We'll need that treasure if the dead ever cross the desert." },
    // #4006: the man stuck on the Kakariko roof (EnHy) half-remembers the torch ward.
    { SCENE_KAKARIKO_VILLAGE, 0x5050, "Being stuck up here, you hear every old story in the village.^Come back when the stars are out. That's when I remember them.", "Being stuck up here, you hear every old story in the village.^Come back when the stars are out. That's when I remember them." },
    { SCENE_KAKARIKO_VILLAGE, 0x5051, "My grandpa sat up here too. He said the old Sheikah never bothered with walls.^They lit a dozen fires in a ring, way out where the dead crawl up, so close there was no dark left between them...^Then again, he also said he saw a fish fly.", "Grandpa said the Sheikah never bothered with walls. A dozen fires in a ring, way out, no dark between them...^Funny. Out over the forest, some nights, I could swear I see blue." },
    // Gossip stones (EnGs, the plain talk without the Mask of Truth)
    { -1, 0x2053, "They say the night things come back every few days... and they always come for the base.", "They say the next raid comes [[when]]. They say [[base]]." },
};
// clang-format on

// Castle Town's night scenes share their day scene's lines.
static int16_t TownScene(int16_t scene) {
    switch (scene) {
        case SCENE_MARKET_NIGHT:
            return SCENE_MARKET_DAY;
        case SCENE_MARKET_ENTRANCE_NIGHT:
            return SCENE_MARKET_ENTRANCE_DAY;
        default:
            return scene;
    }
}

static const WorldLine* FindWorldLine(uint16_t textId) {
    // Rando hints and the like go through their own text: leave them alone.
    if (!BaseEnabled() || gPlayState == nullptr || IS_RANDO) {
        return nullptr;
    }
    // SoH's MarketSneak turns the night gate guard's line into a Yes/No choice
    // (OTRGlobals): replacing it would leave the guard waiting on an answer forever.
    if (textId == TEXT_MARKET_GUARD_NIGHT && CVarGetInteger(CVAR_ENHANCEMENT("MarketSneak"), 0) &&
        gPlayState->sceneNum == SCENE_MARKET_ENTRANCE_NIGHT) {
        return nullptr;
    }
    int16_t scene = TownScene(gPlayState->sceneNum);
    for (auto& line : sWorldLines) {
        if (line.textId == textId && (line.scene < 0 || line.scene == scene)) {
            return &line;
        }
    }
    return nullptr;
}

static std::string NextRaidText() {
    uint32_t n = NightsUntilRaid();
    if (n == UINT32_MAX) {
        return "a few days";
    }
    if (n == 0) {
        return "tonight";
    }
    return n == 1 ? "one day" : fmt::format("{} days", n);
}

// The new line goes after the NPC's own words, in a second box, never over them. Vanilla text that
// runs anything (a choice, an event, an item, a delay) is left to vanilla alone.
static bool AfterVanilla(uint16_t textId, CustomMessage& flavor) {
    CustomMessage vanilla = CustomMessage::LoadVanillaMessageTableEntry(textId);
    std::string v = vanilla.GetEnglish(MF_RAW);
    if (v.empty() || v.back() != '\x02') {
        return false;
    }
    v.pop_back();
    for (size_t i = 0; i < v.size(); i++) {
        unsigned char c = v[i];
        // 0x08/0x09: quick text on/off (the castle gate guard's 0x7006 opens with it)
        if (c >= 0x20 || c == 0x01 || c == 0x04 || c == 0x08 || c == 0x09 || c == 0x0F) {
            continue;
        }
        if (c == 0x05 || c == 0x06) {
            i++;
            continue;
        }
        return false;
    }
    std::string f = flavor.GetEnglish(MF_RAW);
    if (!f.empty() && f.back() == '\x02') {
        f.pop_back();
    }
    flavor = CustomMessage(v + "\x04" + f + "\x02", vanilla.GetTextBoxType(), vanilla.GetTextBoxPosition());
    return true;
}

bool SevenDays::AfterVanillaText(uint16_t textId, CustomMessage& flavor) {
    return AfterVanilla(textId, flavor);
}

static uint16_t sLastTextId = 0; // tests: the last vanilla text id asked for

bool SevenDays::WorldText(uint16_t textId, CustomMessage& out) {
    sLastTextId = textId;
    const char* text = nullptr;
    TextBoxType box = TEXTBOX_TYPE_BLACK;
    const BaseState& b = GetBase();
    if (const WorldLine* line = FindWorldLine(textId)) {
        text = b.hordeNightsSurvived > 0 ? line->after : line->before;
    } else {
        return false;
    }
    out = CustomMessage(text, box, TEXTBOX_POS_BOTTOM);
    FillWorldText(out);
    out.AutoFormat();
    return AfterVanilla(textId, out);
}

void SevenDays::FillWorldText(CustomMessage& msg) {
    const BaseState& b = GetBase();
    const BaseCenter& c = b.center[CurrentEraNow()];
    const char* home = c.valid ? OutdoorSceneName(c.scene) : nullptr;
    auto count = [](uint32_t n, const char* one, const char* many) { return fmt::format("{} {}", n, n == 1 ? one : many); };
    msg.Replace("[[raids]]", count(b.hordeNightsSurvived, "raid", "raids"));
    msg.Replace("[[days]]", count(b.daysSurvived, "day", "days"));
    msg.Replace("[[next]]", NextRaidText());
    uint32_t n = NightsUntilRaid();
    msg.Replace("[[when]]", n == UINT32_MAX ? std::string("in a few days")
                            : n == 0        ? std::string("tonight")
                            : n == 1        ? std::string("tomorrow night")
                                            : fmt::format("in {} days", n));
    msg.Replace("[[base]]", home != nullptr ? fmt::format("someone built walls in {}", home) : "nobody has built walls yet");
    msg.Replace("[[book]]", BookLine()); // #3962: the bookshelf
}

// MARK: - #3935: Navi's C-Up tips

// Crafting and raid tips, used when Navi has nothing new to say: once her story hint
// for this point in the game has been heard, C-Up gives the next tip instead.
// clang-format off
static const char* sNaviTips[NAVI_TIP_COUNT] = {
    "Hey! The next raid comes [[when]].^Check the walls before it gets dark!",
    "Torches keep the dead from crawling up anywhere near them.^Light up the edges of the base!",
    "A damaged piece only packs up into part of its materials.^Repair it at the workbench first!",
    "Spike strips bite anything that walks over them.^Lay them in front of the barricades!",
    "A scarecrow draws raiders away from the workbench.^Give them something else to hit!",
    "A bomb flower in the base blows up the first raider that gets close.^Then it grows a new bomb!",
    "Stone walls take twice the beating wooden barricades do.^Bombs crack rocks for the stone!",
    "A player gate swings open for us and stays shut for them.^They'll have to break it down!",
    "Pots, crates and supply caches pay out materials.^Some caches even hold blueprints!",
    "If everyone falls during a raid, the walls crumble and the stores are raided.^Stay on your feet!",
    "Surviving a raid pays in materials at dawn.^The workbench's Base tab says how often they come.",
    "We've held off [[raids]] so far.^The longer we last, the more the dead send!",
};
// clang-format on

static std::vector<uint16_t> sHeardHints; // vanilla C-Up hints already heard this session
static uint16_t sLastCUpHint = 0;
static uint16_t sNextTip = 0;

// z_elf_message.c: ElfMessage_GetCUpText's result goes through here. The id stays
// vanilla (Player keeps it in an s16 and treats negative ids specially); only the
// text shown for it changes, in NaviTipText.
extern "C" u16 SevenDays_CUpText(u16 vanilla) {
    if (BaseEnabled() && !IS_RANDO) {
        sLastCUpHint = vanilla;
    }
    return vanilla;
}

// OTRGlobals glue: Navi's C-Up hint is opening. The first time she gives her story
// hint; once it has been heard, the next tip from the pool instead.
bool SevenDays::NaviTipText(uint16_t textId, CustomMessage& out) {
    if (!BaseEnabled() || IS_RANDO || textId == 0 || textId == 0x15F || textId != sLastCUpHint) {
        return false;
    }
    if (std::find(sHeardHints.begin(), sHeardHints.end(), textId) == sHeardHints.end()) {
        sHeardHints.push_back(textId);
        return false; // something she hasn't said yet: the story comes first
    }
    out = CustomMessageManager::Instance->RetrieveMessage("SevenDays", TEXT_NAVI_TIPS + sNextTip, MF_AUTO_FORMAT);
    FillWorldText(out);
    sNextTip = (uint16_t)((sNextTip + 1) % NAVI_TIP_COUNT);
    return true;
}

void SevenDays::RegisterNaviTips(const char* table) {
    for (uint16_t i = 0; i < NAVI_TIP_COUNT; i++) {
        AddText(table, TEXT_NAVI_TIPS + i,
                                                      CustomMessage(sNaviTips[i], TEXTBOX_TYPE_BLUE, TEXTBOX_POS_BOTTOM));
    }
}

bool SevenDays::OverridesVanillaText(uint16_t textId) {
    if (!BaseEnabled() || gPlayState == nullptr || gPlayState->sceneNum != SCENE_KOKIRI_FOREST || !LINK_IS_CHILD) {
        return false;
    }
    for (auto& line : sVillageLines) {
        if (line.textId == textId) {
            return true;
        }
    }
    return false;
}


#ifdef __EMSCRIPTEN__
#include <emscripten.h>
extern "C" {
// #4062: display-list pool usage. "" reads, "reset" clears the peaks, "force:N" makes the next N
// frames count as overrun (the failsafe path), "scale" is read and set through the cvar
// gSevenDays.GfxPoolScale.
EMSCRIPTEN_KEEPALIVE
const char* sevendays_test_gfx(const char* cmd) {
    std::string c = cmd ? cmd : "";
    if (c == "reset") {
        Graph_GfxStatsReset();
    } else if (c.rfind("force:", 0) == 0) {
        Graph_GfxForceOverflow(atoi(c.c_str() + 6));
    }
    return Graph_GfxStatsJson();
}

EMSCRIPTEN_KEEPALIVE
int sevendays_test_last_text() {
    return sLastTextId;
}

// #4005: every NPC line this mod adds to vanilla text: the NPC's own words, whether
// the new line can go after them (AfterVanilla), and ids listed twice for one scene.
EMSCRIPTEN_KEEPALIVE
const char* sevendays_test_world_audit() {
    static std::string out;
    nlohmann::json j = nlohmann::json::array();
    auto add = [&j](const char* kind, int16_t scene, uint16_t textId, const char* text) {
        CustomMessage flavor(text, TEXTBOX_TYPE_BLACK, TEXTBOX_POS_BOTTOM);
        std::string vanilla = CustomMessage::LoadVanillaMessageTableEntry(textId).GetEnglish(MF_CLEAN);
        int dupes = 0;
        for (auto& other : sWorldLines) {
            dupes += other.textId == textId && (other.scene < 0 || scene < 0 || other.scene == scene);
        }
        j.push_back({ { "kind", kind },
                      { "scene", scene },
                      { "id", fmt::format("{:04X}", textId) },
                      { "shown", AfterVanilla(textId, flavor) },
                      { "dupes", dupes },
                      { "vanilla", vanilla.substr(0, 90) } });
    };
    for (auto& line : sWorldLines) {
        add("world", line.scene, line.textId, line.before);
    }
    for (auto& line : sVillageLines) {
        add("village", SCENE_KOKIRI_FOREST, line.textId, line.text);
    }
    out = j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    return out.c_str();
}

EMSCRIPTEN_KEEPALIVE
int sevendays_test_trap_blasts() {
    return sTrapBlasts;
}

// #3969: the icon studio. type: a PLACEABLE_* type, 100 small crate, 101 large crate,
// or -1 to put it away; white: the backdrop's shade; turn: the piece's yaw toward the camera.
EMSCRIPTEN_KEEPALIVE
void sevendays_test_icon_studio(int type, int white, int turn) {
    sStudioTurn = (s16)turn;
    sStudioRuin = (type & 0x100) != 0; // type | 0x100: the piece as a ruin
    type &= ~0x100;
    if (gPlayState == nullptr || sStudioId < 0) {
        return;
    }
    bool valid = (type >= 0 && type < PLACEABLE_COUNT) || type == STUDIO_SMALL_CRATE || type == STUDIO_LARGE_CRATE;
    sStudioWhite = white != 0;
    if (sStudio != nullptr && (!valid || sStudioType != type)) {
        Actor_Kill(sStudio);
        sStudio = nullptr;
    }
    sStudioType = valid ? type : -1;
    if (valid && sStudio == nullptr) {
        Player* player = GET_PLAYER(gPlayState);
        int16_t params = type < PLACEABLE_COUNT ? (int16_t)type : (int16_t)0xFF;
        sStudio = Actor_Spawn(&gPlayState->actorCtx, gPlayState, sStudioId, player->actor.world.pos.x,
                              player->actor.world.pos.y, player->actor.world.pos.z, 0, 0, 0, params, false);
    }
}

// Navi's C-Up hint now, how many hints were heard, and the next tip.
EMSCRIPTEN_KEEPALIVE
const char* sevendays_test_cup_state() {
    static std::string out;
    nlohmann::json j;
    j["cup"] = gPlayState != nullptr ? ElfMessage_GetCUpText(gPlayState) : -1;
    j["heard"] = sHeardHints;
    j["nextTip"] = sNextTip;
    out = j.dump();
    return out.c_str();
}
}
#endif
