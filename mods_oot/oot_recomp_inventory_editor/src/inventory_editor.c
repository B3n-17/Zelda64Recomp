/*
 * In-place inventory editor for Ocarina of Time's pause menu.
 *
 * A port of the inventory editor in ProxySaw's Majora's Mask DevTools mod:
 * press a button on the pause menu to enter edit mode, then press A on any slot
 * to grant, cycle or remove what lives there. No separate screen, no new UI - the
 * normal pause menu simply becomes editable, and the cursor turns green to say so.
 *
 * This is deliberately NOT the game's own inventory editor. That one is still in
 * the retail ROM - KaleidoScope_DrawInventoryEditor, which the debug builds
 * opened with L - but it is a separate full-screen text grid that replaces the
 * pause menu, and nothing in this tree reaches it. This edits the pause menu you
 * are already looking at.
 *
 *
 * Why this needs cursor code at all
 * ---------------------------------
 * In Majora's Mask the pause cursor moves over every slot in the grid whether or
 * not the slot holds anything, so its editor only has to read the cursor and act
 * on A. Ocarina of Time's cursor does the opposite on all three editable pages:
 * every movement step is a search that only stops on something you already own.
 * z_kaleido_item.c keeps stepping until `items[cursorPoint] != ITEM_NONE`,
 * z_kaleido_equipment.c until the equipment bit is set, and z_kaleido_collect.c
 * until KaleidoScope_UpdateQuestStatusPoint says yes. An editor that can only
 * reach what you already have is not an editor.
 *
 * So while edit mode is on this mod drives the cursor itself: it reads the stick,
 * steps to the neighbouring cell with no ownership test, and zeroes stickAdjX/Y
 * so the vanilla search never runs. Everything else about the page - drawing,
 * page switching, the name panel - is left alone.
 *
 * The alternative was to briefly fake ownership of everything so the vanilla
 * search would roam freely. That is less code, and it was rejected: it means the
 * save data is a lie for part of every frame, and anything that reads or persists
 * the save in that window (a quicksave, a rewind) captures the lie. This way the
 * only writes to the save are the ones A asks for.
 *
 *
 * The hang this has to avoid
 * --------------------------
 * The item page's horizontal search is a `do {} while (cursorMoveResult == 0)`
 * with nothing to stop it if no slot in the grid is occupied, and it is entered
 * whenever the cursor is on the grid holding nothing - the page forces
 * `stickAdjX = 40` to make it happen. Vanilla is safe only because the cursor can
 * never be on an empty grid in the first place. An editor can leave it exactly
 * there: park the cursor mid-grid, remove the last item, exit edit mode.
 *
 * Two guards, both required:
 *   - in edit mode, cursorItem is forced non-empty so the page never starts the
 *     search (see editor_run), and
 *   - out of edit mode, a cursor sitting on a grid with nothing in it is moved to
 *     the page-edge position (see item_page_keep_cursor_safe), which is where
 *     vanilla itself puts it when the inventory is empty.
 */
#include "modding.h"

// OoT's decomp has no monolithic global.h, so headers are pulled in individually
// the same way patches_oot does it.
#include "array_count.h"
#include "controller.h"
#include "inventory.h"
#include "item.h"
#include "pause.h"
#include "play_state.h"
// For R_PAUSE_STICK_REPEAT_DELAY and friends, so the editor's cursor repeats at
// whatever rate the pause menu itself is set to rather than a copied constant.
#include "regs.h"
#include "save.h"
#include "sfx.h"

// The kaleido overlay's own header, for ITEM_GRID_ROWS/COLS, EQUIP_CURSOR_X_UPG
// and the two cursor helpers below. Reached through -I $(OOT_DIR)/src.
#include "overlays/misc/ovl_kaleido_scope/z_kaleido_scope.h"

/*
 * Declared in z_parameter.c and z_inventory.c rather than in a header, but
 * exported, so the recompiler resolves them by name against the base game.
 */
void Inventory_DeleteItem(u16 item, u16 invSlot);
s32 Inventory_ReplaceItem(PlayState* play, u16 oldItem, u16 newItem);

/* The mod's config options, matching the enums declared in mod.toml. */
RECOMP_IMPORT("*", u32 recomp_get_config_u32(const char* key));

/*
 * Edit mode is toggled with L, which is what the Majora's Mask mod uses and is
 * unbound in OoT's pause menu. Note that a patch reading L in
 * KaleidoScope_Update - the game's own inventory editor did exactly that in the
 * debug builds - would see the press before this mod does.
 */
#define TOGGLE_BUTTON BTN_L

