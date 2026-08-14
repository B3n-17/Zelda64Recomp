/*
 * 64DD-related patches.
 *
 * Ocarina of Time retains full 64DD (Leo drive) support code that Majora's Mask
 * does not have, and Main() probes for a drive during startup. Those probe paths
 * talk to hardware through kseg1, which the recompiler does not support.
 *
 * These are patches rather than entries in the `stubs` list in oot.us.ntsc-1.0.toml
 * on purpose. A stub emits an empty function body, so the return value is whatever
 * happened to be in the return register. For a predicate like "is a disk drive
 * attached" that is actively dangerous: a non-zero leftover would send the game
 * down the 64DD path instead of away from it. Patching lets us return the value
 * that real hardware without a drive would produce.
 */
#include "patches.h"

// u32 LeoDriveExist(void)
//
// The original spins on PI_STATUS_REG, reprograms the PI BSD domain 1 registers,
// reads the 64DD address space at 0x06001010, restores the registers, and reports
// true only if it read back the magic value 0x2129FFF8. With no drive attached the
// read does not return that magic, so the honest answer is false.
//
// Reached from Main() -> func_800AD410 -> func_801C6E80 during boot.
RECOMP_PATCH u32 LeoDriveExist(void) {
    return 0;
}
