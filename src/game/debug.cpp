#include <atomic>
#include "zelda_debug.h"
#include "librecomp/game.hpp"
#include "librecomp/helpers.hpp"
#include "ultramodern/ultramodern.hpp"
#include "../patches/input.h"

std::atomic<uint16_t> pending_warp = 0xFFFF;
std::atomic<uint32_t> pending_set_time = 0xFFFF;

// Matches GameEntry::game_id for Ocarina of Time in src/main/main.cpp.
static const std::u8string oot_game_id = u8"oot.n64.us.1.0";

zelda64::WarpTarget zelda64::current_warp_target() {
    // Asked before asking which game: recomp::current_game_id() unwraps an optional
    // that is empty until a game starts, so calling it from the launcher throws.
    // This runs once a frame from the UI draw hook, launcher included. No game on
    // screen answers Majora's Mask, whose list is the one the menu is built with.
    if (!ultramodern::is_game_started()) {
        return zelda64::WarpTarget::MajorasMask;
    }

    return recomp::current_game_id() == oot_game_id
        ? zelda64::WarpTarget::OcarinaOfTime
        : zelda64::WarpTarget::MajorasMask;
}

const std::vector<zelda64::AreaWarps>& zelda64::current_game_warps() {
    return current_warp_target() == zelda64::WarpTarget::OcarinaOfTime
        ? zelda64::game_warps_oot
        : zelda64::game_warps;
}

void zelda64::do_warp(int area, int scene, int entrance) {
    const std::vector<zelda64::AreaWarps>& warps = zelda64::current_game_warps();
    if (area < 0 || size_t(area) >= warps.size()) {
        return;
    }
    const std::vector<zelda64::SceneWarps>& scenes = warps[area].scenes;
    if (scene < 0 || size_t(scene) >= scenes.size()) {
        return;
    }
    const zelda64::SceneWarps& game_scene = scenes[scene];
    if (entrance < 0 || size_t(entrance) >= game_scene.entrances.size()) {
        return;
    }

    // A list that carries its own values sends one as it stands; one that does not
    // is Majora's Mask's, whose warp is (scene << 8) | (spawn << 4) and is packed
    // here rather than written out four hundred times.
    if (game_scene.values.size() == game_scene.entrances.size()) {
        pending_warp.store(game_scene.values[entrance]);
    }
    else {
        pending_warp.store(((game_scene.index & 0xFF) << 8) | ((entrance & 0x0F) << 4));
    }
}

extern "C" void recomp_get_pending_warp(uint8_t* rdram, recomp_context* ctx) {
    // Return the current warp value and reset it.
    _return(ctx, pending_warp.exchange(0xFFFF));
}

void zelda64::set_time(uint8_t day, uint8_t hour, uint8_t minute) {
    pending_set_time.store((day << 16) | (uint16_t(hour) << 8) | minute);
}

extern "C" void recomp_get_pending_set_time(uint8_t* rdram, recomp_context* ctx) {
    // Return the current set time value and reset it.
    _return(ctx, pending_set_time.exchange(0xFFFF));
}

bool zelda64::warps_available() {
    // Both games have a list now, so this is only false for a game that has
    // neither. Asked anyway rather than deleted: it is the one line that would
    // hide the tab rather than crash in it if a third game ever arrived.
    return !zelda64::current_game_warps().empty();
}

bool zelda64::clock_available() {
    return zelda64::current_warp_target() == zelda64::WarpTarget::MajorasMask;
}
