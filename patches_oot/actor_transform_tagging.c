/*
 * Transform tagging for actors and their skeletons, Ocarina of Time.
 * Counterpart to ../patches/actor_transform_tagging.c (Majora's Mask).
 *
 * Three things happen here.
 *
 * 1. ACTOR IDENTITY. Actor_Init asks the runtime for an extension-data handle and
 *    Actor_Delete gives it back, so every live actor carries a spawn index that is
 *    unique for the run. actor_data.c explains where the handle is kept and why
 *    the spawn index is what an id is derived from.
 *
 * 2. SKELETONS. Every SkelAnime draw entry point is replaced by a copy of itself
 *    with a matrix group pushed around each limb's matrix, and a second one around
 *    whatever the limb's PostLimbDraw callback writes. That is what makes an
 *    animation interpolate limb by limb instead of the whole actor rigidly.
 *
 * 3. EVERYTHING ELSE AN ACTOR DRAWS. Actor_Draw reserves two no-ops in each of
 *    polyOpa and polyXlu before calling the actor's draw function, then counts the
 *    matrices the actor wrote. If it wrote exactly one per list - which is the
 *    common case for an actor with a static model - the reserved slot is filled in
 *    with a group for it. Actors that write several matrices are left alone rather
 *    than guessed at.
 *
 * WHY THE `void* arg` IS NOT USED AS THE ACTOR. Majora's Mask declares these
 * functions as taking `Actor* actor` and tags against it. OoT declares the same
 * parameter `void* arg`, and it is genuinely not always an actor: Player is drawn
 * through Player_DrawImpl, which passes a local struct of its own
 * (src/code/z_player_lib.c), and other callers pass NULL. Reading a spawn handle
 * out of whatever that points at would read four bytes of somebody's locals.
 *
 * So the actor comes from Actor_Draw instead, which is the only place a skeleton
 * is ever drawn from and already has it. It is saved and restored around the draw
 * call rather than just cleared, because an actor drawing another actor is a thing
 * that happens.
 */
#include "patches.h"
#include "transform_ids.h"

/* Trimmed to what the copied function bodies reference; OoT's decomp has no
 * monolithic global.h. Mirrors the includes of z_actor.c, z_skelanime.c and
 * z_skin.c. */
#include "libu64/overlay.h"
#include "array_count.h"
#include "attributes.h"
#include "fault.h"
#include "gfx.h"
#include "segmented_address.h"
#include "sys_matrix.h"
#include "sfx.h"
#include "z_actor_dlftbls.h"
#include "z_lib.h"
#include "zelda_arena.h"
#include "actor.h"
#include "animation.h"
#include "audio.h"
#include "light.h"
#include "play_state.h"
#include "player.h"
#include "skin.h"
#include "skin_matrix.h"

void recomp_clear_all_actor_data(void);
u32 recomp_create_actor_data(u32 actor_type);
void recomp_destroy_actor_data(u32 actor_handle);

/* Declared in no header - z_actor.c, z_eff_ss_dead.c and z_skin.c keep these to
 * themselves - but all are real global symbols the recompiler can resolve. */
void Actor_SetWorldToHome(Actor* actor);
void Actor_SetShapeRotToWorld(Actor* actor);
void Actor_Destroy(Actor* actor, PlayState* play);
Actor* Actor_RemoveFromCategory(PlayState* play, ActorContext* actorCtx, Actor* actorToRemove);
void Actor_FreeOverlay(ActorOverlay* entry);
void Actor_FaultPrint(Actor* actor, char* command);
void func_80026400(PlayState* play, Color_RGBA8* color, s16 arg2, s16 arg3);
void func_80026608(PlayState* play);
void func_80026860(PlayState* play, Color_RGBA8* color, s16 arg2, s16 arg3);
void func_80026A6C(PlayState* play);
void func_80030488(PlayState* play);
extern MtxF gSkinLimbMatrices[60];

#define TAG_FILE "actor_transform_tagging.c"

/* ---------------------------- the current actor ---------------------------- */

/*
 * Resolved once per actor per frame in Actor_Draw rather than per limb. Both the
 * id and the skip flag cost a locked lookup in the runtime's slot map, and a
 * crowded room draws a few thousand limbs.
 */
static Actor* sTaggingActor = NULL;
static u32 sTaggingTransformId = 0;
static s32 sTaggingSkipped = false;

Actor* transform_tagging_current_actor(void) {
    return sTaggingActor;
}

/* 0 means "do not tag": either there is no actor being drawn, or it has no valid
 * spawn handle (see actor_transform_id). Push and pop both go through this, so
 * they can never disagree and leave the group stack unbalanced. */
static u32 tagging_id_for(Actor* actor) {
    return ((actor != NULL) && (actor == sTaggingActor)) ? sTaggingTransformId : 0;
}

/* --------------------------- group push and pop ---------------------------- */

/*
 * G_EX_PUSH here is the *group* stack, not the RSP's matrix stack - it opens a
 * scope that gEXPopMatrixGroup closes, and every matrix loaded inside it inherits
 * the id. G_EX_EDIT_ALLOW lets RT64 rewrite the matrix in place when it produces
 * an in-between frame, which is the whole point.
 *
 * Each returns the display list pointer so a caller that is writing to a bare
 * `Gfx*` (the SkelAnime_Draw family) and one that is writing to POLY_OPA_DISP can
 * use the same helper.
 */

static Gfx* push_limb_matrix_group(Gfx* dlist, Actor* actor, u32 limb_index) {
    u32 cur_transform_id = tagging_id_for(actor);

    if (cur_transform_id != 0) {
        if (sTaggingSkipped) {
            gEXMatrixGroupDecomposedSkipAll(dlist++, cur_transform_id + limb_index, G_EX_PUSH, G_MTX_MODELVIEW,
                                            G_EX_EDIT_NONE);
        } else {
            gEXMatrixGroupDecomposedNormal(dlist++, cur_transform_id + limb_index, G_EX_PUSH, G_MTX_MODELVIEW,
                                           G_EX_EDIT_ALLOW);
        }
    }
    return dlist;
}

/* The second half of the actor's id block. A held item or a chain drawn by a
 * PostLimbDraw is its own transform; sharing the limb's id would have RT64
 * interpolate the two towards each other. */
static Gfx* push_post_limb_matrix_group(Gfx* dlist, Actor* actor, u32 limb_index) {
    u32 cur_transform_id = tagging_id_for(actor);

    if (cur_transform_id != 0) {
        if (sTaggingSkipped) {
            gEXMatrixGroupDecomposedSkipAll(dlist++, cur_transform_id + limb_index, G_EX_PUSH, G_MTX_MODELVIEW,
                                            G_EX_EDIT_NONE);
        } else {
            gEXMatrixGroupDecomposedNormal(dlist++, cur_transform_id + limb_index + ACTOR_TRANSFORM_LIMB_COUNT,
                                           G_EX_PUSH, G_MTX_MODELVIEW, G_EX_EDIT_ALLOW);
        }
    }
    return dlist;
}

