/*
 * Minimum patches required for OoT to run under the recomp runtime.
 *
 * Counterpart to ../patches/required_patches.c (Majora's Mask). MM's version
 * patches Main_Init and Overlay_Load; OoT's equivalents are Main_ThreadEntry
 * (src/boot/idle.c) and Overlay_Load (src/libu64/loadfragment2_n64.c).
 *
 * Both patches exist for the same reason: the game DMAs code into RDRAM and then
 * jumps into it. Native code cannot execute the DMA'd MIPS, so the runtime has to
 * be told which recompiled functions correspond to the region that was just
 * loaded. recomp_load_overlays does that.
 */
#include "patches.h"
#include "transform_ids.h"

#include "dma.h"
#include "libu64/overlay.h"
#include "segment_symbols.h"
#include "stackcheck.h"

void Main(void* arg);
void Overlay_Relocate(void* allocatedRamAddr, OverlayRelocationSection* ovlRelocs, void* vramStart);

extern s32 gOverlayLogSeverity;

// @recomp Patched to load the code segment in the recomp runtime.
RECOMP_PATCH void Main_ThreadEntry(void* arg) {
    // @recomp Claim the actor extension slots the transform tagging needs. Has to
    // happen before the first actor spawns - the runtime refuses a registration
    // once any actor holds extension data, since a late one would shift the
    // offsets under everything already allocated.
    register_base_actor_extensions();

    DmaMgr_Init();

    // @recomp Load the code segment in the recomp runtime.
    recomp_load_overlays((u32)_codeSegmentRomStart, _codeSegmentStart,
                         (u32)(_codeSegmentRomEnd - _codeSegmentRomStart));

    DMA_REQUEST_SYNC(_codeSegmentStart, (uintptr_t)_codeSegmentRomStart,
                     _codeSegmentRomEnd - _codeSegmentRomStart, "../idle.c", 238);

    bzero(_codeSegmentBssStart, _codeSegmentBssEnd - _codeSegmentBssStart);

    Main(arg);
}

// @recomp Patched to load the overlay in the recomp runtime.
RECOMP_PATCH size_t Overlay_Load(uintptr_t vromStart, uintptr_t vromEnd, void* vramStart, void* vramEnd,
                                 void* allocatedRamAddr) {
    s32 size = vromEnd - vromStart;
    uintptr_t end;
    OverlayRelocationSection* ovlRelocs;

    // @recomp Load the overlay in the recomp runtime.
    recomp_load_overlays((u32)vromStart, allocatedRamAddr, (u32)(vromEnd - vromStart));

    end = (uintptr_t)allocatedRamAddr + size;

    DmaMgr_RequestSync(allocatedRamAddr, vromStart, size);

    // The overlay file is expected to contain a 32-bit offset from the end of the file to the start of the
    // relocation section.
    ovlRelocs = (OverlayRelocationSection*)(end - ((s32*)end)[-1]);

    Overlay_Relocate(allocatedRamAddr, ovlRelocs, vramStart);

    if ((s32)ovlRelocs->bssSize != 0) {
        bzero((void*)end, ovlRelocs->bssSize);
    }

    size = (uintptr_t)vramEnd - (uintptr_t)vramStart;

    osWritebackDCache(allocatedRamAddr, size);
    osInvalICache(allocatedRamAddr, size);

    return size;
}
