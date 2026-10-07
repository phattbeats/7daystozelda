#include "global.h"
#include "vt.h"
#include "regs.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

#include "soh/Enhancements/gameconsole.h"
#include "soh/OTRGlobals.h"
#include "libultraship/bridge.h"

#define GFXPOOL_HEAD_MAGIC 0x1234
#define GFXPOOL_TAIL_MAGIC 0x5678

// SOH [Port] Game State management for our render loop
static struct RunFrameContext {
    GraphicsContext gfxCtx;
    GameStateOverlay* nextOvl;
    GameStateOverlay* ovl;
    int state;
} runFrameContext;

OSTime sGraphUpdateTime;
OSTime sGraphSetTaskTime;
FaultClient sGraphFaultClient;
CfbInfo sGraphCfbInfos[3];
FaultClient sGraphUcodeFaultClient;

void Skybox_Setup(PlayState* play, SkyboxContext* skyboxCtx, s16 skyboxId);
void PadMgr_ThreadEntry(PadMgr* padMgr);

// clang-format off
UCodeInfo D_8012D230[3] = {
    //{ UCODE_F3DZEX, D_80155F50 },
    { UCODE_UNK, NULL },
    //{ UCODE_S2DEX, D_80113070 },
};

UCodeInfo D_8012D248[3] = {
    //{ UCODE_F3DZEX, D_80155F50 },
    { UCODE_UNK, NULL },
    //{ UCODE_S2DEX, D_80113070 },
};
// clang-format on

void Graph_FaultClient() {
    void* nextFb = osViGetNextFramebuffer();
    void* newFb = ((uintptr_t)SysCfb_GetFbPtr(0) != (uintptr_t)nextFb) ? SysCfb_GetFbPtr(0) : SysCfb_GetFbPtr(1);

    osViSwapBuffer(newFb);
    Fault_WaitForInput();
    osViSwapBuffer(nextFb);
}

void Graph_DisassembleUCode(Gfx* workBuf) {
#if 0
    UCodeDisas disassembler;

    if (HREG(80) == 7 && HREG(81) != 0) {
        UCodeDisas_Init(&disassembler);
        disassembler.enableLog = HREG(83);
        UCodeDisas_RegisterUCode(&disassembler, ARRAY_COUNT(D_8012D230), D_8012D230);
        //UCodeDisas_SetCurUCode(&disassembler, D_80155F50);
        UCodeDisas_Disassemble(&disassembler, workBuf);
        HREG(93) = disassembler.dlCnt;
        HREG(84) = disassembler.tri2Cnt * 2 + disassembler.tri1Cnt + (disassembler.quadCnt * 2) + disassembler.lineCnt;
        HREG(85) = disassembler.vtxCnt;
        HREG(86) = disassembler.spvtxCnt;
        HREG(87) = disassembler.tri1Cnt;
        HREG(88) = disassembler.tri2Cnt;
        HREG(89) = disassembler.quadCnt;
        HREG(90) = disassembler.lineCnt;
        HREG(91) = disassembler.syncErr;
        HREG(92) = disassembler.loaducodeCnt;
        if (HREG(82) == 1 || HREG(82) == 2) {
            osSyncPrintf("vtx_cnt=%d\n", disassembler.vtxCnt);
            osSyncPrintf("spvtx_cnt=%d\n", disassembler.spvtxCnt);
            osSyncPrintf("tri1_cnt=%d\n", disassembler.tri1Cnt);
            osSyncPrintf("tri2_cnt=%d\n", disassembler.tri2Cnt);
            osSyncPrintf("quad_cnt=%d\n", disassembler.quadCnt);
            osSyncPrintf("line_cnt=%d\n", disassembler.lineCnt);
            osSyncPrintf("sync_err=%d\n", disassembler.syncErr);
            osSyncPrintf("loaducode_cnt=%d\n", disassembler.loaducodeCnt);
            osSyncPrintf("dl_depth=%d\n", disassembler.dlDepth);
            osSyncPrintf("dl_cnt=%d\n", disassembler.dlCnt);
        }
        UCodeDisas_Destroy(&disassembler);
    }
#endif
}

void Graph_UCodeFaultClient(Gfx* workBuf) {
#if 0
    UCodeDisas disassembler;

    UCodeDisas_Init(&disassembler);
    disassembler.enableLog = true;
    UCodeDisas_RegisterUCode(&disassembler, ARRAY_COUNT(D_8012D248), D_8012D248);
    //UCodeDisas_SetCurUCode(&disassembler, D_80155F50);
    UCodeDisas_Disassemble(&disassembler, workBuf);
    UCodeDisas_Destroy(&disassembler);
#endif
}

// #4062: display-list pools.
//
// The stock pools are the N64's (POLY_OPA 0x2FC0 commands, POLY_XLU 0x1000, overlay 0x800, work 0x100).
// The macros write into them with no bounds check, and the opaque list shares its buffer with the
// matrices and light blocks Graph_Alloc hands out from the far end. A large base plus a raid ran the
// list into those allocations; the frame was flagged below, but was still handed to the renderer, which
// read the clobbered commands ("Unhandled OP code: 0x8") and crashed the tab.
//
// The pools here are GFX_POOL_MAX_SCALE times the stock size, fenced by guard words. The scale in use
// is gSevenDays.GfxPoolScale (default GFX_POOL_SCALE_DEFAULT) so tests can run at stock size. Actors
// check the remaining room before they draw (Graph_GfxRoomLow); a frame that still overruns, or
// trips a guard, is replaced by an empty display list and counted instead of being rendered.
#define GFX_POOL_MAX_SCALE 8
#define GFX_POOL_SCALE_DEFAULT 4
#define GFX_GUARD_CMDS 8 // guard words either side of a buffer, in Gfx
#define GFX_GUARD_WORD 0xC0DEF00DC0DEF00DULL