/* Skinned limbs get the vertex component interpolated as well, because their
 * vertices are recomputed on the CPU every frame rather than being a fixed mesh
 * under a moving matrix. */
static Gfx* push_skin_limb_matrix_group(Gfx* dlist, Actor* actor, u32 limb_index) {
    u32 cur_transform_id = tagging_id_for(actor);

    if (cur_transform_id != 0) {
        if (sTaggingSkipped) {
            gEXMatrixGroupDecomposedSkipAll(dlist++, cur_transform_id + limb_index, G_EX_PUSH, G_MTX_MODELVIEW,
                                            G_EX_EDIT_NONE);
        } else {
            gEXMatrixGroupDecomposedVerts(dlist++, cur_transform_id + limb_index, G_EX_PUSH, G_MTX_MODELVIEW,
                                          G_EX_EDIT_ALLOW);
        }
    }
    return dlist;
}

static Gfx* pop_matrix_group(Gfx* dlist, Actor* actor) {
    if (tagging_id_for(actor) != 0) {
        gEXPopMatrixGroup(dlist++, G_MTX_MODELVIEW);
    }
    return dlist;
}

/* ------------------------------ actor lifetime ----------------------------- */

/*
 * Verbatim copies of the decomp originals with the marked lines added, because
 * RECOMP_PATCH replaces the whole function.
 */

RECOMP_PATCH void Actor_Init(Actor* actor, PlayState* play) {
    // @recomp Allocate the actor's extension data before anything can look for it.
    actor_set_slot(actor, recomp_create_actor_data(actor->id));

    Actor_SetWorldToHome(actor);
    Actor_SetShapeRotToWorld(actor);
    Actor_SetFocus(actor, 0.0f);
    Math_Vec3f_Copy(&actor->prevPos, &actor->world.pos);
    Actor_SetScale(actor, 0.01f);
    actor->attentionRangeType = ATTENTION_RANGE_3;
    actor->minVelocityY = -20.0f;
    actor->xyzDistToPlayerSq = MAXFLOAT;
    actor->naviEnemyId = NAVI_ENEMY_NONE;
    actor->cullingVolumeDistance = 1000.0f;
    actor->cullingVolumeScale = 350.0f;
    actor->cullingVolumeDownward = 700.0f;
    CollisionCheck_InitInfo(&actor->colChkInfo);
    actor->floorBgId = BGCHECK_SCENE;
    ActorShape_Init(&actor->shape, 0.0f, NULL, 0.0f);
    if (Object_IsLoaded(&play->objectCtx, actor->objectSlot)) {
        Actor_SetObjectDependency(play, actor);
        actor->init(actor, play);
        actor->init = NULL;
    }
}

/*
 * Note the ordering: the handle is released after Actor_Destroy has run, so an
 * actor's destroy callback can still reach its own extension data, and before
 * ZeldaArena_Free, so the bytes holding the handle are still ours to read.
 */
RECOMP_PATCH Actor* Actor_Delete(ActorContext* actorCtx, Actor* actor, PlayState* play) {
    PlayState* play2 = (PlayState*)play;
    Player* player;
    Actor* newHead;
    ActorOverlay* overlayEntry;

    player = GET_PLAYER(play);

    overlayEntry = actor->overlayEntry;

    if ((player != NULL) && (player->focusActor == actor)) {
        Player_ReleaseLockOn(player);
        Camera_RequestMode(Play_GetCamera(play2, Play_GetActiveCamId(play2)), CAM_MODE_NORMAL);
    }

    if (actorCtx->attention.naviHoverActor == actor) {
        actorCtx->attention.naviHoverActor = NULL;
    }

    if (actorCtx->attention.forcedLockOnActor == actor) {
        actorCtx->attention.forcedLockOnActor = NULL;
    }

    if (actorCtx->attention.bgmEnemy == actor) {
        actorCtx->attention.bgmEnemy = NULL;
    }

    Audio_StopSfxByPos(&actor->projectedPos);
    Actor_Destroy(actor, play2);

    newHead = Actor_RemoveFromCategory(play2, actorCtx, actor);

    // @recomp Release the actor's extension data.
    recomp_destroy_actor_data(actor_get_slot(actor));

    ZELDA_ARENA_FREE(actor, "../z_actor.c", 7242);

    if (overlayEntry->vramStart != NULL) {
        overlayEntry->numLoaded--;
        Actor_FreeOverlay(overlayEntry);
    }

    return newHead;
}

/*
 * OoT's Actor_CleanupContext, which the decomp still carries under its ROM name.
 * Every actor is deleted individually just above, so this only has to reset the
 * runtime's spawn counter - which matters, because it is what keeps ids from
 * climbing without bound across a long session.
 */
RECOMP_PATCH void func_80031C3C(ActorContext* actorCtx, PlayState* play) {
    Actor* actor;
    s32 i;

    for (i = 0; i < ARRAY_COUNT(actorCtx->actorLists); i++) {
        actor = actorCtx->actorLists[i].head;
        while (actor != NULL) {
            Actor_Delete(actorCtx, actor, play);
            actor = actorCtx->actorLists[i].head;
        }
    }

    if (actorCtx->absoluteSpace != NULL) {
        ZELDA_ARENA_FREE(actorCtx->absoluteSpace, "../z_actor.c", 6731);
        actorCtx->absoluteSpace = NULL;
    }

    // @recomp Reset the actor extension data. Every actor above released its own
    // handle; this is what resets the spawn counter, and so what keeps transform
    // ids from climbing across a long session.
    recomp_clear_all_actor_data();

    Play_SaveSceneFlags(play);
    func_80030488(play);
    ActorOverlayTable_Cleanup();
}

/* --------------------------------- skeletons -------------------------------- */