// The four bits of Inventory.questItems that hold the heart piece count, as a
// count rather than as flags. QUEST_HEART_PIECE is the cursor position;
// QUEST_HEART_PIECE_COUNT is where the number lives.
#define HEART_PIECE_SHIFT QUEST_HEART_PIECE_COUNT
#define HEART_PIECE_MASK (0xFu << HEART_PIECE_SHIFT)
#define HEART_PIECES_PER_CONTAINER 4

// Equipment and upgrades share a 4x4 grid: column 0 is the upgrade column and
// columns 1..3 are the three items of that row's equipment type.
#define EQUIP_GRID_COLS 4
#define EQUIP_GRID_ROWS 4

#define UPGRADE_MAX_LEVEL 3

// Whether edit mode is on. Reset by editor_stop, which every path that leaves the
// pause menu goes through.
static u8 sEditing = false;

/* ------------------------------------------------------------------------- *
 *  What A does to an item slot
 * ------------------------------------------------------------------------- *
 *
 * One row per inventory slot, giving the range of item IDs that slot can hold.
 * A walks the range and then back to empty: none -> first -> ... -> last -> none.
 *
 * A range rather than a list because every slot that holds more than one thing
 * holds a contiguous run of ItemID - the two ocarinas, hookshot and longshot, the
 * thirteen bottle contents, and both trade sequences are each consecutive in the
 * enum. A pair of IDs per slot says the same thing as a list of pointers and
 * counts, and cannot fall out of step with itself.
 *
 * first == last is a slot with one item, which is a plain on/off toggle and is
 * what most slots are.
 */
typedef struct SlotItems {
    /* 0x0 */ u8 first;
    /* 0x1 */ u8 last;
} SlotItems; // size = 0x2

static const SlotItems sSlotItems[ITEM_GRID_ROWS * ITEM_GRID_COLS] = {
    [SLOT_DEKU_STICK] = { ITEM_DEKU_STICK, ITEM_DEKU_STICK },
    [SLOT_DEKU_NUT] = { ITEM_DEKU_NUT, ITEM_DEKU_NUT },
    [SLOT_BOMB] = { ITEM_BOMB, ITEM_BOMB },
    [SLOT_BOW] = { ITEM_BOW, ITEM_BOW },
    [SLOT_ARROW_FIRE] = { ITEM_ARROW_FIRE, ITEM_ARROW_FIRE },
    [SLOT_DINS_FIRE] = { ITEM_DINS_FIRE, ITEM_DINS_FIRE },
    [SLOT_SLINGSHOT] = { ITEM_SLINGSHOT, ITEM_SLINGSHOT },
    [SLOT_OCARINA] = { ITEM_OCARINA_FAIRY, ITEM_OCARINA_OF_TIME },
    [SLOT_BOMBCHU] = { ITEM_BOMBCHU, ITEM_BOMBCHU },
    [SLOT_HOOKSHOT] = { ITEM_HOOKSHOT, ITEM_LONGSHOT },
    [SLOT_ARROW_ICE] = { ITEM_ARROW_ICE, ITEM_ARROW_ICE },
    [SLOT_FARORES_WIND] = { ITEM_FARORES_WIND, ITEM_FARORES_WIND },
    [SLOT_BOOMERANG] = { ITEM_BOOMERANG, ITEM_BOOMERANG },
    [SLOT_LENS_OF_TRUTH] = { ITEM_LENS_OF_TRUTH, ITEM_LENS_OF_TRUTH },
    [SLOT_MAGIC_BEAN] = { ITEM_MAGIC_BEAN, ITEM_MAGIC_BEAN },
    [SLOT_HAMMER] = { ITEM_HAMMER, ITEM_HAMMER },
    [SLOT_ARROW_LIGHT] = { ITEM_ARROW_LIGHT, ITEM_ARROW_LIGHT },
    [SLOT_NAYRUS_LOVE] = { ITEM_NAYRUS_LOVE, ITEM_NAYRUS_LOVE },

    // Every bottle cycles the full set of contents, ITEM_BOTTLE_EMPTY through
    // ITEM_BOTTLE_POE.
    [SLOT_BOTTLE_1] = { ITEM_BOTTLE_EMPTY, ITEM_BOTTLE_POE },
    [SLOT_BOTTLE_2] = { ITEM_BOTTLE_EMPTY, ITEM_BOTTLE_POE },
    [SLOT_BOTTLE_3] = { ITEM_BOTTLE_EMPTY, ITEM_BOTTLE_POE },
    [SLOT_BOTTLE_4] = { ITEM_BOTTLE_EMPTY, ITEM_BOTTLE_POE },

    // The trade quests, in order. Pocket Egg through Claim Check, and Weird Egg
    // through the Mask of Truth.
    [SLOT_TRADE_ADULT] = { ITEM_POCKET_EGG, ITEM_CLAIM_CHECK },
    [SLOT_TRADE_CHILD] = { ITEM_WEIRD_EGG, ITEM_MASK_TRUTH },
};

