/*
 * D-Pad quick equip for Ocarina of Time.
 *
 * Bindings, which differ by age because the two Links own different things:
 *
 *   Up     both    play the ocarina (Fairy Ocarina or Ocarina of Time)
 *   Left   adult   cycle boots      (Kokiri / Iron / Hover)
 *   Down   adult   cycle tunics     (Kokiri / Goron / Zora)
 *   Right  adult   warp             - not implemented yet
 *   Left   child   toggle Bunny Hood
 *   Down   child   cycle shields    (Deku / Hylian / none)
 *   Right  child   warp             - not implemented yet
 *
 * Every binding is gated on actually owning the thing: a direction whose item is
 * not in the save does nothing at all, rather than playing an error sound or
 * equipping something the player has not found. The unimplemented warp slots
 * behave exactly as an unowned item does, so they cost nothing until they exist.
 *
 * This file is currently the input and action half only. Drawing the widget
 * needs an icon buffer and DMA out of icon_item_static, and is deliberately a
 * separate step so that the half that changes the game can be tested on its own.
 */
#include "modding.h"

// OoT's decomp has no monolithic global.h, so headers are pulled in individually
// the same way patches_oot does it.
#include "array_count.h"
#include "controller.h"
#include "dma.h"
#include "gfx.h"
#include "gfx_setupdl.h"
#include "interface.h"
#include "inventory.h"
#include "item.h"
#include "play_state.h"
#include "player.h"
#include "save.h"
#include "segment_symbols.h"

// gEXSetRectAlign and friends, the RT64 extended GBI that the widescreen HUD
// anchoring in patches_oot/ui_patches.c is built on.
#include "rt64_extended_gbi.h"

/*
 * The mod config option, matching the "draw_dpad" enum declared in mod.toml.
 * Imported from base recomp ("*") the same way the Majora's Mask D-Pad mod does
 * it - its symbol file names exactly two imports, recomp_get_config_u32 and one
 * export of its own, which is what confirmed this is the supported route.
 */
RECOMP_IMPORT("*")
u32 recomp_get_config_u32(const char* key) RECOMP_IMPORT_STUB(u32)

// Declared in z_player.c rather than a header, but exported, so the recompiler
// resolves them by name against the base game.
void Player_UseItem(PlayState* play, Player* this, s32 item);

// Whether the player has the widget switched on. Read every frame rather than
// cached, because the config menu can be opened mid-game.
u32 dpad_draw_enabled = 1;

/*
 * True when the game, not the player, owns the controls. Deliberately close to
 * the gate the free camera uses in patches_oot/camera_patches.c and for the same
 * reason: swapping boots mid-cutscene or while the pause menu is animating is
 * either ignored or actively wrong, and the game already has precise notions of
 * both states.
 */
static s32 dpad_input_blocked(PlayState* play) {
    return (play->pauseCtx.state != PAUSE_STATE_OFF) || (play->csCtx.state != CS_STATE_IDLE) ||
           (play->msgCtx.msgMode != MSGMODE_NONE) || (play->transitionTrigger != TRANS_TRIGGER_OFF) ||
           (play->transitionMode != TRANS_MODE_OFF) || (play->gameOverCtx.state != GAMEOVER_INACTIVE) ||
           (gSaveContext.gameMode != GAMEMODE_NORMAL) || Player_InCsMode(play);
}

/*
 * Step an equipment type to the next value its owner actually has.
 *
 * `values` lists the equip values in cycle order and `count` how many there are;
 * a value is skipped unless the save says it is owned. CHECK_OWNED_EQUIP wants
 * the inventory index rather than the equip value, and those differ by one
 * (EQUIP_VALUE_BOOTS_KOKIRI is 1, EQUIP_INV_BOOTS_KOKIRI is 0) - hence the -1,
 * which is the kind of thing that silently equips the wrong item if missed.
 *
 * A zero entry means "nothing equipped" and is always considered available; only
 * the shield cycle uses it, because OoT genuinely allows going shieldless and
 * dropping the Deku Shield before a fire fight is a real thing players want.
 */
