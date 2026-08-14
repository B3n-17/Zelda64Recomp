/*
 * Widescreen coverage for the scene transition overlays.
 *
 * The circle ("shutter") and wipe transitions draw a fixed mesh through a
 * perspective camera that Init hardcodes to 4:3:
 *
 *     guPerspective(&this->projection, &this->normal, 60.0f, (4.0f / 3.0f), ...)
 *
 * Widescreen here is a wider field of view, not a stretched image - that is the
 * whole point of it, and it is what lets the player see more of the world at the
 * sides. But a mesh of fixed size at a fixed distance covers a fixed angle, so
 * widening the field of view shrinks how much of the screen it covers. The mesh
 * that exactly reached the edges at 4:3 falls short of them at 16:9, and the
 * scene being transitioned away from stays visible in two strips down the sides.
 *
 * Scaling the mesh by the same factor the field of view grew by restores the
 * coverage exactly. This is the fix the Majora's Mask tree already applies to its
 * own copy of this code in patches/effect_patches.c (TransitionWipe3_Draw is the
 * same function as TransitionCircle_Draw here, down to the 14.8f); the two games
 * share the transition system, so the port is mechanical.
 *
 * Not every transition needs this. TransitionFade fills with gDPFillRectangle
 * over gScreenWidth/gScreenHeight, and TransitionTriforce and TransitionTile draw
 * through guOrtho. Orthographic content is stretched to the wider screen rather
 * than being given more of the world to show, so it still covers the screen it
 * covered before, and scaling it would only make it too large.
 *
 * The two functions below are copies of the decomp originals with the lines
 * marked @recomp added, because RECOMP_PATCH replaces the whole function.
 */
#include "patches.h"

#include "transition_circle.h"
#include "transition_wipe.h"

#include "color.h"
#include "gfx.h"
#include "ultra64.h"

// Provided by the runtime; see patches_oot/syms.ld.
f32 recomp_get_target_aspect_ratio(f32 original);

// Defined in z_fbdemo_circle.c and z_fbdemo_wipe1.c. Ordinary globals rather than
// statics, so the recompiler resolves these against the base game by name.
extern Gfx sTransCircleDL[];
extern Gfx sTransWipeDL[];

// How much wider the view is than the 4:3 the transition meshes were built for.
// 1.0f whenever the player has left the aspect ratio at Original, which makes
// every scale below the value the vanilla code used.
static f32 transition_aspect_scale(void) {
    f32 original_aspect_ratio = ((f32)SCREEN_WIDTH) / ((f32)SCREEN_HEIGHT);

    return recomp_get_target_aspect_ratio(original_aspect_ratio) / original_aspect_ratio;
}

RECOMP_PATCH void TransitionCircle_Draw(void* thisx, Gfx** gfxP) {
    Gfx* gfx = *gfxP;
    Mtx* modelView;
    TransitionCircle* this = (TransitionCircle*)thisx;
    Gfx* texScroll;
    // These variables are a best guess based on the other transition types.
    f32 tPos = 0.0f;
    f32 rot = 0.0f;
    f32 scale = 14.8f;

    // @recomp Grow the mesh with the field of view so it still reaches the edges.
    scale *= transition_aspect_scale();

    modelView = this->modelView[this->frame];

    this->frame ^= 1;
    gDPPipeSync(gfx++);
    texScroll = Gfx_BranchTexScroll(&gfx, this->texX, this->texY, 16, 64);
    gSPSegment(gfx++, 9, texScroll);
    gSPSegment(gfx++, 8, this->texture);
    gDPSetColor(gfx++, G_SETPRIMCOLOR, this->color.rgba);
    gDPSetColor(gfx++, G_SETENVCOLOR, this->color.rgba);
    gSPMatrix(gfx++, &this->projection, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);
    gSPPerspNormalize(gfx++, this->normal);
    gSPMatrix(gfx++, &this->lookAt, G_MTX_NOPUSH | G_MTX_MUL | G_MTX_PROJECTION);

    if (scale != 1.0f) {
        guScale(&modelView[0], scale, scale, 1.0f);
        gSPMatrix(gfx++, &modelView[0], G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    }

    if (rot != 0.0f) {
        guRotate(&modelView[1], rot, 0.0f, 0.0f, 1.0f);
        gSPMatrix(gfx++, &modelView[1], G_MTX_NOPUSH | G_MTX_MUL | G_MTX_MODELVIEW);
    }

    if ((tPos != 0.0f) || (tPos != 0.0f)) {
        guTranslate(&modelView[2], tPos, tPos, 0.0f);
        gSPMatrix(gfx++, &modelView[2], G_MTX_NOPUSH | G_MTX_MUL | G_MTX_MODELVIEW);
    }
    gSPDisplayList(gfx++, sTransCircleDL);
    gDPPipeSync(gfx++);
    *gfxP = gfx;
}

RECOMP_PATCH void TransitionWipe_Draw(void* thisx, Gfx** gfxP) {
    Gfx* gfx = *gfxP;
    Mtx* modelView;
    TransitionWipe* this = (TransitionWipe*)thisx;
    Color_RGBA8_u32* color;
    s32 pad[3];
    Gfx* texScroll;

    // @recomp The wipe's scale is written inline rather than through a variable,
    // so the aspect scale is applied to the literal.
    f32 scale = 0.56f * transition_aspect_scale();

    modelView = this->modelView[this->frame];
    this->frame ^= 1;

    guScale(&modelView[0], scale, scale, 1.0f);
    guRotate(&modelView[1], 0.0f, 0.0f, 0.0f, 1.0f);
    guTranslate(&modelView[2], 0.0f, 0.0f, 0.0f);

    gDPPipeSync(gfx++);

    texScroll = Gfx_BranchTexScroll(&gfx, this->texX, this->texY, 0, 0);
    gSPSegment(gfx++, 8, texScroll);

    color = &this->color;
    gDPSetPrimColor(gfx++, 0, 0x80, color->r, color->g, color->b, 255);

    gSPMatrix(gfx++, &this->projection, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);
    gSPPerspNormalize(gfx++, this->normal);
    gSPMatrix(gfx++, &this->lookAt, G_MTX_NOPUSH | G_MTX_MUL | G_MTX_PROJECTION);
    gSPMatrix(gfx++, &modelView[0], G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPMatrix(gfx++, &modelView[1], G_MTX_NOPUSH | G_MTX_MUL | G_MTX_MODELVIEW);
    gSPMatrix(gfx++, &modelView[2], G_MTX_NOPUSH | G_MTX_MUL | G_MTX_MODELVIEW);
    gSPDisplayList(gfx++, sTransWipeDL);
    gDPPipeSync(gfx++);
    *gfxP = gfx;
}