/*
 * The upgrade shown in the upgrade column, by row. Child and adult differ in the
 * first row only: the same cell is the bullet bag for child and the quiver for
 * adult, which is what z_kaleido_equipment.c draws there.
 */
static const u8 sChildRowUpgrades[EQUIP_GRID_ROWS] = { UPG_BULLET_BAG, UPG_BOMB_BAG, UPG_STRENGTH, UPG_SCALE };
static const u8 sAdultRowUpgrades[EQUIP_GRID_ROWS] = { UPG_QUIVER, UPG_BOMB_BAG, UPG_STRENGTH, UPG_SCALE };

/*
 * Where the quest page cursor can go, as {up, down, left, right} per position.
 *
 * A verbatim copy of sCursorPointLinks in z_kaleido_collect.c, because the quest
 * page is not a grid - it is a hand-authored graph, and reproducing its shape any
 * other way would mean inventing a layout the page does not have. What differs is
 * only how the destination is used: the original walks the links until it finds a
 * quest item the player owns, and this stops at the first one either way.
 *
 * The negative entries are the original's CURSOR_TO_LEFT (-3), CURSOR_TO_RIGHT
 * (-2) and CURSOR_NONE (-1). All three mean "no cell that way" here: the first
 * two scroll to the next page in vanilla, and in edit mode the cursor stays put
 * instead, leaving page switching to Z and R where it cannot be triggered by
 * accident mid-edit.
 */
#define QUEST_CURSOR_NONE (-1)

static const s8 sQuestCursorLinks[QUEST_HEART_PIECE + 1][4] = {
    /* QUEST_MEDALLION_FOREST */
    { QUEST_MEDALLION_LIGHT, QUEST_MEDALLION_FIRE, QUEST_MEDALLION_LIGHT, QUEST_CURSOR_NONE },
    /* QUEST_MEDALLION_FIRE */
    { QUEST_MEDALLION_FOREST, QUEST_MEDALLION_WATER, QUEST_MEDALLION_WATER, QUEST_CURSOR_NONE },
    /* QUEST_MEDALLION_WATER */
    { QUEST_CURSOR_NONE, QUEST_GORON_RUBY, QUEST_MEDALLION_SPIRIT, QUEST_MEDALLION_FIRE },
    /* QUEST_MEDALLION_SPIRIT */
    { QUEST_MEDALLION_SHADOW, QUEST_MEDALLION_WATER, QUEST_SONG_STORMS, QUEST_MEDALLION_WATER },
    /* QUEST_MEDALLION_SHADOW */
    { QUEST_MEDALLION_LIGHT, QUEST_MEDALLION_SPIRIT, QUEST_HEART_PIECE, QUEST_MEDALLION_LIGHT },
    /* QUEST_MEDALLION_LIGHT */
    { QUEST_CURSOR_NONE, QUEST_CURSOR_NONE, QUEST_MEDALLION_SHADOW, QUEST_MEDALLION_FOREST },
    /* QUEST_SONG_MINUET */
    { QUEST_SONG_LULLABY, QUEST_CURSOR_NONE, QUEST_CURSOR_NONE, QUEST_SONG_BOLERO },
    /* QUEST_SONG_BOLERO */
    { QUEST_SONG_EPONA, QUEST_CURSOR_NONE, QUEST_SONG_MINUET, QUEST_SONG_SERENADE },
    /* QUEST_SONG_SERENADE */
    { QUEST_SONG_SARIA, QUEST_CURSOR_NONE, QUEST_SONG_BOLERO, QUEST_SONG_REQUIEM },
    /* QUEST_SONG_REQUIEM */
    { QUEST_SONG_SUN, QUEST_CURSOR_NONE, QUEST_SONG_SERENADE, QUEST_SONG_NOCTURNE },
    /* QUEST_SONG_NOCTURNE */
    { QUEST_SONG_TIME, QUEST_CURSOR_NONE, QUEST_SONG_REQUIEM, QUEST_SONG_PRELUDE },
    /* QUEST_SONG_PRELUDE */
    { QUEST_SONG_STORMS, QUEST_CURSOR_NONE, QUEST_SONG_NOCTURNE, QUEST_KOKIRI_EMERALD },
    /* QUEST_SONG_LULLABY */
    { QUEST_SKULL_TOKEN, QUEST_SONG_MINUET, QUEST_CURSOR_NONE, QUEST_SONG_EPONA },
    /* QUEST_SONG_EPONA */
    { QUEST_SKULL_TOKEN, QUEST_SONG_BOLERO, QUEST_SONG_LULLABY, QUEST_SONG_SARIA },
    /* QUEST_SONG_SARIA */
    { QUEST_SKULL_TOKEN, QUEST_SONG_SERENADE, QUEST_SONG_EPONA, QUEST_SONG_SUN },
    /* QUEST_SONG_SUN */
    { QUEST_HEART_PIECE, QUEST_SONG_REQUIEM, QUEST_SONG_SARIA, QUEST_SONG_TIME },
    /* QUEST_SONG_TIME */
    { QUEST_HEART_PIECE, QUEST_SONG_NOCTURNE, QUEST_SONG_SUN, QUEST_SONG_STORMS },
    /* QUEST_SONG_STORMS */
    { QUEST_HEART_PIECE, QUEST_SONG_PRELUDE, QUEST_SONG_TIME, QUEST_MEDALLION_SPIRIT },
    /* QUEST_KOKIRI_EMERALD */
    { QUEST_MEDALLION_WATER, QUEST_CURSOR_NONE, QUEST_SONG_PRELUDE, QUEST_GORON_RUBY },
    /* QUEST_GORON_RUBY */
    { QUEST_MEDALLION_WATER, QUEST_CURSOR_NONE, QUEST_KOKIRI_EMERALD, QUEST_ZORA_SAPPHIRE },
    /* QUEST_ZORA_SAPPHIRE */
    { QUEST_MEDALLION_WATER, QUEST_CURSOR_NONE, QUEST_GORON_RUBY, QUEST_CURSOR_NONE },
    /* QUEST_STONE_OF_AGONY */
    { QUEST_CURSOR_NONE, QUEST_SKULL_TOKEN, QUEST_CURSOR_NONE, QUEST_GERUDOS_CARD },
    /* QUEST_GERUDOS_CARD */
    { QUEST_CURSOR_NONE, QUEST_SKULL_TOKEN, QUEST_STONE_OF_AGONY, QUEST_HEART_PIECE },
    /* QUEST_SKULL_TOKEN */
    { QUEST_STONE_OF_AGONY, QUEST_SONG_LULLABY, QUEST_CURSOR_NONE, QUEST_HEART_PIECE },
    /* QUEST_HEART_PIECE */
    { QUEST_CURSOR_NONE, QUEST_SONG_TIME, QUEST_GERUDOS_CARD, QUEST_MEDALLION_SHADOW },
};

