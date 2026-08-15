/*
 * Mod-side ABI for Ocarina of Time mods.
 *
 * The equivalent header in the upstream RecompModTemplate is not vendored into
 * this tree, so this is written directly from the section names the recompiler
 * and RecompModTool agree on. The authority is
 * lib/N64ModernRuntime/N64Recomp/include/recompiler/context.h:
 *
 *     PatchSectionName        ".recomp_patch"
 *     ForcedPatchSectionName  ".recomp_force_patch"
 *     ExportSectionName       ".recomp_export"
 *     EventSectionName        ".recomp_event"
 *     ImportSectionPrefix     ".recomp_import."
 *     CallbackSectionPrefix   ".recomp_callback."
 *     HookSectionPrefix       ".recomp_hook."
 *     HookReturnSectionPrefix ".recomp_hook_return."
 *
 * and RecompModTool/main.cpp, which reads those section names back off the mod
 * ELF to build the symbol file.
 */
#ifndef __MODDING_H__
#define __MODDING_H__

/*
 * Replace a base game function outright. The function must be named exactly as
 * the one it replaces, and that name has to resolve in the reference symbols -
 * RecompModTool rejects the mod otherwise, rather than silently doing nothing.
 */
#define RECOMP_PATCH __attribute__((section(".recomp_patch")))
#define RECOMP_FORCE_PATCH __attribute__((section(".recomp_force_patch")))

/*
 * Run before (RECOMP_HOOK) or after (RECOMP_HOOK_RETURN) a base game function,
 * leaving the original in place. This is the reason a mod is a better home for
 * this feature than a patch: patches_oot/ui_patches.c already keeps a verbatim
 * copy of Interface_Draw, and a second copy would have to be kept in step with
 * it by hand. A hook needs no copy at all.
 *
 * The hooked name goes in the section suffix, so it must be a string literal.
 * The hook function itself takes the same arguments as the function it hooks.
 */
#define RECOMP_HOOK(func_name) __attribute__((used, section(".recomp_hook." func_name)))
#define RECOMP_HOOK_RETURN(func_name) __attribute__((used, section(".recomp_hook_return." func_name)))

/* Expose a function to other mods. */
#define RECOMP_EXPORT __attribute__((used, section(".recomp_export")))

/*
 * Call a function provided by the runtime or by another mod. `mod_id` is the
 * dependency the symbol comes from: "*" for base recomp (DependencyBaseRecomp),
 * "." for this mod itself, or another mod's id.
 *
 * Note the section suffix is the dependency ALONE - the imported function's name
 * is not part of it, and appending it silently breaks the mod. RecompModTool
 * takes everything after the ".recomp_import." prefix as the dependency name
 * (main.cpp: `target_section.name.substr(ImportSectionPrefix.size())`) and
 * identifies the function from its symbol instead. Getting this wrong reports
 * the whole string as a missing mod: "Failed to import function
 * recomp_get_config_u32 from mod *.recomp_get_config_u32". All imports from one
 * dependency therefore share a single section.
 *
 * The declaration this precedes must be a definition with a dummy body, not a
 * prototype: RecompModTool skips the bodies of functions in an import section
 * ("Import sections can be skipped, as those only contain dummy functions")
 * and resolves the real target while scanning relocations, but the symbol still
 * has to exist in the section for the section to be emitted at all.
 */
#define RECOMP_IMPORT(mod_id) \
    __attribute__((noinline, used, section(".recomp_import." mod_id)))

/*
 * The body to give an imported function's stub. It looks like pointless
 * ceremony and is not: the stub is a real definition in this translation unit,
 * so a plain `{ return 0; }` is something the optimizer can see through. It
 * infers the function is side effect free, constant folds the result at every
 * call site, and deletes the call - which deletes the relocation, and imports
 * are found by scanning relocations, so the import silently disappears from the
 * mod's symbol file. The first build of this mod did exactly that: the hook
 * compiled down to `jr $ra; nop`.
 *
 * The volatile asm gives the function a side effect the optimizer must respect,
 * so the call survives, and sources the return value from a register it cannot
 * reason about, so the result is never folded away.
 */
#define RECOMP_IMPORT_STUB(type)                        \
    {                                                   \
        type _recomp_stub_result;                       \
        __asm__ __volatile__("" : "=r"(_recomp_stub_result)); \
        return _recomp_stub_result;                     \
    }

/*
 * Declare an event other mods can subscribe to. Deliberately weak and noinline:
 * the event section is scanned for its function symbols so that events survive
 * even when nothing in the mod calls them.
 */
#define RECOMP_DECLARE_EVENT(func) \
    _Pragma("GCC diagnostic push") \
    _Pragma("GCC diagnostic ignored \"-Wunused-parameter\"") \
    __attribute__((noinline, weak, used, section(".recomp_event"))) void func {} \
    _Pragma("GCC diagnostic pop")

/* Subscribe to an event declared by `mod_id`. */
#define RECOMP_CALLBACK(mod_id, event_name) \
    __attribute__((used, section(".recomp_callback." mod_id "." event_name)))

#endif
