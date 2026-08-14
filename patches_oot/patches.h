#ifndef __PATCHES_H__
#define __PATCHES_H__

#include "ultra64/ultratypes.h"

#define RECOMP_EXPORT __attribute__((section(".recomp_export")))
#define RECOMP_PATCH __attribute__((section(".recomp_patch")))
#define RECOMP_FORCE_PATCH __attribute__((section(".recomp_force_patch")))
#define RECOMP_DECLARE_EVENT(func) \
    _Pragma("GCC diagnostic push") \
    _Pragma("GCC diagnostic ignored \"-Wunused-parameter\"") \
    __attribute__((noinline, weak, used, section(".recomp_event"))) void func {} \
    _Pragma("GCC diagnostic pop")

// Redirect libultra calls that the runtime implements natively. These resolve to
// the dummy addresses in syms.ld, which the recompiler turns into direct calls
// into librecomp. Only the ones the OoT patches actually use are listed here;
// MM's equivalent header redirects many more.
#define osWritebackDCache osWritebackDCache_recomp
#define osInvalICache osInvalICache_recomp
// bzero must be redirected too: recompiled output emitting a call to a function
// literally named `bzero` collides with the host compiler's builtin.
#define bzero oot_bzero_recomp
// And sinf, for the same reason bzero is here rather than being left alone: it is
// one of N64Recomp's `renamed_funcs`, so the game's own copy of it is recompiled
// as `oot_sinf_recomp` and a patch that calls plain `sinf` recompiles into a call
// to `oot_sinf`, which nothing defines. That is a link error in the final
// executable and not in the patch build, so it does not show up until everything
// else has succeeded - see combo_butterfly.c, which is what found it.
#define sinf oot_sinf_recomp

// Provided by the runtime, not the game. Tells librecomp which recompiled code
// corresponds to a region that the game just DMA'd into RDRAM.
void recomp_load_overlays(u32 rom, void* ram, u32 size);

// Diagnostic hook. `label` is a pointer to a string in the patch's rodata; the
// host reads it out of RDRAM and prints it with the three values. Deliberately
// not a printf: that would need _Printf and a va_list in the patch, and this is
// enough to trace game state from recompiled code.
void recomp_trace(const char* label, u32 a, u32 b, u32 c);

#endif
