/* (Triforce) The randomizer's beatability data. GENERATED from the OoT Randomizer's logic (GPL-3.0, github.com/OoTRandomizer/OoT-Randomizer),
   run over the 15 key-item identities below (see rando.c). Do not edit by hand. A requirement is a list of masks: any one whose items are all
   held is enough (an empty list: never). A class is a group of chests (a dungeon, the overworld) or one key-item chest. A group's list is
   the way to its EASIEST chest (the remake's chests are not matched one by one with the Randomizer's locations), a key-item chest's its own. */
#ifndef HOST_RANDO_LOGIC_H
#define HOST_RANDO_LOGIC_H
#include <stdint.h>

typedef struct { int n; uint16_t m[4]; } RlReq;

static const char *const kRlItems[15] = {"Item_Range_SlingShot", "Item_Boomerang", "Item_BombBag_Small", "Item_Range_Bow", "Item_Hammer", "Item_Hookshot", "Item_Longshot", "Item_Gauntlet_Silver", "Item_Gauntlet_Gold", "Item_HeavyBoot", "Item_HoverBoot", "Item_Shield_Mirror", "Item_GlassTruth", "Item_Arrow_Ice", "Item_Sword_Kokiri"};
static const char *const kRlDirs[12] = {"BottomOfTheWell", "DodongoCavern", "FireTemple", "ForestTemple", "GerudoTrainingGround", "IceCavern", "InsideGanonCastle", "InsideJabuJabuBelly", "InsideTheDekuTree", "ShadowTemple", "SpiritTemple", "WaterTemple"};
static const uint64_t kRlHash[15] = {821372075822700622ull, 5527401621172639818ull, 12161709650443144121ull, 11082303891540711688ull, 12083259659219538030ull, 16041366920820397198ull, 18059711820131847165ull, 2490063409521505179ull, 17657623307936361670ull, 6859289214373847284ull, 14535080241831873137ull, 10012167258643363548ull, 16654774376625985726ull, 1941083939696166170ull, 16757992849254467386ull};
/* classes: 0..11 the dungeons (kRlDirs), 12 everything else, 13..27 the key-item chests (kRlItems order) */
static const RlReq kRlAny[28] = {  /* a chest holding anything but a key: every key held */
    {1, {0x4007}},
    {1, {0x4001}},
    {3, {0x4027, 0x4047, 0x4407}},
    {2, {0x4027, 0x4047}},
    {4, {0x400f, 0x4027, 0x4047, 0x4407}},
    {1, {0x4007}},
    {2, {0x5cff, 0x5d7f}},
    {1, {0x4007}},
    {1, {0x4000}},
    {1, {0x507f}},
    {2, {0x5067, 0x5407}},
    {3, {0x406f, 0x4227, 0x4247}},
    {1, {0x0}},
    {1, {0x4000}},
    {1, {0x4005}},
    {1, {0x4001}},
    {2, {0x4027, 0x4047}},
    {3, {0x4027, 0x4047, 0x4407}},
    {1, {0x4007}},
    {3, {0x4067, 0x4227, 0x4247}},
    {2, {0x5067, 0x5407}},
    {2, {0x5cff, 0x5d7f}},
    {1, {0x4007}},
    {1, {0x507f}},
    {4, {0x50e7, 0x5167, 0x5487, 0x5507}},
    {1, {0x4007}},
    {4, {0x400f, 0x4027, 0x4047, 0x4407}},
    {1, {0x0}},
};
static const RlReq kRlKey[28] = {  /* a chest holding a small or boss key: every key held but its own dungeon's */
    {1, {0x4007}},
    {1, {0x4001}},
    {3, {0x4027, 0x4047, 0x4407}},
    {2, {0x4027, 0x4047}},
    {4, {0x400f, 0x4027, 0x4047, 0x4407}},
    {1, {0x4007}},
    {2, {0x5cff, 0x5d7f}},
    {1, {0x4007}},
    {1, {0x4000}},
    {1, {0x507f}},
    {2, {0x5067, 0x5407}},
    {2, {0x4227, 0x4247}},
    {1, {0x0}},
    {1, {0x4000}},
    {1, {0x4005}},
    {1, {0x4001}},
    {0, {}},
    {0, {}},
    {1, {0x4007}},
    {0, {}},
    {2, {0x5067, 0x5407}},
    {2, {0x5cff, 0x5d7f}},
    {1, {0x4007}},
    {1, {0x507f}},
    {4, {0x50e7, 0x5167, 0x5487, 0x5507}},
    {1, {0x4007}},
    {0, {}},
    {1, {0x0}},
};
static const RlReq kRlGoal = {1, {0x5dff}};

#endif