/* ------------------------------------------------------------------------- *
 *  Editing
 * ------------------------------------------------------------------------- */

// Bring an upgrade up to at least its first level, so that granting the item that
// uses it also gives somewhere to put the ammo. A quiver of zero holds no arrows.
static void ensure_upgrade(s16 upgrade) {
    if (CUR_UPG_VALUE(upgrade) == 0) {
        Inventory_ChangeUpgrade(upgrade, 1);
    }
}

/*
 * Fill the ammo, and the capacity to hold it, for a slot that has just been
 * granted. Slots not listed here hold no ammo, so there is nothing to do.
 */
static void fill_slot_ammo(u16 slot) {
    switch (slot) {
        case SLOT_DEKU_STICK:
            ensure_upgrade(UPG_DEKU_STICKS);
            AMMO(ITEM_DEKU_STICK) = CUR_CAPACITY(UPG_DEKU_STICKS);
            break;

        case SLOT_DEKU_NUT:
            ensure_upgrade(UPG_DEKU_NUTS);
            AMMO(ITEM_DEKU_NUT) = CUR_CAPACITY(UPG_DEKU_NUTS);
            break;

        case SLOT_BOMB:
            ensure_upgrade(UPG_BOMB_BAG);
            AMMO(ITEM_BOMB) = CUR_CAPACITY(UPG_BOMB_BAG);
            break;

        case SLOT_BOW:
            ensure_upgrade(UPG_QUIVER);
            AMMO(ITEM_BOW) = CUR_CAPACITY(UPG_QUIVER);
            break;

        case SLOT_SLINGSHOT:
            ensure_upgrade(UPG_BULLET_BAG);
            AMMO(ITEM_SLINGSHOT) = CUR_CAPACITY(UPG_BULLET_BAG);
            break;

        // Bombchus and beans have no upgrade behind them, just a fixed maximum.
        case SLOT_BOMBCHU:
            AMMO(ITEM_BOMBCHU) = 50;
            break;

        case SLOT_MAGIC_BEAN:
            AMMO(ITEM_MAGIC_BEAN) = 10;
            break;
    }
}

