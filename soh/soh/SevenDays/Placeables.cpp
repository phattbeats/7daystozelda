#include "SevenDays.h"
#include "soh/ActorDB.h"
#include "soh/frame_interpolation.h"
#include "soh/Enhancements/custom-message/CustomMessageManager.h"
#include "soh/Network/Anchor/Anchor.h"
#include "soh/Network/Anchor/EnemySync.h"

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
extern PlayState* gPlayState;
}

/**
 * M5 placeable actors, registered through ActorDB::AddEntry (no vanilla actor
 * table changes):
 *
 *   SevenDays_Placeable  one actor for every placeable type (params = stable id).
 *                        Drawn with display lists the ROM already has (large/small
 *                        crates, the rectangular sign, the spike). It has no
 *                        collision of its own: see SevenDays_BaseCollision.
 *   SevenDays_BaseCollision  PHA-3916: the pieces' boxes (8 vertices, 12 triangles
 *                        each), merged into one CollisionHeader per 640-unit
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
};

enum { BABA_IDLE, BABA_WINDUP, BABA_LUNGE, BABA_RECOVER };
constexpr f32 BABA_SIZE = 1.3f;
constexpr f32 BABA_SENSE = 120.0f; // starts a bite on a raider this close to the stalk

static int16_t sPlaceableId = -1;
static int16_t sGhostId = -1;
static int16_t sCollisionId = -1;

int16_t SevenDays::PlaceableActorId() {
    return sPlaceableId;
}

int16_t SevenDays::GhostActorId() {
    return sGhostId;
}

// MARK: - Collision: one box header per type, built once

struct BoxCollision {
    Vec3s verts[8];
    CollisionPoly polys[12];
    SurfaceType surface[1];
    CollisionHeader header;
};
static BoxCollision sBoxes[PLACEABLE_COUNT];

static void BuildBox(BoxCollision& box, const PlaceableInfo& info) {
    s16 hx = info.halfX, hz = info.halfZ, h = info.height;
    Vec3s v[8] = { { (s16)-hx, 0, (s16)-hz }, { hx, 0, (s16)-hz }, { hx, 0, hz }, { (s16)-hx, 0, hz },
                   { (s16)-hx, h, (s16)-hz }, { hx, h, (s16)-hz }, { hx, h, hz }, { (s16)-hx, h, hz } };
    memcpy(box.verts, v, sizeof(v));
    // Each face as two triangles, with the outward normal it must have.
    struct Tri {
        u16 a, b, c;
        float nx, ny, nz;
    };
    static const Tri tris[12] = {
        { 0, 1, 2, 0, -1, 0 }, { 0, 2, 3, 0, -1, 0 }, // bottom
        { 4, 7, 6, 0, 1, 0 },  { 4, 6, 5, 0, 1, 0 },  // top
        { 0, 4, 5, 0, 0, -1 }, { 0, 5, 1, 0, 0, -1 }, // -z
        { 3, 2, 6, 0, 0, 1 },  { 3, 6, 7, 0, 0, 1 },  // +z
        { 0, 3, 7, -1, 0, 0 }, { 0, 7, 4, -1, 0, 0 }, // -x
        { 1, 5, 6, 1, 0, 0 },  { 1, 6, 2, 1, 0, 0 },  // +x
    };
    for (int i = 0; i < 12; i++) {
        Tri t = tris[i];
        // DynaPoly recomputes normals as (B-A)x(C-A): wind each triangle so that
        // points outward.
        Vec3f A = { (f32)v[t.a].x, (f32)v[t.a].y, (f32)v[t.a].z };
        Vec3f B = { (f32)v[t.b].x, (f32)v[t.b].y, (f32)v[t.b].z };
        Vec3f C = { (f32)v[t.c].x, (f32)v[t.c].y, (f32)v[t.c].z };
        Vec3f ab = { B.x - A.x, B.y - A.y, B.z - A.z }, ac = { C.x - A.x, C.y - A.y, C.z - A.z };
        Vec3f n = { ab.y * ac.z - ab.z * ac.y, ab.z * ac.x - ab.x * ac.z, ab.x * ac.y - ab.y * ac.x };
        if (n.x * t.nx + n.y * t.ny + n.z * t.nz < 0) {
            u16 tmp = t.b;
            t.b = t.c;
            t.c = tmp;
        }
        CollisionPoly& p = box.polys[i];
        memset(&p, 0, sizeof(p));
        p.type = 0;
        p.flags_vIA = t.a;
        p.flags_vIB = t.b;
        p.vIC = t.c;
        p.normal.x = (s16)(t.nx * 0x7FFF);
        p.normal.y = (s16)(t.ny * 0x7FFF);
        p.normal.z = (s16)(t.nz * 0x7FFF);
        Vec3s& a = v[t.a];
        p.dist = (s16)(-(t.nx * a.x + t.ny * a.y + t.nz * a.z));
    }
    box.surface[0].data[0] = 0x00000000;
    box.surface[0].data[1] = 0x000007C0; // wood-ish floor sound, normal walls
    CollisionHeader& hdr = box.header;
    memset(&hdr, 0, sizeof(hdr));
    hdr.minBounds = { (s16)-hx, 0, (s16)-hz };
    hdr.maxBounds = { hx, h, hz };
    hdr.numVertices = 8;
    hdr.vtxList = box.verts;
    hdr.numPolygons = 12;
    hdr.polyList = box.polys;
    hdr.surfaceTypeList = box.surface;
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
        const BoxCollision& box = sBoxes[((PlaceableActor*)a)->type];
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
    hdr = sBoxes[0].header;
    hdr.minBounds = { minX, minY, minZ };
    hdr.maxBounds = { maxX, maxY, maxZ };
    hdr.numVertices = (u16)ch.verts.size();
    hdr.vtxList = ch.verts.data();
    hdr.numPolygons = (u16)ch.polys.size();
    hdr.polyList = ch.polys.data();
    hdr.surfaceTypeList = sBoxes[0].surface;
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
        if (a->id != sPlaceableId || a->update == nullptr || ((PlaceableActor*)a)->type >= PLACEABLE_COUNT) {
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

static void DrawModel(PlayState* play, uint8_t type, float hpFrac, PlaceableActor* self = nullptr) {
    switch (type) {
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
            // Two large crates side by side. Below half HP one is knocked askew:
            // the village's "broken fence" is a barricade that already took a beating.
            DrawDL(play, (Gfx*)gLargeCrateDL, -30.0f, 0.0f, 0.0f, 0.1f, 0.1f, 0.1f);
            if (hpFrac >= 0.5f) {
                DrawDL(play, (Gfx*)gLargeCrateDL, 30.0f, 0.0f, 0.0f, 0.1f, 0.1f, 0.1f);
            } else {
                DrawDL(play, (Gfx*)gLargeCrateDL, 34.0f, -6.0f, 4.0f, 0.1f, 0.08f, 0.1f, -0.45f);
            }
            break;
        case PLACEABLE_SPIKES:
            for (int i = -1; i <= 1; i++) {
                DrawDL(play, (Gfx*)gUnusedSpikeDL, i * 30.0f, 4.0f, 0.0f, 0.004f, 0.004f, 0.004f);
            }
            break;
        case PLACEABLE_WORKBENCH:
            DrawDL(play, (Gfx*)gLargeCrateDL, 0.0f, 0.0f, 0.0f, 0.1f, 0.1f, 0.1f);
            DrawDL(play, (Gfx*)gSmallWoodenBoxDL, 14.0f, 48.0f, 6.0f, 0.06f, 0.06f, 0.06f);
            break;
        case PLACEABLE_CHEST:
            DrawDL(play, (Gfx*)gSmallWoodenBoxDL, 0.0f, 0.0f, 0.0f, 0.2f, 0.2f, 0.2f);
            break;
        case PLACEABLE_SIGN:
            DrawDL(play, (Gfx*)gSignRectangularDL, 0.0f, 0.0f, 0.0f, 0.01f, 0.01f, 0.01f);
            break;
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
        AT_ON | AT_TYPE_PLAYER, // hurts enemies, never players
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
        AT_ON | AT_TYPE_PLAYER, // bites raiders, never players
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
    self->talking = 0;
    self->atCooldown = 0;
    self->hitFlash = 0;
    self->shake = 0;
    thisx->room = -1; // scene-wide: survives walking between rooms
    Actor_SetScale(thisx, 1.0f);
    thisx->shape.rot = thisx->world.rot = { 0, p->rot, 0 };

    // Re-snap to the floor under it (seeded pieces carry an approximate y).
    Vec3f probe = { p->pos[0], p->pos[1] + 80.0f, p->pos[2] };
    CollisionPoly* poly = nullptr;
    s32 bgId = BGCHECK_SCENE;
    f32 floorY = BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, &probe);
    if (floorY > BGCHECK_Y_MIN && bgId == BGCHECK_SCENE && fabsf(floorY - p->pos[1]) < 200.0f) {
        thisx->world.pos.y = thisx->home.pos.y = floorY;
    }

    sCollisionDirty = true;

    uint16_t textId = TextFor(self->type);
    if (textId != 0) {
        thisx->textId = textId;
        thisx->flags |= ACTOR_FLAG_ATTENTION_ENABLED | ACTOR_FLAG_FRIENDLY;
        thisx->targetMode = 0;
    }
    if (self->type == PLACEABLE_SPIKES) {
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
    } else if (self->type == PLACEABLE_GUARDBABA) {
        SkelAnime_Init(play, &self->skel, (SkeletonHeader*)gDekuBabaSkel, (AnimationHeader*)gDekuBabaPauseChompAnim,
                       nullptr, nullptr, 0);
        self->skel.playSpeed = 0.0f;
        self->hasSkel = true;
        Collider_InitCylinder(play, &self->biteCollider);
        Collider_SetCylinder(play, &self->biteCollider, thisx, &sBiteCylinderInit);
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
    if (self->type == PLACEABLE_SPIKES) {
        Collider_DestroyCylinder(play, &self->spikeCollider);
    }
    if (self->type == PLACEABLE_GUARDBABA) {
        Collider_DestroyCylinder(play, &self->biteCollider);
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
                    CollisionCheck_SetAT(play, &play->colChkCtx, &self->biteCollider.base);
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

    if (self->type == PLACEABLE_SCARECROW && self->hasSkel) {
        SkelAnime_Update(&self->skel); // playSpeed 0: holds the first frame
    } else if (self->type == PLACEABLE_GUARDBABA) {
        BabaUpdate(self, play);
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
                CollisionCheck_SetAT(play, &play->colChkCtx, &self->spikeCollider.base);
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

    bool tint = info.maxHp > 0 && hpFrac < 0.5f;
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    if (tint) {
        // Damaged wood darkens.
        gDPSetGrayscaleColor(POLY_OPA_DISP++, 120, 80, 50, 140);
        gSPGrayscale(POLY_OPA_DISP++, true);
    }
    CLOSE_DISPS(play->state.gfxCtx);

    if (self->shake > 0) {
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
    for (int t = 0; t < PLACEABLE_COUNT; t++) {
        BuildBox(sBoxes[t], GetPlaceableInfo((uint8_t)t));
    }
    ActorDBInit placeable;
    placeable.name = "SevenDays_Placeable";
    placeable.desc = "7 Days to Zelda placeable (barricade, spike strip, workbench, storage chest, sign, scarecrow, Guard Baba)";
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
    // Kakariko: the carpenters (EnDaikuKakariko) and the field-gate guard's night line and Impa's house guard (EnHeishi4)
    { SCENE_KAKARIKO_VILLAGE, 0x5075, "Boss says we're building houses, but half my lumber went to boarding windows.^Folks say something comes out of the field at night.", "[[raids]] now, the field's come knocking. We nail the shutters at dusk and pray they hold." },
    { SCENE_KAKARIKO_VILLAGE, 0x5076, "Hear that? Scratching, out past the gate, every night. Lady Impa says keep the lamps lit.", "Impa's got the watchtower manned every night now. If you see red in the sky, kid, get indoors." },
    { SCENE_KAKARIKO_VILLAGE, 0x5074, "Lady Impa had us shore up the watchtower first. She says she can see the whole field from up there.", "The lookout on the watchtower counted torches out in the field last night. Not ours." },
    { SCENE_KAKARIKO_VILLAGE, 0x506B, "Anju's been bringing her Cuccos in every evening. Says they won't stop squawking at the dark.", "Anju counts her Cuccos every dawn now. Lost two to the last bad night." },
    { SCENE_KAKARIKO_VILLAGE, 0x506A, "We're out of nails. Every house in the village wants its doors barred.", "Barred every door twice. Still hear them on the roofs some nights." },
    { SCENE_KAKARIKO_VILLAGE, 0x5066, "Hey, son, what are you doing out this late? Things have been crawling out of the field at night.^Go inside before they find you.", "Out after dark? After [[raids]] on the field? Son, get indoors.^The watchtower spots them coming. Next red night's in [[next]]." },
    { SCENE_KAKARIKO_VILLAGE, 0x5079, "Lady Impa climbs the watchtower every night now. She says she's counting something out in the field.", "Lady Impa has the watchtower lit every night since the raids began. She says the forest is holding." },
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
    { SCENE_LAKESIDE_LABORATORY, 0x4018, "Fascinating! The night creatures rise at the same hour every few days. A pattern! I must record it.", "My notes: [[raids]], [[days]]. The next red night should come in [[next]]. Science!" },
    { SCENE_LAKE_HYLIA, 0x4021, "I am a Zora. Have you seen anything strange in the lake? Things wash up here after dark now.", "I am a Zora. On the red nights the lake glows strange. I stay under the water until dawn." },
    // Gerudo Valley and Fortress (EnGe1: the gate guard to a kid, the valley floor, the fortress greeting)
    { SCENE_GERUDO_VALLEY, 0x6069, "The Gerudo's Fortress is beyond this gate. A kid like you has no business there, night creatures or not.", "The Gerudo's Fortress is beyond this gate. We cut the bridge on red nights and fix it at dawn. A kid like you has no business there." },
    { SCENE_GERUDO_VALLEY, 0x6019, "Why did you come all the way down here? The night things don't climb these cliffs, at least.", "Why did you come all the way down here? Hiding from the red nights too? Smart." },
    { SCENE_GERUDOS_FORTRESS, 0x6001, "Hey, newcomer! Hylian creatures at night? Ha! Let them try our walls.", "Hey, newcomer! Even we post double guards on red nights now. Don't tell anyone." },
    // Gossip stones (EnGs, the plain talk without the Mask of Truth)
    { -1, 0x2053, "This statue's one-eyed gaze pierces into your mind...^They say the night things come back every few days... and they always come for the base.", "This statue's one-eyed gaze pierces into your mind...^They say the next raid comes in [[next]]. They say [[base]]." },
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
    std::string s = text;
    const BaseCenter& c = b.center[CurrentEraNow()];
    const char* home = c.valid ? OutdoorSceneName(c.scene) : nullptr;
    auto replace = [&s](const std::string& key, const std::string& value) {
        for (size_t at; (at = s.find(key)) != std::string::npos;) {
            s.replace(at, key.size(), value);
        }
    };
    auto count = [](uint32_t n, const char* one, const char* many) { return fmt::format("{} {}", n, n == 1 ? one : many); };
    replace("[[raids]]", count(b.hordeNightsSurvived, "raid", "raids"));
    replace("[[days]]", count(b.daysSurvived, "day", "days"));
    replace("[[next]]", NextRaidText());
    replace("[[base]]", home != nullptr ? fmt::format("someone built walls in {}", home) : "nobody has built walls yet");
    out = CustomMessage(s, box, TEXTBOX_POS_BOTTOM);
    out.AutoFormat();
    return true;
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
}
#endif