static void dpad_cycle_equip(PlayState* play, s32 equip_type, const s32* values, s32 count) {
    s32 current = CUR_EQUIP_VALUE(equip_type);
    s32 start = 0;
    s32 i;

    // Find where the cycle currently sits, so the step goes forwards from here
    // rather than always restarting at the first entry.
    for (i = 0; i < count; i++) {
        if (values[i] == current) {
            start = i;
            break;
        }
    }

    for (i = 1; i <= count; i++) {
        s32 candidate = values[(start + i) % count];

        if ((candidate == 0) || CHECK_OWNED_EQUIP(equip_type, candidate - 1)) {
            // Nothing to do if the only owned entry is the one already equipped.
            if (candidate == current) {
                return;
            }

            Inventory_ChangeEquipment(equip_type, candidate);
            Player_SetEquipmentData(play, GET_PLAYER(play));
            return;
        }
    }
}

static const s32 dpad_boots_cycle[] = {
    EQUIP_VALUE_BOOTS_KOKIRI,
    EQUIP_VALUE_BOOTS_IRON,
    EQUIP_VALUE_BOOTS_HOVER,
};

static const s32 dpad_tunic_cycle[] = {
    EQUIP_VALUE_TUNIC_KOKIRI,
    EQUIP_VALUE_TUNIC_GORON,
    EQUIP_VALUE_TUNIC_ZORA,
};

// Child's shields, with EQUIP_VALUE_SHIELD_NONE in the ring. The Mirror Shield is
// left out on purpose: it is adult only, and child's D-Down is the only binding
// that reaches this cycle.
static const s32 dpad_shield_cycle[] = {
    EQUIP_VALUE_SHIELD_DEKU,
    EQUIP_VALUE_SHIELD_HYLIAN,
    EQUIP_VALUE_SHIELD_NONE,
};

// The ocarina the player owns, or ITEM_NONE if they own neither. The Ocarina of
// Time replaces the Fairy Ocarina in the same inventory slot, so this is a
// single lookup rather than two.
static s32 dpad_current_ocarina(void) {
    s32 item = INV_CONTENT(ITEM_OCARINA_FAIRY);

    if ((item == ITEM_OCARINA_FAIRY) || (item == ITEM_OCARINA_OF_TIME)) {
        return item;
    }

    return ITEM_NONE;
}

static void dpad_press_up(PlayState* play) {
    s32 ocarina = dpad_current_ocarina();

    if (ocarina == ITEM_NONE) {
        return;
    }

    // Player_UseItem is the same entry point Player_ProcessItemButtons calls when
    // a C button holding the ocarina is pressed, so this goes down the game's own
    // path - including its refusal to start while locked on to a hostile actor -
    // rather than reimplementing any of it.
    Player_UseItem(play, GET_PLAYER(play), ocarina);
}

static void dpad_press_left(PlayState* play) {
    if (LINK_IS_ADULT) {
        dpad_cycle_equip(play, EQUIP_TYPE_BOOTS, dpad_boots_cycle, ARRAY_COUNT(dpad_boots_cycle));
    } else if (INV_CONTENT(ITEM_MASK_BUNNY_HOOD) == ITEM_MASK_BUNNY_HOOD) {
        // Toggling the mask is left to the game: Player_UseItem flips currentMask
        // off if a mask is already worn and on otherwise.
        Player_UseItem(play, GET_PLAYER(play), ITEM_MASK_BUNNY_HOOD);
    }
}

static void dpad_press_down(PlayState* play) {
    if (LINK_IS_ADULT) {
        dpad_cycle_equip(play, EQUIP_TYPE_TUNIC, dpad_tunic_cycle, ARRAY_COUNT(dpad_tunic_cycle));
    } else {
        dpad_cycle_equip(play, EQUIP_TYPE_SHIELD, dpad_shield_cycle, ARRAY_COUNT(dpad_shield_cycle));
    }
}

