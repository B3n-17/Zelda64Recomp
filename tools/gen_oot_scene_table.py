#!/usr/bin/env python3
"""Generate src/game/scene_table_oot.cpp - the debug menu's Ocarina of Time warp list.

Majora's Mask's equivalent (src/game/scene_table.cpp) is hand written, and that is
the reason this one is not: Ocarina of Time's tables are already in the decomp in a
form a script can read, and there are 357 warps in them. Hand transcribing that
would be four hundred chances to type a wrong number, each of which lands as a
crash in a menu whose whole job is to get a stuck run out of trouble.

Two decomp headers are the input:

  include/tables/scene_table.h     111 DEFINE_SCENE rows. The last nine are behind
                                   `#if DEBUG_ASSETS` - test scenes whose assets a
                                   retail rom does not have - so they are dropped
                                   and so is SCENE_UNUSED_6E, which the entrance
                                   table still points at.
  include/tables/entrance_table.h  1557 DEFINE_ENTRANCE rows, and only 357 of them
                                   are warp targets. The rest are scene layers:
                                   the table groups four consecutive entries per
                                   entrance (child day, child night, adult day,
                                   adult night, plus cutscene layers), the game
                                   applies the layer offset itself, and the header
                                   says outright that only the first entry of a
                                   group is meant to be referenced. Groups are
                                   separated by blank lines, which is what this
                                   reads them by.

The one thing that cannot be derived is which region a scene belongs to, so AREAS
below is written out. It is checked against the scene table on every run: a scene
that no area claims is an error rather than a scene quietly missing from the menu.

Usage: tools/gen_oot_scene_table.py [--oot-dir ../oot] [--check]
"""

import argparse
import pathlib
import re
import sys

