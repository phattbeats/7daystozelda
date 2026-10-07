#ifndef Z_BOSS_TW_H
#define Z_BOSS_TW_H

#include <libultraship/libultra.h>
#include "global.h"

typedef enum {
    /* 0x00 */ TW_KOTAKE,
    /* 0x01 */ TW_KOUME,
    /* 0x02 */ TW_TWINROVA,
    /* 0x64 */ TW_FIRE_BLAST = 0x64,
    /* 0x65 */ TW_FIRE_BLAST_GROUND,
    /* 0x66 */ TW_ICE_BLAST,
    /* 0x67 */ TW_ICE_BLAST_GROUND,
    /* 0x68 */ TW_DEATHBALL_KOTAKE,
    /* 0x69 */ TW_DEATHBALL_KOUME
} TwinrovaType;

typedef enum {
    /*  0 */ TWEFF_NONE,
    /*  1 */ TWEFF_DOT,
    /*  2 */ TWEFF_2,
    /*  3 */ TWEFF_3,
    /*  4 */ TWEFF_RING,
    /*  5 */ TWEFF_PLYR_FRZ,
    /*  6 */ TWEFF_FLAME,
    /*  7 */ TWEFF_MERGEFLAME,
    /*  8 */ TWEFF_SHLD_BLST,
    /*  9 */ TWEFF_SHLD_DEFL,
    /* 10 */ TWEFF_SHLD_HIT
} TwEffType;

typedef enum {
    /* 0 */ EFF_ARGS,
    /* 1 */ EFF_UNKS1,
    /* 2 */ EFF_WORK_MAX
} EffectWork;

typedef enum {
    /* 0 */ EFF_SCALE,
    /* 1 */ EFF_DIST,
    /* 2 */ EFF_ROLL,
    /* 3 */ EFF_YAW,
    /* 4 */ EFF_FWORK_MAX
} EffectFWork;

typedef struct {
    /* 0x0000 */ u8 type;
    /* 0x0001 */ u8 frame;
    /* 0x0004 */ Vec3f pos;
    /* 0x0010 */ Vec3f curSpeed;
    /* 0x001C */ Vec3f accel;
    /* 0x0028 */ Color_RGB8 color;
    /* 0x002C */ s16 alpha;
    /* 0x002E */ s16 work[EFF_WORK_MAX];
    /* 0x0034 */ f32 workf[EFF_FWORK_MAX];
    /* 0x0044 */ Actor* target;
                 u32 epoch;
} BossTwEffect;

typedef enum {
    /*  0 */ CS_TIMER_1,
    /*  1 */ CS_TIMER_2,
    /*  2 */ TW_PLLR_IDX,
    /*  3 */ TAIL_IDX,
    /*  4 */ BLINK_IDX,
    /*  5 */ INVINC_TIMER,
    /*  6 */ FOG_TIMER,
    /*  7 */ CAN_SHOOT,
    /*  8 */ UNK_S8,
    /*  9 */ TW_BLINK_IDX,
    /* 10 */ YAW_TGT,
    /* 11 */ PLAYED_CHRG_SFX,
    /* 12 */ BURN_TMR,
    /* 13 */ WORK_MAX
} TwWork;

typedef enum {
    /*  0 */ OUTR_CRWN_TX_X1,
    /*  1 */ OUTR_CRWN_TX_X2,
    /*  2 */ INNR_CRWN_TX_X1,
    /*  3 */ INNR_CRWN_TX_X2,
    /*  4 */ OUTR_CRWN_TX_Y1,
    /*  5 */ OUTR_CRWN_TX_Y2,
    /*  6 */ INNR_CRWN_TX_Y1,
    /*  7 */ INNR_CRWN_TX_Y2,
    /*  8 */ ANIM_SW_TGT,
    /*  9 */ UNK_F9,
    /*  9 */ KM_GD_FLM_A = 9,
    /* 10 */ UNK_F10 = 10,
    /* 10 */ TAIL_ALPHA = 10,
    /* 10 */ KM_GD_SMOKE_A = 10,
    /* 14 */ UNK_F11 = 11,
    /* 15 */ KM_GRND_CRTR_A = 11,
    /* 16 */ UNK_F12 = 12,
    /* 17 */ KM_GD_FLM_SCL = 12,
    /* 18 */ UNK_F13 = 13,
    /* 19 */ KM_GD_CRTR_SCL = 13,
    /* 20 */ UNK_F14,
    /* 21 */ UNK_F15,
    /* 22 */ UNK_F16,
    /* 23 */ UNK_F17,
    /* 24 */ UNK_F18,
    /* 25 */ UNK_F19,
    /* 26 */ FWORK_MAX
} TwFwork;

struct BossTw;

typedef void (*BossTwActionFunc)(struct BossTw*, PlayState* play);

