/*
 * Skybox transform tagging for Ocarina of Time.
 * Counterpart to ../patches/sky_transform_tagging.c (Majora's Mask), minus its
 * star billboarding, which is a separate change.
 *
 * The skybox is drawn at the eye - Skybox_Draw's x/y/z are the camera position -
 * so its modelview matrix and the projection matrix encode the same motion, and
 * the two cancel: view * model reduces to the camera's rotation alone, which is
 * why a box a thousand units wide does not slide around as the camera walks.
 *
 * That cancellation is a property of the two matrices being the *same frame's*.
 * RT64 interpolates them separately, so they have to reach the same answer about
 * every frame or the cancellation stops holding in between. Untagged, this one
 * fell to G_EX_ID_AUTO, which pairs transforms by searching the previous frame's
 * compatible draw calls and scoring position, orientation and screen-space
 * distance (computeTransformMatch in rt64_game_frame.cpp). That is a heuristic
 * over a matrix whose translation is the camera position: it is not stable frame
 * to frame, and a frame where it does not pair is a frame where the sky holds
 * still while the projection keeps moving. On the file select, whose camera orbits
 * a 1414-unit radius at 200 binang a frame, that read as a residual shimmer left
 * over after the projection itself was fixed.
 *
 * A tag replaces the search with an equality test on an id, so the pairing is the
 * same every frame by construction. Decomposed rather than simple: unlike a camera
 * this is not an orbit, it is a translation plus the three rot fields, and those
 * are exactly the components the decomposed form interpolates. The error against
 * the projection's whole-matrix interpolation is second order in the per-frame
 * angle - about 1414 * (200/65536 * 2pi)^2 / 4, or a tenth of a unit against a
 * skybox a thousand units across.
 *
 * Following camera_was_skipped is what keeps a scene cut from smearing: on a cut
 * the projection is tagged frozen, and a sky that kept interpolating would swing
 * across the screen on its own for the two frames the camera did not.
 */
#include "patches.h"
#include "transform_ids.h"

#include "gfx.h"
#include "gfx_setupdl.h"
#include "sys_matrix.h"
#include "skybox.h"

#define TAG_FILE "../z_vr_box_draw.c"

extern Mtx* sSkyboxDrawMatrix;

/*
 * Verbatim copy of z_vr_box_draw.c's original plus the lines marked @recomp,
 * because RECOMP_PATCH replaces the whole function.
 */