/*
 * A on the item page: advance the slot to the next thing it can hold.
 *
 * Removal goes through Inventory_DeleteItem and a change of contents through
 * Inventory_ReplaceItem rather than writing items[slot] directly, because both
 * also fix up the C buttons. Writing the array alone leaves a C button holding an
 * item the inventory no longer has, which the HUD will happily draw and the
 * player will happily try to use.
 */
static void toggle_item_slot(PlayState* play, u16 slot) {
    const SlotItems* range;
    u16 current;

    if (slot >= ARRAY_COUNT(sSlotItems)) {
        return;
    }

    range = &sSlotItems[slot];
    if (range->first == ITEM_NONE) {
        // A slot with no entry in the table. There are none today; this keeps a
        // future gap in the table from granting item 0 by accident.
        return;
    }

    current = gSaveContext.save.info.inventory.items[slot];

    if (current == ITEM_NONE) {
        gSaveContext.save.info.inventory.items[slot] = range->first;
        fill_slot_ammo(slot);
    } else if (current < range->first || current >= range->last) {
        // At the end of the range, or holding something the table does not list:
        // either way the next step is empty.
        Inventory_DeleteItem(current, slot);
    } else {
        Inventory_ReplaceItem(play, current, current + 1);
    }
}

/*
 * A on the equipment page: cycle the upgrade in column 0, or toggle ownership of
 * the equipment in columns 1..3.
 *
 * `point` is the cursor position, which is row * 4 + column. The equipment bit
 * for a cell is point - 1: equipment packs four bits per type, and column 1..3
 * are the first three of the row's nibble.
 */
static void toggle_equip_cell(PlayState* play, s16 point) {
    PauseContext* pauseCtx = &play->pauseCtx;
    s16 column = point % EQUIP_GRID_COLS;
    s16 row = point / EQUIP_GRID_COLS;
    s16 equipValue;

    if (point < 0 || row >= EQUIP_GRID_ROWS) {
        return;
    }

    if (column == EQUIP_CURSOR_X_UPG) {
        s16 upgrade = (LINK_AGE_IN_YEARS == YEARS_CHILD) ? sChildRowUpgrades[row] : sAdultRowUpgrades[row];
        s16 level = CUR_UPG_VALUE(upgrade) + 1;

        if (level > UPGRADE_MAX_LEVEL) {
            level = 0;
        }
        Inventory_ChangeUpgrade(upgrade, level);
        return;
    }

    // Column 1 is EQUIP_VALUE_*_1, so the column index is the equip value and
    // column - 1 is the EQUIP_INV_* index of the same thing.
    equipValue = column;

    if (CHECK_OWNED_EQUIP(row, column - 1)) {
        if (CUR_EQUIP_VALUE(row) == equipValue) {
            /*
             * Removing what Link is currently wearing. Inventory_DeleteEquipment
             * is the only thing that unequips correctly - it clears the owned
             * bit, drops back to the Kokiri Tunic, empties the B button for a
             * sword, and re-derives the player's equipment data.
             *
             * It also parks the cursor at the left page edge, which is right when
             * the game does it from a page the player is leaving and wrong here,
             * so that one side effect is undone.
             */
            s16 specialPos = pauseCtx->cursorSpecialPos;

            Inventory_DeleteEquipment(play, row);
            pauseCtx->cursorSpecialPos = specialPos;
        } else {
            gSaveContext.save.info.inventory.equipment ^= OWNED_EQUIP_FLAG(row, column - 1);
        }
    } else {
        gSaveContext.save.info.inventory.equipment |= OWNED_EQUIP_FLAG(row, column - 1);

        /*
         * Granting the Biggoron's Sword slot gives the real sword rather than the
         * Giant's Knife. Without bgsFlag the same cell is the breakable knife,
         * and the equip code reads bgsFlag to decide which of the two it is
         * handing over; swordHealth is what the knife has left before it snaps.
         */
        if ((row == EQUIP_TYPE_SWORD) && ((column - 1) == EQUIP_INV_SWORD_BIGGORON)) {
            gSaveContext.save.info.playerData.bgsFlag = true;
            gSaveContext.save.info.playerData.swordHealth = 8;
        }
    }
}

/*
 * A on the quest page: toggle the quest item, or step the heart piece count.
 *
 * Heart pieces are the one position that is a count rather than a flag - four
 * bits holding 0 to 3, which is why it wraps at four rather than toggling.
 */