typedef struct BossTw {
    /* 0x0000 */ Actor actor;
    /* 0x014C */ BossTwActionFunc actionFunc;
    /* 0x0150 */ s16 work[WORK_MAX];
    /* 0x0168 */ char unused_170[0xE]; // Likely unused Work variables
    /* 0x0178 */ s16 timers[5];
    /* 0x0184 */ f32 workf[FWORK_MAX];
    /* 0x01D4 */ f32 fogR;
    /* 0x01D8 */ f32 fogG;
    /* 0x01DC */ f32 fogB;
    /* 0x01E0 */ f32 fogNear;
    /* 0x01E4 */ f32 fogFar;
    /* 0x01E8 */ Vec3f blastTailPos[50];
    /* 0x0440 */ s16 csState1;
    /* 0x0444 */ Vec3f crownPos;
    /* 0x0450 */ Vec3f scepterFlamePos[5];
    /* 0x048C */ Vec3f beamOrigin;
    /* 0x0498 */ Vec3f leftScepterPos;
    /* 0x04A4 */ Vec3f rightScepterPos;
    /* 0x04B0 */ Vec3f targetPos;
    /* 0x04BC */ Vec3f groundBlastPos2;
    /* 0x04C8 */ f32 rotateSpeed;
    /* 0x04CC */ s16 eyeTexIdx;
    /* 0x04CE */ s16 leftEyeTexIdx;
    /* 0x04D0 */ f32 scepterAlpha;
    /* 0x04D4 */ f32 flameAlpha;
    /* 0x04D8 */ f32 spawnPortalAlpha;
    /* 0x04DC */ f32 unk_4DC;
    /* 0x04E0 */ f32 spawnPortalScale;
    /* 0x04E4 */ f32 updateRate1;
    /* 0x04E8 */ f32 flameRotation;
    /* 0x04EC */ f32 portalRotation;
    /* 0x04F0 */ f32 updateRate2;
    /* 0x04F4 */ u8 twinrovaStun;
    /* 0x04F8 */ f32 beamScale;
    /* 0x04FC */ s16 beamShootState;
    /* 0x0500 */ Vec3f groundBlastPos;
    /* 0x050C */ Vec3f beamReflectionOrigin;
    /* 0x0518 */ f32 beamPitch;
    /* 0x051C */ f32 beamYaw;
    /* 0x0520 */ f32 beamRoll;
    /* 0x0524 */ Vec3s magicDir;
    /* 0x052C */ f32 beamDist;
    /* 0x0530 */ Vec3f unk_530;
    /* 0x053C */ f32 beamReflectionPitch;
    /* 0x0540 */ f32 beamReflectionYaw;
    /* 0x0544 */ f32 unused_544;
    /* 0x0548 */ f32 beamReflectionDist;
    /* 0x054C */ Vec3f unk_54C;
    /* 0x0558 */ Vec3f unk_558;
    /* 0x0564 */ u8 visible;
    /* 0x0565 */ u8 blastActive;
    /* 0x0566 */ s16 blastType;
    /* 0x0568 */ SkelAnime skelAnime;
    /* 0x05AC */ ColliderCylinder collider;
    /* 0x05F8 */ u8 unk_5F8;
    /* 0x05F9 */ u8 unk_5F9;
    /* 0x05FA */ s16 csState2;
    /* 0x05FC */ s16 subCamId;
    /* 0x05FE */ s16 csSfxTimer;
    /* 0x0600 */ Vec3f subCamEye;
    /* 0x060C */ Vec3f subCamAt;
    /* 0x0618 */ char unused_618[0xC];
    /* 0x0624 */ Vec3f subCamEye2;
    /* 0x0630 */ Vec3f subCamAt2;
    /* 0x063C */ char unused_63C[0x18];
    /* 0x0654 */ Vec3f subCamEyeStep;
    /* 0x0660 */ Vec3f subCamAtStep;
    /* 0x066C */ Vec3f subCamEyeTarget;
    /* 0x0678 */ char unused_678[0xC];
    /* 0x0684 */ Vec3f subCamAtTarget;
    /* 0x0690 */ char unused_690[0xC];
    /* 0x069C */ f32 subCamUpdateRate;
    /* 0x06A0 */ f32 subCamDistStep;
    /* 0x06A4 */ f32 subCamDist;
    /* 0x06A8 */ char unused_6A8[4];
    /* 0x06AC */ f32 subCamYaw;
    /* 0x06B0 */ f32 subCamYawStep;
    // Anchor co-op: who holds the mirror shield for this beam or blast (see BossTw_Anchor*).
    u32 anchorReflector; // client id of the remote player reflecting (0 = the local Link, or nobody)
    u8 anchorLocalReflect; // mirror side: this machine's Link is the reflector
    u8 anchorReflectHeld;  // authority side: the remote reflector still holds R
    s16 anchorReflectAge;  // authority side: frames since the last reflector report
    Vec3f anchorReflectBody;
    Vec3s anchorReflectDir;
    u8 anchorBlastLive; // mirror side: blast copy has not been consumed locally
} BossTw; // size = 0x06B4 plus the Anchor fields