typedef struct {
    Gfx guardHead[GFX_GUARD_CMDS];
    Gfx buf[0x2FC0 * GFX_POOL_MAX_SCALE];
    Gfx guardTail[GFX_GUARD_CMDS];
} GfxPoolOpa;
typedef struct {
    Gfx guardHead[GFX_GUARD_CMDS];
    Gfx buf[0x1000 * GFX_POOL_MAX_SCALE];
    Gfx guardTail[GFX_GUARD_CMDS];
} GfxPoolXlu;
typedef struct {
    Gfx guardHead[GFX_GUARD_CMDS];
    Gfx buf[0x800 * GFX_POOL_MAX_SCALE];
    Gfx guardTail[GFX_GUARD_CMDS];
} GfxPoolOvl;
typedef struct {
    Gfx guardHead[GFX_GUARD_CMDS];
    Gfx buf[0x100 * GFX_POOL_MAX_SCALE];
    Gfx guardTail[GFX_GUARD_CMDS];
} GfxPoolWork;

static GfxPoolOpa sPoolOpa[2];
static GfxPoolXlu sPoolXlu[2];
static GfxPoolOvl sPoolOvl[2];
static GfxPoolWork sPoolWork[2];
static s32 sPoolScale = GFX_POOL_SCALE_DEFAULT;
static s32 sPoolGuardsSet = 0;

// Counters since the last Graph_GfxStatsReset, read by sevendays_test_gfx (Placeables.cpp).
typedef struct {
    u32 frames;
    u32 overflowFrames; // frames replaced by an empty list (a pool ran over, or a guard was trampled)
    u32 guardTrips;
    u32 actorSkips;     // actors that did not draw because the pools were nearly full
    u32 placeableSkips; // 7 Days pieces that did not draw (their own, earlier limit)
    u32 peakUsed[4];    // bytes: head side + tail side, per pool (opa, xlu, overlay, work)
    u32 peakHead[4];
    u32 peakTail[4];
    s32 minFree[4];     // bytes left at the end of the frame (negative: ran over)
    u32 size[4];        // bytes in use at the current scale
    u32 actorLoopOpaUsed; // opaque bytes used when the actor loop ended, peak
    u32 actorLoopXluUsed;
    u32 lastUsed[4];
    s32 forceOverflow; // test hook: pretend the next N frames ran over
} GfxBudgetStats;
GfxBudgetStats gGfxBudget;

void Graph_GfxStatsReset(void) {
    s32 force = gGfxBudget.forceOverflow;
    memset(&gGfxBudget, 0, sizeof(gGfxBudget));
    gGfxBudget.forceOverflow = force;
    for (s32 i = 0; i < 4; i++) {
        gGfxBudget.minFree[i] = 0x7FFFFFFF;
    }
}

static void Graph_GfxSetGuards(void) {
    for (s32 i = 0; i < 2; i++) {
        for (s32 g = 0; g < GFX_GUARD_CMDS; g++) {
            ((u64*)&sPoolOpa[i].guardHead[g])[0] = GFX_GUARD_WORD;
            ((u64*)&sPoolOpa[i].guardTail[g])[0] = GFX_GUARD_WORD;
            ((u64*)&sPoolXlu[i].guardHead[g])[0] = GFX_GUARD_WORD;
            ((u64*)&sPoolXlu[i].guardTail[g])[0] = GFX_GUARD_WORD;
            ((u64*)&sPoolOvl[i].guardHead[g])[0] = GFX_GUARD_WORD;
            ((u64*)&sPoolOvl[i].guardTail[g])[0] = GFX_GUARD_WORD;
            ((u64*)&sPoolWork[i].guardHead[g])[0] = GFX_GUARD_WORD;
            ((u64*)&sPoolWork[i].guardTail[g])[0] = GFX_GUARD_WORD;
        }
    }
    sPoolGuardsSet = 1;
}

// The guard words fence the whole array. A list that only runs past the size in use (scale below the
// maximum) lands in spare room and is caught by the free-bytes check in Graph_GfxCheckFrame instead.
static s32 Graph_GfxGuardsOk(const Gfx* head, const Gfx* tail) {
    for (s32 g = 0; g < GFX_GUARD_CMDS; g++) {
        if (((const u64*)&head[g])[0] != GFX_GUARD_WORD || ((const u64*)&tail[g])[0] != GFX_GUARD_WORD) {
            return 0;
        }
    }
    return 1;
}

// Bytes left in a pool: the display list grows up from the start, Graph_Alloc takes from the end.
s32 Graph_GfxFreeBytes(GraphicsContext* gfxCtx, s32 pool) {
    TwoHeadGfxArena* t = (pool == 0) ? &gfxCtx->polyOpa : (pool == 1) ? &gfxCtx->polyXlu : &gfxCtx->overlay;
    return (s32)((intptr_t)t->d - (intptr_t)t->p);
}

// True when the opaque or translucent pool has less than `reserveCmds` commands of room left.
s32 Graph_GfxRoomLow(GraphicsContext* gfxCtx, s32 reserveCmds) {
    s32 bytes = reserveCmds * (s32)sizeof(Gfx);
    return Graph_GfxFreeBytes(gfxCtx, 0) < bytes || Graph_GfxFreeBytes(gfxCtx, 1) < bytes;
}

