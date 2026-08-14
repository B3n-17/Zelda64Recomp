/*
 * The debug menu's warp, Ocarina of Time side.
 *
 * The mirror of patches/debug_patches.c, which has had this for Majora's Mask all
 * along. It is how a tester reaches a site without walking to it, and how a run
 * that has walked into a soft lock gets out.
 *
 * THE VALUE IS NOT THE SAME SHAPE as Majora's Mask's, which is why this is a file
 * rather than a line. Majora's Mask's warp travels as (scene << 8) | (spawn << 4)
 * - eight bits of scene index and four of spawn, which fits because its scene
 * table is small and its spawns are few. Ocarina of Time's entrance is a flat
 * index into a 1557 row table reaching 0x60D, and there is nothing to pack: the
 * scene and the spawn are columns of that table rather than bitfields of the
 * index. So the host carries the value per entrance in its own list
 * (src/game/scene_table_oot.cpp) and sends it as it stands.
 *
 * WHY THE HARD RESTART rather than a transition. `play->nextEntranceIndex` with
 * TRANS_TRIGGER_START is the polite way to change scene, but it needs a play state
 * that is willing to start a transition - not already in one, not in a cutscene,
 * not holding the player in an action that will not let go. The whole point of
 * this control is the frame where something has gone wrong, so it does what
 * ovl_select's own MapSelect_LoadGame does: write the entrance into the save and
 * re-enter Play_Init from the top. Everything below is that function, minus the
 * debug-save initialisation, which belongs to a gamestate that boots without a
 * file.
 */
#include "patches.h"

#include "environment.h"
#include "play_state.h"
#include "save.h"
#include "scene.h"
#include "seqcmd.h"
#include "sequence.h"
#include "z_game_dlftbls.h"

// src/game/debug.cpp. The value the debug menu left, or 0xFFFF for none; reading
// it is what clears it, so this asks once a frame and acts on what it gets.
u16 recomp_get_pending_warp(void);

// Play_Init's address is taken out of the overlay table rather than named, which
// is the same thing the Majora's Mask side does: a patch that referenced the
// function directly would need a relocation against a game function's address,
// and the table already holds it.
static void debug_warp(PlayState* play, u16 entrance) {
    gSaveContext.buttonStatus[0] = gSaveContext.buttonStatus[1] = gSaveContext.buttonStatus[2] =
        gSaveContext.buttonStatus[3] = gSaveContext.buttonStatus[4] = BTN_ENABLED;
    gSaveContext.forceRisingButtonAlphas = gSaveContext.nextHudVisibilityMode = gSaveContext.hudVisibilityMode =
        gSaveContext.hudVisibilityModeTimer = 0;

    SEQCMD_STOP_SEQUENCE(SEQ_PLAYER_BGM_MAIN, 0);

    gSaveContext.save.entranceIndex = entrance;
    // Not a respawn and not a void out: clearing these is what stops the arriving
    // scene putting Link back where the last one left him.
    gSaveContext.respawnFlag = 0;
    gSaveContext.respawn[RESPAWN_MODE_DOWN].entranceIndex = ENTR_LOAD_OPENING;
    gSaveContext.seqId = (u8)NA_BGM_DISABLED;
    gSaveContext.natureAmbienceId = 0xFF;
    gSaveContext.showTitleCard = true;
    gWeatherMode = WEATHER_MODE_CLEAR;

    play->state.running = false;
    play->state.init = gGameStateOverlayTable[GAMESTATE_PLAY].init;
    play->state.size = gGameStateOverlayTable[GAMESTATE_PLAY].instanceSize;
}

// Called once a frame from the Play_Main patch in camera_patches.c, and last:
// it can end this play state, so anything running after it would be acting on a
// frame that is being thrown away.
void debug_warp_update(PlayState* play) {
    u16 pending = recomp_get_pending_warp();

    if (pending != 0xFFFF) {
        debug_warp(play, pending);
    }
}