# Region -> the scenes in it, in the order they should appear. Every retail scene
# has to appear exactly once; the check below is what keeps that true when the
# decomp pin moves.
AREAS = [
    ("Kokiri Forest", [
        "SCENE_KOKIRI_FOREST",
        "SCENE_LINKS_HOUSE",
        "SCENE_MIDOS_HOUSE",
        "SCENE_SARIAS_HOUSE",
        "SCENE_KNOW_IT_ALL_BROS_HOUSE",
        "SCENE_TWINS_HOUSE",
        "SCENE_KOKIRI_SHOP",
        "SCENE_DEKU_TREE",
        "SCENE_DEKU_TREE_BOSS",
    ]),
    ("Lost Woods", [
        "SCENE_LOST_WOODS",
        "SCENE_SACRED_FOREST_MEADOW",
        "SCENE_FOREST_TEMPLE",
        "SCENE_FOREST_TEMPLE_BOSS",
    ]),
    ("Hyrule Field", [
        "SCENE_HYRULE_FIELD",
        "SCENE_LON_LON_RANCH",
        "SCENE_LON_LON_BUILDINGS",
        "SCENE_STABLE",
    ]),
    ("Market", [
        "SCENE_MARKET_ENTRANCE_DAY",
        "SCENE_MARKET_ENTRANCE_NIGHT",
        "SCENE_MARKET_ENTRANCE_RUINS",
        "SCENE_MARKET_DAY",
        "SCENE_MARKET_NIGHT",
        "SCENE_MARKET_RUINS",
        "SCENE_BACK_ALLEY_DAY",
        "SCENE_BACK_ALLEY_NIGHT",
        "SCENE_BACK_ALLEY_HOUSE",
        "SCENE_DOG_LADY_HOUSE",
        "SCENE_BAZAAR",
        "SCENE_POTION_SHOP_MARKET",
        "SCENE_BOMBCHU_SHOP",
        "SCENE_HAPPY_MASK_SHOP",
        "SCENE_TREASURE_BOX_SHOP",
        "SCENE_SHOOTING_GALLERY",
        "SCENE_BOMBCHU_BOWLING_ALLEY",
        "SCENE_MARKET_GUARD_HOUSE",
        "SCENE_POTION_SHOP_GRANNY",
    ]),
    ("Temple of Time", [
        "SCENE_TEMPLE_OF_TIME_EXTERIOR_DAY",
        "SCENE_TEMPLE_OF_TIME_EXTERIOR_NIGHT",
        "SCENE_TEMPLE_OF_TIME_EXTERIOR_RUINS",
        "SCENE_TEMPLE_OF_TIME",
        "SCENE_CHAMBER_OF_THE_SAGES",
    ]),
    ("Hyrule Castle", [
        "SCENE_HYRULE_CASTLE",
        "SCENE_CASTLE_COURTYARD_GUARDS_DAY",
        "SCENE_CASTLE_COURTYARD_GUARDS_NIGHT",
        "SCENE_CASTLE_COURTYARD_ZELDA",
        "SCENE_OUTSIDE_GANONS_CASTLE",
    ]),
    ("Kakariko Village", [
        "SCENE_KAKARIKO_VILLAGE",
        "SCENE_IMPAS_HOUSE",
        "SCENE_KAKARIKO_CENTER_GUEST_HOUSE",
        "SCENE_HOUSE_OF_SKULLTULA",
        "SCENE_POTION_SHOP_KAKARIKO",
        "SCENE_WINDMILL_AND_DAMPES_GRAVE",
        "SCENE_BOTTOM_OF_THE_WELL",
    ]),
    ("Graveyard", [
        "SCENE_GRAVEYARD",
        "SCENE_GRAVEKEEPERS_HUT",
        "SCENE_REDEAD_GRAVE",
        "SCENE_GRAVE_WITH_FAIRYS_FOUNTAIN",
        "SCENE_ROYAL_FAMILYS_TOMB",
        "SCENE_SHADOW_TEMPLE",
        "SCENE_SHADOW_TEMPLE_BOSS",
    ]),
    ("Death Mountain", [
        "SCENE_DEATH_MOUNTAIN_TRAIL",
        "SCENE_DEATH_MOUNTAIN_CRATER",
        "SCENE_GORON_CITY",
        "SCENE_GORON_SHOP",
        "SCENE_DODONGOS_CAVERN",
        "SCENE_DODONGOS_CAVERN_BOSS",
        "SCENE_FIRE_TEMPLE",
        "SCENE_FIRE_TEMPLE_BOSS",
    ]),
    ("Zora's Domain", [
        "SCENE_ZORAS_RIVER",
        "SCENE_ZORAS_DOMAIN",
        "SCENE_ZORAS_FOUNTAIN",
        "SCENE_ZORA_SHOP",
        "SCENE_JABU_JABU",
        "SCENE_JABU_JABU_BOSS",
        "SCENE_ICE_CAVERN",
    ]),
    ("Lake Hylia", [
        "SCENE_LAKE_HYLIA",
        "SCENE_LAKESIDE_LABORATORY",
        "SCENE_FISHING_POND",
        "SCENE_WATER_TEMPLE",
        "SCENE_WATER_TEMPLE_BOSS",
    ]),
    ("Gerudo", [
        "SCENE_GERUDO_VALLEY",
        "SCENE_CARPENTERS_TENT",
        "SCENE_GERUDOS_FORTRESS",
        "SCENE_THIEVES_HIDEOUT",
        "SCENE_GERUDO_TRAINING_GROUND",
        "SCENE_HAUNTED_WASTELAND",
        "SCENE_DESERT_COLOSSUS",
        "SCENE_SPIRIT_TEMPLE",
        "SCENE_SPIRIT_TEMPLE_BOSS",
    ]),
    ("Ganon's Castle", [
        "SCENE_GANONS_TOWER",
        "SCENE_INSIDE_GANONS_CASTLE",
        "SCENE_GANONS_TOWER_COLLAPSE_INTERIOR",
        "SCENE_INSIDE_GANONS_CASTLE_COLLAPSE",
        "SCENE_GANONS_TOWER_COLLAPSE_EXTERIOR",
        "SCENE_GANONDORF_BOSS",
        "SCENE_GANON_BOSS",
    ]),
    ("Fairies and Grottos", [
        "SCENE_GROTTOS",
        "SCENE_FAIRYS_FOUNTAIN",
        "SCENE_GREAT_FAIRYS_FOUNTAIN_MAGIC",
        "SCENE_GREAT_FAIRYS_FOUNTAIN_SPELLS",
    ]),
    ("Cutscenes", [
        "SCENE_CUTSCENE_MAP",
    ]),
]