// True when less than `percent` of the opaque or translucent pool is still free.
s32 Graph_GfxRoomBelowPercent(GraphicsContext* gfxCtx, s32 percent) {
    return Graph_GfxFreeBytes(gfxCtx, 0) < (s32)(gGfxBudget.size[0] / 100 * percent) ||
           Graph_GfxFreeBytes(gfxCtx, 1) < (s32)(gGfxBudget.size[1] / 100 * percent);
}

// JSON for sevendays_test_gfx: pool sizes in bytes, peaks, minimum free and the skip/overflow counters.
const char* Graph_GfxStatsJson(void) {
    static char out[1536];
    static const char* const names[4] = { "opa", "xlu", "ovl", "work" };
    int n = snprintf(out, sizeof(out),
                     "{\"cmdBytes\":%d,\"scale\":%d,\"frames\":%u,\"overflowFrames\":%u,\"guardTrips\":%u,"
                     "\"actorSkips\":%u,\"placeableSkips\":%u,\"actorLoopOpaUsed\":%u,\"actorLoopXluUsed\":%u,"
                     "\"forceOverflow\":%d",
                     (int)sizeof(Gfx), sPoolScale, gGfxBudget.frames, gGfxBudget.overflowFrames, gGfxBudget.guardTrips,
                     gGfxBudget.actorSkips, gGfxBudget.placeableSkips, gGfxBudget.actorLoopOpaUsed,
                     gGfxBudget.actorLoopXluUsed, gGfxBudget.forceOverflow);
    for (int i = 0; i < 4 && n < (int)sizeof(out); i++) {
        n += snprintf(out + n, sizeof(out) - n,
                      ",\"%s\":{\"size\":%u,\"peakUsed\":%u,\"peakHead\":%u,\"peakTail\":%u,\"minFree\":%d,\"last\":%u}",
                      names[i], gGfxBudget.size[i], gGfxBudget.peakUsed[i], gGfxBudget.peakHead[i],
                      gGfxBudget.peakTail[i], gGfxBudget.minFree[i] == 0x7FFFFFFF ? 0 : gGfxBudget.minFree[i],
                      gGfxBudget.lastUsed[i]);
    }
    snprintf(out + n, sizeof(out) - n, "}");
    return out;
}

void Graph_GfxForceOverflow(s32 frames) {
    gGfxBudget.forceOverflow = frames;
}

void Graph_GfxNoteActorSkip(s32 placeable) {
    if (placeable) {
        gGfxBudget.placeableSkips++;
    } else {
        gGfxBudget.actorSkips++;
    }
}

void Graph_GfxNoteActorLoopEnd(GraphicsContext* gfxCtx) {
    u32 opa = (u32)((intptr_t)gfxCtx->polyOpa.p - (intptr_t)gfxCtx->polyOpa.bufp) +
              (u32)((intptr_t)gfxCtx->polyOpa.bufp + gfxCtx->polyOpa.size - (intptr_t)gfxCtx->polyOpa.d);
    u32 xlu = (u32)((intptr_t)gfxCtx->polyXlu.p - (intptr_t)gfxCtx->polyXlu.bufp);
    if (opa > gGfxBudget.actorLoopOpaUsed) {
        gGfxBudget.actorLoopOpaUsed = opa;
    }
    if (xlu > gGfxBudget.actorLoopXluUsed) {
        gGfxBudget.actorLoopXluUsed = xlu;
    }
}

void Graph_InitTHGA(GraphicsContext* gfxCtx) {
    GfxPool* pool = &gGfxPools[gfxCtx->gfxPoolIdx & 1];
    s32 idx = gfxCtx->gfxPoolIdx & 1;
    s32 scale = CVarGetInteger("gSevenDays.GfxPoolScale", GFX_POOL_SCALE_DEFAULT);

    if (scale < 1) {
        scale = 1;
    } else if (scale > GFX_POOL_MAX_SCALE) {
        scale = GFX_POOL_MAX_SCALE;
    }
    if (!sPoolGuardsSet) {
        Graph_GfxSetGuards();
        Graph_GfxStatsReset();
    }
    sPoolScale = scale;

    pool->headMagic = GFXPOOL_HEAD_MAGIC;
    pool->tailMagic = GFXPOOL_TAIL_MAGIC;

    THGA_Ct(&gfxCtx->polyOpa, sPoolOpa[idx].buf, sizeof(Gfx) * 0x2FC0 * scale);
    THGA_Ct(&gfxCtx->polyXlu, sPoolXlu[idx].buf, sizeof(Gfx) * 0x1000 * scale);
    THGA_Ct(&gfxCtx->overlay, sPoolOvl[idx].buf, sizeof(Gfx) * 0x800 * scale);
    THGA_Ct(&gfxCtx->work, sPoolWork[idx].buf, sizeof(Gfx) * 0x100 * scale);

    gfxCtx->polyOpaBuffer = sPoolOpa[idx].buf;
    gfxCtx->polyXluBuffer = sPoolXlu[idx].buf;
    gfxCtx->overlayBuffer = sPoolOvl[idx].buf;
    gfxCtx->workBuffer = sPoolWork[idx].buf;

    gGfxBudget.size[0] = sizeof(Gfx) * 0x2FC0 * scale;
    gGfxBudget.size[1] = sizeof(Gfx) * 0x1000 * scale;
    gGfxBudget.size[2] = sizeof(Gfx) * 0x800 * scale;
    gGfxBudget.size[3] = sizeof(Gfx) * 0x100 * scale;

    gfxCtx->curFrameBuffer = (u16*)SysCfb_GetFbPtr(gfxCtx->fbIdx % 2);
    gfxCtx->unk_014 = 0;
}

