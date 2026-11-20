import sys, json
sys.path.insert(0, '/home/claude/soh/gen'); sys.path.insert(0, '/home/claude/soh')
import oracle
r = json.load(open('/home/claude/soh/gen/oracle2.json'))
o3 = {}
for f in ('o3a', 'o3b'): o3.update(json.load(open('/home/claude/soh/gen/%s.json' % f)))
w, locs, shuf = oracle.build()
L = r['locs']; N = 15; NM = 1 << N
DIRS = ['BottomOfTheWell', 'DodongoCavern', 'FireTemple', 'ForestTemple', 'GerudoTrainingGround', 'IceCavern', 'InsideGanonCastle',
        'InsideJabuJabuBelly', 'InsideTheDekuTree', 'ShadowTemple', 'SpiritTemple', 'WaterTemple']
sid = {'BottomOfTheWell': 8, 'DodongoCavern': 1, 'FireTemple': 4, 'ForestTemple': 3, 'GerudoTrainingGround': 11, 'IceCavern': 9,
       'InsideGanonCastle': 13, 'InsideJabuJabuBelly': 2, 'InsideTheDekuTree': 0, 'ShadowTemple': 7, 'SpiritTemple': 6, 'WaterTemple': 5}
sc2dir = {v: k for k, v in sid.items()}
ident = oracle.IDENT_LOCS
identset = set(ident.values())
def mins(ok):
    out = []
    for m in range(NM):
        if ok[m] and not any(ok[m & ~(1 << b)] for b in range(N) if m >> b & 1): out.append(m)
    return out
def allof(tab, bits):   # (the easiest chest of the group: any one location of it reachable)
    return mins([t & bits != 0 for t in tab])
everK = 0
for t in r['K']: everK |= t
K = {}; Nn = {}
for d in DIRS:
    idx = [i for i, n in enumerate(L) if n not in identset and sc2dir.get(locs[n].scene) == d]
    kb = sum(1 << i for i in idx) & everK
    K[d] = allof(r['K'], kb)
    t3 = o3[d]; names = t3['locs']
    ex = 0
    for t in t3['tab']: ex |= t
    nb = sum(1 << j for j, n in enumerate(names) if n not in identset and ex >> j & 1)
    Nn[d] = allof(t3['tab'], nb) if nb else K[d]
idx = [i for i, n in enumerate(L) if n not in identset and locs[n].scene not in sc2dir]
kb = sum(1 << i for i in idx) & everK
K['Overworld'] = Nn['Overworld'] = allof(r['K'], kb)
IK = {}; IN = {}
for b, n in ident.items():
    i = L.index(n)
    IK[b] = mins([t >> i & 1 for t in r['K']])
    d = sc2dir.get(locs[n].scene)
    if d:
        t3 = o3[d]; j = t3['locs'].index(n)
        IN[b] = mins([t >> j & 1 for t in t3['tab']])
    else:
        IN[b] = IK[b]
goal = mins(r['goal'])
BIT = {'Slingshot': 'Item_Range_SlingShot', 'Boomerang': 'Item_Boomerang', 'BombBag': 'Item_BombBag_Small', 'Bow': 'Item_Range_Bow',
       'Hammer': 'Item_Hammer', 'Hookshot': 'Item_Hookshot', 'Longshot': 'Item_Longshot', 'Silver': 'Item_Gauntlet_Silver',
       'Gold': 'Item_Gauntlet_Gold', 'Iron': 'Item_HeavyBoot', 'Hover': 'Item_HoverBoot', 'Mirror': 'Item_Shield_Mirror',
       'Lens': 'Item_GlassTruth', 'Ice': 'Item_Arrow_Ice', 'KSword': 'Item_Sword_Kokiri'}
IDHASH = json.load(open('/tmp/identhash.json'))
mx = max(len(v) for v in list(K.values()) + list(Nn.values()) + list(IK.values()) + list(IN.values()) + [goal])
print('max terms', mx)
def row(v): return '{%d, {%s}}' % (len(v), ', '.join('0x%x' % m for m in v))
bits = [b for b in r['bits']]
o = ["/* (Triforce) The randomizer's beatability data. GENERATED from the OoT Randomizer's logic (GPL-3.0, github.com/OoTRandomizer/OoT-Randomizer),",
     "   run over the 15 key-item identities below (see rando.c). Do not edit by hand. A requirement is a list of masks: any one whose items are all",
     "   held is enough (an empty list: never). A class is a group of chests (a dungeon, the overworld) or one key-item chest. A group's list is",
     "   the way to its EASIEST chest (the remake's chests are not matched one by one with the Randomizer's locations), a key-item chest's its own. */",
     "#ifndef HOST_RANDO_LOGIC_H", "#define HOST_RANDO_LOGIC_H", "#include <stdint.h>", "",
     "typedef struct { int n; uint16_t m[%d]; } RlReq;" % mx, "",
     "static const char *const kRlItems[15] = {%s};" % ", ".join('"%s"' % BIT[b] for b in bits),
     "static const char *const kRlDirs[12] = {%s};" % ", ".join('"%s"' % d for d in DIRS),
     "static const uint64_t kRlHash[15] = {%s};" % ", ".join("%sull" % IDHASH[BIT[b]][0] for b in bits),
     "/* classes: 0..11 the dungeons (kRlDirs), 12 everything else, 13..27 the key-item chests (kRlItems order) */",
     "static const RlReq kRlHave[28] = {  /* with every key held but the dungeon's own... no: all keys (a chest that holds something else) */"]
o[-1] = "static const RlReq kRlAny[28] = {  /* a chest holding anything but a key: every key held */"
rows = [row(K[d]) for d in DIRS] + [row(K['Overworld'])] + [row(IK[b]) for b in bits]
o += ["    " + x + "," for x in rows] + ["};",
      "static const RlReq kRlKey[28] = {  /* a chest holding a small or boss key: every key held but its own dungeon's */"]
rows = [row(Nn[d]) for d in DIRS] + [row(Nn['Overworld'])] + [row(IN[b]) for b in bits]
o += ["    " + x + "," for x in rows] + ["};",
      "static const RlReq kRlGoal = %s;" % row(goal), "", "#endif", ""]
open('/home/claude/soh/gen/rando_logic.h', 'w').write("\n".join(o))