RECOMP_PATCH void SkelAnime_DrawLimbLod(PlayState* play, s32 limbIndex, void** skeleton, Vec3s* jointTable,
                                        OverrideLimbDrawOpa overrideLimbDraw, PostLimbDrawOpa postLimbDraw, void* arg,
                                        s32 lod) {
    LodLimb* limb;
    Gfx* dList;
    Vec3f pos;
    Vec3s rot;
    // @recomp The actor this skeleton belongs to; see the file header.
    Actor* actor = sTaggingActor;

    OPEN_DISPS(play->state.gfxCtx, TAG_FILE, 0);

    Matrix_Push();
    limb = (LodLimb*)SEGMENTED_TO_VIRTUAL(skeleton[limbIndex]);
    limbIndex++;
    rot = jointTable[limbIndex];

    pos.x = limb->jointPos.x;
    pos.y = limb->jointPos.y;
    pos.z = limb->jointPos.z;

    dList = limb->dLists[lod];

    // @recomp Push the limb's matrix group.
    POLY_OPA_DISP = push_limb_matrix_group(POLY_OPA_DISP, actor, limbIndex);

    if ((overrideLimbDraw == NULL) || !overrideLimbDraw(play, limbIndex, &dList, &pos, &rot, arg)) {
        Matrix_TranslateRotateZYX(&pos, &rot);
        if (dList != NULL) {
            MATRIX_FINALIZE_AND_LOAD(POLY_OPA_DISP++, play->state.gfxCtx, TAG_FILE, 0);
            gSPDisplayList(POLY_OPA_DISP++, dList);
        }
    }

    // @recomp Close the limb's group and open one for the post-limb draw.
    POLY_OPA_DISP = pop_matrix_group(POLY_OPA_DISP, actor);
    POLY_OPA_DISP = push_post_limb_matrix_group(POLY_OPA_DISP, actor, limbIndex);

    if (postLimbDraw != NULL) {
        postLimbDraw(play, limbIndex, &dList, &rot, arg);
    }

    // @recomp Close the post-limb group.
    POLY_OPA_DISP = pop_matrix_group(POLY_OPA_DISP, actor);

    if (limb->child != LIMB_DONE) {
        SkelAnime_DrawLimbLod(play, limb->child, skeleton, jointTable, overrideLimbDraw, postLimbDraw, arg, lod);
    }

    Matrix_Pop();

    if (limb->sibling != LIMB_DONE) {
        SkelAnime_DrawLimbLod(play, limb->sibling, skeleton, jointTable, overrideLimbDraw, postLimbDraw, arg, lod);
    }

    CLOSE_DISPS(play->state.gfxCtx, TAG_FILE, 0);
}

RECOMP_PATCH void SkelAnime_DrawLod(PlayState* play, void** skeleton, Vec3s* jointTable,
                                    OverrideLimbDrawOpa overrideLimbDraw, PostLimbDrawOpa postLimbDraw, void* arg,
                                    s32 lod) {
    LodLimb* rootLimb;
    s32 pad;
    Gfx* dList;
    Vec3f pos;
    Vec3s rot;
    // @recomp
    Actor* actor = sTaggingActor;

    if (skeleton == NULL) {
        return;
    }

    OPEN_DISPS(play->state.gfxCtx, TAG_FILE, 0);

    Matrix_Push();

    rootLimb = (LodLimb*)SEGMENTED_TO_VIRTUAL(skeleton[0]);
    pos.x = jointTable[0].x;
    pos.y = jointTable[0].y;
    pos.z = jointTable[0].z;

    rot = jointTable[1];
    dList = rootLimb->dLists[lod];

    // @recomp Push the root limb's matrix group.
    POLY_OPA_DISP = push_limb_matrix_group(POLY_OPA_DISP, actor, 0);

    if ((overrideLimbDraw == NULL) || !overrideLimbDraw(play, 1, &dList, &pos, &rot, arg)) {
        Matrix_TranslateRotateZYX(&pos, &rot);
        if (dList != NULL) {
            MATRIX_FINALIZE_AND_LOAD(POLY_OPA_DISP++, play->state.gfxCtx, TAG_FILE, 0);
            gSPDisplayList(POLY_OPA_DISP++, dList);
        }
    }

    // @recomp
    POLY_OPA_DISP = pop_matrix_group(POLY_OPA_DISP, actor);
    POLY_OPA_DISP = push_post_limb_matrix_group(POLY_OPA_DISP, actor, 0);

    if (postLimbDraw != NULL) {
        postLimbDraw(play, 1, &dList, &rot, arg);
    }

    // @recomp
    POLY_OPA_DISP = pop_matrix_group(POLY_OPA_DISP, actor);

    if (rootLimb->child != LIMB_DONE) {
        SkelAnime_DrawLimbLod(play, rootLimb->child, skeleton, jointTable, overrideLimbDraw, postLimbDraw, arg, lod);
    }

    Matrix_Pop();

    CLOSE_DISPS(play->state.gfxCtx, TAG_FILE, 0);
}