/* ------------------------------------------------------------------------- *
 *  Drawing
 * ------------------------------------------------------------------------- */

// Which item icon belongs on each direction, or ITEM_NONE for a direction that
// should stay blank. Indices match the DPAD_* order below.
typedef enum DpadDir {
    DPAD_UP,
    DPAD_LEFT,
    DPAD_DOWN,
    DPAD_RIGHT,
    DPAD_MAX
} DpadDir;

/*
 * One 32x32 RGBA16 icon per direction, DMA'd out of icon_item_static.
 *
 * The mod cannot borrow interfaceCtx->iconItemSegment: that holds exactly the
 * four icons for the B and C buttons and the game refills it whenever the
 * player's equipment changes. Hence a buffer of its own, which lands in the
 * mod's .bss and costs 16KB.
 *
 * Aligned to 16 because a DMA destination has to be, and because the RSP loads
 * the texture straight out of it.
 */
static u8 dpad_icons[DPAD_MAX][ITEM_ICON_SIZE] __attribute__((aligned(16)));

// What is currently in each slot of dpad_icons, so the DMA only runs when the
// answer actually changes rather than four times a frame forever.
static s16 dpad_loaded_items[DPAD_MAX] = { ITEM_NONE, ITEM_NONE, ITEM_NONE, ITEM_NONE };

/*
 * The item whose icon represents each direction right now, or ITEM_NONE when the
 * direction should draw nothing.
 *
 * This is what implements "do not show anything the player has not unlocked":
 * the same ownership tests the actions use decide whether an icon exists at all,
 * so the widget can never advertise something pressing it would not do. The
 * icon shown for a cycle is the one that is currently equipped, which makes the
 * widget a readout as well as a menu.
 */
static s32 dpad_icon_for(DpadDir dir) {
    switch (dir) {
        case DPAD_UP:
            return dpad_current_ocarina();

        case DPAD_LEFT:
            if (LINK_IS_ADULT) {
                s32 boots = CUR_EQUIP_VALUE(EQUIP_TYPE_BOOTS);

                // Only worth showing once there is something to cycle to; Kokiri
                // Boots alone are what every save starts with.
                if (!CHECK_OWNED_EQUIP(EQUIP_TYPE_BOOTS, EQUIP_INV_BOOTS_IRON) &&
                    !CHECK_OWNED_EQUIP(EQUIP_TYPE_BOOTS, EQUIP_INV_BOOTS_HOVER)) {
                    return ITEM_NONE;
                }

                if (boots == EQUIP_VALUE_BOOTS_IRON) {
                    return ITEM_BOOTS_IRON;
                }
                if (boots == EQUIP_VALUE_BOOTS_HOVER) {
                    return ITEM_BOOTS_HOVER;
                }
                return ITEM_BOOTS_KOKIRI;
            }

            if (INV_CONTENT(ITEM_MASK_BUNNY_HOOD) == ITEM_MASK_BUNNY_HOOD) {
                return ITEM_MASK_BUNNY_HOOD;
            }
            return ITEM_NONE;

        case DPAD_DOWN:
            if (LINK_IS_ADULT) {
                s32 tunic = CUR_EQUIP_VALUE(EQUIP_TYPE_TUNIC);

                if (!CHECK_OWNED_EQUIP(EQUIP_TYPE_TUNIC, EQUIP_INV_TUNIC_GORON) &&
                    !CHECK_OWNED_EQUIP(EQUIP_TYPE_TUNIC, EQUIP_INV_TUNIC_ZORA)) {
                    return ITEM_NONE;
                }

                if (tunic == EQUIP_VALUE_TUNIC_GORON) {
                    return ITEM_TUNIC_GORON;
                }
                if (tunic == EQUIP_VALUE_TUNIC_ZORA) {
                    return ITEM_TUNIC_ZORA;
                }
                return ITEM_TUNIC_KOKIRI;
            } else {
                // The best shield owned, not the one equipped. Unlike boots and
                // tunics this slot deliberately does not track the current
                // equipment: going shieldless is a position in the cycle, and
                // showing the equipped item there would blank the slot exactly
                // when the player most needs to see how to get a shield back.
                // The icon marks what the direction does, and the slot only
                // disappears when there is genuinely no shield to cycle to.
                if (CHECK_OWNED_EQUIP(EQUIP_TYPE_SHIELD, EQUIP_INV_SHIELD_HYLIAN)) {
                    return ITEM_SHIELD_HYLIAN;
                }
                if (CHECK_OWNED_EQUIP(EQUIP_TYPE_SHIELD, EQUIP_INV_SHIELD_DEKU)) {
                    return ITEM_SHIELD_DEKU;
                }
                return ITEM_NONE;
            }

        // Warp, not implemented. Draws nothing, exactly as an unowned item does.
        case DPAD_RIGHT:
        default:
            return ITEM_NONE;
    }
}

