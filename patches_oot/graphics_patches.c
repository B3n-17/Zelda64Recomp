/*
 * Turns on RT64's extended GBI for Ocarina of Time.
 *
 * Every gEX* command is encoded in opcode space a real RDP would treat as
 * undefined, so RT64 ignores all of them until the game asks for the extension
 * with gEXEnable. That request does not persist: it has to be re-sent on each
 * frame's display list, which is why MM issues it from its gfx pool setup
 * (patches/ui_patches.c). Graph_InitTHGA is OoT's equivalent hook.
 *
 * Without this the extended commands are silently discarded - no error, no
 * crash, the HUD simply stays at its 4:3 positions. Anything here that depends
 * on the extended GBI (widescreen HUD anchoring in ui_patches.c) is inert until
 * this runs.
 *
 * The pool is also relocated and enlarged here, which is what MM sends
 * gEXSetRDRAMExtended for and this now does too. Transform tagging writes two
 * extra commands around every limb of every skeleton and around every actor's own
 * matrix (actor_transform_tagging.c). A crowded room can hold a few thousand
 * limbs, so that is a few thousand commands the vanilla 0x17E0-entry polyOpa
 * buffer has no room for, and overrunning it is not graceful: THGA hands back
 * NULL, Graph_Update trips its "Zelda 0 is dead" check, and the frame is dropped.
 *
 * The replacement pool is a patch global, so it lives in the extra RDRAM the
 * recompiler maps above 8 MB (patches.ld), which a real N64 does not have and
 * RT64 therefore ignores addresses in until gEXSetRDRAMExtended says otherwise.
 * The original gGfxPools stays where it is: nothing draws into it any more, but
 * Graph_Update still reads its head and tail magics, and leaving those working is
 * free.
 */
#include "patches.h"
#include "rt64_extended_gbi.h"

#include "buffers.h"
#include "gfx.h"
#include "regs.h"
#include "sys_cfb.h"
#include "thga.h"

// Both are file-local defines in graph.c rather than header constants, so they
// are repeated here. Values must match src/code/graph.c.
#define GFXPOOL_HEAD_MAGIC 0x1234
#define GFXPOOL_TAIL_MAGIC 0x5678

/*
 * Roughly ten times the vanilla sizes, matching what the Majora's Mask tree
 * settled on for the same reason (patches/ui_patches.c). Two of these are about
 * 1.4 MB, against the ~8 MB patches.ld reserves.
 */
typedef struct {
    Gfx polyOpaBuffer[0x10000];
    Gfx polyXluBuffer[0x4000];
    Gfx overlayBuffer[0x1000];
} BiggerGfxPool;

static BiggerGfxPool gBiggerGfxPools[2];

/**
 * Verbatim copy of Graph_InitTHGA in graph.c, with the extended GBI handshake
 * appended and the arenas pointed at the bigger pool. RECOMP_PATCH replaces the
 * whole function, so the original body has to be carried along.
 */
RECOMP_PATCH void Graph_InitTHGA(GraphicsContext* gfxCtx) {
    GfxPool* pool = &gGfxPools[gfxCtx->gfxPoolIdx & 1];
    // @recomp
    BiggerGfxPool* bigger_pool = &gBiggerGfxPools[gfxCtx->gfxPoolIdx & 1];

    pool->headMagic = GFXPOOL_HEAD_MAGIC;
    pool->tailMagic = GFXPOOL_TAIL_MAGIC;

    // @recomp The three drawing arenas move to the bigger pool. Graph_TaskSet00
    // builds the master list purely out of gfxCtx->*Buffer, so redirecting them
    // here is the whole of the relocation.
    //
    // The work buffer deliberately stays in the vanilla pool: it is where the task
    // starts (Graph_TaskSet00 hands its address to the RSP as data_ptr), and it is
    // the display list that carries gEXSetRDRAMExtended's branch into the others.
    // Nothing is written into it that transform tagging grows, so it has no reason
    // to move and every reason to stay reachable without the extension.
    THGA_Init(&gfxCtx->polyOpa, bigger_pool->polyOpaBuffer, sizeof(bigger_pool->polyOpaBuffer));
    THGA_Init(&gfxCtx->polyXlu, bigger_pool->polyXluBuffer, sizeof(bigger_pool->polyXluBuffer));
    THGA_Init(&gfxCtx->overlay, bigger_pool->overlayBuffer, sizeof(bigger_pool->overlayBuffer));
    THGA_Init(&gfxCtx->work, pool->workBuffer, sizeof(pool->workBuffer));

    gfxCtx->polyOpaBuffer = bigger_pool->polyOpaBuffer;
    gfxCtx->polyXluBuffer = bigger_pool->polyXluBuffer;
    gfxCtx->overlayBuffer = bigger_pool->overlayBuffer;
    gfxCtx->workBuffer = pool->workBuffer;

    gfxCtx->curFrameBuffer = SysCfb_GetFbPtr(gfxCtx->fbIdx % 2);
    gfxCtx->unk_014 = 0;

    // @recomp Ask RT64 to honour the extended GBI for this frame, and to accept
    // the above-8 MB addresses the pool now lives at. Has to happen after
    // THGA_Init, which resets the arenas this writes into.
    //
    // Both go at the head of the work buffer as well as polyOpa, because the work
    // buffer is what runs first and it ends by branching *into* polyOpa. Asking
    // only from polyOpa would mean that branch target - an above-8 MB address -
    // had to be resolved before the command that explains it.
    OPEN_DISPS(gfxCtx, "graphics_patches.c", 0);
    gEXEnable(WORK_DISP++);
    gEXSetRDRAMExtended(WORK_DISP++, 1);
    gEXEnable(POLY_OPA_DISP++);
    gEXSetRDRAMExtended(POLY_OPA_DISP++, 1);

    // @recomp Tell RT64 what rate the *game* is running at, which is the number it
    // compares the target rate against to decide how many frames to generate
    // between two of the game's. Without it RT64 falls back to inferring the rate
    // from how often the VI has been retracing, which is a guess - MM does not
    // rely on it either (patches/ui_patches.c sends the same command).
    //
    // R_UPDATE_RATE is OoT's counterpart to MM's framerateDivisor: 3 for the usual
    // 20 Hz, and the game itself lowers it in places. Read here rather than after
    // GameState_Update, so it is one frame stale - which costs nothing on a value
    // that changes about as often as a scene does, and saves patching the whole of
    // Graph_Update to get at it. Guarded because a zero would be a divide by zero
    // on any frame before the register file is initialised.
    {
        s32 update_rate = R_UPDATE_RATE;

        if (update_rate < 1) {
            update_rate = 3;
        }
        gEXSetRefreshRate(POLY_OPA_DISP++, 60 / update_rate);
    }

    CLOSE_DISPS(gfxCtx, "graphics_patches.c", 0);
}