// Called once the frame's lists are built. Records the pool usage and returns true when the frame
// must not be rendered: a pool ran into its own tail allocations or past its end, or a guard was hit.
static s32 Graph_GfxCheckFrame(GraphicsContext* gfxCtx) {
    TwoHeadGfxArena* arenas[4] = { &gfxCtx->polyOpa, &gfxCtx->polyXlu, &gfxCtx->overlay, &gfxCtx->work };
    s32 idx = gfxCtx->gfxPoolIdx & 1;
    s32 bad = 0;

    gGfxBudget.frames++;
    for (s32 i = 0; i < 4; i++) {
        TwoHeadGfxArena* t = arenas[i];
        u32 head = (u32)((intptr_t)t->p - (intptr_t)t->bufp);
        u32 tail = (u32)((intptr_t)t->bufp + t->size - (intptr_t)t->d);
        s32 freeBytes = (s32)((intptr_t)t->d - (intptr_t)t->p);

        gGfxBudget.lastUsed[i] = head + tail;
        if (head + tail > gGfxBudget.peakUsed[i]) {
            gGfxBudget.peakUsed[i] = head + tail;
        }
        if (head > gGfxBudget.peakHead[i]) {
            gGfxBudget.peakHead[i] = head;
        }
        if (tail > gGfxBudget.peakTail[i]) {
            gGfxBudget.peakTail[i] = tail;
        }
        if (freeBytes < gGfxBudget.minFree[i]) {
            gGfxBudget.minFree[i] = freeBytes;
        }
        if (freeBytes < 0) {
            bad = 1;
        }
    }
    if (!Graph_GfxGuardsOk(sPoolOpa[idx].guardHead, sPoolOpa[idx].guardTail) ||
        !Graph_GfxGuardsOk(sPoolXlu[idx].guardHead, sPoolXlu[idx].guardTail) ||
        !Graph_GfxGuardsOk(sPoolOvl[idx].guardHead, sPoolOvl[idx].guardTail) ||
        !Graph_GfxGuardsOk(sPoolWork[idx].guardHead, sPoolWork[idx].guardTail)) {
        gGfxBudget.guardTrips++;
        bad = 1;
        Graph_GfxSetGuards();
    }
    if (gGfxBudget.forceOverflow > 0) {
        gGfxBudget.forceOverflow--;
        bad = 1;
    }
    if (bad) {
        gGfxBudget.overflowFrames++;
    }
    return bad;
}

GameStateOverlay* Graph_GetNextGameState(GameState* gameState) {
    void* gameStateInitFunc = GameState_GetInit(gameState);

    if (gameStateInitFunc == TitleSetup_Init) {
        return &gGameStateOverlayTable[0];
    }
    if (gameStateInitFunc == Select_Init) {
        return &gGameStateOverlayTable[1];
    }
    if (gameStateInitFunc == Title_Init) {
        return &gGameStateOverlayTable[2];
    }
    if (gameStateInitFunc == Play_Init) {
        return &gGameStateOverlayTable[3];
    }
    if (gameStateInitFunc == Opening_Init) {
        return &gGameStateOverlayTable[4];
    }
    if (gameStateInitFunc == FileChoose_Init) {
        return &gGameStateOverlayTable[5];
    }

    LOG_ADDRESS("game_init_func", gameStateInitFunc);
    return NULL;
}

void Graph_Init(GraphicsContext* gfxCtx) {
    memset(gfxCtx, 0, sizeof(GraphicsContext));
    gfxCtx->gfxPoolIdx = 0;
    gfxCtx->fbIdx = 0;
    gfxCtx->viMode = NULL;
    gfxCtx->viFeatures = gViConfigFeatures;
    gfxCtx->xScale = gViConfigXScale;
    gfxCtx->yScale = gViConfigYScale;
    osCreateMesgQueue(&gfxCtx->queue, gfxCtx->msgBuff, ARRAY_COUNT(gfxCtx->msgBuff));
    func_800D31F0();
    Fault_AddClient(&sGraphFaultClient, Graph_FaultClient, 0, 0);
}

void Graph_Destroy(GraphicsContext* gfxCtx) {
    func_800D3210();
    Fault_RemoveClient(&sGraphFaultClient);
}

