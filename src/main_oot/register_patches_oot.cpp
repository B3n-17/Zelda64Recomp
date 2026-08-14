#include "../../RecompiledPatchesOoT/patches_bin.h"
#include "../../RecompiledPatchesOoT/recomp_overlays.inl"

#include "librecomp/overlays.hpp"
#include "librecomp/game.hpp"

namespace oot {
    void register_patches();
}

void oot::register_patches() {
    recomp::overlays::register_patches(oot_patches_bin, sizeof(oot_patches_bin), section_table, ARRLEN(section_table));
    recomp::overlays::register_base_exports(export_table);
    recomp::overlays::register_base_events(event_names);
    recomp::overlays::register_manual_patch_symbols(manual_patch_symbols);
}