RECOMP_PATCH void Skybox_Draw(SkyboxContext* skyboxCtx, GraphicsContext* gfxCtx, s16 skyboxId, s16 blend, f32 x,
                              f32 y, f32 z) {
    OPEN_DISPS(gfxCtx, TAG_FILE, 52);

    Gfx_SetupDL_40Opa(gfxCtx);

    gSPSegment(POLY_OPA_DISP++, 0x7, skyboxCtx->staticSegments[0]);
    gSPSegment(POLY_OPA_DISP++, 0x8, skyboxCtx->staticSegments[1]);
    gSPSegment(POLY_OPA_DISP++, 0x9, skyboxCtx->palettes);

    gDPSetPrimColor(POLY_OPA_DISP++, 0x00, 0x00, 0, 0, 0, blend);
    gSPTexture(POLY_OPA_DISP++, 0x8000, 0x8000, 0, G_TX_RENDERTILE, G_ON);

    // Prepare matrix
    sSkyboxDrawMatrix = GRAPH_ALLOC(gfxCtx, sizeof(Mtx));
    Matrix_Translate(x, y, z, MTXMODE_NEW);
    Matrix_Scale(1.0f, 1.0f, 1.0f, MTXMODE_APPLY);
    Matrix_RotateX(skyboxCtx->rot.x, MTXMODE_APPLY);
    Matrix_RotateY(skyboxCtx->rot.y, MTXMODE_APPLY);
    Matrix_RotateZ(skyboxCtx->rot.z, MTXMODE_APPLY);
    MATRIX_TO_MTX(sSkyboxDrawMatrix, TAG_FILE, 76);
    gSPMatrix(POLY_OPA_DISP++, sSkyboxDrawMatrix, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);

    // @recomp Give the skybox's matrix an id of its own, and have it reach the
    // same interpolate-or-cut answer the projection it is drawn under reached.
    if (camera_was_skipped()) {
        gEXMatrixGroupDecomposedSkipAll(POLY_OPA_DISP++, SKYBOX_TRANSFORM_ID, G_EX_PUSH, G_MTX_MODELVIEW,
                                        G_EX_EDIT_NONE);
    } else {
        gEXMatrixGroupDecomposedNormal(POLY_OPA_DISP++, SKYBOX_TRANSFORM_ID, G_EX_PUSH, G_MTX_MODELVIEW,
                                       G_EX_EDIT_NONE);
    }

    // Enable magic square RGB dithering and bilinear filtering
    gDPSetColorDither(POLY_OPA_DISP++, G_CD_MAGICSQ);
    gDPSetTextureFilter(POLY_OPA_DISP++, G_TF_BILERP);

    // All skyboxes use CI8 textures with an RGBA16 palette
    gDPLoadTLUT_pal256(POLY_OPA_DISP++, skyboxCtx->palettes[0]);
    gDPSetTextureLUT(POLY_OPA_DISP++, G_TT_RGBA16);

    // Enable texture filtering RDP pipeline stages for bilinear filtering
    gDPSetTextureConvert(POLY_OPA_DISP++, G_TC_FILT);

    if (skyboxCtx->drawType != SKYBOX_DRAW_128) {
        // 256x256 textures, per-face palettes
        // 2, 3 or 4 faces

        gSPDisplayList(POLY_OPA_DISP++, skyboxCtx->dListBuf[0]); // -z face upper
        gSPDisplayList(POLY_OPA_DISP++, skyboxCtx->dListBuf[1]); // -z face lower

        gDPPipeSync(POLY_OPA_DISP++);
        gDPLoadTLUT_pal256(POLY_OPA_DISP++, skyboxCtx->palettes[1]);
        gSPDisplayList(POLY_OPA_DISP++, skyboxCtx->dListBuf[2]); // +x face upper
        gSPDisplayList(POLY_OPA_DISP++, skyboxCtx->dListBuf[3]); // +x face lower

        if (skyboxId != SKYBOX_BAZAAR) {
            if (skyboxId < SKYBOX_KOKIRI_SHOP || skyboxId > SKYBOX_BOMBCHU_SHOP) {
                // Skip remaining faces for most shop skyboxes

                gDPPipeSync(POLY_OPA_DISP++);
                gDPLoadTLUT_pal256(POLY_OPA_DISP++, skyboxCtx->palettes[2]);
                gSPDisplayList(POLY_OPA_DISP++, skyboxCtx->dListBuf[4]); // +z face upper
                gSPDisplayList(POLY_OPA_DISP++, skyboxCtx->dListBuf[5]); // +z face lower

                // Note this pipesync is slightly misplaced and would be better off inside the condition
                gDPPipeSync(POLY_OPA_DISP++);

                if (skyboxCtx->drawType != SKYBOX_DRAW_256_3FACE) {
                    gDPLoadTLUT_pal256(POLY_OPA_DISP++, skyboxCtx->palettes[3]);
                    gSPDisplayList(POLY_OPA_DISP++, skyboxCtx->dListBuf[6]); // -x face upper
                    gSPDisplayList(POLY_OPA_DISP++, skyboxCtx->dListBuf[7]); // -x face lower
                }
            }
        }
    } else {
        // 128x128 and 128x64 textures
        // 5 or 6 faces

        // Draw each face
        gSPDisplayList(POLY_OPA_DISP++, skyboxCtx->dListBuf[0]); // -z face
        gSPDisplayList(POLY_OPA_DISP++, skyboxCtx->dListBuf[2]); // +z face
        gSPDisplayList(POLY_OPA_DISP++, skyboxCtx->dListBuf[4]); // -x face
        gSPDisplayList(POLY_OPA_DISP++, skyboxCtx->dListBuf[6]); // +x face
        gSPDisplayList(POLY_OPA_DISP++, skyboxCtx->dListBuf[8]); // +y face
        if (skyboxId == SKYBOX_CUTSCENE_MAP) {
            // Skip the bottom face in the cutscene map
            gSPDisplayList(POLY_OPA_DISP++, skyboxCtx->dListBuf[10]); // -y face
        }
    }

    gDPPipeSync(POLY_OPA_DISP++);

    // @recomp Pop the skybox's matrix tag. Paired with the G_EX_PUSH above; the
    // group has to close before anything else in POLY_OPA is tagged.
    gEXPopMatrixGroup(POLY_OPA_DISP++, G_MTX_MODELVIEW);

    CLOSE_DISPS(gfxCtx, TAG_FILE, 125);
}