void Graph_TaskSet00(GraphicsContext* gfxCtx) {
    static Gfx* D_8012D260 = NULL;
    static s32 sGraphCfbInfoIdx = 0;

    OSTime time;
    OSTimer timer;
    OSMesg msg;
    OSTask_t* task = &gfxCtx->task.list.t;
    OSScTask* scTask = &gfxCtx->task;
    CfbInfo* cfb;
    s32 pad1;

    D_8016A528 = osGetTime() - sGraphSetTaskTime - D_8016A558;

    osSetTimer(&timer, OS_USEC_TO_CYCLES(3000000), 0, &gfxCtx->queue, OS_MESG_32(666));

    osRecvMesg(&gfxCtx->queue, &msg, OS_MESG_BLOCK);
    osStopTimer(&timer);
// OTRTODO - Proper GFX crash handler
#if 0
    if (msg == (OSMesg)666) {
        osSyncPrintf(VT_FGCOL(RED));
        osSyncPrintf("RCPが帰ってきませんでした。"); // "RCP did not return."
        osSyncPrintf(VT_RST);
        LogUtils_LogHexDump((void*)&HW_REG(SP_MEM_ADDR_REG, u32), 0x20);
        LogUtils_LogHexDump((void*)&DPC_START_REG, 0x20);
        LogUtils_LogHexDump(gGfxSPTaskYieldBuffer, sizeof(gGfxSPTaskYieldBuffer));

        SREG(6) = -1;
        if (D_8012D260 != NULL) {
            HREG(80) = 7;
            HREG(81) = 1;
            HREG(83) = 2;
            D_8012D260 = D_8012D260;
            Graph_DisassembleUCode(D_8012D260);
        }
        Fault_AddHungupAndCrashImpl("RCP is HUNG UP!!", "Oh! MY GOD!!");
    }
#endif
    osRecvMesg(&gfxCtx->queue, &msg, OS_MESG_NOBLOCK);

    D_8012D260 = gfxCtx->workBuffer;
    if (gfxCtx->callback != NULL) {
        gfxCtx->callback(gfxCtx, gfxCtx->callbackParam);
    }

    time = osGetTime();
    if (D_8016A550 != 0) {
        D_8016A558 = (D_8016A558 + time) - D_8016A550;
        D_8016A550 = time;
    }
    D_8016A520 = D_8016A558;
    D_8016A558 = 0;
    sGraphSetTaskTime = osGetTime();

    task->type = M_GFXTASK;
    task->flags = OS_SC_DRAM_DLIST;
    task->ucode_boot = SysUcode_GetUCodeBoot();
    task->ucode_boot_size = SysUcode_GetUCodeBootSize();
    task->ucode = SysUcode_GetUCode();
    task->ucode_data = SysUcode_GetUCodeData();
    task->ucode_size = 0x1000;
    task->ucode_data_size = 0x800;
    task->dram_stack = (u64*)gGfxSPTaskStack;
    task->dram_stack_size = sizeof(gGfxSPTaskStack);
    task->output_buff = gGfxSPTaskOutputBuffer;
    task->output_buff_size = (u64*)((u8*)gGfxSPTaskOutputBuffer + sizeof(gGfxSPTaskOutputBuffer));
    task->data_ptr = (u64*)gfxCtx->workBuffer;

    OPEN_DISPS(gfxCtx);
    task->data_size = (uintptr_t)WORK_DISP - (uintptr_t)gfxCtx->workBuffer;
    CLOSE_DISPS(gfxCtx);

    task->yield_data_ptr = (u64*)gGfxSPTaskYieldBuffer;
    task->yield_data_size = sizeof(gGfxSPTaskYieldBuffer);

    scTask->next = NULL;
    scTask->flags = OS_SC_RCP_MASK | OS_SC_SWAPBUFFER | OS_SC_LAST_TASK;
    if (SREG(33) & 1) {
        SREG(33) &= ~1;
        scTask->flags &= ~OS_SC_SWAPBUFFER;
        gfxCtx->fbIdx--;
    }

    scTask->msgQ = &gfxCtx->queue;
    scTask->msg.ptr = NULL;

    cfb = &sGraphCfbInfos[sGraphCfbInfoIdx++];
    cfb->fb1 = gfxCtx->curFrameBuffer;
    cfb->swapBuffer = gfxCtx->curFrameBuffer;
    cfb->viMode = gfxCtx->viMode;
    cfb->features = gfxCtx->viFeatures;
    cfb->xScale = gfxCtx->xScale;
    cfb->yScale = gfxCtx->yScale;
    cfb->unk_10 = 0;
    cfb->updateRate = R_UPDATE_RATE;

    scTask->framebuffer = cfb;
    sGraphCfbInfoIdx = sGraphCfbInfoIdx % ARRAY_COUNT(sGraphCfbInfos);

    gfxCtx->schedMsgQ = &gSchedContext.cmdQ;

    osSendMesgPtr(&gSchedContext.cmdQ, scTask, OS_MESG_BLOCK);
    Sched_SendEntryMsg(&gSchedContext);
}