# Scene enum -> the name the menu shows. Only where turning the enum into words
# gets it wrong; everything else is title cased from the symbol.
SCENE_NAME_OVERRIDES = {
    "SCENE_KNOW_IT_ALL_BROS_HOUSE": "Know-It-All Brothers' House",
    "SCENE_TWINS_HOUSE": "Twins' House",
    "SCENE_MIDOS_HOUSE": "Mido's House",
    "SCENE_SARIAS_HOUSE": "Saria's House",
    "SCENE_LINKS_HOUSE": "Link's House",
    "SCENE_IMPAS_HOUSE": "Impa's House",
    "SCENE_DOG_LADY_HOUSE": "Dog Lady's House",
    "SCENE_GRAVEKEEPERS_HUT": "Gravekeeper's Hut",
    "SCENE_ROYAL_FAMILYS_TOMB": "Royal Family's Tomb",
    "SCENE_GRAVE_WITH_FAIRYS_FOUNTAIN": "Grave with Fairy's Fountain",
    "SCENE_FAIRYS_FOUNTAIN": "Fairy's Fountain",
    "SCENE_GREAT_FAIRYS_FOUNTAIN_MAGIC": "Great Fairy's Fountain (Magic)",
    "SCENE_GREAT_FAIRYS_FOUNTAIN_SPELLS": "Great Fairy's Fountain (Spells)",
    "SCENE_ZORAS_RIVER": "Zora's River",
    "SCENE_ZORAS_DOMAIN": "Zora's Domain",
    "SCENE_ZORAS_FOUNTAIN": "Zora's Fountain",
    "SCENE_ZORA_SHOP": "Zora Shop",
    "SCENE_GERUDOS_FORTRESS": "Gerudo's Fortress",
    "SCENE_THIEVES_HIDEOUT": "Thieves' Hideout",
    "SCENE_CARPENTERS_TENT": "Carpenters' Tent",
    "SCENE_GANONS_TOWER": "Ganon's Tower",
    "SCENE_GANONS_TOWER_COLLAPSE_INTERIOR": "Ganon's Tower Collapse (Interior)",
    "SCENE_GANONS_TOWER_COLLAPSE_EXTERIOR": "Ganon's Tower Collapse (Exterior)",
    "SCENE_INSIDE_GANONS_CASTLE": "Inside Ganon's Castle",
    "SCENE_INSIDE_GANONS_CASTLE_COLLAPSE": "Inside Ganon's Castle (Collapse)",
    "SCENE_OUTSIDE_GANONS_CASTLE": "Outside Ganon's Castle",
    "SCENE_HOUSE_OF_SKULLTULA": "House of Skulltula",
    "SCENE_KAKARIKO_CENTER_GUEST_HOUSE": "Kakariko Guest House",
    "SCENE_WINDMILL_AND_DAMPES_GRAVE": "Windmill and Dampe's Grave",
    "SCENE_DODONGOS_CAVERN": "Dodongo's Cavern",
    "SCENE_DODONGOS_CAVERN_BOSS": "Dodongo's Cavern Boss",
    "SCENE_LAKESIDE_LABORATORY": "Lakeside Laboratory",
    "SCENE_MARKET_ENTRANCE_DAY": "Market Entrance (Day)",
    "SCENE_MARKET_ENTRANCE_NIGHT": "Market Entrance (Night)",
    "SCENE_MARKET_ENTRANCE_RUINS": "Market Entrance (Ruins)",
    "SCENE_MARKET_DAY": "Market (Day)",
    "SCENE_MARKET_NIGHT": "Market (Night)",
    "SCENE_MARKET_RUINS": "Market (Ruins)",
    "SCENE_BACK_ALLEY_DAY": "Back Alley (Day)",
    "SCENE_BACK_ALLEY_NIGHT": "Back Alley (Night)",
    "SCENE_TEMPLE_OF_TIME_EXTERIOR_DAY": "Temple of Time Exterior (Day)",
    "SCENE_TEMPLE_OF_TIME_EXTERIOR_NIGHT": "Temple of Time Exterior (Night)",
    "SCENE_TEMPLE_OF_TIME_EXTERIOR_RUINS": "Temple of Time Exterior (Ruins)",
    "SCENE_CASTLE_COURTYARD_GUARDS_DAY": "Castle Courtyard Guards (Day)",
    "SCENE_CASTLE_COURTYARD_GUARDS_NIGHT": "Castle Courtyard Guards (Night)",
    "SCENE_CASTLE_COURTYARD_ZELDA": "Castle Courtyard (Zelda)",
    "SCENE_CHAMBER_OF_THE_SAGES": "Chamber of the Sages",
    "SCENE_POTION_SHOP_MARKET": "Potion Shop (Market)",
    "SCENE_POTION_SHOP_KAKARIKO": "Potion Shop (Kakariko)",
    "SCENE_POTION_SHOP_GRANNY": "Potion Shop (Granny)",
    "SCENE_BOTTOM_OF_THE_WELL": "Bottom of the Well",
}

