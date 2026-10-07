#include "Anchor.h"
#include "EnemySync.h"
#include "PuppetFairy.h"
#include "CoopWarp.h"
#include "CoopLifeSync.h"
#include "BossEntry.h"
#include "CutsceneSync.h"
#include "BgmSync.h"
#include "PushBlockSync.h"
#include "AmbientSync.h"
#include "WorldObjectSync.h"
#include <libultraship/libultraship.h>
#include "soh/Enhancements/game-interactor/GameInteractor.h"

extern "C" {
#include "variables.h"
#include "functions.h"
#include "src/overlays/actors/ovl_Bg_Bombwall/z_bg_bombwall.h"
#include "src/overlays/actors/ovl_Bg_Breakwall/z_bg_breakwall.h"
#include "src/overlays/actors/ovl_Bg_Haka_Zou/z_bg_haka_zou.h"
#include "src/overlays/actors/ovl_Bg_Hidan_Hamstep/z_bg_hidan_hamstep.h"
#include "src/overlays/actors/ovl_Bg_Hidan_Hrock/z_bg_hidan_hrock.h"
#include "src/overlays/actors/ovl_Bg_Ice_Shelter/z_bg_ice_shelter.h"
#include "src/overlays/actors/ovl_Bg_Jya_Bombchuiwa/z_bg_jya_bombchuiwa.h"
#include "src/overlays/actors/ovl_Bg_Jya_Bombiwa/z_bg_jya_bombiwa.h"
#include "src/overlays/actors/ovl_Bg_Mizu_Bwall/z_bg_mizu_bwall.h"
#include "src/overlays/actors/ovl_Bg_Spot08_Bakudankabe/z_bg_spot08_bakudankabe.h"
#include "src/overlays/actors/ovl_Bg_Spot11_Bakudankabe/z_bg_spot11_bakudankabe.h"
#include "src/overlays/actors/ovl_Bg_Spot17_Bakudankabe/z_bg_spot17_bakudankabe.h"
#include "src/overlays/actors/ovl_Bg_Ydan_Maruta/z_bg_ydan_maruta.h"
#include "src/overlays/actors/ovl_Bg_Ydan_Sp/z_bg_ydan_sp.h"
#include "src/overlays/actors/ovl_Door_Shutter/z_door_shutter.h"
#include "src/overlays/actors/ovl_En_Door/z_en_door.h"
#include "src/overlays/actors/ovl_En_Si/z_en_si.h"
#include "src/overlays/actors/ovl_En_Sw/z_en_sw.h"
#include "src/overlays/actors/ovl_Item_B_Heart/z_item_b_heart.h"
#include "src/overlays/actors/ovl_Obj_Bombiwa/z_obj_bombiwa.h"
#include "src/overlays/actors/ovl_Obj_Hamishi/z_obj_hamishi.h"
#include "src/overlays/actors/ovl_Obj_Switch/z_obj_switch.h"
#include "src/overlays/actors/ovl_Bg_Ddan_Kd/z_bg_ddan_kd.h"
#include "src/overlays/actors/ovl_Bg_Dodoago/z_bg_dodoago.h"
#include "src/overlays/actors/ovl_Bg_Gnd_Soulmeiro/z_bg_gnd_soulmeiro.h"
#include "src/overlays/actors/ovl_Bg_Heavy_Block/z_bg_heavy_block.h"
#include "src/overlays/actors/ovl_Bg_Jya_Megami/z_bg_jya_megami.h"
#include "src/overlays/actors/ovl_Bg_Spot16_Bombstone/z_bg_spot16_bombstone.h"
#include "src/overlays/actors/ovl_En_Ishi/z_en_ishi.h"
#include "src/overlays/actors/ovl_Obj_Syokudai/z_obj_syokudai.h"
#include "src/overlays/actors/ovl_Bg_Hidan_Dalm/z_bg_hidan_dalm.h"
#include "src/overlays/actors/ovl_Bg_Hidan_Kowarerukabe/z_bg_hidan_kowarerukabe.h"

extern PlayState* gPlayState;

void func_8086ED70(BgBombwall* bgBombwall, PlayState* play);
void BgBreakwall_Wait(BgBreakwall* bgBreakwall, PlayState* play);
void func_80883000(BgHakaZou* bgHakaZou, PlayState* play);
void func_808887C4(BgHidanHamstep* bgHidanHamstep, PlayState* play);
void func_808896B8(BgHidanHrock* bgHidanHrock, PlayState* play);
void func_8089107C(BgIceShelter* bgIceShelter, PlayState* play);
void func_808911BC(BgIceShelter* bgIceShelter);
void ObjBombiwa_Break(ObjBombiwa* objBombiwa, PlayState* play);
void ObjHamishi_Break(ObjHamishi* objHamishi, PlayState* play);
void BgJyaBombchuiwa_WaitForExplosion(BgJyaBombchuiwa* bgJyaBombchuiwa, PlayState* play);
void BgMizuBwall_Idle(BgMizuBwall* bgMizuBwall, PlayState* play);
void func_808B6BC0(BgSpot17Bakudankabe* bgSpot17Bakudankabe, PlayState* play);
void func_808BF078(BgYdanMaruta* bgYdanMaruta, PlayState* play);
void BgYdanSp_FloorWebIdle(BgYdanSp* bgYdanSp, PlayState* play);
void BgYdanSp_WallWebIdle(BgYdanSp* bgYdanSp, PlayState* play);
void BgYdanSp_BurnWeb(BgYdanSp* bgYdanSp, PlayState* play);
void EnDoor_Idle(EnDoor* enDoor, PlayState* play);
void ObjSwitch_FloorUp(ObjSwitch* objSwitch, PlayState* play);
void ObjSwitch_FloorPressInit(ObjSwitch* objSwitch);
void ObjSwitch_FloorDown(ObjSwitch* objSwitch, PlayState* play);
void ObjSwitch_FloorReleaseInit(ObjSwitch* objSwitch);
void ObjSwitch_EyeOpen(ObjSwitch* objSwitch, PlayState* play);
void ObjSwitch_EyeClosingInit(ObjSwitch* objSwitch);
void ObjSwitch_EyeClosed(ObjSwitch* objSwitch, PlayState* play);
void ObjSwitch_EyeOpeningInit(ObjSwitch* objSwitch);
void ObjSwitch_CrystalOff(ObjSwitch* objSwitch, PlayState* play);
void ObjSwitch_CrystalTurnOnInit(ObjSwitch* objSwitch);
void ObjSwitch_CrystalOn(ObjSwitch* objSwitch, PlayState* play);
void ObjSwitch_CrystalTurnOffInit(ObjSwitch* objSwitch);
void func_80AFB950(EnSi* enSi, PlayState* play);
void BgDdanKd_CheckForExplosions(BgDdanKd* bgDdanKd, PlayState* play);
void BgDdanKd_LowerStairs(BgDdanKd* bgDdanKd, PlayState* play);
void BgDodoago_WaitExplosives(BgDodoago* bgDodoago, PlayState* play);
void BgDodoago_OpenJaw(BgDodoago* bgDodoago, PlayState* play);
void func_8087B284(BgGndSoulmeiro* bgGndSoulmeiro, PlayState* play);
void func_8087AF38(BgGndSoulmeiro* bgGndSoulmeiro, PlayState* play);
void BgHeavyBlock_Wait(BgHeavyBlock* bgHeavyBlock, PlayState* play);
void BgHeavyBlock_SpawnPieces(BgHeavyBlock* bgHeavyBlock, PlayState* play);
void BgJyaMegami_DetectLight(BgJyaMegami* bgJyaMegami, PlayState* play);
void func_808B5950(BgSpot16Bombstone* bgSpot16Bombstone, PlayState* play);
void EnIshi_Wait(EnIshi* enIshi, PlayState* play);
void EnIshi_SpawnFragmentsLarge(EnIshi* enIshi, PlayState* play);
void EnIshi_SpawnDustLarge(EnIshi* enIshi, PlayState* play);
}