void Graph_Update(GraphicsContext* gfxCtx, GameState* gameState) {
    u32 problem;

    // Skip game frame updates while gfx debugger is active, and execute with the last frame's DL buffer
    if (GfxDebuggerIsDebugging()) {
        Graph_ProcessGfxCommands(runFrameContext.gfxCtx.workBuffer);
        return;
    }

    gameState->unk_A0 = 0;
    Graph_InitTHGA(gfxCtx);

    OPEN_DISPS(gfxCtx);

    gDPNoOpString(WORK_DISP++, "WORK_DISP 開始", 0);
    gDPNoOpString(POLY_OPA_DISP++, "POLY_OPA_DISP 開始", 0);
    gDPNoOpString(POLY_XLU_DISP++, "POLY_XLU_DISP 開始", 0);
    gDPNoOpString(OVERLAY_DISP++, "OVERLAY_DISP 開始", 0);

    CLOSE_DISPS(gfxCtx);

    GameState_ReqPadData(gameState);
    GameState_Update(gameState);

    OPEN_DISPS(gfxCtx);

    gDPNoOpString(WORK_DISP++, "WORK_DISP 終了", 0);
    gDPNoOpString(POLY_OPA_DISP++, "POLY_OPA_DISP 終了", 0);
    gDPNoOpString(POLY_XLU_DISP++, "POLY_XLU_DISP 終了", 0);
    gDPNoOpString(OVERLAY_DISP++, "OVERLAY_DISP 終了", 0);

    CLOSE_DISPS(gfxCtx);

    OPEN_DISPS(gfxCtx);

    gSPBranchList(WORK_DISP++, gfxCtx->polyOpaBuffer);
    gSPBranchList(POLY_OPA_DISP++, gfxCtx->polyXluBuffer);
    gSPBranchList(POLY_XLU_DISP++, gfxCtx->overlayBuffer);
    gDPPipeSync(OVERLAY_DISP++);
    gDPFullSync(OVERLAY_DISP++);
    gSPEndDisplayList(OVERLAY_DISP++);

    CLOSE_DISPS(gfxCtx);

    if (HREG(80) == 10 && HREG(93) == 2) {
        HREG(80) = 7;
        HREG(81) = -1;
        HREG(83) = HREG(92);
    }

    if (HREG(80) == 7 && HREG(81) != 0) {
        if (HREG(82) == 3) {
            Fault_AddClient(&sGraphUcodeFaultClient, Graph_UCodeFaultClient, gfxCtx->workBuffer, "do_count_fault");
        }

        Graph_DisassembleUCode(gfxCtx->workBuffer);

        if (HREG(82) == 3) {
            Fault_RemoveClient(&sGraphUcodeFaultClient);
        }

        if (HREG(81) < 0) {
            LogUtils_LogHexDump((void*)&HW_REG(SP_MEM_ADDR_REG, u32), 0x20);
            LogUtils_LogHexDump((void*)&DPC_START_REG, 0x20);
        }

        if (HREG(81) < 0) {
            HREG(81) = 0;
        }
    }

    problem = false;

    {
        GfxPool* pool = &gGfxPools[gfxCtx->gfxPoolIdx & 1];

        if (pool->headMagic != GFXPOOL_HEAD_MAGIC) {
            //! @bug (?) : "problem = true;" may be missing
            osSyncPrintf("%c", BEL);
            // "Dynamic area head is destroyed"
            osSyncPrintf(VT_COL(RED, WHITE) "ダイナミック領域先頭が破壊されています\n" VT_RST);
            Fault_AddHungupAndCrash(__FILE__, __LINE__);
        }
        if (pool->tailMagic != GFXPOOL_TAIL_MAGIC) {
            problem = true;
            osSyncPrintf("%c", BEL);
            // "Dynamic region tail is destroyed"
            osSyncPrintf(VT_COL(RED, WHITE) "ダイナミック領域末尾が破壊されています\n" VT_RST);
            Fault_AddHungupAndCrash(__FILE__, __LINE__);
        }
    }

    if (THGA_IsCrash(&gfxCtx->polyOpa)) {
        problem = true;
        osSyncPrintf("%c", BEL);
        // "Zelda 0 is dead"
        osSyncPrintf(VT_COL(RED, WHITE) "ゼルダ0は死んでしまった(graph_alloc is empty)\n" VT_RST);
    }
    if (THGA_IsCrash(&gfxCtx->polyXlu)) {
        problem = true;
        osSyncPrintf("%c", BEL);
        // "Zelda 1 is dead"
        osSyncPrintf(VT_COL(RED, WHITE) "ゼルダ1は死んでしまった(graph_alloc is empty)\n" VT_RST);
    }
    if (THGA_IsCrash(&gfxCtx->overlay)) {
        problem = true;
        osSyncPrintf("%c", BEL);
        // "Zelda 4 is dead"
        osSyncPrintf(VT_COL(RED, WHITE) "ゼルダ4は死んでしまった(graph_alloc is empty)\n" VT_RST);
    }

    if (Graph_GfxCheckFrame(gfxCtx)) {
        // Nothing from this frame may reach the renderer: its lists overlap their own allocations.
        // An empty root list shows nothing for this tick; the next tick starts clean.
        Gfx* emptyList = gfxCtx->workBuffer;
        gSPEndDisplayList(emptyList);
        problem = true;
    }

    if (!problem) {
        Graph_TaskSet00(gfxCtx);
        gfxCtx->gfxPoolIdx++;
        gfxCtx->fbIdx++;
    }

    func_800F3054();

    {
        OSTime time = osGetTime();
        s32 pad[4];

        D_8016A538 = gRSPGFXTotalTime;
        D_8016A530 = gRSPAudioTotalTime;
        D_8016A540 = gRDPTotalTime;
        gRSPGFXTotalTime = 0;
        gRSPAudioTotalTime = 0;
        gRDPTotalTime = 0;

        if (sGraphUpdateTime != 0) {
            D_8016A548 = time - sGraphUpdateTime;
        }
        sGraphUpdateTime = time;
    }

    if (CVarGetInteger(CVAR_DEVELOPER_TOOLS("DebugEnabled"), 0)) {
        if (CHECK_BTN_ALL(gameState->input[0].press.button, BTN_Z) &&
            CHECK_BTN_ALL(gameState->input[0].cur.button, BTN_L | BTN_R)) {
            gSaveContext.gameMode = GAMEMODE_NORMAL;
            SET_NEXT_GAMESTATE(gameState, Select_Init, SelectContext);
            gameState->running = false;
        }
    }

    if (gIsCtrlr2Valid && PreNmiBuff_IsResetting(gAppNmiBufferPtr) && !gameState->unk_A0) {
        // "To reset mode"
        osSyncPrintf(VT_COL(YELLOW, BLACK) "PRE-NMIによりリセットモードに移行します\n" VT_RST);
        SET_NEXT_GAMESTATE(gameState, PreNMI_Init, PreNMIContext);
        gameState->running = false;
    }
}

