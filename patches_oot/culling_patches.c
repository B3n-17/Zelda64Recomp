/*
 * Widescreen culling for Ocarina of Time, ported from patches/culling.c.
 *
 * The culling volume is defined in projected space against a frustum the game
 * assumes is 4:3. Widening the aspect ratio shows more of the world horizontally
 * than that frustum covers, so actors near the left and right edges test as
 * outside it and pop in and out as the camera moves.
 *
 * MM solves this by dropping the frustum test and keeping only the distance test,
 * rather than by widening the volume to match the current aspect ratio. That is
 * the right trade here too: the projected-space test would need the actual aspect
 * ratio threaded through it, and the volume is a performance optimisation whose
 * cost on a modern host is irrelevant. Distance culling is what actually bounds
 * how many actors draw, and it is kept intact.
 */
#include "patches.h"

#include "actor.h"
#include "play_state.h"

/**
 * Original is Actor_CullingVolumeTest in z_actor.c. The z bounds check is the
 * vanilla distance test, unchanged; the x/y frustum test that followed it is
 * deliberately gone.
 */
RECOMP_PATCH s32 Actor_CullingVolumeTest(PlayState* play, Actor* actor, Vec3f* projPos, f32 projW) {
    if ((projPos->z > -actor->cullingVolumeScale) &&
        (projPos->z < (actor->cullingVolumeDistance + actor->cullingVolumeScale))) {
        // @recomp The x/y frustum test lived here. Returning true unconditionally
        // once the actor is within range keeps everything the widened view can see.
        return true;
    }

    return false;
}
