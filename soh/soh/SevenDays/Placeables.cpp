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
extern PlayState* gPlayState;
}

/**
 * M5 placeable actors, registered through ActorDB::AddEntry (no vanilla actor
 * table changes):
 *
 *   SevenDays_Placeable  one actor for every placeable type (params = stable id).
 *                        A DynaPolyActor with an in-code box CollisionHeader
 *                        (8 vertices, 12 triangles) registered with
 *                        DynaPoly_SetBgActor, so Link and enemies collide with it
 *                        like scenery. Drawn with display lists the ROM already
 *                        has (large/small crates, the rectangular sign, the spike).
 *   SevenDays_Ghost      placement mode's translucent ghost. ACTORCAT_SWITCH so it
 *                        updates before Link and can take the buttons it uses.
 */

using namespace SevenDays;

struct PlaceableActor {
    DynaPolyActor dyna;
    ColliderCylinder spikeCollider;
    uint16_t id;
    uint8_t type;
    uint8_t talking;
    int16_t atCooldown;
    int16_t hitFlash;
};

static int16_t sPlaceableId = -1;
static int16_t sGhostId = -1;

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
static void DrawModel(PlayState* play, uint8_t type, float hpFrac) {
    switch (type) {
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

    DynaPolyActor_Init(&self->dyna, 0);
    self->dyna.bgId = DynaPoly_SetBgActor(play, &play->colCtx.dyna, thisx, &sBoxes[self->type].header);

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
    Actor_SetFocus(thisx, (f32)GetPlaceableInfo(self->type).height);
    OnPlaceableSpawned(self->id, thisx);
}

static void Placeable_Destroy(Actor* thisx, PlayState* play) {
    PlaceableActor* self = (PlaceableActor*)thisx;
    if (self->type == 0xFF) {
        return;
    }
    DynaPoly_DeleteBgActor(play, &play->colCtx.dyna, self->dyna.bgId);
    if (self->type == PLACEABLE_SPIKES) {
        Collider_DestroyCylinder(play, &self->spikeCollider);
    }
    OnPlaceableDestroyed(self->id, thisx);
}

static void Placeable_Update(Actor* thisx, PlayState* play) {
    PlaceableActor* self = (PlaceableActor*)thisx;
    const PlaceableInfo& info = GetPlaceableInfo(self->type);

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

    DrawModel(play, self->type, hpFrac);

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
    placeable.desc = "7 Days to Zelda placeable (barricade, spike strip, workbench, storage chest, sign)";
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

