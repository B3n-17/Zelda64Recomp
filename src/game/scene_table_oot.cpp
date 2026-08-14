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
// 92 scenes and 347 entrances, in 15 regions. Only the first entry of
// each scene-layer group is here: the entrance table stores four consecutive
// entries per entrance (child day, child night, adult day, adult night) and the
// game applies the layer offset itself.

std::vector<zelda64::AreaWarps> zelda64::game_warps_oot {
    { "Kokiri Forest", {
        {
            85, "Kokiri Forest", {
                "Spawn 0 (0x0EE)",
                "Spawn 1 (0x209)",
                "Spawn 2 (0x20D)",
                "Spawn 3 (0x211)",
                "Spawn 4 (0x266)",
                "Spawn 5 (0x26A)",
                "Spawn 6 (0x286)",
                "Spawn 7 (0x338)",
                "Spawn 8 (0x33C)",
                "Spawn 9 (0x443)",
                "Spawn 10 (0x447)",
                "Spawn 11 (0x457)",
                "Spawn 12 (0x5E8)",
            }, {
                238, 521, 525, 529, 614, 618, 646, 824, 828, 1091, 1095, 1111, 1512,
            }
        },
        {
            52, "Link's House", {
                "Spawn 0 (0x0BB)",
                "Spawn 1 (0x272)",
            }, {
                187, 626,
            }
        },
        {
            40, "Mido's House", {
                "Spawn 0 (0x433)",
            }, {
                1075,
            }
        },
        {
            41, "Saria's House", {
                "Spawn 0 (0x437)",
            }, {
                1079,
            }
        },
        {
            38, "Know-It-All Brothers' House", {
                "Spawn 0 (0x0C9)",
            }, {
                201,
            }
        },
        {
            39, "Twins' House", {
                "Spawn 0 (0x09C)",
            }, {
                156,
            }
        },
        {
            45, "Kokiri Shop", {
                "Spawn 0 (0x0C1)",
            }, {
                193,
            }
        },
        {
            0, "Deku Tree", {
                "Spawn 0 (0x000)",
                "Spawn 1 (0x252)",
            }, {
                0, 594,
            }
        },
        {
            17, "Deku Tree Boss", {
                "Spawn 0 (0x40F)",
            }, {
                1039,
            }
        },
    }},
    { "Lost Woods", {
        {
            91, "Lost Woods", {
                "Spawn 0 (0x11E)",
                "Spawn 1 (0x1A9)",
                "Spawn 2 (0x1AD)",
                "Spawn 3 (0x1B1)",
                "Spawn 4 (0x4C6)",
                "Spawn 5 (0x4D2)",
                "Spawn 6 (0x4D6)",
                "Spawn 7 (0x4DA)",
                "Spawn 8 (0x4DE)",
                "Spawn 9 (0x5E0)",
            }, {
                286, 425, 429, 433, 1222, 1234, 1238, 1242, 1246, 1504,
            }
        },
        {
            86, "Sacred Forest Meadow", {
                "Spawn 0 (0x0FC)",
                "Spawn 1 (0x215)",
                "Spawn 2 (0x600)",
                "Spawn 3 (0x608)",
            }, {
                252, 533, 1536, 1544,
            }
        },
        {
            3, "Forest Temple", {
                "Spawn 0 (0x169)",
                "Spawn 1 (0x24E)",
                "Spawn 2 (0x584)",
            }, {
                361, 590, 1412,
            }
        },
        {
            20, "Forest Temple Boss", {
                "Spawn 0 (0x00C)",
            }, {
                12,
            }
        },
    }},
    { "Hyrule Field", {
        {
            81, "Hyrule Field", {
                "Spawn 0 (0x0CD)",
                "Spawn 1 (0x17D)",
                "Spawn 2 (0x181)",
                "Spawn 3 (0x185)",
                "Spawn 4 (0x189)",
                "Spawn 5 (0x18D)",
                "Spawn 6 (0x1F9)",
                "Spawn 7 (0x1FD)",
                "Spawn 8 (0x27A)",
                "Spawn 9 (0x27E)",
                "Spawn 10 (0x282)",
                "Spawn 11 (0x28A)",
                "Spawn 12 (0x28E)",
                "Spawn 13 (0x292)",
                "Spawn 14 (0x311)",
                "Spawn 15 (0x476)",
                "Spawn 16 (0x50F)",
                "Spawn 17 (0x594)",
            }, {
                205, 381, 385, 389, 393, 397, 505, 509, 634, 638, 642, 650, 654, 658, 785, 1142, 1295, 1428,
            }
        },
        {
            99, "Lon Lon Ranch", {
                "Spawn 0 (0x157)",
                "Spawn 1 (0x2AE)",
                "Spawn 2 (0x2E2)",
                "Spawn 3 (0x2E6)",
                "Spawn 4 (0x378)",
                "Spawn 5 (0x42F)",
                "Spawn 6 (0x4CA)",
                "Spawn 7 (0x4CE)",
                "Spawn 8 (0x558)",
                "Spawn 9 (0x55C)",
                "Spawn 10 (0x5D4)",
            }, {
                343, 686, 738, 742, 888, 1071, 1226, 1230, 1368, 1372, 1492,
            }
        },
        {
            76, "Lon Lon Buildings", {
                "Spawn 0 (0x04F)",
                "Spawn 1 (0x5D0)",
                "Spawn 2 (0x5E4)",
            }, {
                79, 1488, 1508,
            }
        },
        {
            54, "Stable", {
                "Spawn 0 (0x2F9)",
            }, {
                761,
            }
        },
    }},
    { "Market", {
        {
            27, "Market Entrance (Day)", {
                "Spawn 0 (0x033)",
                "Spawn 2 (0x26E)",
                "Spawn 1 (0x276)",
            }, {
                51, 622, 630,
            }
        },
        {
            32, "Market (Day)", {
                "Spawn 0 (0x0B1)",
                "Spawn 8 (0x1CD)",
                "Spawn 9 (0x1D1)",
                "Spawn 10 (0x1D5)",
                "Spawn 1 (0x25A)",
                "Spawn 2 (0x25E)",
                "Spawn 3 (0x262)",
                "Spawn 4 (0x29E)",
                "Spawn 5 (0x2A2)",
                "Spawn 6 (0x3B8)",
                "Spawn 7 (0x3BC)",
            }, {
                177, 461, 465, 469, 602, 606, 610, 670, 674, 952, 956,
            }
        },
        {
            30, "Back Alley (Day)", {
                "Spawn 3 (0x067)",
                "Spawn 0 (0x0AD)",
                "Spawn 1 (0x29A)",
                "Spawn 4 (0x38C)",
                "Spawn 2 (0x3C0)",
            }, {
                103, 173, 666, 908, 960,
            }
        },
        {
            43, "Back Alley House", {
                "Spawn 0 (0x43B)",
            }, {
                1083,
            }
        },
        {
            53, "Dog Lady's House", {
                "Spawn 0 (0x398)",
            }, {
                920,
            }
        },
        {
            44, "Bazaar", {
                "Spawn 0 (0x0B7)",
                "Spawn 1 (0x52C)",
            }, {
                183, 1324,
            }
        },
        {
            49, "Potion Shop (Market)", {
                "Spawn 0 (0x388)",
            }, {
                904,
            }
        },
        {
            50, "Bombchu Shop", {
                "Spawn 0 (0x390)",
                "Spawn 1 (0x528)",
            }, {
                912, 1320,
            }
        },
        {
            51, "Happy Mask Shop", {
                "Spawn 0 (0x530)",
            }, {
                1328,
            }
        },
        {
            16, "Treasure Box Shop", {
                "Spawn 0 (0x063)",
            }, {
                99,
            }
        },
        {
            66, "Shooting Gallery", {
                "Spawn 0 (0x03B)",
                "Spawn 1 (0x16D)",
                "Test Shooting Gallery 0 (0x2EA)",
            }, {
                59, 365, 746,
            }
        },
        {
            75, "Bombchu Bowling Alley", {
                "Spawn 0 (0x507)",
            }, {
                1287,
            }
        },
        {
            77, "Market Guard House", {
                "Spawn 0 (0x07E)",
            }, {
                126,
            }
        },
        {
            78, "Potion Shop (Granny)", {
                "Spawn 0 (0x072)",
            }, {
                114,
            }
        },
    }},
    { "Temple of Time", {
        {
            35, "Temple of Time Exterior (Day)", {
                "Spawn 0 (0x171)",
                "Spawn 1 (0x472)",
            }, {
                369, 1138,
            }
        },
        {
            67, "Temple of Time", {
                "Spawn 0 (0x053)",
                "Spawn 2 (0x2CA)",
                "Spawn 3 (0x320)",
                "Spawn 4 (0x324)",
                "Spawn 5 (0x58C)",
                "Spawn 6 (0x590)",
                "Spawn 7 (0x5F4)",
            }, {
                83, 714, 800, 804, 1420, 1424, 1524,
            }
        },
        {
            68, "Chamber of the Sages", {
                "Spawn 0 (0x06B)",
                "Spawn 1 (0x2CE)",
            }, {
                107, 718,
            }
        },
    }},
    { "Hyrule Castle", {
        {
            95, "Hyrule Castle", {
                "Spawn 0 (0x138)",
                "Spawn 1 (0x23D)",
                "Spawn 2 (0x340)",
                "Spawn 4 (0x47E)",
                "Spawn 3 (0x4FA)",
            }, {
                312, 573, 832, 1150, 1274,
            }
        },
        {
            69, "Castle Courtyard Guards (Day)", {
                "Spawn 0 (0x07A)",
                "Spawn 1 (0x296)",
            }, {
                122, 662,
            }
        },
        {
            74, "Castle Courtyard (Zelda)", {
                "Spawn 0 (0x400)",
                "Spawn 1 (0x5F0)",
            }, {
                1024, 1520,
            }
        },
    }},
    { "Kakariko Village", {
        {
            82, "Kakariko Village", {
                "Spawn 0 (0x0DB)",
                "Spawn 1 (0x191)",
                "Spawn 2 (0x195)",
                "Spawn 3 (0x201)",
                "Spawn 4 (0x2A6)",
                "Spawn 5 (0x345)",
                "Spawn 6 (0x349)",
                "Spawn 7 (0x34D)",
                "Spawn 8 (0x351)",
                "Spawn 9 (0x44B)",
                "Spawn 10 (0x463)",
                "Spawn 11 (0x4EE)",
                "Spawn 12 (0x4FF)",
                "Spawn 13 (0x513)",
                "Spawn 14 (0x554)",
                "Spawn 15 (0x5DC)",
            }, {
                219, 401, 405, 513, 678, 837, 841, 845, 849, 1099, 1123, 1262, 1279, 1299, 1364, 1500,
            }
        },
        {
            55, "Impa's House", {
                "Spawn 0 (0x39C)",
                "Spawn 1 (0x5C8)",
            }, {
                924, 1480,
            }
        },
        {
            42, "Kakariko Guest House", {
                "Spawn 0 (0x2FD)",
            }, {
                765,
            }
        },
        {
            80, "House of Skulltula", {
                "Spawn 0 (0x550)",
            }, {
                1360,
            }
        },
        {
            48, "Potion Shop (Kakariko)", {
                "Spawn 0 (0x384)",
                "Spawn 1 (0x3E8)",
                "Spawn 2 (0x3EC)",
            }, {
                900, 1000, 1004,
            }
        },
        {
            72, "Windmill and Dampe's Grave", {
                "Spawn 0 (0x44F)",
                "Spawn 1 (0x453)",
                "Spawn 2 (0x503)",
            }, {
                1103, 1107, 1283,
            }
        },
        {
            8, "Bottom of the Well", {
                "Spawn 0 (0x098)",
                "Spawn 1 (0x5CC)",
            }, {
                152, 1484,
            }
        },
    }},
    { "Graveyard", {
        {
            83, "Graveyard", {
                "Spawn 0 (0x0E4)",
                "Spawn 1 (0x205)",
                "Spawn 2 (0x355)",
                "Spawn 3 (0x359)",
                "Spawn 4 (0x35D)",
                "Spawn 5 (0x361)",
                "Spawn 6 (0x50B)",
                "Spawn 7 (0x568)",
                "Spawn 8 (0x580)",
            }, {
                228, 517, 853, 857, 861, 865, 1291, 1384, 1408,
            }
        },
        {
            58, "Gravekeeper's Hut", {
                "Spawn 0 (0x30D)",
            }, {
                781,
            }
        },
        {
            63, "Redead Grave", {
                "Spawn 0 (0x31C)",
            }, {
                796,
            }
        },
        {
            64, "Grave with Fairy's Fountain", {
                "Spawn 0 (0x04B)",
            }, {
                75,
            }
        },
        {
            65, "Royal Family's Tomb", {
                "Spawn 0 (0x02D)",
                "Spawn 1 (0x574)",
            }, {
                45, 1396,
            }
        },
        {
            7, "Shadow Temple", {
                "Spawn 0 (0x037)",
                "Spawn 1 (0x2B2)",
                "Spawn 2 (0x2B6)",
                "Spawn 3 (0x4EA)",
            }, {
                55, 690, 694, 1258,
            }
        },
        {
            24, "Shadow Temple Boss", {
                "Spawn 0 (0x413)",
            }, {
                1043,
            }
        },
    }},
    { "Death Mountain", {
        {
            96, "Death Mountain Trail", {
                "Spawn 0 (0x13D)",
                "Spawn 1 (0x1B9)",
                "Spawn 2 (0x1BD)",
                "Spawn 3 (0x242)",
                "Spawn 4 (0x45B)",
                "Spawn 5 (0x47A)",
            }, {
                317, 441, 445, 578, 1115, 1146,
            }
        },
        {
            97, "Death Mountain Crater", {
                "Spawn 0 (0x147)",
                "Spawn 1 (0x246)",
                "Spawn 2 (0x24A)",
                "Spawn 3 (0x482)",
                "Spawn 4 (0x4F6)",
                "Spawn 5 (0x564)",
            }, {
                327, 582, 586, 1154, 1270, 1380,
            }
        },
        {
            98, "Goron City", {
                "Spawn 0 (0x14D)",
                "Spawn 1 (0x1C1)",
                "Spawn 2 (0x3FC)",
                "Spawn 3 (0x4E2)",
            }, {
                333, 449, 1020, 1250,
            }
        },
        {
            46, "Goron Shop", {
                "Spawn 0 (0x37C)",
            }, {
                892,
            }
        },
        {
            1, "Dodongo's Cavern", {
                "Spawn 0 (0x004)",
                "Spawn 1 (0x0C5)",
            }, {
                4, 197,
            }
        },
        {
            18, "Dodongo's Cavern Boss", {
                "Spawn 0 (0x40B)",
            }, {
                1035,
            }
        },
        {
            4, "Fire Temple", {
                "Spawn 0 (0x165)",
                "Spawn 1 (0x175)",
            }, {
                357, 373,
            }
        },
        {
            21, "Fire Temple Boss", {
                "Spawn 0 (0x305)",
            }, {
                773,
            }
        },
    }},
    { "Zora's Domain", {
        {
            84, "Zora's River", {
                "Spawn 0 (0x0EA)",
                "Spawn 1 (0x199)",
                "Spawn 2 (0x19D)",
                "Spawn 3 (0x1D9)",
                "Spawn 4 (0x1DD)",
            }, {
                234, 409, 413, 473, 477,
            }
        },
        {
            88, "Zora's Domain", {
                "Spawn 0 (0x108)",
                "Spawn 3 (0x153)",
                "Spawn 1 (0x1A1)",
                "Spawn 4 (0x328)",
                "Spawn 2 (0x3C4)",
            }, {
                264, 339, 417, 808, 964,
            }
        },
        {
            89, "Zora's Fountain", {
                "Spawn 0 (0x10E)",
                "Spawn 1 (0x221)",
                "Spawn 2 (0x225)",
                "Spawn 5 (0x394)",
                "Spawn 3 (0x3D4)",
                "Spawn 4 (0x3D8)",
            }, {
                270, 545, 549, 916, 980, 984,
            }
        },
        {
            47, "Zora Shop", {
                "Spawn 0 (0x380)",
            }, {
                896,
            }
        },
        {
            2, "Jabu Jabu", {
                "Spawn 0 (0x028)",
                "Spawn 1 (0x407)",
            }, {
                40, 1031,
            }
        },
        {
            19, "Jabu Jabu Boss", {
                "Spawn 0 (0x301)",
            }, {
                769,
            }
        },
        {
            9, "Ice Cavern", {
                "Spawn 0 (0x088)",
                "Spawn 1 (0x5D8)",
            }, {
                136, 1496,
            }
        },
    }},
    { "Lake Hylia", {
        {
            87, "Lake Hylia", {
                "Spawn 0 (0x102)",
                "Spawn 1 (0x219)",
                "Spawn 2 (0x21D)",
                "Spawn 6 (0x309)",
                "Spawn 3 (0x3C8)",
                "Spawn 4 (0x3CC)",
                "Spawn 5 (0x4E6)",
                "Spawn 7 (0x560)",
                "Spawn 8 (0x604)",
                "Spawn 9 (0x60C)",
            }, {
                258, 537, 541, 777, 968, 972, 1254, 1376, 1540, 1548,
            }
        },
        {
            56, "Lakeside Laboratory", {
                "Spawn 0 (0x043)",
                "Spawn 1 (0x1C5)",
            }, {
                67, 453,
            }
        },
        {
            73, "Fishing Pond", {
                "Spawn 0 (0x45F)",
            }, {
                1119,
            }
        },
        {
            5, "Water Temple", {
                "Spawn 0 (0x010)",
                "Spawn 1 (0x423)",
            }, {
                16, 1059,
            }
        },
        {
            22, "Water Temple Boss", {
                "Spawn 0 (0x417)",
            }, {
                1047,
            }
        },
    }},
    { "Gerudo", {
        {
            90, "Gerudo Valley", {
                "Spawn 0 (0x117)",
                "Spawn 1 (0x1A5)",
                "Spawn 2 (0x229)",
                "Spawn 3 (0x22D)",
                "Spawn 4 (0x3D0)",
            }, {
                279, 421, 553, 557, 976,
            }
        },
        {
            57, "Carpenters' Tent", {
                "Spawn 0 (0x3A0)",
            }, {
                928,
            }
        },
        {
            93, "Gerudo's Fortress", {
                "Spawn 0 (0x129)",
                "Spawn 1 (0x231)",
                "Spawn 2 (0x235)",
                "Spawn 3 (0x239)",
                "Spawn 4 (0x2AA)",
                "Spawn 5 (0x2BA)",
                "Spawn 6 (0x2BE)",
                "Spawn 7 (0x2C2)",
                "Spawn 8 (0x2C6)",
                "Spawn 9 (0x2D2)",
                "Spawn 10 (0x2D6)",
                "Spawn 11 (0x2DA)",
                "Spawn 12 (0x2DE)",
                "Spawn 13 (0x3A4)",
                "Spawn 14 (0x3A8)",
                "Spawn 15 (0x3AC)",
                "Spawn 16 (0x3B0)",
                "Spawn 17 (0x3B4)",
                "Spawn 18 (0x5F8)",
            }, {
                297, 561, 565, 569, 682, 698, 702, 706, 710, 722, 726, 730, 734, 932, 936, 940, 944, 948, 1528,
            }
        },
        {
            12, "Thieves' Hideout", {
                "Spawn 0 (0x486)",
                "Spawn 1 (0x48A)",
                "Spawn 2 (0x48E)",
                "Spawn 3 (0x492)",
                "Spawn 4 (0x496)",
                "Spawn 5 (0x49A)",
                "Spawn 6 (0x49E)",
                "Spawn 7 (0x4A2)",
                "Spawn 8 (0x4A6)",
                "Spawn 9 (0x4AA)",
                "Spawn 10 (0x4AE)",
                "Spawn 11 (0x4B2)",
                "Spawn 12 (0x570)",
            }, {
                1158, 1162, 1166, 1170, 1174, 1178, 1182, 1186, 1190, 1194, 1198, 1202, 1392,
            }
        },
        {
            11, "Gerudo Training Ground", {
                "Spawn 0 (0x008)",
            }, {
                8,
            }
        },
        {
            94, "Haunted Wasteland", {
                "Spawn 0 (0x130)",
                "Spawn 1 (0x365)",
                "Spawn 2 (0x369)",
            }, {
                304, 869, 873,
            }
        },
        {
            92, "Desert Colossus", {
                "Spawn 0 (0x123)",
                "Spawn 1 (0x1E1)",
                "Spawn 2 (0x1E5)",
                "Spawn 3 (0x1E9)",
                "Spawn 4 (0x1ED)",
                "Spawn 5 (0x1F1)",
                "Spawn 6 (0x1F5)",
                "Spawn 7 (0x57C)",
                "Spawn 8 (0x610)",
            }, {
                291, 481, 485, 489, 493, 497, 501, 1404, 1552,
            }
        },
        {
            6, "Spirit Temple", {
                "Spawn 0 (0x082)",
                "Spawn 1 (0x2F5)",
                "Spawn 2 (0x3F0)",
                "Spawn 3 (0x3F4)",
                "Spawn 4 (0x3F8)",
            }, {
                130, 757, 1008, 1012, 1016,
            }
        },
        {
            23, "Spirit Temple Boss", {
                "Spawn 0 (0x08D)",
                "Spawn 2 (0x5EC)",
            }, {
                141, 1516,
            }
        },
    }},
    { "Ganon's Castle", {
        {
            10, "Ganon's Tower", {
                "Spawn 0 (0x41B)",
                "Spawn 1 (0x427)",
                "Spawn 2 (0x42B)",
            }, {
                1051, 1063, 1067,
            }
        },
        {
            13, "Inside Ganon's Castle", {
                "Spawn 0 (0x467)",
                "Spawn 1 (0x534)",
                "Spawn 2 (0x538)",
                "Spawn 3 (0x53C)",
                "Spawn 4 (0x540)",
                "Spawn 5 (0x544)",
                "Spawn 6 (0x548)",
                "Spawn 7 (0x54C)",
            }, {
                1127, 1332, 1336, 1340, 1344, 1348, 1352, 1356,
            }
        },
        {
            14, "Ganon's Tower Collapse (Interior)", {
                "Spawn 1 (0x134)",
                "Spawn 0 (0x179)",
                "Spawn 2 (0x1B5)",
                "Spawn 3 (0x256)",
                "Spawn 4 (0x3DC)",
                "Spawn 5 (0x3E0)",
                "Spawn 6 (0x3E4)",
                "Spawn 7 (0x4B6)",
            }, {
                308, 377, 437, 598, 988, 992, 996, 1206,
            }
        },
        {
            15, "Inside Ganon's Castle (Collapse)", {
                "Spawn 0 (0x56C)",
            }, {
                1388,
            }
        },
        {
            26, "Ganon's Tower Collapse (Exterior)", {
                "Spawn 5 (0x1C9)",
                "Spawn 2 (0x32C)",
                "Spawn 3 (0x330)",
                "Spawn 4 (0x334)",
                "Spawn 0 (0x43F)",
                "Spawn 1 (0x4BA)",
                "Spawn 6 (0x51C)",
                "Spawn 7 (0x524)",
            }, {
                457, 812, 816, 820, 1087, 1210, 1308, 1316,
            }
        },
        {
            25, "Ganondorf Boss", {
                "Spawn 0 (0x41F)",
            }, {
                1055,
            }
        },
        {
            79, "Ganon Boss", {
                "Spawn 0 (0x517)",
            }, {
                1303,
            }
        },
    }},
    { "Fairies and Grottos", {
        {
            62, "Grottos", {
                "Spawn 0 (0x03F)",
                "Spawn 1 (0x598)",
                "Spawn 2 (0x59C)",
                "Spawn 3 (0x5A0)",
                "Spawn 4 (0x5A4)",
                "Spawn 5 (0x5A8)",
                "Spawn 6 (0x5AC)",
                "Spawn 7 (0x5B0)",
                "Spawn 8 (0x5B4)",
                "Spawn 9 (0x5B8)",
                "Spawn 10 (0x5BC)",
                "Spawn 11 (0x5C0)",
                "Spawn 12 (0x5C4)",
                "Spawn 13 (0x5FC)",
            }, {
                63, 1432, 1436, 1440, 1444, 1448, 1452, 1456, 1460, 1464, 1468, 1472, 1476, 1532,
            }
        },
        {
            60, "Fairy's Fountain", {
                "Spawn 0 (0x36D)",
            }, {
                877,
            }
        },
        {
            59, "Great Fairy's Fountain (Magic)", {
                "Spawn 0 (0x315)",
                "Spawn 1 (0x4BE)",
                "Spawn 2 (0x4C2)",
                "Spawn 3 (0x4F2)",
            }, {
                789, 1214, 1218, 1266,
            }
        },
        {
            61, "Great Fairy's Fountain (Spells)", {
                "Spawn 0 (0x371)",
                "Spawn 1 (0x578)",
                "Spawn 2 (0x588)",
            }, {
                881, 1400, 1416,
            }
        },
    }},
    { "Cutscenes", {
        {
            71, "Cutscene Map", {
                "Spawn 0 (0x0A0)",
            }, {
                160,
            }
        },
    }},
};
