/*
 * Per-actor identity and extension data for Ocarina of Time.
 * Counterpart to ../patches/actor_data.c (Majora's Mask).
 *
 * The runtime owns a slot map of extension data keyed by a 32-bit handle, and
 * hands out a monotonically increasing spawn index alongside it
 * (src/game/recomp_actor_api.cpp, shared by both games). What each game has to
 * solve for itself is where to keep the handle, because `Actor` is the ROM's
 * struct and cannot grow.
 *
 * THE HANDLE LIVES IN COMPILER PADDING, four bytes of it, in two pairs.
 *
 * Majora's Mask uses 0x22-0x23 and 0x3A-0x3B. Neither is available here: 0x22-0x23
 * is this game's only *standalone* padding and combo_xflag.c already keeps the
 * spawn index and room number there (B11a, B11c), and 0x3A-0x3B is inside MM's
 * `audioFlags`, which OoT does not have.
 *
 * So this uses padding that sits *inside* two struct members instead:
 *
 *   0x4A-0x4B  the two bytes `PosRot focus` carries after its Vec3s rot, since a
 *              Vec3f + Vec3s is 18 bytes rounded to 20.
 *   0xCA-0xCB  the same gap in `ActorShape shape`, between `feetFloorFlag` (0x15)
 *              and `feetPos` (0x18).
 *
 * Padding inside a member is only safe if nothing ever assigns that member as a
 * whole struct - a word-wise struct copy carries the padding with it. Both were
 * checked against the decomp: `Actor_SetFocus` writes focus's six fields one at a
 * time and `ActorShape_Init` writes four of shape's, and the only whole-PosRot
 * assignment anywhere in the game is `actor->world = actor->home` in
 * Actor_SetWorldToHome, which touches neither. `home` and `world` were rejected
 * for exactly that reason. The asserts below fail the build if either gap closes
 * or moves; they cannot catch a new struct assignment appearing, which is what
 * this paragraph is for.
 */
#include "patches.h"
#include "transform_ids.h"

#include "actor.h"
#include "z_math.h"

/* Provided by the runtime; addresses in syms.ld must match ../patches/syms.ld,
 * since one implementation serves both games. */
u32 recomp_register_actor_extension(u32 actor_type, u32 size);
u32 recomp_register_actor_extension_generic(u32 size);
void recomp_clear_all_actor_data(void);
u32 recomp_create_actor_data(u32 actor_type);
void recomp_destroy_actor_data(u32 actor_handle);
void* recomp_get_actor_data(u32 actor_handle, u32 extension_handle, u32 actor_type);
u32 recomp_get_actor_spawn_index(u32 actor_handle);

/* ------------------------------- the handle -------------------------------- */

#define actorSlotByte0(actor) ((u8*)(actor))[0x4A]
#define actorSlotByte1(actor) ((u8*)(actor))[0x4B]
#define actorSlotByte2(actor) ((u8*)(actor))[0xCA]
#define actorSlotByte3(actor) ((u8*)(actor))[0xCB]

_Static_assert(sizeof(PosRot) == 0x14, "PosRot is no longer padded; the focus gap has closed");
_Static_assert(__builtin_offsetof(PosRot, rot) == 0x0C, "PosRot::rot moved; the focus gap moves with it");
_Static_assert(__builtin_offsetof(Actor, focus) == 0x38, "Actor::focus moved; the handle bytes move with it");
_Static_assert(__builtin_offsetof(Actor, lockOnArrowOffset) == 0x4C, "Actor::focus's trailing padding has closed");

_Static_assert(__builtin_offsetof(Actor, shape) == 0xB4, "Actor::shape moved; the handle bytes move with it");
_Static_assert(__builtin_offsetof(ActorShape, feetFloorFlag) == 0x15, "ActorShape's gap moved");
_Static_assert(__builtin_offsetof(ActorShape, feetPos) == 0x18, "ActorShape's gap has closed");

/* Not ours, and the reason the handle is not simply at 0x22 like the other game's.
 * Kept as an assert so the two never silently overlap. */
_Static_assert(__builtin_offsetof(Actor, world) == 0x24, "combo_xflag.c's bytes at 0x22 have moved");