uint64_t GetFrequency();
uint64_t GetPerfCounter();

extern AudioMgr gAudioMgr;

extern void ProcessSaveStateRequests(void);

static void RunFrame() {
    u32 size;
    char faultMsg[0x50];
    static bool hasSetupSkybox = false;

    switch (runFrameContext.state) {
        case 0:
            break;
        case 1:
            goto nextFrame;
    }

    runFrameContext.nextOvl = &gGameStateOverlayTable[0];

    osSyncPrintf("グラフィックスレッド実行開始\n"); // "Start graphic thread execution"
    Graph_Init(&runFrameContext.gfxCtx);

    while (runFrameContext.nextOvl) {
        runFrameContext.ovl = runFrameContext.nextOvl;
        Overlay_LoadGameState(runFrameContext.ovl);

        size = runFrameContext.ovl->instanceSize;
        osSyncPrintf("クラスサイズ＝%dバイト\n", size); // "Class size = %d bytes"

        gGameState = SYSTEM_ARENA_MALLOC_DEBUG(size);

        if (!gGameState) {
            osSyncPrintf("確保失敗\n"); // "Failure to secure"

            snprintf(faultMsg, sizeof(faultMsg), "CLASS SIZE= %d bytes", size);
            Fault_AddHungupAndCrashImpl("GAME CLASS MALLOC FAILED", faultMsg);
        }
        GameState_Init(gGameState, runFrameContext.ovl->init, &runFrameContext.gfxCtx);

        // Setup the normal skybox once before entering any game states to avoid the 0xabababab crash.
        // The crash is due to certain skyboxes not loading all the data they need from Skybox_Setup.
        if (!hasSetupSkybox) {
            PlayState* play = (PlayState*)gGameState;
            Skybox_Setup(play, &play->skyboxCtx, SKYBOX_NORMAL_SKY);
            hasSetupSkybox = true;
        }

        uint64_t freq = GetFrequency();

        while (GameState_IsRunning(gGameState)) {
            // uint64_t ticksA, ticksB;
            // ticksA = GetPerfCounter();

            Graph_StartFrame();

            PadMgr_ThreadEntry(&gPadMgr);

            Graph_Update(&runFrameContext.gfxCtx, gGameState);
            // ticksB = GetPerfCounter();

            if (GfxDebuggerIsDebuggingRequested()) {
                GfxDebuggerDebugDisplayList(runFrameContext.gfxCtx.workBuffer);
            }

            Graph_ProcessGfxCommands(runFrameContext.gfxCtx.workBuffer);

            // uint64_t diff = (ticksB - ticksA) / (freq / 1000);
            // printf("Frame simulated in %ims\n", diff);
            runFrameContext.state = 1;
            ProcessSaveStateRequests();
            return;
        nextFrame:;
        }

        runFrameContext.nextOvl = Graph_GetNextGameState(gGameState);
        GameState_Destroy(gGameState);
        SYSTEM_ARENA_FREE_DEBUG(gGameState);
        Overlay_FreeGameState(runFrameContext.ovl);
    }
    Graph_Destroy(&runFrameContext.gfxCtx);
    osSyncPrintf("グラフィックスレッド実行終了\n"); // "End of graphic thread execution"

    // Graph_Update(gfxCtxTest, gameStateTest);
    exit(0);
}


#ifdef __EMSCRIPTEN__
// Pattern from sm64coopdx:
// 1. Graph_ProcessGfxCommands (inside RunFrame) is short-circuited on web
//    to render 1 frame with identity matrices and return immediately.
// 2. Every rAF, we re-render the last display list with an interpolation
//    delta_frac that smoothly goes 0→1 between game ticks.
// Base display rate for N64 (VI interrupts per second)
#define OOT_DISPLAY_HZ 60
static double sLastTickTime = 0;
static Gfx* sLastDisplayList = NULL;
static bool sGameTickReady = false;

// Measured rAF rate — fed to GetInterpolationFPS so SoH knows the display rate.
static double sRafTimes[10];
static int sRafTimeIndex = 0;
static int sRafTimeCount = 0;
uint32_t gWebMeasuredFPS = 60; // exported to OTRGlobals.cpp

// Interpolation fraction [0..1] within current game tick.
// Set by RunFrameWeb before each Graph_ProcessGfxCommands call.
float gWebInterpolationFraction = 1.0f;

