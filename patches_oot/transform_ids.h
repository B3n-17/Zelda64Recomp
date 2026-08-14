#ifndef __OOT_TRANSFORM_IDS_H__
#define __OOT_TRANSFORM_IDS_H__

/*
 * Transform tagging for Ocarina of Time - the data half of the high framerate
 * modes. Counterpart to ../patches/transform_ids.h (Majora's Mask).
 *
 * The game keeps running at its native 20 Hz. Everything above that is RT64
 * rendering in-between frames, which it can only do if it can tell which matrix
 * in this frame is the same matrix as one in the previous frame. A display list
 * carries no such identity - two frames of Link's left forearm are just two
 * unrelated `gSPMatrix` commands at whatever address the gfx pool happened to
 * hand out - so the game has to supply one, and that is what a transform id is.
 *
 * gEXMatrixGroup* writes an id (plus a per-component interpolation policy) into
 * an opcode a real RDP treats as undefined. RT64 pairs matrices across frames by
 * that id. Untagged matrices fall back to RT64's own heuristics, which pair by
 * position in the display list and get it wrong the moment an actor is culled,
 * spawns, or draws a variable number of limbs - the failure looks like limbs
 * snapping between two poses, which is why OoT was pinned to the console refresh
 * rate until this existed.
 *
 * An id has to be stable for one thing across frames and never collide with
 * another thing's. Actors get theirs from the runtime's spawn counter rather than
 * from an address or a list index: an actor's slot in the arena is reused by the
 * next actor to spawn there, and its index in a category list shifts whenever a
 * neighbour dies. See actor_data.c.
 */

#include "ultra64.h"
#include "actor.h"

#include "rt64_extended_gbi.h"

/* ------------------------- interpolation policies -------------------------- */

/*
 * gEXMatrixGroupDecomposed takes one policy per decomposed component - position,
 * rotation, scale, skew, perspective, vertices, tiles - so a caller can say, for
 * one example, "follow this matrix's rotation but let its position jump". These
 * name the combinations that get used, and are copied from ../patches/patches.h
 * so both games spell the same policy the same way.
 */

/* Everything interpolates except the raw vertices, which for a rigid limb are
 * the same every frame and cost real work to compare. The default. */
#define gEXMatrixGroupDecomposedNormal(cmd, id, push, proj, edit) \
    gEXMatrixGroupDecomposed(cmd, id, push, proj, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_INTERPOLATE, G_EX_ORDER_LINEAR, edit)

/* Position and rotation hold still, the rest follows. For an actor that teleported
 * but should still scale and fade smoothly. */
#define gEXMatrixGroupDecomposedSkipPosRot(cmd, id, push, proj, edit) \
    gEXMatrixGroupDecomposed(cmd, id, push, proj, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_INTERPOLATE, G_EX_ORDER_LINEAR, edit)

/* Nothing follows but the texture tiles, so a scrolling texture still animates on
 * a transform that is deliberately frozen. */
#define gEXMatrixGroupDecomposedSkipAll(cmd, id, push, proj, edit) \
    gEXMatrixGroupDecomposed(cmd, id, push, proj, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_INTERPOLATE, G_EX_ORDER_LINEAR, edit)

/* As Normal, plus the vertices. For skinned meshes, whose vertex positions are
 * recomputed on the CPU every frame and genuinely differ between them. */
#define gEXMatrixGroupDecomposedVerts(cmd, id, push, proj, edit) \
    gEXMatrixGroupDecomposed(cmd, id, push, proj, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE, G_EX_ORDER_LINEAR, edit)

/* ------------------------------ the id space ------------------------------- */

/*
 * One flat u32 space shared by everything that tags. The low ids are singletons,
 * the actor range at the top is carved into a block per spawned actor.
 *
 * Majora's Mask reserves a great deal more of the low space (skyboxes, particles,
 * effects, item and UI groups - see ../patches/transform_ids.h). Only what this
 * tree actually emits is defined here; the gaps are deliberately left free so the
 * two games can keep the same numbers for the same things if the rest is ported.
 */

#define CAMERA_TRANSFORM_ID 0x10U

/*
 * 256 limbs is above every skeleton in the game (Link has 22), and each actor gets
 * that twice: one id per limb for the limb's own matrix, and a second for whatever
 * a PostLimbDraw callback draws hanging off it. A post-limb draw is a separate
 * transform - a held sword, a hookshot chain - and sharing the limb's id would
 * make RT64 interpolate the sword towards the forearm.
 */
#define ACTOR_TRANSFORM_LIMB_COUNT 256
#define ACTOR_TRANSFORM_ID_COUNT (ACTOR_TRANSFORM_LIMB_COUNT * 2)
#define ACTOR_TRANSFORM_ID_START 0x1000000U

/* --------------------------- actor extension data -------------------------- */

typedef u32 ActorExtensionId;

ActorExtensionId oot_extend_actor(s16 actor_id, u32 size);
ActorExtensionId oot_extend_actor_all(u32 size);
void* oot_get_extended_actor_data(Actor* actor, ActorExtensionId extension);
u32 oot_get_actor_spawn_index(Actor* actor);

ActorExtensionId actor_get_slot(Actor* actor);
void actor_set_slot(Actor* actor, ActorExtensionId slot);

/* The base of this actor's block of ids. Add a limb index to it. */
u32 actor_transform_id(Actor* actor);

/*
 * "This actor moved in a way no interpolation should follow." Set it during an
 * update, and the next Actor_Draw tags every matrix the actor writes as skipped
 * and clears the flag again. Nothing sets it generically - a warp and a fast
 * arrow are the same displacement to a heuristic - so it exists for specific
 * actors that know they teleported.
 */
u32 actor_get_interpolation_skipped(Actor* actor);
void actor_set_interpolation_skipped(Actor* actor);
void actor_clear_interpolation_skipped(Actor* actor);

void register_base_actor_extensions(void);

/* ------------------------------- the camera -------------------------------- */

/*
 * View_Apply decides per frame whether the camera moved or cut, by predicting
 * where eye and at should be from last frame's velocity and seeing how far off it
 * lands (camera_transform_tagging.c). These override that decision for the frame
 * they are called on, for code that already knows the answer.
 */
void force_camera_interpolation(void);
void force_camera_skip_interpolation(void);

/* The current actor being drawn by Actor_Draw, or NULL outside one. This is what
 * the SkelAnime patches tag against; see actor_transform_tagging.c for why it is
 * not the `void* arg` those functions are handed. */
Actor* transform_tagging_current_actor(void);

#endif