ActorExtensionId actor_get_slot(Actor* actor) {
    return (actorSlotByte0(actor) << 24) | (actorSlotByte1(actor) << 16) | (actorSlotByte2(actor) << 8) |
           (actorSlotByte3(actor) << 0);
}

void actor_set_slot(Actor* actor, ActorExtensionId slot) {
    actorSlotByte0(actor) = (slot >> 24) & 0xFF;
    actorSlotByte1(actor) = (slot >> 16) & 0xFF;
    actorSlotByte2(actor) = (slot >> 8) & 0xFF;
    actorSlotByte3(actor) = (slot >> 0) & 0xFF;
}

/* ------------------------------- extensions -------------------------------- */

ActorExtensionId oot_extend_actor(s16 actor_id, u32 size) {
    return recomp_register_actor_extension(actor_id, size);
}

ActorExtensionId oot_extend_actor_all(u32 size) {
    return recomp_register_actor_extension_generic(size);
}

void* oot_get_extended_actor_data(Actor* actor, ActorExtensionId extension) {
    return recomp_get_actor_data(actor_get_slot(actor), extension, actor->id);
}

u32 oot_get_actor_spawn_index(Actor* actor) {
    return recomp_get_actor_spawn_index(actor_get_slot(actor));
}

/*
 * The spawn index, not the arena address and not the position in a category list.
 *
 * Both of those are reused: the arena hands the same bytes to the next actor that
 * fits there, and a list index shifts every time a neighbour is deleted. Either
 * one would make RT64 interpolate a newly spawned Deku Baba out of wherever the
 * Bombchu that died in its memory was standing. The spawn counter only ever goes
 * up, so an id is retired with the actor that held it.
 */
u32 actor_transform_id(Actor* actor) {
    u32 spawn_index = oot_get_actor_spawn_index(actor);

    /* The runtime answers 0xFFFFFFFF for a handle it does not know, which happens
     * for anything holding an `Actor` that never went through Actor_Init. Folding
     * that into the formula would give every such actor the *same* id, which is
     * worse than giving them none - RT64 would interpolate them into each other.
     * 0 is the caller's signal not to tag at all. */
    if (spawn_index == 0xFFFFFFFFU) {
        return 0;
    }

    return (spawn_index * ACTOR_TRANSFORM_ID_COUNT) + ACTOR_TRANSFORM_ID_START;
}

/* ---------------------------- the base extension --------------------------- */

typedef enum {
    ACTOR_TRANSFORM_FLAG_INTERPOLATION_SKIPPED = 1 << 0,
} CustomActorFlags;

typedef struct {
    CustomActorFlags flags;
} BaseActorExtensionData;

static ActorExtensionId base_actor_extension_handle;

void register_base_actor_extensions(void) {
    base_actor_extension_handle = oot_extend_actor_all(sizeof(BaseActorExtensionData));
}

static BaseActorExtensionData* get_base_extension_data(Actor* actor) {
    return (BaseActorExtensionData*)oot_get_extended_actor_data(actor, base_actor_extension_handle);
}

u32 actor_get_interpolation_skipped(Actor* actor) {
    BaseActorExtensionData* data = get_base_extension_data(actor);

    /* NULL when the actor has no handle yet - the player exists for a few
     * instructions before Actor_Init runs - so answer "interpolate normally"
     * rather than dereferencing it. */
    if (data == NULL) {
        return 0;
    }
    return (data->flags & ACTOR_TRANSFORM_FLAG_INTERPOLATION_SKIPPED) != 0;
}

void actor_set_interpolation_skipped(Actor* actor) {
    BaseActorExtensionData* data = get_base_extension_data(actor);

    if (data != NULL) {
        data->flags |= ACTOR_TRANSFORM_FLAG_INTERPOLATION_SKIPPED;
    }
}

void actor_clear_interpolation_skipped(Actor* actor) {
    BaseActorExtensionData* data = get_base_extension_data(actor);

    if (data != NULL) {
        data->flags &= ~ACTOR_TRANSFORM_FLAG_INTERPOLATION_SKIPPED;
    }
}