static void toggle_quest_cell(s16 point) {
    u32* questItems = &gSaveContext.save.info.inventory.questItems;

    if (point == QUEST_HEART_PIECE) {
        u32 pieces = (*questItems & HEART_PIECE_MASK) >> HEART_PIECE_SHIFT;

        pieces = (pieces + 1) % HEART_PIECES_PER_CONTAINER;
        *questItems = (*questItems & ~HEART_PIECE_MASK) | (pieces << HEART_PIECE_SHIFT);
        return;
    }

    if (point < 0 || point >= QUEST_HEART_PIECE) {
        return;
    }

    *questItems ^= gBitFlags[point];
}

/* ------------------------------------------------------------------------- *
 *  Cursor movement
 * ------------------------------------------------------------------------- */

/*
 * Step one cell through a rectangular grid, wrapping at the edges.
 *
 * Wrapping rather than scrolling to the next page, for the same reason the quest
 * table's page links are dropped: while editing, an overshoot at the edge of the
 * grid should not throw the player onto another page.
 */
static void grid_move(PauseContext* pauseCtx, u16 page, s16 cols, s16 rows, s16 dx, s16 dy) {
    s16 x = pauseCtx->cursorX[page] + dx;
    s16 y = pauseCtx->cursorY[page] + dy;

    if (x < 0) {
        x = cols - 1;
    } else if (x >= cols) {
        x = 0;
    }

    if (y < 0) {
        y = rows - 1;
    } else if (y >= rows) {
        y = 0;
    }

    pauseCtx->cursorX[page] = x;
    pauseCtx->cursorY[page] = y;
    pauseCtx->cursorPoint[page] = (y * cols) + x;
    pauseCtx->cursorSlot[page] = pauseCtx->cursorPoint[page];
}

// Step one position along the quest page's link graph, ignoring ownership.
static void quest_move(PauseContext* pauseCtx, s16 dx, s16 dy) {
    s16 point = pauseCtx->cursorPoint[PAUSE_QUEST];
    s16 next;

    if (point < 0 || point > QUEST_HEART_PIECE) {
        point = QUEST_MEDALLION_FOREST;
    }

    if (dx != 0) {
        next = sQuestCursorLinks[point][(dx < 0) ? 2 : 3];
        if (next >= 0) {
            point = next;
        }
    }

    if (dy != 0) {
        next = sQuestCursorLinks[point][(dy < 0) ? 0 : 1];
        if (next >= 0) {
            point = next;
        }
    }

    pauseCtx->cursorPoint[PAUSE_QUEST] = point;
    pauseCtx->cursorSlot[PAUSE_QUEST] = point;
}

/*
 * One axis of stick input, turned into at most one step per press with the pause
 * menu's own repeat timing: a step, then a wait of R_PAUSE_STICK_REPEAT_DELAY_
 * FIRST frames, then a step every R_PAUSE_STICK_REPEAT_DELAY frames while held.
 *
 * Duplicating that timing rather than reading pauseCtx->stickAdjX/Y is forced by
 * where the vanilla copy of it lives. The pause menu filters the stick inside
 * KaleidoScope_DrawPages, which runs during KaleidoScope_Draw - after this hook,
 * not before it - so stickAdjX/Y still hold the previous frame's filtered values
 * when this runs. Reading those gave a cursor that acted a frame late on stale
 * input. The raw stick is the only value here that describes this frame.
 *
 * The thresholds and the direction of the Y axis are the page code's own, so a
 * step here happens on exactly the same stick position a vanilla step would.
 */
static s16 stick_step(s16 value, s16* state, s16* timer) {
    s16 dir = (value < -30) ? -1 : (value > 30) ? 1 : 0;

    if (dir == 0) {
        *state = 0;
        return 0;
    }

    if (*state != dir) {
        // First frame in this direction: step immediately, then hold off.
        *state = dir;
        *timer = R_PAUSE_STICK_REPEAT_DELAY_FIRST;
        return dir;
    }

    (*timer)--;
    if (*timer < 0) {
        *timer = R_PAUSE_STICK_REPEAT_DELAY;
        return dir;
    }

    return 0;
}