namespace {

// #4044: a switch a partner pressed only moves the flag on this client; its own copy of
// the switch never looks at the flag again (except a few subtypes). Make the idle copy
// follow the flag, animating as if pressed but without calling SetOn/SetOff (no chime, no
// re-broadcast). cooldownOn = false lets the press/release animation run at once.
void FollowSwitchFlag(ObjSwitch* sw) {
    s32 flag = Flags_GetSwitch(gPlayState, (sw->dyna.actor.params >> 8) & 0x3F);
    s32 type = sw->dyna.actor.params & 7;
    s32 subType = (sw->dyna.actor.params >> 4) & 7;

    if ((sw->dyna.actor.params >> 7) & 1) {
        return; // still frozen in ice
    }

    if (type == OBJSWITCH_TYPE_FLOOR || type == OBJSWITCH_TYPE_FLOOR_RUSTY) {
        bool hold = type == OBJSWITCH_TYPE_FLOOR &&
                    (subType == OBJSWITCH_SUBTYPE_FLOOR_2 || subType == OBJSWITCH_SUBTYPE_FLOOR_3);
        // A hold switch is "on" while pressed (subtype 3 inverts: pressed clears the flag).
        bool pressedByFlag = subType == OBJSWITCH_SUBTYPE_FLOOR_3 ? !flag : flag;

        if (sw->actionFunc == ObjSwitch_FloorUp) {
            if (hold) {
                // Show the partner's weight without entering FloorDown, which would release
                // (and clear the flag) six frames later because nobody stands on this copy.
                // Subtype 3's resting flag state isn't fixed, so it keeps its own look.
                if (subType == OBJSWITCH_SUBTYPE_FLOOR_2) {
                    sw->dyna.actor.scale.y = flag ? 33.0f / 2000.0f : 33.0f / 200.0f;
                }
            } else if (flag) {
                sw->cooldownOn = false;
                ObjSwitch_FloorPressInit(sw);
            }
        } else if (sw->actionFunc == ObjSwitch_FloorDown) {
            if (type == OBJSWITCH_TYPE_FLOOR && subType == OBJSWITCH_SUBTYPE_FLOOR_1 && !flag) {
                sw->cooldownOn = false;
                ObjSwitch_FloorReleaseInit(sw);
            } else if (hold && DynaPolyActor_IsSwitchPressed(&sw->dyna) && !pressedByFlag) {
                // The partner stepped off their copy while this player still stands on
                // ours: hold the door open again (this broadcasts).
                if (subType == OBJSWITCH_SUBTYPE_FLOOR_2) {
                    Flags_SetSwitch(gPlayState, (sw->dyna.actor.params >> 8) & 0x3F);
                } else {
                    Flags_UnsetSwitch(gPlayState, (sw->dyna.actor.params >> 8) & 0x3F);
                }
            }
        }
    } else if (type == OBJSWITCH_TYPE_EYE) {
        if (sw->actionFunc == ObjSwitch_EyeOpen && flag) {
            sw->cooldownOn = false;
            ObjSwitch_EyeClosingInit(sw);
        } else if (sw->actionFunc == ObjSwitch_EyeClosed && subType == OBJSWITCH_SUBTYPE_EYE_1 && !flag) {
            sw->cooldownOn = false;
            ObjSwitch_EyeOpeningInit(sw);
        }
    } else if (type == OBJSWITCH_TYPE_CRYSTAL || type == OBJSWITCH_TYPE_CRYSTAL_TARGETABLE) {
        if (sw->actionFunc == ObjSwitch_CrystalOff && flag) {
            sw->cooldownOn = false;
            ObjSwitch_CrystalTurnOnInit(sw);
        } else if (sw->actionFunc == ObjSwitch_CrystalOn && subType == OBJSWITCH_SUBTYPE_CRYSTAL_1 && !flag) {
            sw->cooldownOn = false;
            ObjSwitch_CrystalTurnOffInit(sw);
        }
    }
}

} // namespace

