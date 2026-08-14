__start = 0x80000000;

/* Dummy addresses that get recompiled into calls into librecomp.
   These must match the addresses librecomp expects; see ../patches/syms.ld for
   the full Majora's Mask set. Only the entries the OoT patches use are listed. */
recomp_trace = 0x8F000000;
recomp_load_overlays = 0x8F000090;
/* Config and input hooks for the analog camera. These addresses must match
   ../patches/syms.ld exactly: they identify one shared, game agnostic
   implementation in src/game/recomp_api.cpp, registered once for both games. */
recomp_get_analog_cam_enabled = 0x8F000098;
/* The window's aspect ratio, or the game's own if the player left the setting on
   Original. Used by the transition patches to grow the wipe and circle meshes so
   they still reach the edges of a widened field of view. */
recomp_get_target_aspect_ratio = 0x8F00001C;
recomp_get_camera_inputs = 0x8F00009C;
recomp_get_analog_inverted_axes = 0x8F0000B0;
recomp_get_targeting_mode = 0x8F00002C;
/* The debug menu's warp, taken once and reset by the read. Same address as the
   Majora's Mask tree's for the same reason as the ones above: one implementation
   in src/game/debug.cpp, and both games ask it. What comes back is not the same
   shape in the two games - this one is a flat entrance index - so the host only
   ever leaves a value meant for the game that is running. */
recomp_get_pending_warp = 0x8F000020;
/* bzero is in N64Recomp's `renamed_funcs`: the game recompiles it under the
   `_recomp` name rather than the runtime providing it, so it carries the OoT
   function prefix. The genuinely runtime-provided symbols above do not. */
oot_bzero_recomp = 0x8F00005C;
/* sinf, the same way and for the same reason - also a renamed func, so also
   prefixed. Same dummy address the Majora's Mask tree gives its own copy. */
oot_sinf_recomp = 0x8F000038;
osWritebackDCache_recomp = 0x8F000068;
osInvalICache_recomp = 0x8F000094;

/* The actor extension store, which is what the high framerate modes are built on:
   it is the only place a spawned actor can be given an identity that survives the
   arena reusing its memory, and a transform id has to be exactly that. Registered
   once in src/main/main.cpp and served to whichever game is running, so these
   addresses match ../patches/syms.ld exactly. See patches_oot/actor_data.c. */
recomp_register_actor_extension = 0x8F0000BC;
recomp_register_actor_extension_generic = 0x8F0000C0;
recomp_clear_all_actor_data = 0x8F0000C4;
recomp_create_actor_data = 0x8F0000C8;
recomp_destroy_actor_data = 0x8F0000CC;
recomp_get_actor_data = 0x8F0000D0;
recomp_get_actor_spawn_index = 0x8F0000D4;