// Move the cursor for `page` according to the stick, and report whether it moved.
static u8 move_cursor(PauseContext* pauseCtx, Input* input, u16 page) {
    static s16 stickXState = 0;
    static s16 stickXTimer = 0;
    static s16 stickYState = 0;
    static s16 stickYTimer = 0;

    s16 dx = stick_step(input->rel.stick_x, &stickXState, &stickXTimer);
    // Stick up is positive, which is one row towards zero.
    s16 dy = -stick_step(input->rel.stick_y, &stickYState, &stickYTimer);
    s16 before;

    if ((dx == 0) && (dy == 0)) {
        return false;
    }

    before = pauseCtx->cursorPoint[page];

    switch (page) {
        case PAUSE_ITEM:
            grid_move(pauseCtx, PAUSE_ITEM, ITEM_GRID_COLS, ITEM_GRID_ROWS, dx, dy);
            break;

        case PAUSE_EQUIP:
            grid_move(pauseCtx, PAUSE_EQUIP, EQUIP_GRID_COLS, EQUIP_GRID_ROWS, dx, dy);
            break;

        case PAUSE_QUEST:
            quest_move(pauseCtx, dx, dy);
            break;
    }

    return pauseCtx->cursorPoint[page] != before;
}

/*
 * Keep the item page out of the state its cursor search cannot get out of: on the
 * grid with nothing in the grid to find. See the note at the top of the file.
 */
static void item_page_keep_cursor_safe(PlayState* play) {
    PauseContext* pauseCtx = &play->pauseCtx;
    u16 i;

    if (pauseCtx->cursorSpecialPos != 0) {
        return;
    }

    for (i = 0; i < ARRAY_COUNT(gSaveContext.save.info.inventory.items); i++) {
        if (gSaveContext.save.info.inventory.items[i] != ITEM_NONE) {
            return;
        }
    }

    KaleidoScope_MoveCursorToSpecialPos(play, PAUSE_CURSOR_PAGE_LEFT);
}

/* ------------------------------------------------------------------------- *
 *  Edit mode
 * ------------------------------------------------------------------------- */

static void editor_start(PlayState* play, u16 page) {
    PauseContext* pauseCtx = &play->pauseCtx;

    sEditing = true;

    /*
     * The cursor may be parked on one of the page-edge positions, where there is
     * no cell under it to edit. Put it back on the page at its first position -
     * cursorPoint is kept up to date there by vanilla, but cursorSpecialPos is
     * what decides whether the cursor is on the page at all.
     */
    if (pauseCtx->cursorSpecialPos != 0) {
        pauseCtx->cursorSpecialPos = 0;
        pauseCtx->cursorPoint[page] = 0;
        pauseCtx->cursorX[page] = 0;
        pauseCtx->cursorY[page] = 0;
        pauseCtx->cursorSlot[page] = 0;
    }

    SFX_PLAY_CENTERED(NA_SE_SY_DECIDE);
}

static void editor_stop(PlayState* play) {
    sEditing = false;
    item_page_keep_cursor_safe(play);
    SFX_PLAY_CENTERED(NA_SE_SY_CANCEL);
}

// Whether a page has anything to edit. The map page does not.
static u8 page_is_editable(u16 page) {
    return (page == PAUSE_ITEM) || (page == PAUSE_EQUIP) || (page == PAUSE_QUEST);
}

/*
 * The whole editor, once per frame, for whichever page is on screen.
 */