SMALL_WORDS = {"of", "the", "and", "with"}

SCENE_RE = re.compile(
    r"^\s*/\*\s*(0x[0-9A-Fa-f]+)\s*\*/\s*DEFINE_SCENE\(\s*\w+\s*,\s*\w+\s*,\s*([A-Z0-9_]+)")
ENTRANCE_RE = re.compile(
    r"^\s*/\*\s*(0x[0-9A-Fa-f]+)\s*\*/\s*DEFINE_ENTRANCE\(\s*([A-Z0-9_]+)\s*,\s*([A-Z0-9_]+)\s*,\s*(\d+)")


def scene_display_name(symbol):
    if symbol in SCENE_NAME_OVERRIDES:
        return SCENE_NAME_OVERRIDES[symbol]
    words = symbol[len("SCENE_"):].split("_")
    return " ".join(w.lower() if (i and w.lower() in SMALL_WORDS) else w.capitalize()
                    for i, w in enumerate(words))


def read_scenes(oot_dir):
    """Retail scene ids, in table order. Anything under `#if DEBUG_ASSETS` is skipped:
    those scenes are not in a retail rom and warping to one would be a crash."""
    path = oot_dir / "include" / "tables" / "scene_table.h"
    scenes = []
    debug_only = False
    for line in path.read_text().splitlines():
        stripped = line.strip()
        if stripped.startswith("#if DEBUG_ASSETS"):
            debug_only = True
            continue
        if stripped.startswith("#endif"):
            debug_only = False
            continue
        match = SCENE_RE.match(line)
        if match and not debug_only:
            scenes.append((int(match.group(1), 16), match.group(2)))
    return scenes


def read_entrance_groups(oot_dir):
    """The first entry of each scene-layer group: (index, symbol, scene, spawn).

    Blank lines are the group separator, which is the decomp's own convention and
    the only thing in the file that marks where one entrance ends and the next
    begins - the layer entries carry the same scene and spawn as their base."""
    path = oot_dir / "include" / "tables" / "entrance_table.h"
    groups = []
    current = []
    for line in path.read_text().splitlines():
        match = ENTRANCE_RE.match(line)
        if match:
            current.append((int(match.group(1), 16), match.group(2),
                            match.group(3), int(match.group(4))))
        elif not line.strip() and current:
            groups.append(current)
            current = []
    if current:
        groups.append(current)
    return [group[0] for group in groups]


def entrance_display_name(symbol, scene_symbol, index):
    stem = "ENTR_" + scene_symbol[len("SCENE_"):] + "_"
    suffix = symbol[len(stem):] if symbol.startswith(stem) else None
    if suffix is not None and suffix.isdigit():
        label = "Spawn " + suffix
    else:
        # ENTR_TEST_SHOOTING_GALLERY_0 is the only one in the table that is not
        # named after the scene it lands in.
        label = symbol[len("ENTR_"):].replace("_", " ").title()
    return "{} (0x{:03X})".format(label, index)