/*
 * Where the widget sits, in the 320x240 space the HUD is authored against.
 * Left hand side, matching where the Majora's Mask D-Pad mod puts its own, and
 * clear of the hearts at the bottom and the minimap at the top.
 */
// Matching the Majora's Mask mod's own centre, so the two games put their D-Pads
// in the same place.
#define DPAD_CENTER_X 32
#define DPAD_CENTER_Y 76
// Every dimension below is 0.75 of what the widget first shipped at: icons 16,
// spacing 15, background 32. Kept as literals rather than a scale expression so
// the drawn sizes stay whole pixels - a texture rectangle takes 10.2 fixed point
// coordinates, and a fractional edge lands the widget on a half pixel.
#define DPAD_ICON_SIZE 12 // icons are 32x32 in the ROM, drawn smaller
#define DPAD_SPACING 11   // centre to icon centre

// How far inside the real screen edge the HUD sits, in game pixels. Must match
// hud_margin in patches_oot/ui_patches.c, which is what the rest of the HUD is
// anchored with; a different value here would leave the widget out of step with
// everything around it.
#define DPAD_HUD_MARGIN 8

// The background is a 32x32 texture, no longer drawn at 1:1, so its texture step
// has to be derived the same way the icons' is rather than left at (1 << 10).
#define DPAD_BG_TEX_SIZE 32
#define DPAD_BG_SIZE 24
#define DPAD_BG_DTDX ((s32)(((f32)DPAD_BG_TEX_SIZE / DPAD_BG_SIZE) * (1 << 10)))

/*
 * Widget opacity, 0-255. The combiner is G_CC_MODULATEIA_PRIM, whose alpha stage
 * is TEXEL0.a * PRIM.a, so this scales each texture's own alpha rather than
 * replacing it - the D-Pad background keeps its soft edges and the item icons
 * keep their cutouts, everything just gets more see-through.
 *
 * Set once before the background and inherited by the icons, so the whole widget
 * fades together.
 */
#define DPAD_ALPHA 128 // ~0.5

extern const u64 dpad_bg_texture[];

// A texture rectangle's texture coordinate step, in 10.5 fixed point. A 32 texel
// icon drawn into DPAD_ICON_SIZE pixels needs 32/DPAD_ICON_SIZE texels per pixel.
#define DPAD_ICON_DTDX ((s32)((32.0f / DPAD_ICON_SIZE) * (1 << 10)))