static void editor_run(PlayState* play) {
    PauseContext* pauseCtx = &play->pauseCtx;
    Input* input = &play->state.input[0];
    u16 page = pauseCtx->pageIndex;

    if (!IS_PAUSED(pauseCtx)) {
        sEditing = false;
        return;
    }

    /*
     * Editing is only allowed while the menu is sitting still and waiting for
     * input. PAUSE_MAIN_STATE_IDLE_CURSOR_ON_SONG is the same idle state with the
     * quest page cursor on a song, so the quest page needs it too; every other
     * main state is an animation, a prompt or a song playing, and none of them
     * want a second reader of the A button.
     *
     * This is also what turns edit mode off on the way out of the menu. The
     * debugState test costs nothing and means that if the ROM's own full-screen
     * editor is ever reachable again, whichever one is open owns the buttons.
     */
    if ((pauseCtx->state != PAUSE_STATE_MAIN) || (pauseCtx->debugState != PAUSE_DEBUG_STATE_CLOSED) ||
        ((pauseCtx->mainState != PAUSE_MAIN_STATE_IDLE) &&
         (pauseCtx->mainState != PAUSE_MAIN_STATE_IDLE_CURSOR_ON_SONG)) ||
        !recomp_get_config_u32("editor_enabled")) {
        if (sEditing) {
            editor_stop(play);
        } else if (page == PAUSE_ITEM) {
            item_page_keep_cursor_safe(play);
        }
        return;
    }

    if (page_is_editable(page) && CHECK_BTN_ALL(input->press.button, TOGGLE_BUTTON)) {
        if (sEditing) {
            editor_stop(play);
        } else {
            editor_start(play, page);
        }
    }

    if (!sEditing || !page_is_editable(page)) {
        if (page == PAUSE_ITEM) {
            item_page_keep_cursor_safe(play);
        }
        return;
    }

    if (move_cursor(pauseCtx, input, page)) {
        SFX_PLAY_CENTERED(NA_SE_SY_CURSOR);
    }

    if (CHECK_BTN_ALL(input->press.button, BTN_A)) {
        // Taken from the page, which would otherwise equip the item under the
        // cursor or play the song under it in the same frame.
        input->press.button &= ~BTN_A;

        switch (page) {
            case PAUSE_ITEM:
                toggle_item_slot(play, pauseCtx->cursorPoint[PAUSE_ITEM]);
                break;

            case PAUSE_EQUIP:
                toggle_equip_cell(play, pauseCtx->cursorPoint[PAUSE_EQUIP]);
                break;

            case PAUSE_QUEST:
                toggle_quest_cell(pauseCtx->cursorPoint[PAUSE_QUEST]);
                break;
        }

        SFX_PLAY_CENTERED(NA_SE_SY_DECIDE);
    }

    /*
     * The page's own cursor search must not run on top of the move just made,
     * and clearing stickAdjX/Y is not enough to stop it. KaleidoScope_Draw opens
     * by reloading both of them straight from input->rel, so anything written
     * here is overwritten before a single page function runs:
     *
     *     pauseCtx->stickAdjX = input->rel.stick_x;
     *     pauseCtx->stickAdjY = input->rel.stick_y;
     *
     * That reload is what made editing feel broken rather than merely wrong: the
     * editor moved the cursor one way while the page's own search dragged it to
     * the nearest owned slot, every frame, from the same stick push. Zeroing the
     * source as well as the copy is what actually leaves the cursor alone.
     *
     * Safe because the pause menu is the only thing still to read the stick this
     * frame - the player and camera updates are long past by the time
     * Interface_Update runs, and the next frame brings fresh input.
     */
    pauseCtx->stickAdjX = 0;
    pauseCtx->stickAdjY = 0;
    input->rel.stick_x = 0;
    input->rel.stick_y = 0;

    if (page == PAUSE_ITEM) {
        /*
         * Two things the item page only does for a slot that holds something, and
         * the editor needs for every slot:
         *
         * cursorItem is what the page tests to decide whether to start the search
         * that has no exit when the grid is empty. Anything other than
         * PAUSE_ITEM_NONE stops it; the page overwrites this with the slot's real
         * contents further down, so nothing downstream sees the placeholder.
         *
         * The cursor quad is only repositioned for an occupied slot, so on an
         * empty one it would be left behind at the last item the cursor visited.
         */
        pauseCtx->cursorItem[PAUSE_ITEM] = ITEM_SOLD_OUT;
        KaleidoScope_SetCursorPos(pauseCtx, pauseCtx->cursorPoint[PAUSE_ITEM] * 4, pauseCtx->itemVtx);
    }
}

/* ------------------------------------------------------------------------- *
 *  The hook
 * ------------------------------------------------------------------------- *
 *
 * Interface_Update, which is the one place in the frame that has everything this
 * needs. In Play_Update it runs immediately after KaleidoScopeCall_Update, which
 * is what turns the raw stick into pauseCtx->stickAdjX/Y with the pause menu's
 * repeat delay applied, and well before Play_DrawOverlayElements reaches
 * KaleidoScopeCall_Draw, which is where OoT does its page cursor work. So the
 * cursor can be moved and the A press taken before any page sees either, and the
 * stick values read here are the same ones the pages would have used.
 *
 * The obvious targets were the three page functions themselves, and those cannot
 * be hooked in this tree: hooking makes the runtime regenerate the target out of
 * the ROM with the live recompiler, and for all three of those the regeneration
 * fails to resolve a call - "Failed to load mod code (Code mod loading internal
 * error)" at startup, with the useful half of the message dropped on the floor by
 * apply_regenlist. They are all in ovl_kaleido_scope. Interface_Update is in the
 * always-loaded code section, which is where the D-Pad mod's proven hooks live.
 *
 * The one thing lost with them is the green cursor that used to say edit mode was
 * on: cursorColorSet is assigned by each page during its own draw, after this
 * runs, so it cannot be set from here and stick. Edit mode announces itself with
 * a sound instead, and gives itself away by sitting on empty slots.
 */

RECOMP_HOOK("Interface_Update")
void inv_editor_on_interface_update(PlayState* play) {
    editor_run(play);
}
