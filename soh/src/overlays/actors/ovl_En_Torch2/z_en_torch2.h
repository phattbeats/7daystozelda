#ifndef Z_EN_TORCH2_H
#define Z_EN_TORCH2_H

#include <libultraship/libultra.h>
#include "global.h"

// Uses the Player struct (from z64player.h)

// Co-op mirroring (#4055): the file statics Draw and the defeat read.
s32 EnTorch2_MirrorGetState(void);
s32 EnTorch2_MirrorGetAlpha(void);
s32 EnTorch2_MirrorGetCounter(void);
f32 EnTorch2_MirrorGetSwordJump(void);
void EnTorch2_MirrorApply(Player* en, s32 state, s32 alpha, s32 counter, f32 swordJump);

#endif
