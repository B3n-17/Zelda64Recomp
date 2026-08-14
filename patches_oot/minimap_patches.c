/*
 * Fix for the pause-menu freeze caused by the minimap compass arrows.
 *
 * What goes wrong
 * ---------------
 * Opening the pause menu reuses the object bank for the menu's own buffers:
 * KaleidoScope_Update's PAUSE_STATE_INIT case sets
 *     pauseCtx->playerSegment = objectCtx.spaceStart + 0x30
 * and DMAs the icon textures over it (z_kaleido_scope.c:3725). gameplay_keep
 * lives at spaceStart, so everything segment 0x04 points at is replaced.
 *
 * Minimap_DrawCompassIcons draws gCompassArrowDL, which is a gameplay_keep asset
 * at segment offset 0xC820. Its guard in Minimap_Draw is
 * `pauseCtx.state <= PAUSE_STATE_INIT`, so it still runs on the frame that state
 * reaches PAUSE_STATE_INIT - one frame before the bank is overwritten.
 *
 * On hardware that is harmless: the RSP consumes the display list within the
 * frame, long before the CPU reaches the overwrite on the next one. Here the
 * renderer reads display lists from a separate thread and can reach that branch
 * after the bank has already been reused, at which point segment 0x04 resolves
 * into icon texture data. The interpreter then walks garbage until it leaves
 * RDRAM. Confirmed from the RT64 dumps: the branch
 *     0xDE000000 0x0400C820 -> gameplay_keep + 0xC820
 * landed on 0x01010101 rather than the expected 0xD7000000 (G_TEXTURE).
 *
 * The fix
 * -------
 * Skip the compass icons while the pause background prerender is running. This
 * is the guard the game already applies to the only other gameplay_keep draw in
 * the same area: z_parameter.c wraps Attention_Draw in exactly this condition,
 * two lines after it calls Minimap_Draw. Extending it to the compass icons is
 * consistent with what the game does for the same class of asset.
 *
 * Visually this costs nothing. The affected frames are the ones where the pause
 * background is being composited over the play view, so the minimap is being
 * covered anyway.
 */
#include "patches.h"

#include "gfx.h"
#include "gfx_setupdl.h"
#include "regs.h"
#include "sys_matrix.h"
#include "play_state.h"
#include "player.h"

// Declared directly rather than including
// assets/objects/gameplay_keep/compass_arrow.h, which is not on the patch include
// path. The recompiler resolves the symbol from the data symbol file.
extern Gfx gCompassArrowDL[];

extern s16 sPlayerInitialPosX;
extern s16 sPlayerInitialPosZ;
extern s16 sPlayerInitialDirection;

void Minimap_DrawCompassIcons(PlayState* play);

// @recomp Patched to skip the compass icons while the pause background prerender
// is active, since they reference gameplay_keep after the pause menu has taken
// that memory for its own buffers.
RECOMP_PATCH void Minimap_DrawCompassIcons(PlayState* play) {
    s32 pad;
    Player* player = GET_PLAYER(play);
    s16 tempX, tempZ;

    // @recomp The added guard. Everything below is the original function.
    if ((R_PAUSE_BG_PRERENDER_STATE == PAUSE_BG_PRERENDER_PROCESS) ||
        (R_PAUSE_BG_PRERENDER_STATE == PAUSE_BG_PRERENDER_READY)) {
        return;
    }

    OPEN_DISPS(play->state.gfxCtx, "../z_map_exp.c", 565);

    if (play->interfaceCtx.minimapAlpha >= 0xAA) {
        Gfx_SetupDL_42Overlay(play->state.gfxCtx);

        gSPMatrix(OVERLAY_DISP++, &gIdentityMtx, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gDPSetCombineLERP(OVERLAY_DISP++, PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0,
                          PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0);
        gDPSetEnvColor(OVERLAY_DISP++, 0, 0, 0, 255);
        gDPSetCombineMode(OVERLAY_DISP++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);

        tempX = player->actor.world.pos.x;
        tempZ = player->actor.world.pos.z;
        tempX /= R_COMPASS_SCALE_X;
        tempZ /= R_COMPASS_SCALE_Y;
        Matrix_Translate((R_COMPASS_OFFSET_X + tempX) / 10.0f, (R_COMPASS_OFFSET_Y - tempZ) / 10.0f, 0.0f, MTXMODE_NEW);
        Matrix_Scale(0.4f, 0.4f, 0.4f, MTXMODE_APPLY);
        Matrix_RotateX(-1.6f, MTXMODE_APPLY);
        tempX = (0x7FFF - player->actor.shape.rot.y) / 0x400;
        Matrix_RotateY(tempX / 10.0f, MTXMODE_APPLY);
        MATRIX_FINALIZE_AND_LOAD(OVERLAY_DISP++, play->state.gfxCtx, "../z_map_exp.c", 585);

        gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 200, 255, 0, 255);
        gSPDisplayList(OVERLAY_DISP++, gCompassArrowDL);

        tempX = sPlayerInitialPosX;
        tempZ = sPlayerInitialPosZ;
        tempX /= R_COMPASS_SCALE_X;
        tempZ /= R_COMPASS_SCALE_Y;
        Matrix_Translate((R_COMPASS_OFFSET_X + tempX) / 10.0f, (R_COMPASS_OFFSET_Y - tempZ) / 10.0f, 0.0f, MTXMODE_NEW);
        Matrix_Scale(VREG(9) / 100.0f, VREG(9) / 100.0f, VREG(9) / 100.0f, MTXMODE_APPLY);
        Matrix_RotateX(VREG(52) / 10.0f, MTXMODE_APPLY);
        Matrix_RotateY(sPlayerInitialDirection / 10.0f, MTXMODE_APPLY);
        MATRIX_FINALIZE_AND_LOAD(OVERLAY_DISP++, play->state.gfxCtx, "../z_map_exp.c", 603);

        gDPSetPrimColor(OVERLAY_DISP++, 0, 0xFF, 200, 0, 0, 255);
        gSPDisplayList(OVERLAY_DISP++, gCompassArrowDL);
    }

    CLOSE_DISPS(play->state.gfxCtx, "../z_map_exp.c", 607);
}