// Fight stage, shared by all three Boss_Tw actors (one per client; the authority's is streamed).
typedef enum {
    /* 0 */ TW_STAGE_INTRO,
    /* 1 */ TW_STAGE_WITCHES,
    /* 2 */ TW_STAGE_MERGE,
    /* 3 */ TW_STAGE_TWINROVA,
    /* 4 */ TW_STAGE_DEFEATED
} TwStage;

typedef enum {
    /* 0 */ TW_ACT_OTHER,
    /* 1 */ TW_ACT_CSWAIT,
    /* 2 */ TW_ACT_FLYTO,
    /* 3 */ TW_ACT_TURN,
    /* 4 */ TW_ACT_SHOOTBEAM,
    /* 5 */ TW_ACT_FINISHBEAM,
    /* 6 */ TW_ACT_HITBYBEAM,
    /* 7 */ TW_ACT_LAUGH,
    /* 8 */ TW_ACT_SPIN,
    /* 9 */ TW_ACT_MERGECS,
    /* 10 */ TW_ACT_DEATHCS,
    /* 11 */ TW_ACT_WAIT,
    /* 12 */ TW_ACT_T_ARRIVE,
    /* 13 */ TW_ACT_T_CHARGE,
    /* 14 */ TW_ACT_T_SHOOT,
    /* 15 */ TW_ACT_T_DONESHOOT,
    /* 16 */ TW_ACT_T_STUN,
    /* 17 */ TW_ACT_T_GETUP,
    /* 18 */ TW_ACT_T_FLY,
    /* 19 */ TW_ACT_T_SPIN,
    /* 20 */ TW_ACT_T_LAUGH,
    /* 21 */ TW_ACT_T_MERGECS,
    /* 22 */ TW_ACT_T_DEATHCS,
    /* 23 */ TW_ACT_T_INTROCS
} TwAction;

// Anchor co-op accessors (BossAdapters/TwinrovaAdapter.cpp).
BossTw* BossTw_AnchorGlobal(s32 which); // 0 Kotake, 1 Koume, 2 Twinrova
s32 BossTw_AnchorStage(void);
u8 BossTw_AnchorActionCode(BossTw* tw);
void BossTw_AnchorSetAction(BossTw* tw, u8 code);
void BossTw_AnchorStartDefeat(BossTw* twinrova, PlayState* play);
void BossTw_AnchorStartMerge(PlayState* play);
void BossTw_AnchorResume(BossTw* tw, PlayState* play);
void BossTw_AnchorMirrorTick(BossTw* tw, PlayState* play);
// Mirror side: this machine's Link against the streamed beam. Returns 0 none, 1 reflecting (the
// beam is clipped to the shield), 2 hit (freeze/burn applied locally), 3 diverted by a plain shield.
s32 BossTw_AnchorBeamVictim(BossTw* tw, PlayState* play, f32* outDist);
// Mirror side: this machine's Link against a streamed blast's shield hit. Returns 0 nothing,
// 1 absorbed (blast consumed), 2 charged and released (blast becomes a reflect).
s32 BossTw_AnchorBlastShield(BossTw* tw, PlayState* play);
// Authority side: a remote reflector's per-frame state.
void BossTw_AnchorReflectReport(BossTw* tw, s32 held, const Vec3f* body, s16 dirX, s16 dirY, u32 clientId);
void BossTw_AnchorBeamReflected(BossTw* tw, f32 dist, const Vec3f* body, s16 dirX, s16 dirY, u32 clientId);
void BossTw_AnchorBlastAbsorbed(BossTw* tw, s32 released, const Vec3f* body, s16 dirX, s16 dirY, u32 clientId);
void BossTw_AnchorReleaseDone(s32 blastType);
void BossTw_AnchorReflectSparks(BossTw* tw, PlayState* play);
void BossTw_AnchorTwinrovaStunned(void);
u8 BossTw_AnchorIsBlast(BossTw* tw);
s32 BossTw_AnchorEnvType(void);
s32 BossTw_AnchorGroundBlastType(void);
s32 BossTw_AnchorBlastType(void);
void BossTw_AnchorSetEnvType(s32 env, s32 groundBlastType);
void BossTw_AnchorSetBlastType(s32 blastType);
s32 BossTw_AnchorShieldCharge(void);
void BossTw_AnchorSetShieldCharge(s32 charge);
void BossTw_AnchorForceAttack(BossTw* tw, PlayState* play, s32 blastType);

#endif
