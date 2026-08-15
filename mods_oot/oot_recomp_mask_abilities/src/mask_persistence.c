/*
 * Keep the mask on Link's face - across a scene change, and in water.
 *
 * In vanilla the mask comes off at every door, loading zone and warp. Nothing
 * takes it off deliberately - the mask is not stored in the save at all, only in
 * the Player actor's `currentMask`, and the Player actor is destroyed and built
 * again for each new scene. Whatever was being worn simply ceases to exist.
 *
 * So this is not a case of suppressing something, it is a case of putting the
 * value back: remember the mask every frame, and write it into the new Player
 * once the new scene has one.
 *
 *
 * Why it needs two hooks rather than one
 * --------------------------------------
 * Restoring means telling a scene change apart from the player deciding to take
 * the mask off, and both look identical in `currentMask` - it goes to zero. The
 * difference is that only one of them is preceded by Play_Init, which is the
 * hook that arms the restore. A mask taken off with the C button while a scene is
 * running is left off, which is what the button is for.
 *
 * The restore is deferred to the next frame rather than done inside Play_Init
 * because the Player actor is spawned during scene setup and is not reliably
 * there to write to yet. Interface_Update runs once a frame with the actor list
 * fully built, so the pending restore lands on the first frame that has a Player.
 *
 *
 * Water is a different fault, and putting the mask back is not enough
 * --------------------------------------------------------------------
 * Water does not take the mask off on purpose either. Swimming disables the C
 * buttons - z_parameter.c sets buttonStatus[1..3] to BTN_DISABLED for anything
 * that is not a hookshot - and Player_ProcessItemButtons removes any mask that is
 * not on a C button, using C_BTN_ITEM, which reports a disabled button as holding
 * nothing. The mask is dropped for failing a test it was never meant to be taking.
 *
 * Putting it back afterwards, which is what this file did first, is not enough,
 * and the way it failed is worth keeping in mind. The mask reappeared and looked
 * right, but the Zora Mask still swam at vanilla speed, because the order within
 * the frame is: the player update clears the mask, the swimming code runs and
 * sees no mask, and only then does this restore it in time for drawing. The mask
 * was on Link's face and absent from the physics.
 *
 * So the button is kept alive instead, which stops the removal from happening at
 * all rather than papering over it afterwards - and as a bonus, an enabled button
 * is a pressable one, so masks can be put on and taken off while swimming.
 *
 * The restore below is kept for the other things that disable buttons, crawling
 * and a few cutscene states, where wearing the mask has no mechanical effect and
 * only the look matters. It keys on the button being disabled: a mask whose
 * button is dead was taken by circumstance, while a mask whose button is live and
 * which is off anyway was taken off by the player pressing it, and stays off.
 *
 *
 * Why hooks and not a patch
 * -------------------------
 * The natural places for this are the end of Player_Init and the mask test at the
 * top of Player_ProcessItemButtons, and neither is reachable. Hooking makes the
 * runtime regenerate the target with the live recompiler, which fails for
 * functions in ovl_player_actor. Patching means a verbatim copy, and Player_Init
 * reaches a dozen private statics including the whole sStartModeFuncs table,
 * while Player_ProcessItemButtons needs sControlInput, sItemButtons and
 * sHeldItemButtonIsHeldDown - none of which are in OoT's data symbols, so a copy
 * would link against nothing and crash. Both functions used here are in the
 * always-loaded code section, where hooks work.
 *
 * The cost of doing it from here is that the mask is briefly off within the
 * frame: the player update clears it and this puts it back afterwards. Nothing
 * sees the gap, because drawing happens later still - but an actor that asks
 * Player_GetMask during its own update gets NONE for that frame, which only
 * matters to the mask reaction NPCs, none of whom are underwater.
 */
#include "modding.h"

#include "play_state.h"
#include "player.h"
#include "save.h"

#include "mask_shared.h"

/*
 * The mask being worn, carried across the gap where no Player actor exists.
 * PLAYER_MASK_NONE means there is nothing to put back.
 */
static u8 sSavedMask = PLAYER_MASK_NONE;

// Set by Play_Init, cleared once the mask has been handed to a Player.
static u8 sRestorePending = false;

/*
 * The inventory item for a mask, which is what makes the restore safe across a
 * file change. The two enums run in the same order, so this is arithmetic rather
 * than a table: PLAYER_MASK_KEATON is ITEM_MASK_KEATON, and so on up to
 * PLAYER_MASK_TRUTH and ITEM_MASK_TRUTH.
 */
static u8 mask_to_item(u8 mask) {
    return ITEM_MASK_KEATON + mask - 1;
}

