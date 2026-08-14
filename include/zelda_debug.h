#ifndef __ZELDA_DEBUG_H__
#define __ZELDA_DEBUG_H__

#include <cstdint>
#include <vector>
#include <string>

namespace zelda64 {
    struct SceneWarps {
        int index;
        std::string name;
        std::vector<std::string> entrances;
        // What to send for each entrance above, when the game's warp value cannot
        // be packed out of the indices the menu is showing. Empty means it can:
        // Majora's Mask's is (scene << 8) | (spawn << 4) and its list leaves this
        // out, Ocarina of Time's is a flat index into a 1557 row entrance table
        // and its list fills it in. Same size as `entrances` when it is present.
        std::vector<uint16_t> values;
    };

    struct AreaWarps {
        std::string name;
        std::vector<SceneWarps> scenes;
    };

    // Majora's Mask's, hand written. Ocarina of Time's is generated from the
    // decomp's own tables; see tools/gen_oot_scene_table.py.
    extern std::vector<AreaWarps> game_warps;
    extern std::vector<AreaWarps> game_warps_oot;

    // Which game a warp is for. The value alone is not enough to say: an Ocarina
    // of Time entrance index acted on by Majora's Mask is a warp to whatever scene
    // happens to hold those bits.
    enum class WarpTarget : uint32_t {
        MajorasMask = 0,
        OcarinaOfTime = 1,
    };

    // The list the menu should be showing: the running game's, and Majora's Mask's
    // before anything has started, which is the launcher's state.
    const std::vector<AreaWarps>& current_game_warps();
    WarpTarget current_warp_target();

    void do_warp(int area, int scene, int entrance);
    void set_time(uint8_t day, uint8_t hour, uint8_t minute);

    // Whether the Debug tab's warp control means anything for the game on screen.
    // Both games have a scene table now, so this is only false for a game that has
    // neither - which no shipped build has, and which is why it is still asked.
    bool warps_available();

    // Whether the Set time control does. Majora's Mask only: the block sets a
    // three-day clock and Ocarina of Time has no such thing. Separate from
    // warps_available so that Ocarina of Time gets the warps without a control
    // underneath them that does nothing.
    bool clock_available();
}

#endif
