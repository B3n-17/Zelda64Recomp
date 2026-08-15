/*
 * Mask abilities for Ocarina of Time.
 *
 * In vanilla OoT the masks are cosmetic - they change what NPCs say and nothing
 * else. This gives them the abilities their Majora's Mask counterparts have.
 *
 * The Bunny Hood runs faster on land and the Zora Mask swims faster, both at a
 * speed the player picks. The Goron entry is present in the table below with its
 * ability declared but not yet applied, because the point of the table is that
 * adding an ability is a data change plus one place that reads it, not a new pass
 * through the file. See "Adding an ability" below.
 *
 * Speed abilities need two things patched, not one, and the second is easy to
 * miss: the speed the player asks for, and the cap on the speed they are allowed
 * to be travelling at. OoT keeps a separate hard cap for each kind of movement -
 * running, airborne, swimming - and each one silently undoes a boost that only
 * raised the request. Both are patched here for both masks.
 */
#include "modding.h"

// OoT's decomp has no monolithic global.h, so headers are pulled in individually
// the same way patches_oot does it.
#include "array_count.h"
#include "attributes.h"
#include "camera.h"
#include "play_state.h"
#include "player.h"
// For R_RUN_SPEED_LIMIT, the cap the airborne patch below has to raise.
#include "regs.h"
#include "save.h"
#include "sys_math3d.h"
#include "z_lib.h"
#include "z_math.h"

#include "mask_shared.h"

/*
 * Declared in z_player.c rather than a header, but exported, so the recompiler
 * resolves them by name against the base game. Needed because the function
 * patched below calls them.
 */
s32 Player_CalcSpeedAndYawFromControlStick(PlayState* play, Player* this, f32* outSpeedTarget, s16* outYawTarget,
                                           f32 speedMode);
s32 Player_FriendlyLockOnOrParallel(Player* this);

/* The mod's config options, matching the enums declared in mod.toml. */
RECOMP_IMPORT("*", u32 recomp_get_config_u32(const char* key));

/*
 * The speeds the two speed options can select, in the order their options are
 * listed. Read every time rather than cached, because the config menu can be
 * opened mid-game.
 */
static const f32 sBunnyHoodSpeeds[] = { 1.5f, 2.0f };
static const f32 sZoraSwimSpeeds[] = { 1.5f, 2.0f };

/* ------------------------------------------------------------------------- *
 *  The ability table
 * ------------------------------------------------------------------------- *
 *
 * One row per mask. Everything a mask does lives here; the code below only reads
 * it. A row of all defaults is a mask that does nothing, which is every mask OoT
 * ships with.
 *
 * Adding an ability:
 *   1. Add a field here with a neutral default (a 1.0f scale, or false).
 *   2. Fill it in for the masks that should have it.
 *   3. Read it from exactly one place - a patch or hook on whichever game
 *      function decides that behaviour, the way ground_speed_scale is read by
 *      Player_GetMovementSpeedAndYaw below.
 *
 * Keeping the neutral value at the identity for the operation matters: it means
 * an unimplemented ability and a mask that lacks it are the same thing, so a
 * half finished row can never change behaviour by accident.
 */
typedef struct MaskAbilities {
    // Multiplies Link's target movement speed on land. 1.0f leaves it alone.
    f32 ground_speed_scale;

    // Multiplies swimming speed, read by mask_swim_speed_scale. 1.0f leaves it
    // alone. Separate from the ground scale because swimming comes down a
    // different path than ground movement, with its own cap.
    f32 swim_speed_scale;

    // Suppresses the hot room damage timer, the way the Goron Tunic does.
    // Declared and not yet read.
    u8 heat_resistant;
} MaskAbilities;

/*
 * Every row states every field. Deliberately not a defaults macro plus overrides
 * - that compiles, but warns on every override and, worse, hides the one thing
 * about this table that can actually bite: the neutral value for a scale is 1.0f
 * and NOT zero, so a row that leaves a field out gets a multiplier of zero and
 * pins the player in place. Writing all three fields out makes a missing one a
 * compile-visible gap rather than a silent trap.
 *
 *                              ground  swim   heat
 */
static const MaskAbilities sMaskAbilities[PLAYER_MASK_MAX] = {
    [PLAYER_MASK_NONE] =   { 1.0f,   1.0f,  false },
    [PLAYER_MASK_KEATON] = { 1.0f,   1.0f,  false },
    [PLAYER_MASK_SKULL] =  { 1.0f,   1.0f,  false },
    [PLAYER_MASK_SPOOKY] = { 1.0f,   1.0f,  false },

    // The one ability that is live. 1.5x matches the Bunny Hood in Majora's
    // Mask, which is the behaviour being ported, and is the first of the two
    // values the "bunny_hood_speed" option offers - see mask_ground_speed_scale,
    // which is what actually reaches the player and which reads the option
    // instead of this field. The row keeps the value anyway so that a glance at
    // the table still says what this mask does.
    [PLAYER_MASK_BUNNY] =  { 1.5f,   1.0f,  false },

    // Declared, not yet applied - nothing reads heat_resistant, so the Goron
    // Mask still behaves as vanilla.
    [PLAYER_MASK_GORON] =  { 1.0f,   1.0f,  true  },

    // Live, and the same arrangement as the Bunny Hood above: 1.5x is the first
    // of the two values "zora_swim_speed" offers, and mask_swim_speed_scale reads
    // the option rather than this field.
    [PLAYER_MASK_ZORA] =   { 1.0f,   1.5f,  false },

    [PLAYER_MASK_GERUDO] = { 1.0f,   1.0f,  false },
    [PLAYER_MASK_TRUTH] =  { 1.0f,   1.0f,  false },
};

