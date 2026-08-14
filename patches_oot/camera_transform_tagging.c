/*
 * Camera transform tagging for Ocarina of Time.
 * Counterpart to ../patches/camera_transform_tagging.c (Majora's Mask).
 *
 * The projection matrix is one transform, so it needs one id and one decision per
 * frame: did the camera *move* between the last frame and this one, or did it
 * *cut*? Interpolating through a cut smears the whole scene across the screen for
 * two frames, which is far more noticeable than the missing smoothness of not
 * interpolating a move.
 *
 * Nothing in the game says which happened. A camera setting change, a cutscene
 * boundary and a door transition all just write a new eye and at. So the same
 * heuristic Majora's Mask uses is used here: predict where eye and at should have
 * landed from last frame's velocity, and treat a large enough miss as a cut. The
 * thresholds are its numbers, arrived at by testing, and they transfer because the
 * two cameras are the same code with different names.
 *
 * G_EX_INTERPOLATE_SIMPLE (gEXMatrixGroupSimple) rather than the decomposed form
 * the actors use: a camera orbits a focus, and interpolating the matrix as a whole
 * follows an orbit correctly where interpolating decomposed position and rotation
 * separately does not.
 *
 * NOT PORTED, deliberately. Majora's Mask carries a list of per-scene workarounds
 * where the heuristic guesses wrong (the Pirates' Fortress moat switch, the Stone
 * Tower block puzzles, one frame of the Music Box House cutscene) and forces the
 * answer. OoT will have its own such places and they are found by playing, not by
 * translating MM's. force_camera_interpolation and force_camera_skip_interpolation
 * are exported and take effect on the next View_Apply, so each one is a one-line
 * fix once it is identified.
 *
 * The pause menu and file select are likewise not special-cased. Both drive a view
 * of their own whose eye jumps around every frame, so the heuristic answers "cut"
 * throughout and the menus simply do not interpolate, which is what they did at
 * the console refresh rate anyway.
 */
#include "patches.h"
#include "transform_ids.h"

#include "gfx.h"
#include "view.h"
#include "z_lib.h"

s32 View_ApplyPerspective(View* view);
s32 View_ApplyOrtho(View* view);

#define TAG_FILE "camera_transform_tagging.c"

static s32 camera_interpolation_forced = false;
static s32 camera_skip_interpolation_forced = false;

void force_camera_interpolation(void) {
    camera_interpolation_forced = true;
}

void force_camera_skip_interpolation(void) {
    camera_skip_interpolation_forced = true;
}

static s32 should_interpolate_perspective(Vec3f* eye, Vec3f* at) {
    static Vec3f prev_eye = { 0.0f, 0.0f, 0.0f };
    static Vec3f prev_at = { 0.0f, 0.0f, 0.0f };
    static Vec3f eye_velocity = { 0.0f, 0.0f, 0.0f };
    static Vec3f at_velocity = { 0.0f, 0.0f, 0.0f };

    Vec3f predicted_eye;
    Vec3f predicted_at;
    f32 eye_dist;
    f32 at_dist;
    f32 velocity_diff;

    /* Where last frame's motion said this frame would land. */
    Math_Vec3f_Sum(&prev_eye, &eye_velocity, &predicted_eye);
    Math_Vec3f_Sum(&prev_at, &at_velocity, &predicted_at);

    Math_Vec3f_Diff(eye, &prev_eye, &eye_velocity);
    Math_Vec3f_Diff(at, &prev_at, &at_velocity);

    eye_dist = Math_Vec3f_DistXYZ(&predicted_eye, eye);
    at_dist = Math_Vec3f_DistXYZ(&predicted_at, at);

    /* How differently the two points are moving. Equal velocities mean the camera
     * is being carried along as a unit rather than swinging. */
    velocity_diff = Math_Vec3f_DistXYZ(&eye_velocity, &at_velocity);

    prev_eye = *eye;
    prev_at = *at;

    if (velocity_diff <= 3.0f && eye_dist <= 100.0f && at_dist <= 100.0f) {
        return true;
    }

    /* One end held still and the other swung: a pan or an orbit, not a cut. */
    if (at_dist <= 10.0f && eye_dist <= 300.0f) {
        return true;
    }
    if (eye_dist <= 10.0f && at_dist <= 300.0f) {
        return true;
    }

    if (velocity_diff > 50.0f || at_dist > 50.0f || eye_dist > 300.0f) {
        /* A cut invalidates the velocity too; carrying it forward would make the
         * frame after the cut mispredict as well. */
        eye_velocity.x = eye_velocity.y = eye_velocity.z = 0.0f;
        at_velocity.x = at_velocity.y = at_velocity.z = 0.0f;
        return false;
    }

    return true;
}

RECOMP_PATCH s32 View_Apply(View* view, s32 mask) {
    s32 ret;
    // @recomp
    s32 interpolate_camera = false;
    GraphicsContext* gfxCtx;

    mask = (view->flags & mask) | (mask >> 4);

    if (mask & VIEW_PROJECTION_ORTHO) {
        ret = View_ApplyOrtho(view);
    } else {
        ret = View_ApplyPerspective(view);

        // @recomp Only a perspective view is a camera; an ortho one is UI, and is
        // in the same place every frame by construction.
        interpolate_camera = should_interpolate_perspective(&view->eye, &view->at);
    }

    // @recomp Overrides, for code that already knows what the heuristic is trying
    // to work out. Skip wins over force: it is the safe answer.
    if (camera_skip_interpolation_forced) {
        interpolate_camera = false;
    } else if (camera_interpolation_forced) {
        interpolate_camera = true;
    }

    camera_interpolation_forced = false;
    camera_skip_interpolation_forced = false;

    // @recomp Tag the projection matrix in both lists. The tile component keeps
    // interpolating either way so that scrolling textures do not stutter on the
    // frame a cut happens.
    gfxCtx = view->gfxCtx;
    OPEN_DISPS(gfxCtx, TAG_FILE, 0);

    if (interpolate_camera) {
        gEXMatrixGroupSimple(POLY_OPA_DISP++, CAMERA_TRANSFORM_ID, G_EX_NOPUSH, G_MTX_PROJECTION,
                             G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE,
                             G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE, G_EX_ORDER_LINEAR, G_EX_EDIT_NONE);
        gEXMatrixGroupSimple(POLY_XLU_DISP++, CAMERA_TRANSFORM_ID, G_EX_NOPUSH, G_MTX_PROJECTION,
                             G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE,
                             G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE, G_EX_ORDER_LINEAR, G_EX_EDIT_NONE);
    } else {
        gEXMatrixGroupSimple(POLY_OPA_DISP++, CAMERA_TRANSFORM_ID, G_EX_NOPUSH, G_MTX_PROJECTION,
                             G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP,
                             G_EX_COMPONENT_INTERPOLATE, G_EX_ORDER_LINEAR, G_EX_EDIT_NONE);
        gEXMatrixGroupSimple(POLY_XLU_DISP++, CAMERA_TRANSFORM_ID, G_EX_NOPUSH, G_MTX_PROJECTION,
                             G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP,
                             G_EX_COMPONENT_INTERPOLATE, G_EX_ORDER_LINEAR, G_EX_EDIT_NONE);
    }

    CLOSE_DISPS(gfxCtx, TAG_FILE, 0);

    return ret;
}
