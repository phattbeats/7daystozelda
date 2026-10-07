#ifndef Z_EN_FLOORMAS_H
#define Z_EN_FLOORMAS_H

#include <libultraship/libultra.h>
#include "global.h"

typedef struct EnFloormas EnFloormas;

typedef void (*EnFloormasActionFunc)(EnFloormas* en, PlayState* play);

struct EnFloormas{
    /* 0x0000 */ Actor actor;
    /* 0x014C */ SkelAnime skelAnime;
    /* 0x0190 */ EnFloormasActionFunc actionFunc;
    /* 0x0194 */ s16 actionTimer;
    /* 0x0196 */ s16 actionTarget;
    /* 0x0198 */ s16 zOffset;
    /* 0x019A */ s16 smActionTimer;
    /* 0x019C */ Vec3s jointTable[25];
    /* 0x0232 */ Vec3s morphTable[25];
    /* 0x02C8 */ ColliderCylinder collider;
}; // size = 0x0314

// Co-op enemy mirroring (soh/Network/Anchor/BossAdapters/EnFloormasAdapter.cpp).
// The suppressed mirror never runs Update, so the split/merge state machine is
// carried over the stream as small indices instead of function/animation pointers.
s32 EnFloormas_MirrorGetAction(EnFloormas* en);
s32 EnFloormas_MirrorGetAnim(EnFloormas* en);
s32 EnFloormas_MirrorGetDraw(EnFloormas* en);
s32 EnFloormas_MirrorIsShrinking(EnFloormas* en);
s32 EnFloormas_MirrorIsJumpingAtLink(EnFloormas* en);
void EnFloormas_MirrorApply(EnFloormas* en, s32 action, s32 anim, s32 mode, f32 curFrame, f32 playSpeed,
                            f32 endFrame, s32 draw);

#endif
