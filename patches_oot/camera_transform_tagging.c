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
 * File select is special-cased, because leaving it to the heuristic is actively
 * wrong rather than merely unsmooth. FileSelect_*Draw applies three perspective
 * views per frame: one at the orbiting eye that draws the sky, then two at
 * (0, 0, 64) for the menu geometry. Feeding that alternation to a predictor keyed
 * on one previous position makes the orbit view miss by its full ~1414-unit radius
 * every frame, so the sky's projection is tagged "cut" and freezes at 20 Hz - while
 * the skybox's own modelview matrix, which is untagged and so falls to RT64's
 * G_EX_ID_AUTO matching, keeps interpolating and follows the eye smoothly. Two
 * transforms that have to agree disagreeing at 20 Hz is what the jitter is; it is
 * worse than either answer applied consistently. FileSelect_SetView below forces
 * the interpolating answer and, via force_camera_ignore_tracking, keeps its three
 * views out of the predictor's state entirely so they cannot poison the gameplay
 * camera's history either. This is what ../patches/camera_transform_tagging.c does.
 *
 * The pause menu is still not special-cased. KaleidoScope_SetView has the same
 * shape and MM patches it the same way, but nothing there is paired with an
 * untagged transform the way the sky is, so it is choppy rather than wrong.
 */
#include "patches.h"
#include "transform_ids.h"

#include "gfx.h"
#include "view.h"
#include "z_lib.h"

#include "file_select_state.h"

s32 View_ApplyPerspective(View* view);
s32 View_ApplyOrtho(View* view);

#define TAG_FILE "camera_transform_tagging.c"

static s32 camera_interpolation_forced = false;
static s32 camera_skip_interpolation_forced = false;
static s32 camera_ignore_tracking = false;

void force_camera_interpolation(void) {
    camera_interpolation_forced = true;
}

void force_camera_skip_interpolation(void) {
    camera_skip_interpolation_forced = true;
}

void force_camera_ignore_tracking(void) {
    camera_ignore_tracking = true;
}

/*
 * The verdict the last View_Apply reached, for transforms drawn under that camera
 * that have to make the same call. The skybox is the one that does: it is drawn at
 * the eye, so its matrix and the projection describe the same motion and have to
 * be interpolated or not together. Read it after the view is applied and before
 * the next one, which is where Skybox_Draw sits.
 */
static s32 camera_skipped = false;

s32 camera_was_skipped(void) {
    return camera_skipped;
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
        // in the same place every frame by construction. A view that asked to be
        // ignored is left out of the predictor's history as well as its verdict,
        // so a menu drawing several views a frame does not overwrite the position
        // the gameplay camera is being predicted from.
        if (!camera_ignore_tracking) {
            interpolate_camera = should_interpolate_perspective(&view->eye, &view->at);
        }
    }

    camera_ignore_tracking = false;

    // @recomp Overrides, for code that already knows what the heuristic is trying
    // to work out. Skip wins over force: it is the safe answer.
    if (camera_skip_interpolation_forced) {
        interpolate_camera = false;
    } else if (camera_interpolation_forced) {
        interpolate_camera = true;
    }

    camera_interpolation_forced = false;
    camera_skip_interpolation_forced = false;

    // @recomp Publish the verdict for anything drawn under this view that has to
    // match it. An ortho view leaves this reading "skipped", since it never
    // reaches a verdict of its own; that is harmless because the one reader,
    // Skybox_Draw, is always preceded by the perspective view it belongs to.
    camera_skipped = !interpolate_camera;

    // @recomp Tag the projection matrix in both lists. The tile component keeps
    // interpolating either way so that scrolling textures do not stutter on the
    // frame a cut happens.
    gfxCtx = view->gfxCtx;
    OPEN_DISPS(gfxCtx, TAG_FILE, 0);

    if (interpolate_camera) {
        gEXMatrixGroupSimple(POLY_OPA_DISP++, CAMERA_TRANSFORM_ID, G_EX_NOPUSH, G_MTX_PROJECTION,
                             G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE,
                             G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE, G_EX_ORDER_LINEAR, G_EX_EDIT_NONE,
                             G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP);
        gEXMatrixGroupSimple(POLY_XLU_DISP++, CAMERA_TRANSFORM_ID, G_EX_NOPUSH, G_MTX_PROJECTION,
                             G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE,
                             G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE, G_EX_ORDER_LINEAR, G_EX_EDIT_NONE,
                             G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP);
    } else {
        gEXMatrixGroupSimple(POLY_OPA_DISP++, CAMERA_TRANSFORM_ID, G_EX_NOPUSH, G_MTX_PROJECTION,
                             G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP,
                             G_EX_COMPONENT_INTERPOLATE, G_EX_ORDER_LINEAR, G_EX_EDIT_NONE,
                             G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP);
        gEXMatrixGroupSimple(POLY_XLU_DISP++, CAMERA_TRANSFORM_ID, G_EX_NOPUSH, G_MTX_PROJECTION,
                             G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP,
                             G_EX_COMPONENT_INTERPOLATE, G_EX_ORDER_LINEAR, G_EX_EDIT_NONE,
                             G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP);
    }

    CLOSE_DISPS(gfxCtx, TAG_FILE, 0);

    return ret;
}

/*
 * Verbatim copy of z_file_choose.c's original, plus the two @recomp lines, because
 * RECOMP_PATCH replaces the whole function.
 *
 * All three of the views this applies per frame get the same treatment. The orbit
 * view is the one that matters - it is the sky's projection, and the one the
 * predictor gets wrong - but the menu views are applied from the same function, and
 * letting them keep writing the predictor's prev_eye is exactly what makes the
 * orbit view mispredict. Both halves have to go together.
 */
RECOMP_PATCH void FileSelect_SetView(FileSelectState* this, f32 eyeX, f32 eyeY, f32 eyeZ) {
    Vec3f eye;
    Vec3f lookAt;
    Vec3f up;

    eye.x = eyeX;
    eye.y = eyeY;
    eye.z = eyeZ;

    lookAt.x = lookAt.y = lookAt.z = 0.0f;

    up.x = up.z = 0.0f;
    up.y = 1.0f;

    // @recomp The file select drives its own camera; the heuristic has nothing to
    // work out here and gets it wrong when it tries.
    force_camera_interpolation();
    force_camera_ignore_tracking();

    View_LookAt(&this->view, &eye, &lookAt, &up);
    View_Apply(&this->view, VIEW_ALL | VIEW_FORCE_VIEWING | VIEW_FORCE_VIEWPORT | VIEW_FORCE_PROJECTION_PERSPECTIVE);
}
