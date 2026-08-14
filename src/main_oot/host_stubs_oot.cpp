/*
 * Stub implementations of the host-side hooks the OoT patches import, for the
 * standalone milestone target only.
 *
 * The real implementations live in src/game/recomp_api.cpp and read the main
 * app's config and input systems, which this launcher deliberately does not
 * build. The stubs answer with the defaults: Switch targeting, analog camera
 * off, no inverted axes. The actor extension store is NOT stubbed - the real
 * src/game/recomp_actor_api.cpp is compiled in, since the transform tagging
 * patches need it to behave correctly rather than merely link.
 *
 * The combined launcher does not compile this file; it gets the real ones.
 */
#include "librecomp/helpers.hpp"

// zelda64::TargetingMode::Switch, the config default in the main app.
extern "C" void recomp_get_targeting_mode(uint8_t* rdram, recomp_context* ctx) {
    _return<s32>(ctx, 0);
}

extern "C" void recomp_get_analog_cam_enabled(uint8_t* rdram, recomp_context* ctx) {
    _return<s32>(ctx, 0);
}

extern "C" void recomp_get_camera_inputs(uint8_t* rdram, recomp_context* ctx) {
    float* x_out = _arg<0, float*>(rdram, ctx);
    float* y_out = _arg<1, float*>(rdram, ctx);

    *x_out = 0.0f;
    *y_out = 0.0f;
}

extern "C" void recomp_get_analog_inverted_axes(uint8_t* rdram, recomp_context* ctx) {
    s32* x_out = _arg<0, s32*>(rdram, ctx);
    s32* y_out = _arg<1, s32*>(rdram, ctx);

    *x_out = 0;
    *y_out = 0;
}

// 0xFFFF is "nothing pending", which is the only answer a target without the
// debug warp menu can give. The real one is in src/game/debug.cpp, which reaches
// into the warp lists and the RmlUi menu that sets them and so cannot be compiled
// here. Without this the standalone target does not link at all: debug_warp_update
// in patches_oot/debug_patches.c imports the symbol unconditionally.
extern "C" void recomp_get_pending_warp(uint8_t* rdram, recomp_context* ctx) {
    _return<u16>(ctx, 0xFFFF);
}
