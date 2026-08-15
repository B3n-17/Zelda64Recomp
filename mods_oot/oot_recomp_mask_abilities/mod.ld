/*
 * Linker script for an OoT mod.
 *
 * Deliberately different from patches_oot/patches.ld in three ways, all of which
 * matter:
 *
 * 1. ORIGIN is 0, not a fixed RAM address. RecompModTool parses the mod ELF with
 *    use_absolute_symbols = false and all_sections_relocatable = true, and the
 *    runtime places the mod in mod memory at load time. A fixed base would be
 *    both wrong and unnecessary.
 *
 * 2. The special sections are NOT collected with wildcards. patches.ld can merge
 *    .recomp_patch* into one output section because the patch build only cares
 *    that the section exists. A mod cannot: the hooked function's name lives in
 *    the section name itself (.recomp_hook.Interface_Draw), as does an import's
 *    target (.recomp_import.*.recomp_get_config_u32). Merging them would erase
 *    exactly the information RecompModTool reads back out. They are left to
 *    orphan placement so the linker keeps each one under its own name, and there
 *    is no /DISCARD/ catch-all that would otherwise consume them.
 *
 * 3. There is no MEMORY block and no AT > rom mapping. Separate load and virtual
 *    addresses are what patches.ld needs to build a ROM image; a mod has no ROM,
 *    and giving the named sections an explicit load region while the special
 *    sections arrive as orphans put the two in different address spaces. lld
 *    placed the orphans over .data and failed with "load address range overlaps".
 *    One flat, sequentially assigned space leaves orphan placement nowhere to
 *    collide.
 */
SECTIONS {
    . = 0;

    .text          : { *(.text*)           }
    .rodata        : { *(.rodata*)         }
    .data          : { *(.data*)           }
    .bss  (NOLOAD) : { *(.bss*)  *(COMMON) }

    .reloc 0 : { *(.reloc*) }
}