RECOMP_PATCH void SkelAnime_DrawFlexLimbLod(PlayState* play, s32 limbIndex, void** skeleton, Vec3s* jointTable,
                                            OverrideLimbDrawOpa overrideLimbDraw, PostLimbDrawOpa postLimbDraw,
                                            void* arg, s32 lod, Mtx** mtx) {
    LodLimb* limb;
    Gfx* newDList;
    Gfx* limbDList;
    Vec3f pos;
    Vec3s rot;
    // @recomp
    Actor* actor = sTaggingActor;

    // @recomp The original opens the display list only inside the branch that
    // draws; the groups have to be written whether or not this limb has a mesh,
    // so the whole body is wrapped instead.
    OPEN_DISPS(play->state.gfxCtx, TAG_FILE, 0);

    Matrix_Push();

    limb = (LodLimb*)SEGMENTED_TO_VIRTUAL(skeleton[limbIndex]);
    limbIndex++;

    rot = jointTable[limbIndex];

    pos.x = limb->jointPos.x;
    pos.y = limb->jointPos.y;
    pos.z = limb->jointPos.z;

    newDList = limbDList = limb->dLists[lod];

    // @recomp
    POLY_OPA_DISP = push_limb_matrix_group(POLY_OPA_DISP, actor, limbIndex);

    if ((overrideLimbDraw == NULL) || !overrideLimbDraw(play, limbIndex, &newDList, &pos, &rot, arg)) {
        Matrix_TranslateRotateZYX(&pos, &rot);
        if (newDList != NULL) {
            MATRIX_TO_MTX(*mtx, TAG_FILE, 0);
            gSPMatrix(POLY_OPA_DISP++, *mtx, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPDisplayList(POLY_OPA_DISP++, newDList);
            (*mtx)++;
        } else if (limbDList != NULL) {
            MATRIX_TO_MTX(*mtx, TAG_FILE, 0);
            (*mtx)++;
        }
    }

    // @recomp
    POLY_OPA_DISP = pop_matrix_group(POLY_OPA_DISP, actor);
    POLY_OPA_DISP = push_post_limb_matrix_group(POLY_OPA_DISP, actor, limbIndex);

    if (postLimbDraw != NULL) {
        postLimbDraw(play, limbIndex, &limbDList, &rot, arg);
    }

    // @recomp
    POLY_OPA_DISP = pop_matrix_group(POLY_OPA_DISP, actor);

    if (limb->child != LIMB_DONE) {
        SkelAnime_DrawFlexLimbLod(play, limb->child, skeleton, jointTable, overrideLimbDraw, postLimbDraw, arg, lod,
                                  mtx);
    }

    Matrix_Pop();

    if (limb->sibling != LIMB_DONE) {
        SkelAnime_DrawFlexLimbLod(play, limb->sibling, skeleton, jointTable, overrideLimbDraw, postLimbDraw, arg, lod,
                                  mtx);
    }

    CLOSE_DISPS(play->state.gfxCtx, TAG_FILE, 0);
}

RECOMP_PATCH void SkelAnime_DrawFlexLod(PlayState* play, void** skeleton, Vec3s* jointTable, s32 dListCount,
                                        OverrideLimbDrawOpa overrideLimbDraw, PostLimbDrawOpa postLimbDraw, void* arg,
                                        s32 lod) {
    LodLimb* rootLimb;
    s32 pad;
    Gfx* newDList;
    Gfx* limbDList;
    Vec3f pos;
    Vec3s rot;
    Mtx* mtx = GRAPH_ALLOC(play->state.gfxCtx, dListCount * sizeof(Mtx));
    // @recomp
    Actor* actor = sTaggingActor;

    if (skeleton == NULL) {
        return;
    }

    OPEN_DISPS(play->state.gfxCtx, TAG_FILE, 0);

    gSPSegment(POLY_OPA_DISP++, 0xD, mtx);
    Matrix_Push();

    rootLimb = (LodLimb*)SEGMENTED_TO_VIRTUAL(skeleton[0]);
    pos.x = jointTable[0].x;
    pos.y = jointTable[0].y;
    pos.z = jointTable[0].z;

    rot = jointTable[1];

    newDList = limbDList = rootLimb->dLists[lod];

    // @recomp
    POLY_OPA_DISP = push_limb_matrix_group(POLY_OPA_DISP, actor, 0);

    if ((overrideLimbDraw == NULL) || !overrideLimbDraw(play, 1, &newDList, &pos, &rot, arg)) {
        Matrix_TranslateRotateZYX(&pos, &rot);
        if (newDList != NULL) {
            MATRIX_TO_MTX(mtx, TAG_FILE, 0);
            gSPMatrix(POLY_OPA_DISP++, mtx, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPDisplayList(POLY_OPA_DISP++, newDList);
            mtx++;
        } else if (limbDList != NULL) {
            MATRIX_TO_MTX(mtx, TAG_FILE, 0);
            mtx++;
        }
    }

    // @recomp
    POLY_OPA_DISP = pop_matrix_group(POLY_OPA_DISP, actor);
    POLY_OPA_DISP = push_post_limb_matrix_group(POLY_OPA_DISP, actor, 0);

    if (postLimbDraw != NULL) {
        postLimbDraw(play, 1, &limbDList, &rot, arg);
    }

    // @recomp
    POLY_OPA_DISP = pop_matrix_group(POLY_OPA_DISP, actor);

    if (rootLimb->child != LIMB_DONE) {
        SkelAnime_DrawFlexLimbLod(play, rootLimb->child, skeleton, jointTable, overrideLimbDraw, postLimbDraw, arg, lod,
                                  &mtx);
    }

    Matrix_Pop();

    CLOSE_DISPS(play->state.gfxCtx, TAG_FILE, 0);
}

RECOMP_PATCH void SkelAnime_DrawLimbOpa(PlayState* play, s32 limbIndex, void** skeleton, Vec3s* jointTable,
                                        OverrideLimbDrawOpa overrideLimbDraw, PostLimbDrawOpa postLimbDraw, void* arg) {
    StandardLimb* limb;
    Gfx* dList;
    Vec3f pos;
    Vec3s rot;
    // @recomp
    Actor* actor = sTaggingActor;

    OPEN_DISPS(play->state.gfxCtx, TAG_FILE, 0);
    Matrix_Push();

    limb = (StandardLimb*)SEGMENTED_TO_VIRTUAL(skeleton[limbIndex]);
    limbIndex++;
    rot = jointTable[limbIndex];
    pos.x = limb->jointPos.x;
    pos.y = limb->jointPos.y;
    pos.z = limb->jointPos.z;
    dList = limb->dList;

    // @recomp
    POLY_OPA_DISP = push_limb_matrix_group(POLY_OPA_DISP, actor, limbIndex);

    if ((overrideLimbDraw == NULL) || !overrideLimbDraw(play, limbIndex, &dList, &pos, &rot, arg)) {
        Matrix_TranslateRotateZYX(&pos, &rot);
        if (dList != NULL) {
            MATRIX_FINALIZE_AND_LOAD(POLY_OPA_DISP++, play->state.gfxCtx, TAG_FILE, 0);
            gSPDisplayList(POLY_OPA_DISP++, dList);
        }
    }

    // @recomp
    POLY_OPA_DISP = pop_matrix_group(POLY_OPA_DISP, actor);
    POLY_OPA_DISP = push_post_limb_matrix_group(POLY_OPA_DISP, actor, limbIndex);

    if (postLimbDraw != NULL) {
        postLimbDraw(play, limbIndex, &dList, &rot, arg);
    }

    // @recomp
    POLY_OPA_DISP = pop_matrix_group(POLY_OPA_DISP, actor);

    if (limb->child != LIMB_DONE) {
        SkelAnime_DrawLimbOpa(play, limb->child, skeleton, jointTable, overrideLimbDraw, postLimbDraw, arg);
    }

    Matrix_Pop();

    if (limb->sibling != LIMB_DONE) {
        SkelAnime_DrawLimbOpa(play, limb->sibling, skeleton, jointTable, overrideLimbDraw, postLimbDraw, arg);
    }
    CLOSE_DISPS(play->state.gfxCtx, TAG_FILE, 0);
}

RECOMP_PATCH void SkelAnime_DrawOpa(PlayState* play, void** skeleton, Vec3s* jointTable,
                                    OverrideLimbDrawOpa overrideLimbDraw, PostLimbDrawOpa postLimbDraw, void* arg) {
    StandardLimb* rootLimb;
    s32 pad;
    Gfx* dList;
    Vec3f pos;
    Vec3s rot;
    // @recomp
    Actor* actor = sTaggingActor;

    if (skeleton == NULL) {
        return;
    }

    OPEN_DISPS(play->state.gfxCtx, TAG_FILE, 0);

    Matrix_Push();
    rootLimb = (StandardLimb*)SEGMENTED_TO_VIRTUAL(skeleton[0]);

    pos.x = jointTable[0].x;
    pos.y = jointTable[0].y;
    pos.z = jointTable[0].z;

    rot = jointTable[1];
    dList = rootLimb->dList;

    // @recomp
    POLY_OPA_DISP = push_limb_matrix_group(POLY_OPA_DISP, actor, 0);

    if ((overrideLimbDraw == NULL) || !overrideLimbDraw(play, 1, &dList, &pos, &rot, arg)) {
        Matrix_TranslateRotateZYX(&pos, &rot);
        if (dList != NULL) {
            MATRIX_FINALIZE_AND_LOAD(POLY_OPA_DISP++, play->state.gfxCtx, TAG_FILE, 0);
            gSPDisplayList(POLY_OPA_DISP++, dList);
        }
    }

    // @recomp
    POLY_OPA_DISP = pop_matrix_group(POLY_OPA_DISP, actor);
    POLY_OPA_DISP = push_post_limb_matrix_group(POLY_OPA_DISP, actor, 0);

    if (postLimbDraw != NULL) {
        postLimbDraw(play, 1, &dList, &rot, arg);
    }

    // @recomp
    POLY_OPA_DISP = pop_matrix_group(POLY_OPA_DISP, actor);

    if (rootLimb->child != LIMB_DONE) {
        SkelAnime_DrawLimbOpa(play, rootLimb->child, skeleton, jointTable, overrideLimbDraw, postLimbDraw, arg);
    }

    Matrix_Pop();

    CLOSE_DISPS(play->state.gfxCtx, TAG_FILE, 0);
}

RECOMP_PATCH void SkelAnime_DrawFlexLimbOpa(PlayState* play, s32 limbIndex, void** skeleton, Vec3s* jointTable,
                                            OverrideLimbDrawOpa overrideLimbDraw, PostLimbDrawOpa postLimbDraw,
                                            void* arg, Mtx** limbMatrices) {
    StandardLimb* limb;
    Gfx* newDList;
    Gfx* limbDList;
    Vec3f pos;
    Vec3s rot;
    // @recomp
    Actor* actor = sTaggingActor;

    OPEN_DISPS(play->state.gfxCtx, TAG_FILE, 0);

    Matrix_Push();

    limb = (StandardLimb*)SEGMENTED_TO_VIRTUAL(skeleton[limbIndex]);
    limbIndex++;
    rot = jointTable[limbIndex];

    pos.x = limb->jointPos.x;
    pos.y = limb->jointPos.y;
    pos.z = limb->jointPos.z;

    newDList = limbDList = limb->dList;

    // @recomp
    POLY_OPA_DISP = push_limb_matrix_group(POLY_OPA_DISP, actor, limbIndex);

    if ((overrideLimbDraw == NULL) || !overrideLimbDraw(play, limbIndex, &newDList, &pos, &rot, arg)) {
        Matrix_TranslateRotateZYX(&pos, &rot);
        if (newDList != NULL) {
            MATRIX_TO_MTX(*limbMatrices, TAG_FILE, 0);
            gSPMatrix(POLY_OPA_DISP++, *limbMatrices, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPDisplayList(POLY_OPA_DISP++, newDList);
            (*limbMatrices)++;
        } else if (limbDList != NULL) {
            MATRIX_TO_MTX(*limbMatrices, TAG_FILE, 0);
            (*limbMatrices)++;
        }
    }

    // @recomp
    POLY_OPA_DISP = pop_matrix_group(POLY_OPA_DISP, actor);
    POLY_OPA_DISP = push_post_limb_matrix_group(POLY_OPA_DISP, actor, limbIndex);

    if (postLimbDraw != NULL) {
        postLimbDraw(play, limbIndex, &limbDList, &rot, arg);
    }

    // @recomp
    POLY_OPA_DISP = pop_matrix_group(POLY_OPA_DISP, actor);

    if (limb->child != LIMB_DONE) {
        SkelAnime_DrawFlexLimbOpa(play, limb->child, skeleton, jointTable, overrideLimbDraw, postLimbDraw, arg,
                                  limbMatrices);
    }

    Matrix_Pop();

    if (limb->sibling != LIMB_DONE) {
        SkelAnime_DrawFlexLimbOpa(play, limb->sibling, skeleton, jointTable, overrideLimbDraw, postLimbDraw, arg,
                                  limbMatrices);
    }
    CLOSE_DISPS(play->state.gfxCtx, TAG_FILE, 0);
}

RECOMP_PATCH void SkelAnime_DrawFlexOpa(PlayState* play, void** skeleton, Vec3s* jointTable, s32 dListCount,
                                        OverrideLimbDrawOpa overrideLimbDraw, PostLimbDrawOpa postLimbDraw, void* arg) {
    StandardLimb* rootLimb;
    s32 pad;
    Gfx* newDList;
    Gfx* limbDList;
    Vec3f pos;
    Vec3s rot;
    Mtx* mtx = GRAPH_ALLOC(play->state.gfxCtx, dListCount * sizeof(Mtx));
    // @recomp
    Actor* actor = sTaggingActor;

    if (skeleton == NULL) {
        return;
    }

    OPEN_DISPS(play->state.gfxCtx, TAG_FILE, 0);

    gSPSegment(POLY_OPA_DISP++, 0xD, mtx);

    Matrix_Push();

    rootLimb = SEGMENTED_TO_VIRTUAL(skeleton[0]);

    pos.x = jointTable[0].x;
    pos.y = jointTable[0].y;
    pos.z = jointTable[0].z;

    rot = jointTable[1];

    newDList = limbDList = rootLimb->dList;

    // @recomp
    POLY_OPA_DISP = push_limb_matrix_group(POLY_OPA_DISP, actor, 0);

    if ((overrideLimbDraw == NULL) || !overrideLimbDraw(play, 1, &newDList, &pos, &rot, arg)) {
        Matrix_TranslateRotateZYX(&pos, &rot);
        if (newDList != NULL) {
            MATRIX_TO_MTX(mtx, TAG_FILE, 0);
            gSPMatrix(POLY_OPA_DISP++, mtx, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPDisplayList(POLY_OPA_DISP++, newDList);
            mtx++;
        } else if (limbDList != NULL) {
            MATRIX_TO_MTX(mtx, TAG_FILE, 0);
            mtx++;
        }
    }

    // @recomp
    POLY_OPA_DISP = pop_matrix_group(POLY_OPA_DISP, actor);
    POLY_OPA_DISP = push_post_limb_matrix_group(POLY_OPA_DISP, actor, 0);

    if (postLimbDraw != NULL) {
        postLimbDraw(play, 1, &limbDList, &rot, arg);
    }

    // @recomp
    POLY_OPA_DISP = pop_matrix_group(POLY_OPA_DISP, actor);

    if (rootLimb->child != LIMB_DONE) {
        SkelAnime_DrawFlexLimbOpa(play, rootLimb->child, skeleton, jointTable, overrideLimbDraw, postLimbDraw, arg,
                                  &mtx);
    }

    Matrix_Pop();
    CLOSE_DISPS(play->state.gfxCtx, TAG_FILE, 0);
}

/*
 * The four below write into a caller-supplied `Gfx*` rather than POLY_OPA_DISP, so
 * the group commands go straight into it and no display list has to be opened.
 */

RECOMP_PATCH Gfx* SkelAnime_DrawLimb(PlayState* play, s32 limbIndex, void** skeleton, Vec3s* jointTable,
                                     OverrideLimbDraw overrideLimbDraw, PostLimbDraw postLimbDraw, void* arg,
                                     Gfx* gfx) {
    StandardLimb* limb;
    Gfx* dList;
    Vec3f pos;
    Vec3s rot;
    // @recomp
    Actor* actor = sTaggingActor;

    Matrix_Push();

    limb = (StandardLimb*)SEGMENTED_TO_VIRTUAL(skeleton[limbIndex]);
    limbIndex++;

    rot = jointTable[limbIndex];

    pos.x = limb->jointPos.x;
    pos.y = limb->jointPos.y;
    pos.z = limb->jointPos.z;

    dList = limb->dList;

    // @recomp
    gfx = push_limb_matrix_group(gfx, actor, limbIndex);

    if ((overrideLimbDraw == NULL) || !overrideLimbDraw(play, limbIndex, &dList, &pos, &rot, arg, &gfx)) {
        Matrix_TranslateRotateZYX(&pos, &rot);
        if (dList != NULL) {
            MATRIX_FINALIZE_AND_LOAD(gfx++, play->state.gfxCtx, TAG_FILE, 0);
            gSPDisplayList(gfx++, dList);
        }
    }

    // @recomp
    gfx = pop_matrix_group(gfx, actor);
    gfx = push_post_limb_matrix_group(gfx, actor, limbIndex);

    if (postLimbDraw != NULL) {
        postLimbDraw(play, limbIndex, &dList, &rot, arg, &gfx);
    }

    // @recomp
    gfx = pop_matrix_group(gfx, actor);

    if (limb->child != LIMB_DONE) {
        gfx = SkelAnime_DrawLimb(play, limb->child, skeleton, jointTable, overrideLimbDraw, postLimbDraw, arg, gfx);
    }

    Matrix_Pop();

    if (limb->sibling != LIMB_DONE) {
        gfx = SkelAnime_DrawLimb(play, limb->sibling, skeleton, jointTable, overrideLimbDraw, postLimbDraw, arg, gfx);
    }

    return gfx;
}

RECOMP_PATCH Gfx* SkelAnime_Draw(PlayState* play, void** skeleton, Vec3s* jointTable,
                                 OverrideLimbDraw overrideLimbDraw, PostLimbDraw postLimbDraw, void* arg, Gfx* gfx) {
    StandardLimb* rootLimb;
    s32 pad;
    Gfx* dList;
    Vec3f pos;
    Vec3s rot;
    // @recomp
    Actor* actor = sTaggingActor;

    if (skeleton == NULL) {
        return NULL;
    }

    Matrix_Push();

    rootLimb = (StandardLimb*)SEGMENTED_TO_VIRTUAL(skeleton[0]);

    pos.x = jointTable[0].x;
    pos.y = jointTable[0].y;
    pos.z = jointTable[0].z;

    rot = jointTable[1];

    dList = rootLimb->dList;

    // @recomp
    gfx = push_limb_matrix_group(gfx, actor, 0);

    if ((overrideLimbDraw == NULL) || !overrideLimbDraw(play, 1, &dList, &pos, &rot, arg, &gfx)) {
        Matrix_TranslateRotateZYX(&pos, &rot);
        if (dList != NULL) {
            MATRIX_FINALIZE_AND_LOAD(gfx++, play->state.gfxCtx, TAG_FILE, 0);
            gSPDisplayList(gfx++, dList);
        }
    }

    // @recomp
    gfx = pop_matrix_group(gfx, actor);
    gfx = push_post_limb_matrix_group(gfx, actor, 0);

    if (postLimbDraw != NULL) {
        postLimbDraw(play, 1, &dList, &rot, arg, &gfx);
    }

    // @recomp
    gfx = pop_matrix_group(gfx, actor);

    if (rootLimb->child != LIMB_DONE) {
        gfx = SkelAnime_DrawLimb(play, rootLimb->child, skeleton, jointTable, overrideLimbDraw, postLimbDraw, arg, gfx);
    }

    Matrix_Pop();

    return gfx;
}

RECOMP_PATCH Gfx* SkelAnime_DrawFlexLimb(PlayState* play, s32 limbIndex, void** skeleton, Vec3s* jointTable,
                                         OverrideLimbDraw overrideLimbDraw, PostLimbDraw postLimbDraw, void* arg,
                                         Mtx** mtx, Gfx* gfx) {
    StandardLimb* limb;
    Gfx* newDList;
    Gfx* limbDList;
    Vec3f pos;
    Vec3s rot;
    // @recomp
    Actor* actor = sTaggingActor;

    Matrix_Push();

    limb = (StandardLimb*)SEGMENTED_TO_VIRTUAL(skeleton[limbIndex]);
    limbIndex++;
    rot = jointTable[limbIndex];

    pos.x = limb->jointPos.x;
    pos.y = limb->jointPos.y;
    pos.z = limb->jointPos.z;

    newDList = limbDList = limb->dList;

    // @recomp
    gfx = push_limb_matrix_group(gfx, actor, limbIndex);

    if ((overrideLimbDraw == NULL) || !overrideLimbDraw(play, limbIndex, &newDList, &pos, &rot, arg, &gfx)) {
        Matrix_TranslateRotateZYX(&pos, &rot);
        if (newDList != NULL) {
            MATRIX_TO_MTX(*mtx, TAG_FILE, 0);
            gSPMatrix(gfx++, *mtx, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPDisplayList(gfx++, newDList);
            (*mtx)++;
        } else if (limbDList != NULL) {
            MATRIX_TO_MTX(*mtx, TAG_FILE, 0);
            (*mtx)++;
        }
    }

    // @recomp
    gfx = pop_matrix_group(gfx, actor);
    gfx = push_post_limb_matrix_group(gfx, actor, limbIndex);

    if (postLimbDraw != NULL) {
        postLimbDraw(play, limbIndex, &limbDList, &rot, arg, &gfx);
    }

    // @recomp
    gfx = pop_matrix_group(gfx, actor);

    if (limb->child != LIMB_DONE) {
        gfx = SkelAnime_DrawFlexLimb(play, limb->child, skeleton, jointTable, overrideLimbDraw, postLimbDraw, arg, mtx,
                                     gfx);
    }

    Matrix_Pop();

    if (limb->sibling != LIMB_DONE) {
        gfx = SkelAnime_DrawFlexLimb(play, limb->sibling, skeleton, jointTable, overrideLimbDraw, postLimbDraw, arg,
                                     mtx, gfx);
    }

    return gfx;
}

RECOMP_PATCH Gfx* SkelAnime_DrawFlex(PlayState* play, void** skeleton, Vec3s* jointTable, s32 dListCount,
                                     OverrideLimbDraw overrideLimbDraw, PostLimbDraw postLimbDraw, void* arg,
                                     Gfx* gfx) {
    StandardLimb* rootLimb;
    s32 pad;
    Gfx* newDList;
    Gfx* limbDList;
    Vec3f pos;
    Vec3s rot;
    Mtx* mtx = GRAPH_ALLOC(play->state.gfxCtx, dListCount * sizeof(*mtx));
    // @recomp
    Actor* actor = sTaggingActor;

    if (skeleton == NULL) {
        return NULL;
    }

    gSPSegment(gfx++, 0xD, mtx);
    Matrix_Push();
    rootLimb = (StandardLimb*)SEGMENTED_TO_VIRTUAL(skeleton[0]);

    pos.x = jointTable[0].x;
    pos.y = jointTable[0].y;
    pos.z = jointTable[0].z;

    rot = jointTable[1];

    newDList = limbDList = rootLimb->dList;

    // @recomp
    gfx = push_limb_matrix_group(gfx, actor, 0);

    if ((overrideLimbDraw == NULL) || !overrideLimbDraw(play, 1, &newDList, &pos, &rot, arg, &gfx)) {
        Matrix_TranslateRotateZYX(&pos, &rot);
        if (newDList != NULL) {
            MATRIX_TO_MTX(mtx, TAG_FILE, 0);
            gSPMatrix(gfx++, mtx, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPDisplayList(gfx++, newDList);
            mtx++;
        } else if (limbDList != NULL) {
            MATRIX_TO_MTX(mtx, TAG_FILE, 0);
            mtx++;
        }
    }

    // @recomp
    gfx = pop_matrix_group(gfx, actor);
    gfx = push_post_limb_matrix_group(gfx, actor, 0);

    if (postLimbDraw != NULL) {
        postLimbDraw(play, 1, &limbDList, &rot, arg, &gfx);
    }

    // @recomp
    gfx = pop_matrix_group(gfx, actor);

    if (rootLimb->child != LIMB_DONE) {
        gfx = SkelAnime_DrawFlexLimb(play, rootLimb->child, skeleton, jointTable, overrideLimbDraw, postLimbDraw, arg,
                                     &mtx, gfx);
    }

    Matrix_Pop();

    return gfx;
}

/*
 * Skinned skeletons. Unlike the SkelAnime family this one loads no per-limb
 * matrix - the limbs are deformed on the CPU and drawn under a single transform -
 * so there is nothing for a group to attach to. An identity matrix is pushed per
 * limb purely to create something to tag, which is what Majora's Mask does too.
 * Multiplying by the identity leaves the transform unchanged, so the geometry is
 * bit-identical and only the interpolation behaviour differs.
 */
RECOMP_PATCH void Skin_DrawImpl(Actor* actor, PlayState* play, Skin* skin, SkinPostDraw postDraw,
                                SkinOverrideLimbDraw overrideLimbDraw, s32 setTranslation, s32 arg6, s32 drawFlags) {
    s32 i;
    s32 segmentType;
    SkinLimb** skeleton;
    GraphicsContext* gfxCtx = play->state.gfxCtx;
    Mtx* mtx;

    OPEN_DISPS(gfxCtx, TAG_FILE, 0);

    if (!(drawFlags & SKIN_DRAW_FLAG_CUSTOM_TRANSFORMS)) {
        Skin_ApplyAnimTransformations(skin, gSkinLimbMatrices, actor, setTranslation);
    }

    skeleton = SEGMENTED_TO_VIRTUAL(skin->skeletonHeader->segment);

    if (!(drawFlags & SKIN_DRAW_FLAG_CUSTOM_MATRIX)) {
        gSPMatrix(POLY_OPA_DISP++, &gIdentityMtx, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        mtx = SkinMatrix_MtxFToNewMtx(gfxCtx, &skin->mtx);

        if (mtx == NULL) {
            goto close_disps;
        }

        gSPMatrix(POLY_OPA_DISP++, mtx, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    }

    for (i = 0; i < skin->skeletonHeader->limbCount; i++) {
        s32 shouldDraw = true;

        if (overrideLimbDraw != NULL) {
            shouldDraw = overrideLimbDraw(actor, play, i, skin);
        }

        segmentType = ((SkinLimb*)SEGMENTED_TO_VIRTUAL(skeleton[i]))->segmentType;

        // @recomp Push a matrix that changes nothing, so this limb has a matrix of
        // its own to carry a group id, then tag it.
        gSPMatrix(POLY_OPA_DISP++, &gIdentityMtx, G_MTX_PUSH | G_MTX_MUL | G_MTX_MODELVIEW);
        POLY_OPA_DISP = push_skin_limb_matrix_group(POLY_OPA_DISP, actor, i);

        if (segmentType == SKIN_LIMB_TYPE_ANIMATED && shouldDraw == true) {
            Skin_DrawAnimatedLimb(gfxCtx, skin, i, arg6, drawFlags);
        } else if (segmentType == SKIN_LIMB_TYPE_NORMAL && shouldDraw == true) {
            Skin_DrawLimb(gfxCtx, skin, i, NULL, drawFlags);
        }

        // @recomp Undo both.
        gSPPopMatrix(POLY_OPA_DISP++, G_MTX_MODELVIEW);
        POLY_OPA_DISP = pop_matrix_group(POLY_OPA_DISP, actor);
    }

    if (postDraw != NULL) {
        postDraw(actor, play, skin);
    }

close_disps:
    CLOSE_DISPS(gfxCtx, TAG_FILE, 0);
}

/* ------------------------- everything else an actor draws ------------------- */

static s32 scan_for_matrices(Gfx* start, Gfx* end) {
    s32 matrix_count = 0;
    Gfx* cur;

    for (cur = start; cur != end; cur++) {
        if ((cur->words.w0 >> 24) == G_MTX) {
            matrix_count++;
        }
    }
    return matrix_count;
}

/*
 * Fill in the tag slots reserved in Actor_Draw, but only for an actor that wrote
 * at most one matrix to each list. One matrix is unambiguously "this actor's
 * transform"; several means the actor is building its own hierarchy and a single
 * group spanning all of them would interpolate them as one rigid body. Those are
 * left untagged, which costs the actor smooth motion but never makes it wrong -
 * the specific ones worth tagging get their own patch, the way Majora's Mask does
 * in specific_actor_transform_tagging.c.
 */
static void tag_actor_displaylists(PlayState* play, u32 cur_transform_id, s32 skipped, Gfx* opa_start,
                                   Gfx* xlu_start) {
    OPEN_DISPS(play->state.gfxCtx, TAG_FILE, 0);

    s32 opa_matrices = scan_for_matrices(opa_start, POLY_OPA_DISP);
    s32 xlu_matrices = scan_for_matrices(xlu_start, POLY_XLU_DISP);

    if ((cur_transform_id != 0) && (opa_matrices == 1 || xlu_matrices == 1) && opa_matrices <= 1 &&
        xlu_matrices <= 1) {
        if (opa_matrices == 1) {
            if (skipped) {
                gEXMatrixGroupDecomposedSkipPosRot(opa_start, cur_transform_id, G_EX_PUSH, G_MTX_MODELVIEW,
                                                   G_EX_EDIT_ALLOW);
            } else {
                gEXMatrixGroupDecomposedNormal(opa_start, cur_transform_id, G_EX_PUSH, G_MTX_MODELVIEW,
                                               G_EX_EDIT_ALLOW);
            }
            gEXPopMatrixGroup(POLY_OPA_DISP++, G_MTX_MODELVIEW);
        }

        if (xlu_matrices == 1) {
            if (skipped) {
                gEXMatrixGroupDecomposedSkipPosRot(xlu_start, cur_transform_id + 1, G_EX_PUSH, G_MTX_MODELVIEW,
                                                   G_EX_EDIT_ALLOW);
            } else {
                gEXMatrixGroupDecomposedNormal(xlu_start, cur_transform_id + 1, G_EX_PUSH, G_MTX_MODELVIEW,
                                               G_EX_EDIT_ALLOW);
            }
            gEXPopMatrixGroup(POLY_XLU_DISP++, G_MTX_MODELVIEW);
        }
    }

    CLOSE_DISPS(play->state.gfxCtx, TAG_FILE, 0);
}

RECOMP_PATCH void Actor_Draw(PlayState* play, Actor* actor) {
    FaultClient faultClient;
    Lights* lights;
    // @recomp Saved rather than nulled: an actor's draw function can draw another
    // actor, and the outer one must get its own id back on the way out.
    Actor* prevTaggingActor = sTaggingActor;
    u32 prevTransformId = sTaggingTransformId;
    s32 prevSkipped = sTaggingSkipped;
    // @recomp Resolved once here and reused by every limb this actor draws.
    u32 transformId = actor_transform_id(actor);
    s32 skipped = actor_get_interpolation_skipped(actor);

    Fault_AddClient(&faultClient, Actor_FaultPrint, actor, "Actor_draw");

    OPEN_DISPS(play->state.gfxCtx, TAG_FILE, 0);

    lights = LightContext_NewLights(&play->lightCtx, play->state.gfxCtx);

    Lights_BindAll(lights, play->lightCtx.listHead,
                   (actor->flags & ACTOR_FLAG_IGNORE_POINT_LIGHTS) ? NULL : &actor->world.pos);
    Lights_Draw(lights, play->state.gfxCtx);

    if (actor->flags & ACTOR_FLAG_IGNORE_QUAKE) {
        Matrix_SetTranslateRotateYXZ(actor->world.pos.x + play->mainCamera.quakeOffset.x,
                                     actor->world.pos.y +
                                         ((actor->shape.yOffset * actor->scale.y) + play->mainCamera.quakeOffset.y),
                                     actor->world.pos.z + play->mainCamera.quakeOffset.z, &actor->shape.rot);
    } else {
        Matrix_SetTranslateRotateYXZ(actor->world.pos.x, actor->world.pos.y + (actor->shape.yOffset * actor->scale.y),
                                     actor->world.pos.z, &actor->shape.rot);
    }

    Matrix_Scale(actor->scale.x, actor->scale.y, actor->scale.z, MTXMODE_APPLY);
    Actor_SetObjectDependency(play, actor);

    gSPSegment(POLY_OPA_DISP++, 0x06, play->objectCtx.slots[actor->objectSlot].segment);
    gSPSegment(POLY_XLU_DISP++, 0x06, play->objectCtx.slots[actor->objectSlot].segment);

    if (actor->colorFilterTimer != 0) {
        Color_RGBA8 color = { 0, 0, 0, 255 };

        if (actor->colorFilterParams & COLORFILTER_COLORFLAG_GRAY) {
            color.r = color.g = color.b = COLORFILTER_GET_COLORINTENSITY(actor->colorFilterParams) | 7;
        } else if (actor->colorFilterParams & COLORFILTER_COLORFLAG_RED) {
            color.r = COLORFILTER_GET_COLORINTENSITY(actor->colorFilterParams) | 7;
        } else {
            color.b = COLORFILTER_GET_COLORINTENSITY(actor->colorFilterParams) | 7;
        }

        if (actor->colorFilterParams & COLORFILTER_BUFFLAG_XLU) {
            func_80026860(play, &color, actor->colorFilterTimer, COLORFILTER_GET_DURATION(actor->colorFilterParams));
        } else {
            func_80026400(play, &color, actor->colorFilterTimer, COLORFILTER_GET_DURATION(actor->colorFilterParams));
        }
    }

    // @recomp Two no-ops in each list, held open for a matrix group that can only
    // be written once the actor's draw function has revealed how many matrices it
    // uses. A group command is two words wide, which is why there are two.
    {
        Gfx* opa_tag_slot = POLY_OPA_DISP;
        Gfx* xlu_tag_slot = POLY_XLU_DISP;

        gDPNoOp(POLY_OPA_DISP++);
        gDPNoOp(POLY_OPA_DISP++);
        gDPNoOp(POLY_XLU_DISP++);
        gDPNoOp(POLY_XLU_DISP++);

        // @recomp Tell the skeleton patches which actor is drawing.
        sTaggingActor = actor;
        sTaggingTransformId = transformId;
        sTaggingSkipped = skipped;

        actor->draw(actor, play);

        sTaggingActor = prevTaggingActor;
        sTaggingTransformId = prevTransformId;
        sTaggingSkipped = prevSkipped;

        tag_actor_displaylists(play, transformId, skipped, opa_tag_slot, xlu_tag_slot);
    }

    if (actor->colorFilterTimer != 0) {
        if (actor->colorFilterParams & COLORFILTER_BUFFLAG_XLU) {
            func_80026A6C(play);
        } else {
            func_80026608(play);
        }
    }

    if (actor->shape.shadowDraw != NULL) {
        actor->shape.shadowDraw(actor, lights, play);
    }

    // @recomp One frame of skipped interpolation is all a request buys; whatever
    // set it has to set it again next time.
    actor_clear_interpolation_skipped(actor);

    CLOSE_DISPS(play->state.gfxCtx, TAG_FILE, 0);

    Fault_RemoveClient(&faultClient);
}