static void dpad_draw_icon(PlayState* play, DpadDir dir, s32 x, s32 y) {
    OPEN_DISPS(play->state.gfxCtx, "dpad.c", __LINE__);

    gDPLoadTextureBlock(OVERLAY_DISP++, dpad_icons[dir], G_IM_FMT_RGBA, G_IM_SIZ_32b, 32, 32, 0,
                        G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD,
                        G_TX_NOLOD);

    gSPTextureRectangle(OVERLAY_DISP++, x << 2, y << 2, (x + DPAD_ICON_SIZE) << 2, (y + DPAD_ICON_SIZE) << 2,
                        G_TX_RENDERTILE, 0, 0, DPAD_ICON_DTDX, DPAD_ICON_DTDX);

    CLOSE_DISPS(play->state.gfxCtx, "dpad.c", __LINE__);
}

static void dpad_draw(PlayState* play) {
    static const s32 offsets_x[DPAD_MAX] = { 0, -DPAD_SPACING, 0, DPAD_SPACING };
    static const s32 offsets_y[DPAD_MAX] = { -DPAD_SPACING, 0, DPAD_SPACING, 0 };
    s32 items[DPAD_MAX];
    s32 any = false;
    s32 i;

    for (i = 0; i < DPAD_MAX; i++) {
        items[i] = dpad_icon_for((DpadDir)i);
        if (items[i] != ITEM_NONE) {
            any = true;
        }
    }

    // Nothing unlocked on any direction means no widget at all, rather than an
    // empty cross sitting on the HUD for the first hour of the game.
    if (!any) {
        return;
    }

    // Refresh only the slots whose item changed. DmaMgr_RequestSync blocks, so
    // doing this unconditionally would stall every frame for no reason.
    for (i = 0; i < DPAD_MAX; i++) {
        if ((items[i] != ITEM_NONE) && (items[i] != dpad_loaded_items[i])) {
            DmaMgr_RequestSync(dpad_icons[i], GET_ITEM_ICON_VROM(items[i]), ITEM_ICON_SIZE);
        }
        dpad_loaded_items[i] = items[i];
    }

    OPEN_DISPS(play->state.gfxCtx, "dpad.c", __LINE__);

    /*
     * Anchor to the LEFT screen edge for the duration of the widget.
     *
     * This is what put the icons in the middle of the screen on the first build
     * that drew anything. patches_oot/ui_patches.c anchors the B and C buttons
     * to the right edge and then calls Interface_DrawItemButtons, so a hook on
     * that function inherits a right-edge origin carrying a whole screen width
     * of offset. Alignment is display list state, not a per-rect argument, so it
     * has to be set here and put back afterwards or the game's own C buttons
     * follow the widget to the wrong side.
     *
     * Only gEXSetRectAlign is touched, because everything drawn below is a
     * gSPTextureRectangle. gEXSetViewportAlign governs geometry, of which there
     * is none here, and changing it would mean re-emitting the ortho view.
     */
    gEXSetRectAlign(OVERLAY_DISP++, G_EX_ORIGIN_LEFT, G_EX_ORIGIN_LEFT, -DPAD_HUD_MARGIN * 4, 0, -DPAD_HUD_MARGIN * 4,
                    0);

    // Exactly what the Majora's Mask D-Pad mod does, and deliberately no more.
    // It sets a combiner and a primitive colour and nothing else - no
    // Gfx_SetupDL, no render mode, no cycle type - because it runs inside
    // Interface_DrawItemButtons, where the interface has already established all
    // of that. Adding a Gfx_SetupDL call here would fight state that is already
    // correct rather than establish state that is missing.
    gDPPipeSync(OVERLAY_DISP++);
    gDPSetCombineMode(OVERLAY_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
    gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 255, 255, 255, DPAD_ALPHA);

    // The D-Pad background, under the icons. CLAMP rather than WRAP because it
    // is drawn at its own size and must not tile at the edges.
    gDPLoadTextureBlock(OVERLAY_DISP++, dpad_bg_texture, G_IM_FMT_RGBA, G_IM_SIZ_32b, DPAD_BG_TEX_SIZE,
                        DPAD_BG_TEX_SIZE, 0, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK,
                        G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);

    gSPTextureRectangle(OVERLAY_DISP++, (DPAD_CENTER_X - DPAD_BG_SIZE / 2) << 2,
                        (DPAD_CENTER_Y - DPAD_BG_SIZE / 2) << 2, (DPAD_CENTER_X + DPAD_BG_SIZE / 2) << 2,
                        (DPAD_CENTER_Y + DPAD_BG_SIZE / 2) << 2, G_TX_RENDERTILE, 0, 0, DPAD_BG_DTDX, DPAD_BG_DTDX);

    CLOSE_DISPS(play->state.gfxCtx, "dpad.c", __LINE__);

    for (i = 0; i < DPAD_MAX; i++) {
        if (items[i] == ITEM_NONE) {
            continue;
        }

        dpad_draw_icon(play, (DpadDir)i, DPAD_CENTER_X + offsets_x[i] - (DPAD_ICON_SIZE / 2),
                       DPAD_CENTER_Y + offsets_y[i] - (DPAD_ICON_SIZE / 2));
    }

    OPEN_DISPS(play->state.gfxCtx, "dpad.c", __LINE__);

    // Put the right-edge anchor back exactly as ui_patches.c left it, so the B
    // and C buttons drawn after this hook returns are unaffected.
    gEXSetRectAlign(OVERLAY_DISP++, G_EX_ORIGIN_RIGHT, G_EX_ORIGIN_RIGHT, -(SCREEN_WIDTH - DPAD_HUD_MARGIN) * 4, 0,
                    -(SCREEN_WIDTH - DPAD_HUD_MARGIN) * 4, 0);

    CLOSE_DISPS(play->state.gfxCtx, "dpad.c", __LINE__);
}

