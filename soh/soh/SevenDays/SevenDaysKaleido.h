#ifndef SEVEN_DAYS_KALEIDO_H
#define SEVEN_DAYS_KALEIDO_H

/**
 * 7 Days to Zelda: the Workbench as a fifth pause-menu page (#3871).
 *
 * The pause menu is a box with four faces; the camera sits inside and turns
 * from face to face. With the Workbench page on, five pages share those four
 * faces: the face behind the camera is never seen, so before every turn it is
 * given the page that comes after the next one (KaleidoFace_PrepareTurn).
 * pauseCtx->pageIndex stays the page (what the page code reads); the camera,
 * the fold angles and the open/close spin work in faces. With the page off the
 * mapping is the identity and the pause menu is vanilla.
 *
 * Faces are numbered like the vanilla pages they start with: 0 Item (-z),
 * 1 Map (+x), 2 Quest (+z), 3 Equip (-x). Turning right goes face f -> f + 1.
 */

#include "z64.h"

#define PAUSE_SEVENDAYS 5

#ifdef __cplusplus
extern "C" {
#endif

// z_kaleido_setup.c
extern u8 gKaleidoFaceContent[4];
u8 KaleidoFace_Of(u16 page);
u16 KaleidoFace_NextPage(u16 page, s32 dir); // dir: 1 right (R), -1 left (Z)
void KaleidoFace_Arrange(u16 page);          // on open: page on a face, its neighbours beside it
void KaleidoFace_PrepareTurn(u16 page, s32 dir);
void KaleidoSetup_RequestOpen(u16 page); // open the pause menu on a page as soon as play allows

// CraftingWindow.cpp
s32 SevenDaysKaleido_PageOn(void);
void SevenDaysKaleido_InitPageVtx(PlayState* play, Vtx* vtx);
void SevenDaysKaleido_DrawPage(PlayState* play, s32 current); // after the frame; input when current
void SevenDaysKaleido_DrawInfo(PlayState* play, s16 top);     // the bottom panel's line
void SevenDaysKaleido_DrawPageLabel(PlayState* play, s16 top); // "To Workbench" on the L/R arrows

// z_kaleido_scope_PAL.c: touch on the pause pages (#4125). Coordinates are normalized canvas
// positions (0..1, y down) plus the canvas aspect; see KaleidoScope_TouchTap for the result codes.
s32 KaleidoScope_TouchOpen(void);
s32 KaleidoScope_TouchTap(f32 nx, f32 ny, f32 aspect);
void KaleidoScope_TouchSwipe(s32 dir);
u32 KaleidoScope_TouchFrame(void);
const char* KaleidoScope_TouchProbe(f32 aspect);

// CraftingWindow.cpp: the Workbench page's tap targets, in page space (#4125).
s32 SevenDaysKaleido_TouchHit(PlayState* play, f32 px, f32 py);
s32 SevenDaysKaleido_TouchRect(PlayState* play, s32 index, f32* x0, f32* y0, f32* x1, f32* y1);
s32 SevenDaysKaleido_TouchCursor(void);

// CraftingWindow.cpp: kits bound to the D-pad (#4071). dir 0 up, 1 down, 2 left, 3 right.
s32 SevenDaysDpad_Bound(s32 dir);
s32 SevenDaysDpad_Any(void);
s32 SevenDaysDpad_Count(s32 dir); // kits in the pool (capped at 99), -1 when nothing is bound
void* SevenDaysDpad_Icon(s32 dir); // 32x32 item icon texture, or NULL

#ifdef __cplusplus
}
#endif

#endif // SEVEN_DAYS_KALEIDO_H
