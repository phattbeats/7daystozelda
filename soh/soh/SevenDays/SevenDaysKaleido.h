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

#ifdef __cplusplus
}
#endif

#endif // SEVEN_DAYS_KALEIDO_H