def build(oot_dir):
    scenes = read_scenes(oot_dir)
    scene_ids = {symbol: scene_id for scene_id, symbol in scenes}

    claimed = [symbol for _, symbols in AREAS for symbol in symbols]
    missing = [symbol for _, symbol in scenes if symbol not in claimed]
    unknown = [symbol for symbol in claimed if symbol not in scene_ids]
    duplicated = [symbol for symbol in claimed if claimed.count(symbol) > 1]
    if missing or unknown or duplicated:
        raise SystemExit(
            "AREAS no longer matches the scene table.\n"
            "  claimed by no area: {}\n"
            "  not a retail scene: {}\n"
            "  claimed twice: {}".format(missing, unknown, sorted(set(duplicated))))

    by_scene = {}
    for index, symbol, scene_symbol, _spawn in read_entrance_groups(oot_dir):
        if scene_symbol not in scene_ids:
            # SCENE_UNUSED_6E and the debug scenes: still in the entrance table,
            # not in a retail rom.
            continue
        by_scene.setdefault(scene_symbol, []).append(
            (index, entrance_display_name(symbol, scene_symbol, index)))

    areas = []
    for area_name, scene_symbols in AREAS:
        area = []
        for scene_symbol in scene_symbols:
            entrances = by_scene.get(scene_symbol, [])
            if not entrances:
                # A scene with no entrance of its own is only reachable from
                # inside another one, so there is nothing the menu could send.
                continue
            area.append((scene_ids[scene_symbol], scene_display_name(scene_symbol),
                         entrances))
        if area:
            areas.append((area_name, area))
    return areas


HEADER = """\
#include <cstdint>
#include <vector>
#include <string>
#include "zelda_debug.h"

// GENERATED - do not edit. Regenerate with tools/gen_oot_scene_table.py.
//
// The debug menu's Ocarina of Time warp list, built from the decomp's
// scene_table.h and entrance_table.h. Majora's Mask's list beside this one
// (scene_table.cpp) is hand written and stays that way; this one is four hundred
// numbers copied out of two tables, which is a job for a script.
//
// The `values` on each scene are what makes the two lists able to share one
// control. Majora's Mask's warp travels as (scene << 8) | (spawn << 4) and
// do_warp packs it from the indices the menu is showing; Ocarina of Time's is a
// flat index into a 1557 row entrance table and cannot be packed from anything,
// so it is carried here per entrance and sent as itself. A scene with no
// `values` is the old packing, which is how the Majora's Mask list keeps working
// unchanged.
//
// {scene_count} scenes and {entrance_count} entrances, in {area_count} regions. Only the first entry of
// each scene-layer group is here: the entrance table stores four consecutive
// entries per entrance (child day, child night, adult day, adult night) and the
// game applies the layer offset itself.
"""


def emit(areas):
    scene_count = sum(len(scenes) for _, scenes in areas)
    entrance_count = sum(len(entrances) for _, scenes in areas for _, _, entrances in scenes)
    out = [HEADER.format(scene_count=scene_count, entrance_count=entrance_count,
                         area_count=len(areas))]
    out.append("\nstd::vector<zelda64::AreaWarps> zelda64::game_warps_oot {\n")
    for area_name, scenes in areas:
        out.append('    {{ "{}", {{\n'.format(area_name))
        for scene_id, scene_name, entrances in scenes:
            out.append("        {\n")
            out.append('            {}, "{}", {{\n'.format(scene_id, scene_name))
            for _, label in entrances:
                out.append('                "{}",\n'.format(label))
            out.append("            }, {\n")
            out.append("                " + ", ".join(str(index) for index, _ in entrances) + ",\n")
            out.append("            }\n")
            out.append("        },\n")
        out.append("    }},\n")
    out.append("};\n")
    return "".join(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    here = pathlib.Path(__file__).resolve().parent
    parser.add_argument("--oot-dir", type=pathlib.Path,
                        default=here.parent.parent / "oot",
                        help="path to the zeldaret/oot checkout")
    parser.add_argument("--out", type=pathlib.Path,
                        default=here.parent / "src" / "game" / "scene_table_oot.cpp")
    parser.add_argument("--check", action="store_true",
                        help="exit non-zero if the file on disk is out of date")
    args = parser.parse_args()

    if not (args.oot_dir / "include" / "tables" / "scene_table.h").exists():
        raise SystemExit("no decomp checkout at {} (pass --oot-dir)".format(args.oot_dir))

    text = emit(build(args.oot_dir))
    if args.check:
        current = args.out.read_text() if args.out.exists() else ""
        if current != text:
            print("{} is out of date".format(args.out), file=sys.stderr)
            return 1
        print("{} is up to date".format(args.out))
        return 0

    args.out.write_text(text)
    print("wrote {}".format(args.out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