static void RunFrameWeb(void) {
    double now = emscripten_get_now() / 1000.0;

    // Measure actual rAF rate from recent frame times
    sRafTimes[sRafTimeIndex] = now;
    sRafTimeIndex = (sRafTimeIndex + 1) % 10;
    if (sRafTimeCount < 10) sRafTimeCount++;
    if (sRafTimeCount >= 2) {
        int oldest = (sRafTimeIndex - sRafTimeCount + 10) % 10;
        double span = now - sRafTimes[oldest];
        if (span > 0.0) {
            uint32_t measured = (uint32_t)((sRafTimeCount - 1) / span + 0.5);
            if (measured >= 20 && measured <= 240) {
                gWebMeasuredFPS = measured;
            }
        }
    }

    if (sLastTickTime == 0) {
        sLastTickTime = now;
    }

    // Game tick rate adapts to R_UPDATE_RATE:
    //   R_UPDATE_RATE=3 → 60/3 = 20 Hz (normal gameplay)
    //   R_UPDATE_RATE=1 → 60/1 = 60 Hz (menus, file select, title screen)
    int updateRate = R_UPDATE_RATE;
    if (updateRate < 1) updateRate = 1;
    if (updateRate > 3) updateRate = 3;
    double gameTickTime = (double)updateRate / (double)OOT_DISPLAY_HZ;

    double elapsed = now - sLastTickTime;

    // Step 1: Run game logic when enough time has accumulated.
    // Tick rate follows R_UPDATE_RATE so menus run at proper speed.
    if (elapsed >= gameTickTime) {
        int ticks = (int)(elapsed / gameTickTime);
        if (ticks > 4) {
            ticks = 4;  // Cap catch-up to prevent spiral (higher cap for 60Hz menus)
        }

        for (int i = 0; i < ticks; i++) {
            gWebInterpolationFraction = 1.0f;
            RunFrame();
        }

        sLastDisplayList = runFrameContext.gfxCtx.workBuffer;

        sLastTickTime += ticks * gameTickTime;
        if (now - sLastTickTime > gameTickTime) {
            sLastTickTime = now;
        }

        sGameTickReady = true;
        // Recalculate elapsed after advancing tick time
        elapsed = now - sLastTickTime;
    }

    // Step 2: Render an interpolation frame every rAF call.
    // delta_frac goes from 0 (just after tick) to ~1 (just before next tick).
    if (sGameTickReady && sLastDisplayList != NULL) {
        float delta_frac = (float)(elapsed / gameTickTime);
        if (delta_frac < 0.0f) delta_frac = 0.0f;
        if (delta_frac > 1.0f) delta_frac = 1.0f;

        gWebInterpolationFraction = delta_frac;
        Graph_ProcessGfxCommands(sLastDisplayList);
    }
}
#endif

void Graph_ThreadEntry(void* arg0) {
#ifdef __EMSCRIPTEN__
    // Use rAF (fps=0) for smooth rendering, throttle game logic to 20fps internally.
    // Between game ticks, re-render with interpolation for visual smoothness.
    emscripten_set_main_loop(RunFrameWeb, 0, 0);
    return;
#else
    while (WindowIsRunning()) {
        RunFrame();
    }
#endif
}

void* Graph_Alloc(GraphicsContext* gfxCtx, size_t size) {
    TwoHeadGfxArena* thga = &gfxCtx->polyOpa;

    if (HREG(59) == 1) {
        osSyncPrintf("graph_alloc siz=%d thga size=%08x bufp=%08x head=%08x tail=%08x\n", size, thga->size, thga->bufp,
                     thga->p, thga->d);
    }
    return THGA_AllocEnd(&gfxCtx->polyOpa, ALIGN16(size));
}

void* Graph_Alloc2(GraphicsContext* gfxCtx, size_t size) {
    TwoHeadGfxArena* thga = &gfxCtx->polyOpa;

    if (HREG(59) == 1) {
        osSyncPrintf("graph_alloc siz=%d thga size=%08x bufp=%08x head=%08x tail=%08x\n", size, thga->size, thga->bufp,
                     thga->p, thga->d);
    }
    return THGA_AllocEnd(&gfxCtx->polyOpa, ALIGN16(size));
}

void Graph_OpenDisps(Gfx** dispRefs, GraphicsContext* gfxCtx, const char* file, s32 line) {
    // SOH [Debugging] Force open/close disp string handling on so that the graphics debugger can leverage it
    if (true || HREG(80) == 7 && HREG(82) != 4) {
        dispRefs[0] = gfxCtx->polyOpa.p;
        dispRefs[1] = gfxCtx->polyXlu.p;
        dispRefs[2] = gfxCtx->overlay.p;

        gDPNoOpOpenDisp(gfxCtx->polyOpa.p++, file, line);
        gDPNoOpOpenDisp(gfxCtx->polyXlu.p++, file, line);
        gDPNoOpOpenDisp(gfxCtx->overlay.p++, file, line);
    }
}

void Graph_CloseDisps(Gfx** dispRefs, GraphicsContext* gfxCtx, const char* file, s32 line) {
    // SOH [Debugging] Force open/close disp string handling on so that the graphics debugger can leverage it
    if (true || HREG(80) == 7 && HREG(82) != 4) {
        if (dispRefs[0] + 1 == gfxCtx->polyOpa.p) {
            gfxCtx->polyOpa.p = dispRefs[0];
        } else {
            gDPNoOpCloseDisp(gfxCtx->polyOpa.p++, file, line);
        }

        if (dispRefs[1] + 1 == gfxCtx->polyXlu.p) {
            gfxCtx->polyXlu.p = dispRefs[1];
        } else {
            gDPNoOpCloseDisp(gfxCtx->polyXlu.p++, file, line);
        }

        if (dispRefs[2] + 1 == gfxCtx->overlay.p) {
            gfxCtx->overlay.p = dispRefs[2];
        } else {
            gDPNoOpCloseDisp(gfxCtx->overlay.p++, file, line);
        }
    }
}

Gfx* Graph_GfxPlusOne(Gfx* gfx) {
    return gfx + 1;
}

Gfx* Graph_BranchDlist(Gfx* gfx, Gfx* dst) {
    gSPBranchList(gfx, dst);
    return dst;
}

void* Graph_DlistAlloc(Gfx** gfx, size_t size) {
    u8* ptr;
    Gfx* dst;

    size = ((size + 7) & ~7),

    ptr = (u8*)(*gfx + 1);

    dst = (Gfx*)(ptr + size);
    gSPBranchList(*gfx, dst);

    *gfx = dst;
    return ptr;
}
