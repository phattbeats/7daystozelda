#include "SevenDays.h"
#include "soh/ActorDB.h"
#include "soh/frame_interpolation.h"
#include "soh/Enhancements/custom-message/CustomMessageManager.h"
#include "soh/Enhancements/custom-message/CustomMessageTypes.h"
#include "soh/Network/Anchor/Anchor.h"
#include "soh/Network/Anchor/EnemySync.h"
#include "soh/Network/Anchor/HordeNight.h"

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

// PHA-3904: models cut from Majora's Mask by art/mm-pack/build_mm_pack.py. They are
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

// PHA-3945: floors, stairs and doors, from the same pack (a newer build of it).
static const ALIGN_ASSET(2) char gMMPiratePanelDL[] = "__OTR__objects/7dtz_mm/taru/gMMPiratePanelDL";
static const ALIGN_ASSET(2) char gMMRanchPlankDL[] = "__OTR__objects/7dtz_mm/gMMRanchPlankDL";
static const ALIGN_ASSET(2) char gMMStonePlatformDL[] = "__OTR__objects/7dtz_mm/raillift/gMMStonePlatformDL";
static const ALIGN_ASSET(2) char gMMFestivalDeckDL[] = "__OTR__objects/7dtz_mm/tokei_turret/gMMFestivalDeckDL";
static const ALIGN_ASSET(2) char gMMLadderDL[] = "__OTR__objects/7dtz_mm/ladder/gMMLadderDL";
static const ALIGN_ASSET(2) char gMMInnStairsDL[] = "__OTR__objects/7dtz_mm/gMMInnStairsDL";
static const ALIGN_ASSET(2) char gMMSwampDoorDL[] = "__OTR__objects/7dtz_mm/dor03/gMMSwampDoorDL";
static const ALIGN_ASSET(2) char gMMMusicBoxDoorDL[] = "__OTR__objects/7dtz_mm/wdor05/gMMMusicBoxDoorDL";
static const ALIGN_ASSET(2) char gMMPirateDoorDL[] = "__OTR__objects/7dtz_mm/kaizoku_obj/gMMPirateDoorDL";

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
 *                        and, since PHA-3904, a few Majora's Mask models from the
 *                        server's soh.o2r (palisade logs, the workbench's desk and
 *                        hammer). It has no collision of its own: see
 *                        SevenDays_BaseCollision.
 *   SevenDays_BaseCollision  PHA-3916: the pieces' boxes (8 vertices, 12 triangles
 *                        each; PHA-3945: a ramp for the stairs, a slab on posts for
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
    // PHA-3935: the torch's light, the bomb-flower trap and the gate.
    LightNode* lightNode;
    LightInfo lightInfo;
    ColliderCylinder blastCollider;
    int16_t trapFuse;    // frames until the bomb goes off (0: not lit)
    int16_t trapRegrow;  // frames until the bomb has grown back (0: armed)
    int16_t blastFrames; // frames left of the blast's attack collider
    f32 gateOpen;        // 0 shut .. 1 swung open
    bool gatePassable;   // left out of the base collision while a player walks through
    bool ruin;           // the child base after the seven-year jump: drawn broken, does nothing
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
static int16_t sCollisionId = -1;

int16_t SevenDays::PlaceableActorId() {
    return sPlaceableId;
}

int16_t SevenDays::GhostActorId() {
    return sGhostId;
}

// MARK: - Collision: one shape per type, built once

// Surface 0: wood-ish floor sound, normal walls. Surface 1 (PHA-3945): the same with
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