/*
 * The abilities of whatever Link is currently wearing, or the neutral row.
 *
 * Bounds checked rather than trusting currentMask: it is a u8 the player actor
 * writes from several places, and an out of range read here would be a garbage
 * float scaling the player's speed - a spectacular and hard to trace failure for
 * something a comparison prevents.
 */
static const MaskAbilities* mask_abilities_current(Player* this) {
    static const MaskAbilities none = { 1.0f, 1.0f, false };

    if ((this == NULL) || (this->currentMask <= PLAYER_MASK_NONE) || (this->currentMask >= PLAYER_MASK_MAX)) {
        return &none;
    }

    return &sMaskAbilities[this->currentMask];
}

/*
 * The ground speed multiplier to apply, which is the table's value for every mask
 * except the Bunny Hood.
 *
 * The Bunny Hood is the one ability with a number worth arguing about, so it is
 * the one the config menu exposes. Going through here rather than editing the
 * table entry keeps the option out of a structure that is otherwise pure data,
 * and keeps the read to the frames where a Bunny Hood is actually on.
 */
static f32 mask_ground_speed_scale(Player* this) {
    if ((this != NULL) && (this->currentMask == PLAYER_MASK_BUNNY)) {
        u32 choice;

        // The Bunny Hood is a running ability and stops at the waterline, which
        // also keeps it from stacking with the Zora Mask: the swimming actions
        // ask this function for their speed target before handing it to the
        // swimming code that the Zora Mask scales.
        if (PLAYER_IS_IN_WATER(this)) {
            return 1.0f;
        }

        choice = recomp_get_config_u32("bunny_hood_speed");
        if (choice >= ARRAY_COUNT(sBunnyHoodSpeeds)) {
            choice = 0;
        }
        return sBunnyHoodSpeeds[choice];
    }

    return mask_abilities_current(this)->ground_speed_scale;
}

/*
 * The swimming counterpart, and the whole of the Zora Mask's ability.
 *
 * Only consulted from the swimming code below, so unlike the ground scale it does
 * not need a water test of its own.
 */
static f32 mask_swim_speed_scale(Player* this) {
    if ((this != NULL) && (this->currentMask == PLAYER_MASK_ZORA)) {
        u32 choice = recomp_get_config_u32("zora_swim_speed");

        if (choice >= ARRAY_COUNT(sZoraSwimSpeeds)) {
            choice = 0;
        }
        return sZoraSwimSpeeds[choice];
    }

    return mask_abilities_current(this)->swim_speed_scale;
}

/* ------------------------------------------------------------------------- *
 *  Ground speed - the Bunny Hood
 * ------------------------------------------------------------------------- */

/*
 * A verbatim copy of the decomp original with the two lines marked @recomp
 * added, because RECOMP_PATCH replaces the whole function.
 *
 * A patch rather than a return hook, despite the copy: this function is small
 * and stable, whereas return hooks are not yet proven in this tree - the D-Pad
 * mod's first attempt at drawing used one and produced nothing. A patch is the
 * mechanism whose behaviour is known here.
 *
 * It is also the right interception point rather than merely a convenient one.
 * Every ground movement state in the player actor asks this function what speed
 * the stick is requesting, so scaling its answer speeds up walking, running and
 * everything derived from them at once, and leaves rolls, jumps and knockback -
 * which do not come through here - untouched.
 *
 * The one structural change from the original is that the early `return false`
 * inside the focusActor branch is folded into the shared exit. It returned the
 * same value the branch below it does, so the behaviour is identical, and it
 * means the scale is applied on every path out rather than being skipped when
 * the player is locked on.
 */
RECOMP_PATCH s32 Player_GetMovementSpeedAndYaw(Player* this, f32* outSpeedTarget, s16* outYawTarget, f32 speedMode,
                                               PlayState* play) {
    s32 ret;

    if (!Player_CalcSpeedAndYawFromControlStick(play, this, outSpeedTarget, outYawTarget, speedMode)) {
        *outYawTarget = this->actor.shape.rot.y;
        ret = false;

        if (this->focusActor != NULL) {
            if ((play->actorCtx.attention.reticleSpinCounter != 0) && !(this->stateFlags2 & PLAYER_STATE2_6)) {
                *outYawTarget = Math_Vec3f_Yaw(&this->actor.world.pos, &this->focusActor->focus.pos);
            }
        } else if (Player_FriendlyLockOnOrParallel(this)) {
            *outYawTarget = this->parallelYaw;
        }
    } else {
        *outYawTarget += Camera_GetInputDirYaw(GET_ACTIVE_CAM(play));
        ret = true;
    }

    // @recomp Apply the worn mask's ground speed. A mask with no speed ability
    // scales by 1.0f, so this is a no-op for everything except the Bunny Hood.
    *outSpeedTarget *= mask_ground_speed_scale(this);

    return ret;
}

