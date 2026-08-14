/*
 * recomp_load_overlays for the standalone OoT target only.
 *
 * Split out of runtime_funcs_oot.cpp because the MM app defines the same symbol
 * in src/game/recomp_api.cpp. The implementations are identical - it is a thin
 * wrapper over librecomp's load_overlays - so the combined launcher links MM's
 * and this file is left out of that build.
 */

#include "recomp.h"
#include "librecomp/overlays.hpp"

// Called by the patched Main_ThreadEntry and Overlay_Load. Tells librecomp which
// recompiled code corresponds to a region the game just DMA'd into RDRAM.
// Args: a0 = rom address, a1 = ram address, a2 = size.
extern "C" void recomp_load_overlays(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    load_overlays((uint32_t)ctx->r4, (int32_t)ctx->r5, (uint32_t)ctx->r6);
}