/*
 * Whether the mask can be put back on.
 *
 * The check is against the inventory rather than a straight `!= NONE`, because
 * this state outlives the save file: load a different file, or start a new one,
 * and a mask remembered from the last session would otherwise reappear on a Link
 * who never got it. Every mask lives in the child trade slot, so a mask that is
 * still owned is a mask that is still the item in that slot.
 */
static u8 mask_is_owned(u8 mask) {
    u8 item;

    if ((mask <= PLAYER_MASK_NONE) || (mask >= PLAYER_MASK_MAX)) {
        return false;
    }

    item = mask_to_item(mask);

    return INV_CONTENT(item) == item;
}

/*
 * Whether the mask is still assigned to a C button that the game has switched
 * off, which is the signature of a mask removed by circumstance rather than by
 * the player. Swimming is what does this in practice; crawling and a few
 * cutscene states disable the same buttons and are covered by the same test.
 *
 * A mask on a live button that is no longer being worn was taken off deliberately
 * and must stay off, so that case returns false and the mask is left alone.
 */
static u8 mask_was_taken_by_a_disabled_button(u8 mask) {
    u8 item;
    s32 i;

    if ((mask <= PLAYER_MASK_NONE) || (mask >= PLAYER_MASK_MAX)) {
        return false;
    }

    item = mask_to_item(mask);

    // Index 0 is the B button; the C buttons are 1 through 3, and buttonStatus is
    // indexed the same way as equips.buttonItems.
    for (i = 1; i < 4; i++) {
        if (gSaveContext.save.info.equips.buttonItems[i] == item) {
            return gSaveContext.buttonStatus[i] == BTN_DISABLED;
        }
    }

    return false;
}

RECOMP_HOOK("Play_Init")
void mask_persistence_on_play_init(GameState* thisx) {
    sRestorePending = true;
}

/*
 * Keep a C button holding a mask usable while Link is in the water.
 *
 * Two things follow from it, and both are the point: Player_ProcessItemButtons no
 * longer strips the mask, so a worn mask keeps working underwater rather than
 * merely looking worn; and the button can be pressed, so masks can be put on and
 * taken off while swimming.
 *
 * Play_Update is the right frame slot and the only workable one. It runs before
 * the actors, so the player update reads what is written here, and it runs after
 * Interface_Update did its own pass last frame, so there is nothing left to
 * overwrite it. Writing this any later in the frame - from the Interface_Update
 * hook below, say - would be undone immediately, because func_80083108 recomputes
 * every button's status from scratch inside Interface_Update, every frame.
 *
 * Which also means this is not a fight worth trying to win outright: the status
 * goes back to disabled later in the same frame, before the HUD reads it. That is
 * deliberate. The C buttons stay dimmed exactly as they do in vanilla, and the
 * only code that sees them enabled is the player update that needs to.
 */
RECOMP_HOOK("Play_Update")
void mask_persistence_on_play_update(PlayState* play) {
    Player* player = GET_PLAYER(play);
    s32 i;

    if ((player == NULL) || !PLAYER_IS_IN_WATER(player)) {
        return;
    }

    for (i = 1; i < 4; i++) {
        u8 item = gSaveContext.save.info.equips.buttonItems[i];

        if ((item >= ITEM_MASK_KEATON) && (item <= ITEM_MASK_TRUTH)) {
            gSaveContext.buttonStatus[i] = BTN_ENABLED;
        }
    }
}

RECOMP_HOOK("Interface_Update")
void mask_persistence_on_interface_update(PlayState* play) {
    Player* player = GET_PLAYER(play);

    if (player == NULL) {
        return;
    }

    if (sRestorePending) {
        if (mask_is_owned(sSavedMask)) {
            player->currentMask = sSavedMask;
        }
        sRestorePending = false;
        return;
    }

    /*
     * Put back a mask that was removed only because its button went dead - the
     * water case. Checked before the mask is recorded below, so that the removal
     * is undone rather than remembered.
     */
    if ((player->currentMask == PLAYER_MASK_NONE) && mask_is_owned(sSavedMask) &&
        mask_was_taken_by_a_disabled_button(sSavedMask)) {
        player->currentMask = sSavedMask;
        return;
    }

    /*
     * Track what is being worn, including nothing: taking the mask off has to be
     * remembered as firmly as putting it on, or the next scene change would put
     * back a mask the player has already removed. This also picks up the actors
     * that call Player_UnsetMask - the mask salesman and the Skulltula man - so
     * handing a mask over still ends with it off.
     */
    sSavedMask = player->currentMask;
}