/* ------------------------------------------------------------------------- *
 *  Keeping the speed through a jump
 * ------------------------------------------------------------------------- *
 *
 * Scaling the speed the player asks for is not enough on its own, because the
 * moment Link leaves the ground the game clamps how fast he is actually moving
 * back down to the run limit. The first frame of a jump therefore threw away the
 * whole boost, which reads as the jump snatching the speed away rather than as
 * one continuous movement.
 *
 * The clamp lives here, in the function both airborne actions call to steer while
 * off the ground - Player_Action_8084411C, which is falling and jumping, and
 * Player_Action_80844AF4. It is a hard cap on `speedXZ`, the speed Link is
 * travelling at, not on the speed being requested, so nothing upstream can raise
 * it. R_RUN_SPEED_LIMIT is the vanilla running maximum, and a boosted Link is
 * over it by exactly the mask's multiplier.
 *
 * So the cap is scaled by that same multiplier and nothing else changes. Without
 * a speed mask the scale is 1.0f and this is the original clamp to the byte.
 *
 * A verbatim copy of the decomp original with the one line marked @recomp
 * changed, because RECOMP_PATCH replaces the whole function. A patch rather than
 * a hook for the reason the rest of this mod is: hooks cannot reach into
 * ovl_player_actor in this tree.
 */
RECOMP_PATCH void func_8083DFE0(Player* this, f32* arg1, s16* arg2) {
    s16 yawDiff = this->yaw - *arg2;

    if (this->meleeWeaponState == 0) {
        // @recomp Scale the airborne speed cap by the worn mask's ground speed,
        // so a jump carries the boost instead of cancelling it.
        f32 speedLimit = (R_RUN_SPEED_LIMIT / 100.0f) * mask_ground_speed_scale(this);

        this->speedXZ = CLAMP(this->speedXZ, -speedLimit, speedLimit);
    }

    if (ABS(yawDiff) > 0x6000) {
        if (Math_StepToF(&this->speedXZ, 0.0f, 1.0f)) {
            this->yaw = *arg2;
        }
    } else {
        Math_AsymStepToF(&this->speedXZ, *arg1, 0.05f, 0.1f);
        Math_ScaledStepToS(&this->yaw, *arg2, 200);
    }
}

/* ------------------------------------------------------------------------- *
 *  Swimming - the Zora Mask
 * ------------------------------------------------------------------------- *
 *
 * This is where swimming speed is decided, for both the horizontal stroke and the
 * vertical one - the underwater action calls it a second time with velocity.y.
 * Everything about how fast Link swims is in these three numbers, so the ability
 * is applied here and nowhere else:
 *
 *   - the cap, 0.8 of the running limit, which is a hard write to the current
 *     speed rather than a target. Leaving it alone would do to swimming exactly
 *     what the airborne clamp did to jumping.
 *   - the target being stepped towards.
 *   - the acceleration, which is scaled too so that reaching a higher top speed
 *     still takes the same moment it always did. Without this the Zora Mask would
 *     feel sluggish off the mark and only pay off in long straight swims.
 *
 * Note that this deliberately does not touch the turn rate or the animation
 * speed: swimming faster should not mean flailing faster.
 *
 * A verbatim copy of the decomp original with the lines marked @recomp changed.
 * Without the Zora Mask the scale is 1.0f and every one of them is the original
 * expression.
 */
RECOMP_PATCH void func_8084AEEC(Player* this, f32* arg1, f32 arg2, s16 arg3) {
    f32 temp1;
    f32 temp2;
    // @recomp The Zora Mask's multiplier, 1.0f for everyone else.
    f32 swimScale = mask_swim_speed_scale(this);

    temp1 = this->skelAnime.curFrame - 10.0f;

    // @recomp Scaled, for the same reason the airborne cap above is.
    temp2 = (R_RUN_SPEED_LIMIT / 100.0f) * 0.8f * swimScale;
    if (*arg1 > temp2) {
        *arg1 = temp2;
    }

    if ((0.0f < temp1) && (temp1 < 10.0f)) {
        // @recomp Scale the acceleration with the top speed, so the time taken to
        // reach it is unchanged.
        temp1 *= 6.0f * swimScale;
    } else {
        temp1 = 0.0f;
        arg2 = 0.0f;
    }

    // @recomp Scaled target.
    Math_AsymStepToF(arg1, arg2 * 0.8f * swimScale, temp1, (fabsf(*arg1) * 0.02f) + 0.05f);
    Math_ScaledStepToS(&this->yaw, arg3, 1600);
}