/*
 * Drawing hangs off Interface_DrawItemButtons, which is the hook the Majora's
 * Mask D-Pad mod uses for its own widget, and the reason matters: it runs in the
 * middle of the HUD pass, after the interface has set up its segments, its ortho
 * view and its render mode. Hooking Interface_Draw itself - which is what the
 * first three attempts here did - runs before any of that exists, so nothing the
 * hook emitted could appear no matter how correct the drawing was.
 *
 * It also sidesteps patches_oot/ui_patches.c entirely: that replaces
 * Interface_Draw with RECOMP_PATCH but only calls Interface_DrawItemButtons, so
 * this hook attaches to an ordinary, unpatched base game function.
 */
RECOMP_HOOK("Interface_DrawItemButtons") void dpad_on_draw_item_buttons(PlayState* play) {
    if (play->pauseCtx.state != PAUSE_STATE_OFF) {
        return;
    }

    dpad_draw(play);
}

/*
 * Runs before the base game's Interface_Draw, which is where the input is read.
 * A hook rather than a patch:
 * patches_oot/ui_patches.c already holds a verbatim copy of this function for
 * the widescreen HUD anchoring, and a second copy would have to be kept in step
 * with it by hand.
 *
 * Interface_Draw runs once per frame with the play state to hand, which is all
 * the input handling needs too, so both live here for now.
 */
RECOMP_HOOK("Interface_Draw") void dpad_on_interface_draw(PlayState* play) {
    Input* input = &play->state.input[0];

    dpad_draw_enabled = recomp_get_config_u32("draw_dpad");

    if (dpad_input_blocked(play)) {
        return;
    }

    if (CHECK_BTN_ALL(input->press.button, BTN_DUP)) {
        dpad_press_up(play);
    } else if (CHECK_BTN_ALL(input->press.button, BTN_DLEFT)) {
        dpad_press_left(play);
    } else if (CHECK_BTN_ALL(input->press.button, BTN_DDOWN)) {
        dpad_press_down(play);
    }

    // BTN_DRIGHT is deliberately unhandled: warp is not implemented yet, and an
    // unbound direction should do nothing rather than something surprising.
}
