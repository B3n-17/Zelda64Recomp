/*
 * Runtime-provided functions for OoT.
 *
 * Two groups live here:
 *
 * 1. recomp_trace, a diagnostic hook for the patches.
 *
 * 2. Four libultra functions that N64Recomp places in its `ignored_funcs` list.
 *    Anything in that list gets `_recomp` appended and is NOT recompiled, on the
 *    assumption that the runtime supplies it. librecomp does supply most of them
 *    (see librecomp/src/pi.cpp), but not these four, because Majora's Mask never
 *    calls them and nothing has needed them until now. Ocarina of Time does:
 *    it retains 64DD support code and touches PI/RSP registers that MM does not.
 *
 *    These are stubs, not implementations. They are enough to link and to get
 *    through boot on hardware that has no 64DD attached, but any code path that
 *    genuinely depends on them will misbehave rather than fail loudly.
 */

#include <cstdio>

#include "recomp.h"
#include "librecomp/overlays.hpp"

// void recomp_trace(const char* label, u32 a, u32 b, u32 c)
// Diagnostic hook for the patches. a0 is a guest pointer to a string, which has
// to be walked byte by byte out of RDRAM rather than memcpy'd, because guest
// memory is stored word-swapped.
extern "C" void recomp_trace(uint8_t* rdram, recomp_context* ctx) {
    gpr label_addr = ctx->r4;
    char label[128];
    size_t i = 0;
    for (; i < sizeof(label) - 1; i++) {
        char c = (char)MEM_B(i, label_addr);
        if (c == '\0') {
            break;
        }
        label[i] = c;
    }
    label[i] = '\0';

    fprintf(stderr, "[trace] %s a=%u (0x%X) b=%u (0x%X) c=%u (0x%X)\n", label, (uint32_t)ctx->r5,
            (uint32_t)ctx->r5, (uint32_t)ctx->r6, (uint32_t)ctx->r6, (uint32_t)ctx->r7, (uint32_t)ctx->r7);
    fflush(stderr);
}

// s32 osEPiWriteIo(OSPiHandle* handle, u32 devAddr, u32 data)
// Programmed I/O write to a PI device. OoT's n64dd code and cart-write paths use
// this. Stubbed to a no-op success; there is no writable PI device here.
extern "C" void osEPiWriteIo_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    ctx->r2 = 0;
}

// void __osSpSetStatus(u32 status)
// Direct RSP status register write. The RSP is emulated by librecomp rather than
// driven through this register, so writes are dropped. MM sidesteps the same
// problem by stubbing RcpUtils_Reset and patching Sched_HandleRspCancel.
extern "C" void __osSpSetStatus_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    (void)ctx;
}

// u32 __osSpGetStatus(void)
// Reports the RSP as halted and idle, which is the state the game expects to see
// whenever it polls, since tasks complete synchronously in the runtime.
extern "C" void __osSpGetStatus_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    constexpr uint32_t SP_STATUS_HALT = 0x1;
    ctx->r2 = SP_STATUS_HALT;
}

// s32 osLeoDiskInit(void)
// 64DD drive initialization. No disk drive exists, so report failure so that the
// game's n64dd paths take their "no drive" branch.
extern "C" void osLeoDiskInit_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    ctx->r2 = (gpr)(int32_t)-1;
}