void Anchor::RegisterHooks() {

    // #region Hooks that are required for basic Anchor functionality

    COND_HOOK(OnSceneSpawnActors, isConnected, [&]() {
        SendPacket_UpdateClientState();

        if (IsSaveLoaded()) {
            RefreshClientActors();
        }
    });

    COND_HOOK(OnPresentFileSelect, isConnected, [&]() { SendPacket_UpdateClientState(); });

    COND_ID_HOOK(ShouldActorInit, ACTOR_PLAYER, isConnected, [&](void* actorRef, bool* should) {
        Actor* actor = (Actor*)actorRef;

        if (refreshingActors) {
            // By the time we get here, the actor was already added to the ACTORCAT_PLAYER list, so we need to move it
            Actor_ChangeCategory(gPlayState, &gPlayState->actorCtx, actor, ACTORCAT_NPC);
            actor->id = ACTOR_EN_OE2;
            actor->category = ACTORCAT_NPC;
            actor->init = DummyPlayer_Init;
            actor->update = DummyPlayer_Update;
            actor->draw = DummyPlayer_Draw;
            actor->destroy = DummyPlayer_Destroy;
        }
    });

    COND_HOOK(OnPlayerUpdate, isConnected, [&]() {
        if (justLoadedSave) {
            justLoadedSave = false;
            SendPacket_RequestTeamState();
        }
        SendPacket_PlayerUpdate();
    });

    // NOTE: the packet-queue drain is NOT registered as its own OnGameFrameUpdate hook;
    // it is the FIRST step of the single Anchor per-frame dispatcher registered at the end
    // of this function (see Anchor_PerFrameTick below), which guarantees the Anchor-internal
    // tick order that map-iteration order does not.

    COND_HOOK(OnPlayerSfx, isConnected, [&](u16 sfxId) { SendPacket_PlayerSfx(sfxId); });

    // Shared wallet: broadcast every local rupee delta (echo-guarded inside the sender).
    COND_HOOK(OnRupeeChange, isConnected, [&](s16 rupeeChange) { SendPacket_RupeeChange(rupeeChange); });

    COND_HOOK(OnLoadGame, isConnected, [&](s16 fileNum) { justLoadedSave = true; });

    COND_HOOK(OnSaveFile, isConnected, [&](s16 fileNum, int sectionID) {
        if (sectionID == 0) {
            SendPacket_UpdateTeamState();
        }
    });

    COND_HOOK(OnFlagSet, isConnected,
              [&](s16 flagType, s16 flag) { SendPacket_SetFlag(SCENE_ID_MAX, flagType, flag); });

    COND_HOOK(OnFlagUnset, isConnected,
              [&](s16 flagType, s16 flag) { SendPacket_UnsetFlag(SCENE_ID_MAX, flagType, flag); });

    COND_HOOK(OnSceneFlagSet, isConnected,
              [&](s16 sceneNum, s16 flagType, s16 flag) { SendPacket_SetFlag(sceneNum, flagType, flag); });

    COND_HOOK(OnSceneFlagUnset, isConnected,
              [&](s16 sceneNum, s16 flagType, s16 flag) { SendPacket_UnsetFlag(sceneNum, flagType, flag); });

    COND_HOOK(OnRandoSetCheckStatus, isConnected, [&](RandomizerCheck rc, RandomizerCheckStatus status) {
        if (!isHandlingUpdateTeamState) {
            SendPacket_SetCheckStatus(rc);
        }
    });

    COND_HOOK(OnRandoSetIsSkipped, isConnected, [&](RandomizerCheck rc, bool isSkipped) {
        if (!isHandlingUpdateTeamState) {
            SendPacket_SetCheckStatus(rc);
        }
    });

    COND_HOOK(OnRandoEntranceDiscovered, isConnected,
              [&](u16 entranceIndex, u8 isReversedEntrance) { SendPacket_EntranceDiscovered(entranceIndex); });

    COND_ID_HOOK(OnBossDefeat, ACTOR_BOSS_GANON2, isConnected, [&](void* refActor) { SendPacket_GameComplete(); });

    COND_HOOK(OnItemReceive, isConnected, [&](GetItemEntry itemEntry) {
        // Handle vanilla dungeon items a bit differently
        if (itemEntry.modIndex == MOD_NONE &&
            (itemEntry.itemId >= ITEM_KEY_BOSS && itemEntry.itemId <= ITEM_KEY_SMALL)) {
            SendPacket_UpdateDungeonItems();
            return;
        }

        // A3 (channel exclusivity): rupee value now travels ONLY through RUPEE_CHANGE
        // (Rupees_ChangeBy -> OnRupeeChange). Drop pure rupee items from GIVE_ITEM so a
        // synced pickup is not counted twice (item channel + rupee channel). Only the
        // rupee items are filtered — shop merchandise (shields, nuts, ...) still
        // replicates through GIVE_ITEM. MOD_NONE ITEM_RUPEE_GREEN..ITEM_RUPEE_GOLD covers
        // green/blue/red/purple/gold; MOD_RANDOMIZER RG_GREEN_RUPEE..RG_HUGE_RUPEE covers
        // the rando rupee set (incl. Greg, whose RAND_INF flag still syncs via SET_FLAG).
        if ((itemEntry.modIndex == MOD_NONE && itemEntry.itemId >= ITEM_RUPEE_GREEN &&
             itemEntry.itemId <= ITEM_RUPEE_GOLD) ||
            (itemEntry.modIndex == MOD_RANDOMIZER && itemEntry.getItemId >= RG_GREEN_RUPEE &&
             itemEntry.getItemId <= RG_HUGE_RUPEE)) {
            return;
        }

        // A heart/ammo/magic drop belongs to whoever picked it up: the partner has their own
        // copy of the drop (see Packets/WorldObject.cpp).
        if (WorldObject_IsPersonalPickup(itemEntry)) {
            return;
        }

        SendPacket_GiveItem(itemEntry.tableId, itemEntry.getItemId);
    });

    COND_HOOK(OnDungeonKeyUsed, isConnected, [&](uint16_t mapIndex) {
        // Handle vanilla dungeon items a bit differently
        SendPacket_UpdateDungeonItems();
    });

    // #endregion

    // #region Hooks that are purely to sync actor states across the clients, not super essential

    COND_ID_HOOK(OnActorUpdate, ACTOR_EN_ITEM00, isConnected, [&](void* refActor) {
        EnItem00* actor = static_cast<EnItem00*>(refActor);

        if (Flags_GetCollectible(gPlayState, actor->collectibleFlag)) {
            Actor_Kill(&actor->actor);
        }
    });

    COND_ID_HOOK(ShouldActorUpdate, ACTOR_BG_BOMBWALL, isConnected, [&](void* refActor, bool* should) {
        BgBombwall* actor = static_cast<BgBombwall*>(refActor);

        if (actor->actionFunc == func_8086ED70 && Flags_GetSwitch(gPlayState, actor->dyna.actor.params & 0x3F)) {
            actor->collider.base.acFlags |= AC_HIT;
        }
    });

    COND_ID_HOOK(ShouldActorUpdate, ACTOR_BG_BREAKWALL, isConnected, [&](void* refActor, bool* should) {
        BgBreakwall* actor = static_cast<BgBreakwall*>(refActor);

        if (actor->actionFunc == BgBreakwall_Wait && Flags_GetSwitch(gPlayState, actor->dyna.actor.params & 0x3F)) {
            actor->collider.base.acFlags |= AC_HIT;
        }
    });

    COND_ID_HOOK(ShouldActorUpdate, ACTOR_BG_HAKA_ZOU, isConnected, [&](void* refActor, bool* should) {
        BgHakaZou* actor = static_cast<BgHakaZou*>(refActor);

        if (actor->actionFunc == func_80883000 && Flags_GetSwitch(gPlayState, actor->switchFlag)) {
            actor->collider.base.acFlags |= AC_HIT;
        }
    });

    COND_ID_HOOK(ShouldActorUpdate, ACTOR_BG_HIDAN_HAMSTEP, isConnected, [&](void* refActor, bool* should) {
        BgHidanHamstep* actor = static_cast<BgHidanHamstep*>(refActor);

        if (actor->actionFunc == func_808887C4 && Flags_GetSwitch(gPlayState, (actor->dyna.actor.params >> 8) & 0xFF)) {
            actor->collider.base.acFlags |= AC_HIT;
        }
    });

    COND_ID_HOOK(ShouldActorUpdate, ACTOR_BG_HIDAN_HROCK, isConnected, [&](void* refActor, bool* should) {
        BgHidanHrock* actor = static_cast<BgHidanHrock*>(refActor);

        if (actor->actionFunc == func_808896B8 && Flags_GetSwitch(gPlayState, actor->unk_16A)) {
            actor->collider.base.acFlags |= AC_HIT;
        }
    });

    COND_ID_HOOK(ShouldActorUpdate, ACTOR_BG_ICE_SHELTER, isConnected, [&](void* refActor, bool* should) {
        BgIceShelter* actor = static_cast<BgIceShelter*>(refActor);

        if (actor->actionFunc == func_8089107C && Flags_GetSwitch(gPlayState, actor->dyna.actor.params & 0x3F)) {
            func_808911BC(actor);
            Audio_PlayActorSound2(&actor->dyna.actor, NA_SE_EV_ICE_MELT);
        }
    });

    COND_ID_HOOK(ShouldActorUpdate, ACTOR_BG_JYA_BOMBCHUIWA, isConnected, [&](void* refActor, bool* should) {
        BgJyaBombchuiwa* actor = static_cast<BgJyaBombchuiwa*>(refActor);

        if (actor->actionFunc == BgJyaBombchuiwa_WaitForExplosion &&
            Flags_GetSwitch(gPlayState, actor->actor.params & 0x3F)) {
            actor->collider.base.acFlags |= AC_HIT;
        }
    });

    COND_ID_HOOK(ShouldActorUpdate, ACTOR_BG_JYA_BOMBIWA, isConnected, [&](void* refActor, bool* should) {
        BgJyaBombiwa* actor = static_cast<BgJyaBombiwa*>(refActor);

        if (Flags_GetSwitch(gPlayState, actor->dyna.actor.params & 0x3F)) {
            actor->collider.base.acFlags |= AC_HIT;
        }
    });

    COND_ID_HOOK(ShouldActorUpdate, ACTOR_BG_MIZU_BWALL, isConnected, [&](void* refActor, bool* should) {
        BgMizuBwall* actor = static_cast<BgMizuBwall*>(refActor);

        if (actor->actionFunc == BgMizuBwall_Idle &&
            Flags_GetSwitch(gPlayState, ((u16)actor->dyna.actor.params >> 8) & 0x3F)) {
            actor->collider.base.acFlags |= AC_HIT;
        }
    });

    COND_ID_HOOK(ShouldActorUpdate, ACTOR_BG_SPOT08_BAKUDANKABE, isConnected, [&](void* refActor, bool* should) {
        BgSpot08Bakudankabe* actor = static_cast<BgSpot08Bakudankabe*>(refActor);

        if (Flags_GetSwitch(gPlayState, (actor->dyna.actor.params & 0x3F))) {
            actor->collider.base.acFlags |= AC_HIT;
        }
    });

    COND_ID_HOOK(ShouldActorUpdate, ACTOR_BG_SPOT11_BAKUDANKABE, isConnected, [&](void* refActor, bool* should) {
        BgSpot11Bakudankabe* actor = static_cast<BgSpot11Bakudankabe*>(refActor);

        if (Flags_GetSwitch(gPlayState, (actor->dyna.actor.params & 0x3F))) {
            actor->collider.base.acFlags |= AC_HIT;
        }
    });

    COND_ID_HOOK(ShouldActorUpdate, ACTOR_BG_SPOT17_BAKUDANKABE, isConnected, [&](void* refActor, bool* should) {
        BgSpot17Bakudankabe* actor = static_cast<BgSpot17Bakudankabe*>(refActor);

        if (Flags_GetSwitch(gPlayState, (actor->dyna.actor.params & 0x3F))) {
            func_808B6BC0(actor, gPlayState);
            SoundSource_PlaySfxAtFixedWorldPos(gPlayState, &actor->dyna.actor.world.pos, 40, NA_SE_EV_WALL_BROKEN);
            Sfx_PlaySfxCentered(NA_SE_SY_CORRECT_CHIME);
            Actor_Kill(&actor->dyna.actor);
            *should = false;
        }
    });

    COND_ID_HOOK(ShouldActorUpdate, ACTOR_BG_YDAN_MARUTA, isConnected, [&](void* refActor, bool* should) {
        BgYdanMaruta* actor = static_cast<BgYdanMaruta*>(refActor);

        if (actor->actionFunc == func_808BF078 && Flags_GetSwitch(gPlayState, actor->switchFlag)) {
            actor->collider.base.acFlags |= AC_HIT;
        }
    });

    COND_ID_HOOK(ShouldActorUpdate, ACTOR_BG_YDAN_SP, isConnected, [&](void* refActor, bool* should) {
        BgYdanSp* actor = static_cast<BgYdanSp*>(refActor);

        if ((actor->actionFunc == BgYdanSp_FloorWebIdle || actor->actionFunc == BgYdanSp_WallWebIdle) &&
            Flags_GetSwitch(gPlayState, actor->isDestroyedSwitchFlag)) {
            BgYdanSp_BurnWeb(actor, gPlayState);
        }
    });

    COND_ID_HOOK(ShouldActorUpdate, ACTOR_DOOR_SHUTTER, isConnected, [&](void* refActor, bool* should) {
        DoorShutter* actor = static_cast<DoorShutter*>(refActor);

        if (Flags_GetSwitch(gPlayState, actor->dyna.actor.params & 0x3F)) {
            DECR(actor->unk_16E);
        }
    });

    COND_ID_HOOK(ShouldActorUpdate, ACTOR_EN_DOOR, isConnected, [&](void* refActor, bool* should) {
        EnDoor* actor = static_cast<EnDoor*>(refActor);

        if (actor->actionFunc == EnDoor_Idle && Flags_GetSwitch(gPlayState, actor->actor.params & 0x3F)) {
            DECR(actor->lockTimer);
        }
    });

    COND_ID_HOOK(ShouldActorUpdate, ACTOR_EN_SI, isConnected, [&](void* refActor, bool* should) {
        EnSi* actor = static_cast<EnSi*>(refActor);

        // The token this player just collected sets its flag early (Packets/WorldObject.cpp);
        // let it finish its textbox instead of vanishing.
        if (actor->actionFunc != func_80AFB950 &&
            (GET_GS_FLAGS((actor->actor.params & 0x1F00) >> 8) & (actor->actor.params & 0xFF))) {
            Actor_Kill(&actor->actor);
            *should = false;
        }
    });

    COND_ID_HOOK(ShouldActorUpdate, ACTOR_EN_SW, isConnected, [&](void* refActor, bool* should) {
        EnSw* actor = static_cast<EnSw*>(refActor);

        if (GET_GS_FLAGS((actor->actor.params & 0x1F00) >> 8) & (actor->actor.params & 0xFF)) {
            Actor_Kill(&actor->actor);
            *should = false;
        }
    });

    COND_ID_HOOK(ShouldActorUpdate, ACTOR_ITEM_B_HEART, isConnected, [&](void* refActor, bool* should) {
        ItemBHeart* actor = static_cast<ItemBHeart*>(refActor);

        if (Flags_GetCollectible(gPlayState, 0x1F)) {
            Actor_Kill(&actor->actor);
            *should = false;
        }
    });

    COND_ID_HOOK(ShouldActorUpdate, ACTOR_OBJ_BOMBIWA, isConnected, [&](void* refActor, bool* should) {
        ObjBombiwa* actor = static_cast<ObjBombiwa*>(refActor);

        if (Flags_GetSwitch(gPlayState, actor->actor.params & 0x3F)) {
            ObjBombiwa_Break(actor, gPlayState);
            SoundSource_PlaySfxAtFixedWorldPos(gPlayState, &actor->actor.world.pos, 80, NA_SE_EV_WALL_BROKEN);
            Actor_Kill(&actor->actor);
            *should = false;
        }
    });

    COND_ID_HOOK(ShouldActorUpdate, ACTOR_OBJ_HAMISHI, isConnected, [&](void* refActor, bool* should) {
        ObjHamishi* actor = static_cast<ObjHamishi*>(refActor);

        if (Flags_GetSwitch(gPlayState, actor->actor.params & 0x3F)) {
            ObjHamishi_Break(actor, gPlayState);
            SoundSource_PlaySfxAtFixedWorldPos(gPlayState, &actor->actor.world.pos, 40, NA_SE_EV_WALL_BROKEN);
            Actor_Kill(&actor->actor);
            *should = false;
        }
    });

    // #4044: breakables that set a switch flag but only read it at Init, so the partner's
    // copy stayed whole (and in the way) until the room reloaded.

    // Dodongo's Cavern: the stairs the two bomb flowers drop.
    COND_ID_HOOK(ShouldActorUpdate, ACTOR_BG_DDAN_KD, isConnected, [&](void* refActor, bool* should) {
        BgDdanKd* actor = static_cast<BgDdanKd*>(refActor);

        if (actor->actionFunc == BgDdanKd_CheckForExplosions && Flags_GetSwitch(gPlayState, actor->dyna.actor.params)) {
            actor->actionFunc = BgDdanKd_LowerStairs;
        }
    });

    // Dodongo's Cavern: the giant skull's jaw (both eyes bombed).
    COND_ID_HOOK(ShouldActorUpdate, ACTOR_BG_DODOAGO, isConnected, [&](void* refActor, bool* should) {
        BgDodoago* actor = static_cast<BgDodoago*>(refActor);

        if (actor->actionFunc == BgDodoago_WaitExplosives &&
            Flags_GetSwitch(gPlayState, actor->dyna.actor.params & 0x3F)) {
            gPlayState->roomCtx.unk_74[BGDODOAGO_EYE_LEFT] = 255;
            gPlayState->roomCtx.unk_74[BGDODOAGO_EYE_RIGHT] = 255;
            actor->state = 0;
            actor->actionFunc = BgDodoago_OpenJaw;
        }
    });

    // Death Mountain Trail: the boulder in front of Dodongo's Cavern.
    COND_ID_HOOK(ShouldActorUpdate, ACTOR_BG_SPOT16_BOMBSTONE, isConnected, [&](void* refActor, bool* should) {
        BgSpot16Bombstone* actor = static_cast<BgSpot16Bombstone*>(refActor);

        if (actor->actionFunc == func_808B5950 && Flags_GetSwitch(gPlayState, actor->switchFlag)) {
            actor->colliderCylinder.base.acFlags |= AC_HIT;
        }
    });

    // Spirit Temple: the goddess statue's face (crumbles under mirror light).
    COND_ID_HOOK(ShouldActorUpdate, ACTOR_BG_JYA_MEGAMI, isConnected, [&](void* refActor, bool* should) {
        BgJyaMegami* actor = static_cast<BgJyaMegami*>(refActor);

        if (actor->actionFunc == BgJyaMegami_DetectLight &&
            Flags_GetSwitch(gPlayState, actor->dyna.actor.params & 0x3F)) {
            actor->lightTimer = 41;
        }
    });

    // Ganon's Castle Spirit Trial: the web that lets the sunlight in.
    COND_ID_HOOK(ShouldActorUpdate, ACTOR_BG_GND_SOULMEIRO, isConnected, [&](void* refActor, bool* should) {
        BgGndSoulmeiro* actor = static_cast<BgGndSoulmeiro*>(refActor);

        if ((actor->actor.params & 0xFF) == 0 && actor->actionFunc == func_8087B284 &&
            Flags_GetSwitch(gPlayState, (actor->actor.params >> 8) & 0x3F)) {
            actor->unk_198 = 40;
            actor->actionFunc = func_8087AF38;
        }
    });

    // Silver-gauntlet boulders.
    COND_ID_HOOK(ShouldActorUpdate, ACTOR_EN_ISHI, isConnected, [&](void* refActor, bool* should) {
        EnIshi* actor = static_cast<EnIshi*>(refActor);
        s16 params = actor->actor.params;

        if ((params & 1) == ROCK_LARGE && actor->actionFunc == EnIshi_Wait &&
            Flags_GetSwitch(gPlayState, ((params >> 0xA) & 0x3C) | ((params >> 6) & 3))) {
            EnIshi_SpawnFragmentsLarge(actor, gPlayState);
            EnIshi_SpawnDustLarge(actor, gPlayState);
            SoundSource_PlaySfxAtFixedWorldPos(gPlayState, &actor->actor.world.pos, 40, NA_SE_EV_WALL_BROKEN);
            Actor_Kill(&actor->actor);
            *should = false;
        }
    });

    // Golden-gauntlet pillars (Ganon's Castle, Fire Temple).
    COND_ID_HOOK(ShouldActorUpdate, ACTOR_BG_HEAVY_BLOCK, isConnected, [&](void* refActor, bool* should) {
        BgHeavyBlock* actor = static_cast<BgHeavyBlock*>(refActor);

        if ((actor->dyna.actor.params & 0xFF) == HEAVYBLOCK_BREAKABLE && actor->actionFunc == BgHeavyBlock_Wait &&
            !Actor_HasParent(&actor->dyna.actor, gPlayState) &&
            Flags_GetSwitch(gPlayState, (actor->dyna.actor.params >> 8) & 0x3F)) {
            BgHeavyBlock_SpawnPieces(actor, gPlayState);
            SoundSource_PlaySfxAtFixedWorldPos(gPlayState, &actor->dyna.actor.world.pos, 40, NA_SE_EV_WALL_BROKEN);
            Actor_Kill(&actor->dyna.actor);
            *should = false;
        }
    });

    COND_ID_HOOK(ShouldActorUpdate, ACTOR_OBJ_SWITCH, isConnected,
                 [&](void* refActor, bool* should) { FollowSwitchFlag(static_cast<ObjSwitch*>(refActor)); });

    // A single torch a partner lit only reads its flag at Init (timed torch groups already
    // follow it every frame).
    COND_ID_HOOK(ShouldActorUpdate, ACTOR_OBJ_SYOKUDAI, isConnected, [&](void* refActor, bool* should) {
        ObjSyokudai* actor = static_cast<ObjSyokudai*>(refActor);

        if (((actor->actor.params >> 6) & 0xF) == 0 && actor->litTimer == 0 &&
            Flags_GetSwitch(gPlayState, actor->actor.params & 0x3F)) {
            actor->litTimer = -1;
        }
    });

    COND_VB_SHOULD(VB_HAMMER_TOTEM_BREAK, isConnected, {
        BgHidanDalm* actor = va_arg(args, BgHidanDalm*);

        if (Flags_GetSwitch(gPlayState, actor->switchFlag)) {
            *should = true;
        }
    });

    COND_VB_SHOULD(VB_FIRE_TEMPLE_BOMBABLE_WALL_BREAK, isConnected, {
        BgHidanKowarerukabe* actor = va_arg(args, BgHidanKowarerukabe*);

        if (Flags_GetSwitch(gPlayState, (actor->dyna.actor.params >> 8) & 0x3F)) {
            *should = true;
        }
    });

    // #endregion

    // Each module below registers its NON-per-frame hooks (OnSceneInit, VBs, actor
    // hooks, ...) and does its connect/disconnect Reset. Their per-frame ticks are NOT
    // self-registered on OnGameFrameUpdate — they are invoked in an explicit, documented
    // order by the single Anchor_PerFrameTick dispatcher at the end of this function.
    // Registration order below therefore no longer implies execution order.

    // Co-op death / spectate / revival life-state machine + the two death-flow VBs
    // (see CoopLifeSync.cpp). Its per-frame tick (CoopLifeSyncTick) SETS myLifeState.
    RegisterCoopLifeSyncHooks(isConnected);

    // Shared enemy/boss HP + synced deaths (see EnemySync.cpp)
    EnemySync::RegisterHooks(isConnected);

    // Cosmetic companion fairy per remote-player puppet (see PuppetFairy.cpp)
    RegisterPuppetFairyHooks(isConnected);

    // Queued, gated system warps for boss co-entry / spectate / cutscene sync (see CoopWarp.cpp)
    RegisterCoopWarpHooks(isConnected);

    // Boss-room co-entry: pull the partner into the boss fight (see Packets/BossEntry.cpp)
    RegisterBossEntryHooks(isConnected);

    // Story-cutscene sync: pull the partner into entrance / scene-layer cutscenes,
    // replayed locally (see Packets/CutsceneSync.cpp)
    RegisterCutsceneSyncHooks(isConnected);

    // BGM rendezvous-seek sync: phase-lock the MAIN track when both players share an
    // area (arriver seeks to the resident) + spectate game-over-music suppression
    // (see Packets/BgmSync.cpp).
    RegisterBgmSyncHooks(isConnected);

    // Push blocks move for everyone in the room (see Packets/PushBlock.cpp)
    RegisterPushBlockHooks(isConnected);

    // Cuccos, dogs and other wanderers follow one client's copy (see Packets/AmbientSync.cpp)
    RegisterAmbientSyncHooks(isConnected);

    // Grottos, personal pickups and Skulltula tokens (see Packets/WorldObject.cpp)
    RegisterWorldObjectHooks(isConnected);

    // ---- Anchor per-frame dispatcher --------------------------------------------------
    // GameInteractor::ExecuteHooks iterates an unordered_map, so per-hook execution order
    // is implementation-defined — NOT registration order. The Anchor layer has real
    // producer->consumer chains within a single frame, so we make the order EXPLICIT by
    // driving every Anchor OnGameFrameUpdate tick from ONE hook, in call sequence. All of
    // these ticks previously shared the exact same registration condition (isConnected),
    // so this single COND_HOOK(isConnected) dispatcher preserves each tick's condition; the
    // load-bearing orderings it guarantees are: (1) the packet-queue drain runs FIRST
    // (applies peer lifeState/wallet/warps), (2) CoopLifeSyncTick (SETS myLifeState) runs
    // before EnemySync::PerFrameTick (authority election READS myLifeState) and before
    // BgmSyncTick (spectate restore READS myLifeState). On disconnect this hook unregisters
    // (condition false), so no tick runs post-disconnect; each module's Reset still ran in
    // its Register*Hooks(false) call above.
    COND_HOOK(OnGameFrameUpdate, isConnected, [this]() {
        ProcessIncomingPacketQueue(); // drain: apply peer lifeState / wallet / warps
        CoopLifeSyncTick();           // SETS myLifeState
        EnemySync::PerFrameTick();    // authority election READS myLifeState
        PuppetFairyTick();            // companion-fairy spawn/reap lifecycle
        CoopWarpTick();               // drain queued, gated system warps
        BossEntryTick();              // reconcile boss echo-latch (Fix A)
        CutsceneSyncTick();           // reconcile cutscene pull-replay
        BgmSyncTick();                // spectate restore READS myLifeState
        PushBlockTick();              // pending remote pushes + room-entry block request
        AmbientSyncTick();            // stream driven cuccos/dogs/walkers (READS the EnemySync authority)
        WorldObjectTick();            // ages the personal-pickup window
    });
}