// PHA-3916: grow the dynamic collision lists where a base can stand
// (BgCheck_Allocate, z_bgcheck.c). 4096 polys/vertices/nodes, about 80 KB of
// the play arena: room for 100+ pieces next to the scene's own movers.
extern "C" s32 SevenDays_DynaBudget(s16 sceneNum) {
    return BaseEnabled() && IsOutdoorScene(sceneNum) ? 4096 : 0;
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
static SkelAnime sChestSkel;
static Vec3s sChestJoints[5];
static Vec3s sChestMorph[5];
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

static void DrawChest(PlayState* play) {
    if (!sChestReady) {
        AnimationHeader* anim = (AnimationHeader*)gTreasureChestAnim_00024C;
        SkelAnime_Init(play, &sChestSkel, (SkeletonHeader*)gTreasureChestSkel, anim, sChestJoints, sChestMorph, 5);
        Animation_Change(&sChestSkel, anim, 0.0f, 0.0f, 0.0f, ANIMMODE_ONCE, 0.0f);
        SkelAnime_Update(&sChestSkel);
        sChestReady = true;
    }
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
    SkelAnime_DrawOpa(play, sChestSkel.skeleton, sChestSkel.jointTable, nullptr, ChestPostLimbDraw, nullptr);
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
// against its neighbour and another has snapped off.
static void DrawPalisade(PlayState* play, float hpFrac) {
    if (!MMPackLoaded()) {
        // No MM pack: the horse-jump fence stretched to the wall's height.
        DrawDL(play, (Gfx*)gJumpableHorseFenceDL, 0.0f, 0.0f, 0.0f, 0.0375f, 0.12f, 0.1f);
        return;
    }
    static const float kHeight[5] = { 1.0f, 0.95f, 1.04f, 0.97f, 1.01f };
    static const s16 kYaw[5] = { 0x0000, 0x3000, 0x6800, 0x9C00, 0xD000 };
    const float s = 96.0f / 630.0f;
    for (int i = 0; i < 5; i++) {
        float x = -48.0f + 24.0f * i, sy = s * kHeight[i], rz = 0.0f;
        if (hpFrac < 0.5f && i == 1) {
            sy *= 0.55f; // snapped off
        } else if (hpFrac < 0.5f && i == 3) {
            rz = -0.3f; // leaning on the log beside it
        }
        DrawDLYaw(play, (Gfx*)gMMPracticeLogDL, x, 0.0f, 0.0f, kYaw[i], s, sy, s, rz);
    }
}

// Workbench: the Stock Pot Inn's desk with drawers (44 x 29 x 29 in the room, at
// -435..-391, 210..239, 360..389), scaled 1.8x, with Gabora's smithing hammer and a
// red-hot sword blank from the Mountain Village smithy lying on top.
static void DrawWorkbench(PlayState* play) {
    if (!MMPackLoaded()) {
        // No MM pack: the dungeon shop's wooden shelves at half size.
        DrawDL(play, (Gfx*)gShopDungenWoodenShelvesDL, 0.0f, 0.0f, 9.0f, 0.5f, 0.5f, 0.5f);
        return;
    }
    const float s = 1.8f, top = 29.0f * s;
    DrawDL(play, (Gfx*)gMMInnDeskDL, 413.0f * s, -210.0f * s, -374.5f * s, s, s, s);
    OPEN_DISPS(play->state.gfxCtx);
    // The hammer stands with its handle along +y and its head across x (315..3034)
    // at 0.01. Laid flat: handle along x, head pointing back, resting on the top.
    Matrix_Push();
    Matrix_Translate(-10.0f, top + 4.6f, -2.0f, MTXMODE_APPLY);
    Matrix_RotateX(M_PI / 2, MTXMODE_APPLY);
    Matrix_RotateZ(-M_PI / 2, MTXMODE_APPLY);
    Matrix_Scale(0.006f, 0.006f, 0.006f, MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_OPA_DISP++, (Gfx*)gMMSmithyHammerDL);
    Matrix_Pop();
    // The blade (x 426..1727, its width along y) laid flat in front of the hammer.
    // Segments 8 and 9 are En_Kgy's render-mode hooks: empty lists keep the opaque mode.
    Gfx* empty = (Gfx*)Graph_Alloc(play->state.gfxCtx, sizeof(Gfx));
    gSPEndDisplayList(empty);
    gSPSegment(POLY_OPA_DISP++, 0x08, (uintptr_t)empty);
    gSPSegment(POLY_OPA_DISP++, 0x09, (uintptr_t)empty);
    Matrix_Push();
    Matrix_Translate(-21.0f, top + 1.6f, 12.0f, MTXMODE_APPLY);
    Matrix_RotateX(M_PI / 2, MTXMODE_APPLY);
    Matrix_Scale(0.02f, 0.02f, 0.02f, MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_OPA_DISP++, (Gfx*)gMMSmithyBladeDL);
    Matrix_Pop();
    CLOSE_DISPS(play->state.gfxCtx);
}

// MARK: - PHA-3945: floors, stairs and doors

// Without the pack: OoT's push block (8000 units square at scale 1) stretched over the box.
static void DrawFallbackBox(PlayState* play, uint8_t type) {
    const PlaceableInfo& info = GetPlaceableInfo(type);
    DrawDL(play, (Gfx*)gBlockSmallDL, 0.0f, 0.0f, 0.0f, info.halfX / 4000.0f, info.height / 8000.0f,
           info.halfZ / 4000.0f);
}

// The Pirates' Fortress panel is grey, weathered wood: washed a little warmer, unless the
// piece is already tinted (damaged, a ruin, the ghost).
static void WoodWash(PlayState* play, bool on) {
    OPEN_DISPS(play->state.gfxCtx);
    if (on) {
        gDPSetGrayscaleColor(POLY_OPA_DISP++, 205, 160, 110, 150);
    }
    gSPGrayscale(POLY_OPA_DISP++, on);
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
static void DrawStep(PlayState* play, bool wash) {
    WoodWash(play, wash);
    for (int i = 0; i < 4; i++) {
        s16 yaw = (s16)(i * 0x4000);
        f32 sx = Math_SinS(yaw) * 28.0f, sz = Math_CosS(yaw) * 28.0f;
        DrawPanel(play, sx, 0.0f, sz, yaw, 60.0f, 48.0f, 4.0f, false);
    }
    DrawPanel(play, 0.0f, 48.0f, 0.0f, 0, 60.0f, 60.0f, 4.0f, true);
    WoodWash(play, false);
}

// Ranch floor: three planks from the Romani Ranch house (40 x 6 x 164 in the room, at
// 600..640, 57..63, -100..64), side by side and cut to 120 long.
static void DrawRanchFloor(PlayState* play) {
    OPEN_DISPS(play->state.gfxCtx);
    for (int i = -1; i <= 1; i++) {
        Matrix_Push();
        Matrix_Translate(i * 40.0f, 0.0f, 0.0f, MTXMODE_APPLY);
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
static void DrawStairs(PlayState* play) {
    OPEN_DISPS(play->state.gfxCtx);
    for (int mirror = 0; mirror < 2; mirror++) {
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
static void DrawDoor(PlayState* play, Gfx* leaf, f32 open) {
    s16 swing = (s16)(open * 0x4000);
    OPEN_DISPS(play->state.gfxCtx);
    for (int side = 0; side < 2; side++) {
        Matrix_Push();
        Matrix_Translate(side == 0 ? -60.0f : 60.0f, 0.0f, 0.0f, MTXMODE_APPLY);
        Matrix_RotateY((side == 0 ? -swing : (s16)(0x8000 + swing)) * (M_PI / 0x8000), MTXMODE_APPLY);
        Matrix_RotateX(-M_PI / 2, MTXMODE_APPLY); // stand it up
        Matrix_Scale(0.01f, 0.01f, 0.01f, MTXMODE_APPLY);
        gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(POLY_OPA_DISP++, leaf);
        Matrix_Pop();
    }
    CLOSE_DISPS(play->state.gfxCtx);
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

// Where the head sits relative to the stalk's base: up, and forward along its yaw.
// EnDekubaba draws its sections from the head down, so the sines come out negative.
static void BabaHead(f32 lunge, f32* y, f32* z) {
    s16 ang[3];
    BabaAngles(lunge, ang);
    *y = *z = 0.0f;
    for (int i = 0; i < 3; i++) {
        *y -= 20.0f * BABA_SIZE * Math_SinS(ang[i]);
        *z += 20.0f * BABA_SIZE * Math_CosS(ang[i]);
    }
}

static void DrawTorchFlame(PlayState* play, PlaceableActor* self);
static void DrawBombFlower(PlayState* play, PlaceableActor* self);
static void DrawGate(PlayState* play, f32 open);

static void DrawModel(PlayState* play, uint8_t type, float hpFrac, PlaceableActor* self = nullptr) {
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
            // The stone wall's blocks, washed steel blue-grey, with an iron grate across.
            {
                OPEN_DISPS(play->state.gfxCtx);
                gDPSetGrayscaleColor(POLY_OPA_DISP++, 120, 130, 150, 255);
                gSPGrayscale(POLY_OPA_DISP++, true);
                CLOSE_DISPS(play->state.gfxCtx);
                DrawDL(play, (Gfx*)gBlockSmallDL, -30.0f, 0.0f, 0.0f, 0.0075f, 0.0075f, 0.0075f);
                DrawDL(play, (Gfx*)gBlockSmallDL, 30.0f, 0.0f, 0.0f, 0.0075f, hpFrac >= 0.5f ? 0.0075f : 0.005f,
                       0.0075f);
                OPEN_DISPS(play->state.gfxCtx);
                gSPGrayscale(POLY_OPA_DISP++, false);
                CLOSE_DISPS(play->state.gfxCtx);
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
            DrawPalisade(play, hpFrac);
            break;
        case PLACEABLE_SPIKES:
            for (int i = -1; i <= 1; i++) {
                DrawDL(play, (Gfx*)gUnusedSpikeDL, i * 30.0f, 4.0f, 0.0f, 0.004f, 0.004f, 0.004f);
            }
            break;
        case PLACEABLE_WORKBENCH:
            DrawWorkbench(play);
            break;
        case PLACEABLE_CHEST:
            DrawChest(play);
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
                DrawPanel(play, 0.0f, 0.0f, 0.0f, 0, 120.0f, 120.0f, 8.0f, true);
                WoodWash(play, false);
            } else if (type == PLACEABLE_FLOOR_RANCH) {
                DrawRanchFloor(play);
            } else if (type == PLACEABLE_FLOOR_STONE) {
                // A Woodfall Temple platform (1000 square, 200 thick, its top at y = 0).
                DrawDL(play, (Gfx*)gMMStonePlatformDL, 0.0f, 24.0f, 0.0f, 0.12f, 0.12f, 0.12f);
            } else if (type == PLACEABLE_DECK) {
                // The top of the Clock Town carnival tower (1360 square, 800 tall): planks on four posts.
                DrawDL(play, (Gfx*)gMMFestivalDeckDL, 0.0f, 0.0f, 0.0f, 120.0f / 1360.0f,
                       (f32)STOREY_HEIGHT / 800.0f, 120.0f / 1360.0f);
            } else if (type == PLACEABLE_STEP) {
                DrawStep(play, self != nullptr && hpFrac >= 0.5f);
            } else if (type == PLACEABLE_LADDER) {
                DrawLadder(play);
            } else {
                DrawStairs(play);
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
                         open);
            }
            break;
        }
    }
}

// The Deku Baba's stalk and head drawn from home outward, head in front (+z in
// the frame yawed by `yaw`). Without an actor (the placement ghost) it is just the
// stalk and leaves.
static void DrawBaba(PlayState* play, PlaceableActor* self, f32 lunge, s16 yaw) {
    const f32 size = BABA_SIZE, sc = 0.01f * size;
    s16 ang[3];
    BabaAngles(lunge, ang);
    f32 headY, headZ;
    BabaHead(lunge, &headY, &headZ);
    OPEN_DISPS(play->state.gfxCtx);
    // Tamed: a warm gold-green wash over the usual jungle green.
    if (self != nullptr) {
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
    if (self != nullptr && self->hasSkel) {
        Matrix_Push();
        Matrix_Translate(0.0f, headY, headZ, MTXMODE_APPLY);
        Matrix_Scale(sc, sc, sc, MTXMODE_APPLY);
        SkelAnime_DrawSkeletonOpa(play, &self->skel, nullptr, nullptr, &self->actor);
        Matrix_Pop();
    }
    Matrix_Pop();
    if (self != nullptr) {
        gSPGrayscale(POLY_OPA_DISP++, false);
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

// ObjSyokudai's flame: the scrolling fire billboard over the stand, facing the camera.
static void DrawTorchFlame(PlayState* play, PlaceableActor* self) {
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    gSPSegment(POLY_XLU_DISP++, 0x08,
               (uintptr_t)Gfx_TwoTexScroll(play->state.gfxCtx, 0, 0, 0, 0x20, 0x40, 1, 0,
                                (play->gameplayFrames * -20) & 0x1FF, 0x20, 0x80));
    gDPSetPrimColor(POLY_XLU_DISP++, 0x80, 0x80, 255, 255, 0, 255);
    gDPSetEnvColor(POLY_XLU_DISP++, 255, 0, 0, 0);
    Matrix_Push();
    Matrix_Translate(0.0f, 52.0f, 0.0f, MTXMODE_APPLY);
    Matrix_RotateY((s16)(Camera_GetCamDirYaw(GET_ACTIVE_CAM(play)) - self->actor.shape.rot.y + 0x8000) *
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
        AT_ON | AT_TYPE_PLAYER, // blows up raiders, never players
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

static uint16_t TextFor(uint8_t type) {
    switch (type) {
        case PLACEABLE_SIGN:
            return TEXT_SIGN_DAY;
        case PLACEABLE_WORKBENCH:
            return TEXT_WORKBENCH;
        case PLACEABLE_CHEST:
            return TEXT_CHEST;
    }
    return 0;
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
    thisx->room = -1; // scene-wide: survives walking between rooms
    Actor_SetScale(thisx, 1.0f);
    thisx->shape.rot = thisx->world.rot = { 0, p->rot, 0 };

    // Re-snap to the floor under it (seeded pieces carry an approximate y). PHA-3945:
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
    static const uint8_t kCategories[] = { ACTORCAT_PLAYER, ACTORCAT_PROP, ACTORCAT_NPC };
    Player* self = GET_PLAYER(play);
    Vec3f at = { (f32)c.dim.pos.x, (f32)c.dim.pos.y, (f32)c.dim.pos.z };
    for (uint8_t cat : kCategories) {
        for (Actor* a = play->actorCtx.actorLists[cat].head; a != nullptr; a = a->next) {
            if (a == &self->actor || a->update == nullptr) {
                continue;
            }
            // Remote players only take hits with PvP on against their team: DummyPlayer
            // clears this flag exactly then.
            if (cat == ACTORCAT_PLAYER && (a->flags & ACTOR_FLAG_LOCK_ON_DISABLED)) {
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
    u8 brightness = (u8)(Rand_ZeroOne() * 127.0f) + 128;
    Lights_PointSetColorAndRadius(&self->lightInfo, brightness, brightness, 0, 250);
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
            Collider_UpdateCylinder(thisx, &self->blastCollider);
            CollisionCheck_SetAT(play, &play->colChkCtx, &self->blastCollider.base);
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

// Player gate (and PHA-3945's doors): swings open while a player stands in front of or
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
        if (self->talking) {
            if (Actor_TextboxIsClosing(thisx, play) || play->msgCtx.msgMode == MSGMODE_NONE) {
                self->talking = 0;
                OnPlaceableInteract(self->type);
            }
        } else if (Actor_ProcessTalkRequest(thisx, play)) {
            self->talking = 1;
        } else if (!InPlacement()) {
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

static void Placeable_Draw(Actor* thisx, PlayState* play) {
    PlaceableActor* self = (PlaceableActor*)thisx;
    const Placeable* p = FindPlaceable(self->id);
    const PlaceableInfo& info = GetPlaceableInfo(self->type);
    float hpFrac = (p != nullptr && info.maxHp > 0) ? (float)p->hp / (float)info.maxHp : 1.0f;
    if (self->ruin) {
        hpFrac = 0.1f; // the broken look: a crate knocked askew, a block knocked down
    }

    bool tint = (info.maxHp > 0 && hpFrac < 0.5f) || self->ruin;
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    if (self->ruin) {
        // Seven years of moss and rot.
        gDPSetGrayscaleColor(POLY_OPA_DISP++, 70, 85, 55, 190);
        gSPGrayscale(POLY_OPA_DISP++, true);
    } else if (tint) {
        // Damaged wood darkens.
        gDPSetGrayscaleColor(POLY_OPA_DISP++, 120, 80, 50, 140);
        gSPGrayscale(POLY_OPA_DISP++, true);
    }
    CLOSE_DISPS(play->state.gfxCtx);

    if (self->ruin) {
        // Half sunk and leaning.
        Matrix_Push();
        Matrix_Translate(0.0f, -6.0f, 0.0f, MTXMODE_APPLY);
        Matrix_RotateZ(0.12f, MTXMODE_APPLY);
        Matrix_RotateX(-0.08f, MTXMODE_APPLY);
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

    OPEN_DISPS(play->state.gfxCtx);
    if (tint) {
        gSPGrayscale(POLY_OPA_DISP++, false);
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

// MARK: - SevenDays_Ghost (placement mode)

static void Ghost_Init(Actor* thisx, PlayState* play) {
    thisx->room = -1;
    Actor_SetScale(thisx, 1.0f);
}

static void Ghost_Destroy(Actor* thisx, PlayState* play) {
    OnGhostDestroyed(thisx);
}

static void Ghost_Update(Actor* thisx, PlayState* play) {
    PlacementUpdate(thisx, play);
}

static void Ghost_Draw(Actor* thisx, PlayState* play) {
    uint8_t type = (uint8_t)thisx->params;
    if (type >= PLACEABLE_COUNT) {
        return;
    }
    bool valid = PlacementGhostValid();
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    gDPSetGrayscaleColor(POLY_OPA_DISP++, valid ? 150 : 255, valid ? 255 : 90, valid ? 170 : 90, 200);
    gSPGrayscale(POLY_OPA_DISP++, true);
    CLOSE_DISPS(play->state.gfxCtx);
    // Flicker the solid model so the ghost reads as see-through.
    if ((play->gameplayFrames & 1) == 0) {
        DrawModel(play, type, 1.0f);
    }
    OPEN_DISPS(play->state.gfxCtx);
    gSPGrayscale(POLY_OPA_DISP++, false);
    CLOSE_DISPS(play->state.gfxCtx);
    DrawGhostVolume(play, GetPlaceableInfo(type), valid);
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
    ghost.instanceSize = sizeof(Actor);
    ghost.init = Ghost_Init;
    ghost.destroy = Ghost_Destroy;
    ghost.update = Ghost_Update;
    ghost.draw = Ghost_Draw;
    sGhostId = (int16_t)ActorDB::Instance->AddEntry(ghost).entry.id;

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
    } else if (type == PLACEABLE_CHEST) {
        OpenCraftingWindow(2);
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
        CustomMessageManager::Instance->CreateMessage(table, line.textId,
                                                      CustomMessage(line.text, line.box, TEXTBOX_POS_BOTTOM));
    }
    CustomMessageManager::Instance->CreateMessage(
        table, TEXT_SIGN_DAY,
        CustomMessage("Day [[day]]^The boards hold. Build at the workbench. Survive the nights.", TEXTBOX_TYPE_WOODEN,
                      TEXTBOX_POS_BOTTOM));
    CustomMessageManager::Instance->CreateMessage(
        table, TEXT_WORKBENCH,
        CustomMessage("A sturdy workbench. Time to build something!", TEXTBOX_TYPE_BLACK, TEXTBOX_POS_BOTTOM));
    CustomMessageManager::Instance->CreateMessage(
        table, TEXT_CHEST,
        CustomMessage("The village storage chest. Everything the village has gathered is in here.", TEXTBOX_TYPE_BLACK,
                      TEXTBOX_POS_BOTTOM));
}

// MARK: - M9: the world reacts (towns talk about the nights)

// Idle lines elsewhere in Hyrule, replaced while the 7 Days base is on. Each has a
// line for before the first raid is survived and one for after; [[raids]] ("2 raids"),
// [[days]] ("5 days"), [[base]] and [[next]] are filled in when the line is shown.
// Flavor only: every replaced vanilla text was checked in the ROM's message table
// to have no choice, event, item, ocarina or clock code, and none carries a quest
// hint, a direction or the time.
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
    { SCENE_MARKET_DAY, 0x700E, "It seems like it's taking forever for dawn to come... Every night feels longer lately.", "Dawn takes forever on the red nights. I count every minute of them." },
    { SCENE_MARKET_ENTRANCE_DAY, 0x7002, "Welcome to Hyrule Castle Town. The drawbridge closes at dusk, and these days we mean it. The field isn't safe at night.", "Welcome to Hyrule Castle Town. The patrols report [[raids]] on the forest. We raise the bridge early now." },
    { SCENE_MARKET_ENTRANCE_DAY, 0x7003, "Kids shouldn't be out at night! Something's been testing the drawbridge. Stay indoors until morning!", "Red sky, red moon... Kids shouldn't be out on nights like these. Stay indoors until morning!" },
    { SCENE_HYRULE_CASTLE, 0x7002, "Welcome to Hyrule Castle. Rumor in the barracks says the night creatures are out in force.", "Welcome to Hyrule Castle. Rumor in the barracks: a forest base has held off [[raids]]." },
    // PHA-3935: the castle gate's guard (EnHeishi2) and Impa in the courtyard (DemoIm's repeat line)
    { SCENE_HYRULE_CASTLE, 0x7006, "There's a lot going on in the castle right now. I can't allow even a dog in. Not with things crawling out of the field at night!", "There's a lot going on in the castle right now. Not even a dog gets in, and certainly not the dead. We've counted [[raids]] on the forest." },
    { -1, 0x708E, "If the castle soldiers find you, there will be trouble. Let me lead you out of the castle.^The field is no place for a child after dark these days. Go home quickly.", "If the castle soldiers find you, there will be trouble. Let me lead you out of the castle.^A forest village has held off [[raids]], I hear. The Sheikah are watching it." },
    // PHA-3935: Talon (EnTa: awake at the ranch; asleep in Kakariko as an adult) and Anju (EnNiwLady)
    { -1, 0x2055, "I'm gonna turn over a new leaf and work real hard from now on.^Somebody's gotta mend the fences before the night things find 'em!", "I'm gonna turn over a new leaf and work real hard from now on.^Mended the fences twice since the raids started. Only napped through one of 'em!" },
    { -1, 0x5015, "Z Z Z... Malon...doing all right... Mumble...mumble... Bar the stable, Ingo...", "Z Z Z... Malon... Mumble... [[raids]]... the walls held... Sorry to make you worry..." },
    { -1, 0x503D, "Please don't tease my Cuccos! They're jumpy enough with all that scratching at night.", "Please don't tease my Cuccos! They haven't laid a single egg since the sky went red." },
    { -1, 0x5047, "My brother must have been very lonely... out there all alone, with the nights the way they are.", "My brother must have been very lonely... I hope he had walls around him on the red nights." },
    // Lon Lon Ranch and the castle: Malon (EnMa1, the Epona lines that carry no song or event)
    { -1, 0x2048, "Epona's jumpy lately, fairy boy. Dad says we're going to barricade the ranch. Something's been scaring the horses at night!", "We put boards over the stable doors. Epona still stamps all night when the sky goes red." },
    { -1, 0x204A, "Oh, Epona! She likes you, fairy boy. Mr. Ingo is nailing up the corral gates. He says the night things don't like fences.", "Oh, Epona! The fence held last raid night, and Mr. Ingo even smiled. A little." },
    // Zora's Domain (EnZo)
    { SCENE_ZORAS_DOMAIN, 0x400A, "We Zoras all serve the great King Zora. He says the surface's troubles are not ours. I'm not so sure.", "We Zoras all serve the great King Zora. Even he asked about the forest's walls. Word travels fast down the river." },
    { SCENE_ZORAS_DOMAIN, 0x4011, "All of the water in Hyrule flows from Zora's Fountain... and lately, strange things wash down it after dark.", "The river runs strange on the red nights. We stay deep in the Domain until dawn." },
    { SCENE_ZORAS_DOMAIN, 0x402D, "Oh, hi, @! Princess Ruto talks about you all the time. She says you're braver than the night things.", "Oh, hi, @! They say you've lasted [[days]] out there. Impressive, for a land-dweller." },
    { SCENE_ZORAS_DOMAIN, 0x402E, "Oh, it's you, @! Thanks to you, Lord Jabu-Jabu is back to normal. Now if only the nights were quiet...", "Oh, it's you, @! Jabu-Jabu is well, and you've held off [[raids]]. Is there anything you can't do?" },
    // Goron City and Death Mountain Trail (EnGo2)
    { SCENE_GORON_CITY, 0x3015, "Sigh... I want to eat Dodongo's Cavern rocks... and now things crawl up the mountain at night too!", "We rolled boulders across the trail at night. Nothing gets into Goron City, goro!" },
    { SCENE_GORON_CITY, 0x3027, "You are incredible, destroying the Dodongos! Now if only the nights were quiet, Big Brother...", "Big Brother, you saved our food and you keep beating back the nights! [[raids]], goro!" },
    { SCENE_DEATH_MOUNTAIN_TRAIL, 0x3026, "The trail's dangerous at night, brother. Things come up from Kakariko.", "We stacked rocks at the trail's bend. Even Stalchildren can't climb those!" },
    { SCENE_DEATH_MOUNTAIN_TRAIL, 0x3027, "The volcano smokes, and the field howls. What a time, goro!", "The night things never come this high. You should move your base up here, brother!" },
    // Lake Hylia: the lab scientist (EnMk) and the Zora at the lake (EnZo)
    { SCENE_LAKESIDE_LABORATORY, 0x4018, "Fascinating! The night creatures rise at the same hour every few days. A pattern! I must record it.", "My notes: [[raids]], [[days]]. The next red night should come [[when]]. Science!" },
    { SCENE_LAKE_HYLIA, 0x4021, "I am a Zora. Have you seen anything strange in the lake? Things wash up here after dark now.", "I am a Zora. On the red nights the lake glows strange. I stay under the water until dawn." },
    // Gerudo Valley and Fortress (EnGe1: the gate guard to a kid, the valley floor, the fortress greeting)
    { SCENE_GERUDO_VALLEY, 0x6069, "The Gerudo's Fortress is beyond this gate. A kid like you has no business there, night creatures or not.", "The Gerudo's Fortress is beyond this gate. We cut the bridge on red nights and fix it at dawn. A kid like you has no business there." },
    { SCENE_GERUDO_VALLEY, 0x601A, "Well, now that you're down here, you may as well make the best of things! At least the night things don't climb these cliffs.", "Well, now that you're down here, you may as well make the best of things! Hiding from the red nights too? Smart." },
    { SCENE_GERUDOS_FORTRESS, 0x6001, "Hey, newcomer! Hylian creatures at night? Ha! Let them try our walls.", "Hey, newcomer! Even we post double guards on red nights now. Don't tell anyone." },
    // PHA-3935: the Training Ground's gate guard (EnGe1), unqualified / qualified
    { SCENE_GERUDOS_FORTRESS, 0x6070, "This is the Gerudo's Training Ground. Unqualified persons are not allowed. Not even if the dead come knocking.", "This is the Gerudo's Training Ground. Unqualified persons are not allowed. The red nights changed nothing!" },
    { SCENE_GERUDOS_FORTRESS, 0x6072, "This is the Gerudo's Training Ground. Even though you're qualified, don't hog all the treasure here for yourself! Some of it buys walls.", "This is the Gerudo's Training Ground. Even though you're qualified, don't hog all the treasure! We'll need it if the dead ever cross the desert." },
    // Gossip stones (EnGs, the plain talk without the Mask of Truth)
    { -1, 0x2053, "This statue's one-eyed gaze pierces into your mind...^They say the night things come back every few days... and they always come for the base.", "This statue's one-eyed gaze pierces into your mind...^They say the next raid comes [[when]]. They say [[base]]." },
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
    return true;
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
}

// MARK: - PHA-3935: Navi's C-Up tips

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
        CustomMessageManager::Instance->CreateMessage(table, TEXT_NAVI_TIPS + i,
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
EMSCRIPTEN_KEEPALIVE
int sevendays_test_last_text() {
    return sLastTextId;
}

EMSCRIPTEN_KEEPALIVE
int sevendays_test_trap_blasts() {
    return sTrapBlasts;
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
