"""The item randomizer's generator (host/rando.c, host/rando.h): what it writes from a made-up RomFS.

No game: a RomFS is made here (Pack/ZsDic.pack.zs with a dictionary, and Banc scenes in the game's BYML v7 layout, chests
that share one Dynamic dictionary as the game's files do, a pot among them, chests that must stay put) and host/rando.c is
compiled with zstd's decoder (third_party/zstd) and run on it through a small driver. The checks, for every setting and a
few seeds:

  - Shuffle chests Off writes nothing; a scene it leaves alone is not written.
  - Only the chests' Drop__DropActor changes. The pot that shares a chest's Dynamic dictionary, the minigame chest, the
    chest with a Drop__CheckActor and every other byte of the scene's content stay as they were (the changed chests get
    dictionaries of their own).
  - The items are conserved: the chests hold the same items after as before.
  - Dungeon items: Vanilla leaves them where they are; Own dungeon keeps each dungeon's in its own chests; Within each
    scene Anywhere is Own dungeon, and Everywhere with Anywhere moves them across dungeons.
  - Within each scene: every scene's items stay in its own chests (a dungeon's two files are one scene).
  - An item name a scene's string table lacks is added (the table is rebuilt at the end of the file), and the scene still
    reads back.
  - The same seed gives the same bytes; a seed of 0 is a new one and is returned. Every file is a zstd frame of raw blocks
    with its content size, which zstandard decodes.
  - rando_clear removes what was written.

The BYML here is written and read by the small functions below, not a package: the layout (header, key table, string
table, dictionaries of 8-byte entries, arrays with their types first, 8-byte values by offset) is the game's v7, which
the real files were checked against with the byml package.

    python -I -B tests/test_rando.py [--cc gcc]
"""
import collections
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile

import zstandard

RUN_ALL_ARGS = ["--cc", "gcc"]
RUN_ALL_MODULES = ["zstandard"]

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
ZSTD = os.path.join(ROOT, "third_party", "zstd", "lib")
failures = []


def check(cond, what):
    if not cond:
        failures.append(what)
        print("FAIL: " + what)


# ---- BYML v7 -----------------------------------------------------------------------------------------------------------

STR, ARR, DICT, U64 = 0xA0, 0xC0, 0xC1, 0xD5


def table(names):
    body = b"".join(n.encode() + b"\0" for n in names)
    first = 4 + (len(names) + 1) * 4
    offs, at = [], first
    for n in names:
        offs.append(at)
        at += len(n) + 1
    offs.append(at)
    return bytes([0xC2]) + len(names).to_bytes(3, "little") + b"".join(struct.pack("<I", o) for o in offs) + body


def byml_write(root):
    keys, strs = set(), set()

    def collect(n):
        if isinstance(n, dict):
            for k, v in n.items():
                keys.add(k)
                collect(v)
        elif isinstance(n, list):
            for v in n:
                collect(v)
        elif isinstance(n, str):
            strs.add(n)
    collect(root)
    kt, st = sorted(keys), sorted(strs)
    out = bytearray(16)
    kt_at = len(out)
    out += table(kt)
    out += b"\0" * (-len(out) % 4)
    st_at = len(out)
    out += table(st)
    out += b"\0" * (-len(out) % 4)
    done = {}

    def emit(n):   # (the type and the value word of a node; containers equal to one written before are shared, as the game's are)
        if isinstance(n, str):
            return STR, st.index(n)
        if isinstance(n, int):
            at = len(out)
            out.extend(struct.pack("<Q", n))
            return U64, at
        key = repr(n)
        if key in done:
            return done[key]
        if isinstance(n, dict):
            ents = sorted((kt.index(k), emit(v)) for k, v in n.items())
            at = len(out)
            out.extend(bytes([DICT]) + len(ents).to_bytes(3, "little"))
            for ki, (t, v) in ents:
                out.extend(ki.to_bytes(3, "little") + bytes([t]) + struct.pack("<I", v))
            res = (DICT, at)
        else:
            kids = [emit(v) for v in n]
            at = len(out)
            out.extend(bytes([ARR]) + len(kids).to_bytes(3, "little") + bytes(t for t, _ in kids))
            out.extend(b"\0" * (-len(out) % 4))
            for _, v in kids:
                out.extend(struct.pack("<I", v))
            res = (ARR, at)
        done[key] = res
        return res
    root_at = emit(root)[1]
    out[0:16] = b"YB" + struct.pack("<HIII", 7, kt_at, st_at, root_at)
    return bytes(out)


def byml_read_actors(b):
    """[(Gyaml, Hash, Drop__DropActor, GetEventName)] of the Actors array"""
    u16 = lambda o: struct.unpack_from("<H", b, o)[0]
    u32 = lambda o: struct.unpack_from("<I", b, o)[0]
    u24 = lambda o: u16(o) | b[o + 2] << 16
    assert b[:2] == b"YB" and u16(2) == 7
    kt, st, root = u32(4), u32(8), u32(12)
    tstr = lambda t, i: b[t + u32(t + 4 + 4 * i):].split(b"\0", 1)[0].decode()
    keys = {tstr(kt, i): i for i in range(u24(kt + 1))}

    def entries(d):
        assert b[d] == DICT
        res = {}
        for i in range(u24(d + 1)):
            e = d + 4 + 8 * i
            res[u24(e)] = (b[e + 3], u32(e + 4))
        return res

    def value(t, v):
        if t == STR:
            return tstr(st, v)
        if t == U64:
            return struct.unpack_from("<Q", b, v)[0]
        return None
    top = entries(root)
    t, arr = top[keys["Actors"]]
    assert t == ARR
    n = u24(arr + 1)
    vals = arr + 4 + n + (-(arr + 4 + n) % 4)
    actors = []
    for i in range(n):
        d = entries(u32(vals + 4 * i))
        dyn = entries(d[keys["Dynamic"]][1])
        get = lambda dd, k: value(*dd[keys[k]]) if k in keys and keys[k] in dd else None
        actors.append((get(d, "Gyaml"), get(d, "Hash"), get(dyn, "Drop__DropActor"), get(dyn, "GetEventName")))
    return actors


# ---- the made-up game --------------------------------------------------------------------------------------------------

DICT_BYTES = b"made-up dictionary of the bcett files " * 40


def actor(gyaml, h, drop=None, event=None, check=None):
    dyn = {"DefaultDungeonRoomIndex": 3 if drop else 0}
    if drop:
        dyn["Drop__DropActor"] = drop
    if event:
        dyn["GetEventName"] = event
    if check:
        dyn["Drop__CheckActor"] = check
    return {"Gyaml": gyaml, "Hash": h, "Dynamic": dyn}


def sarc(files):
    names = b"".join(n.encode() + b"\0" + b"\0" * (-(len(n) + 1) % 4) for n, _ in files)
    count = len(files)
    sfat_len = 12 + 16 * count
    data0 = (0x14 + sfat_len + 8 + len(names) + 0xff) & ~0xff
    nodes, name_at, data_at, blob = b"", 0, 0, b""
    for n, d in files:
        nodes += struct.pack("<IIII", 0, 0x01000000 | name_at // 4, data_at, data_at + len(d))
        name_at += len(n) + 1 + (-(len(n) + 1) % 4)
        blob += d + b"\0" * (-len(d) % 4)
        data_at += len(d) + (-len(d) % 4)
    head = b"SARC" + struct.pack("<HHIIHH", 0x14, 0xFEFF, data0 + len(blob), data0, 0x100, 0)
    body = head + b"SFAT" + struct.pack("<HHI", 12, count, 0x65) + nodes + b"SFNT" + struct.pack("<HH", 8, 0) + names
    return body + b"\0" * (data0 - len(body)) + blob


SCENES = {
    "InsideTheDekuTree/InsideTheDekuTree_Static": [
        actor("Obj_TBox_A_02", 11, "Item_Dungeon_Map", "InsideTheDekuTree_GetMap"),
        actor("Obj_TBox_A_01", 12, "Item_Dungeon_SmallKey"),
        actor("Obj_TBox_A_01", 13, "TBox_Rupee_Red"),
        actor("Obj_TBox_A_01", 14, "TBox_Rupee_Red"),      # (the same Dynamic dictionary as 13 and the pot)
        actor("Obj_Pot_A_01", 15, "TBox_Rupee_Red"),       # (a pot: not a chest, shares the dictionary)
        actor("Obj_TBox_A_02", 16, "Item_Range_SlingShot", "InsideTheDekuTree_GetSlingShot"),
        actor("Obj_TBox_A_01", 17, "TBox_Rupee_Blue", "MiniGame_ChoiceTreasure_OpenClearTreasureBox"),   # (stays)
        actor("Obj_TBox_A_01", 18, "TBox_Rupee_Purple", check="Obj_Heart_Piece"),   # (stays)
    ],
    "InsideTheDekuTree/InsideTheDekuTree_Dynamic": [
        actor("Obj_TBox_A_02", 21, "Item_Dungeon_Compass", "InsideTheDekuTree_GetCompass"),
        actor("Obj_TBox_A_01", 22, "TBox_Rupee_Purple"),
        actor("Obj_TBox_A_01", 23, "Item_Dungeon_SmallKey"),
    ],
    "DodongoCavern/DodongoCavern_Static": [
        actor("Obj_TBox_A_02", 31, "Item_Dungeon_Map"),
        actor("Obj_TBox_A_01", 32, "TBox_Rupee_Green"),
        actor("Obj_TBox_A_01", 33, "TBox_Rupee_Red"),
        actor("Obj_TBox_A_01", 34, "Item_Dungeon_BossKey"),
    ],
    "InsideJabuJabuBelly/InsideJabuJabuBelly_Static": [   # (one dungeon item and one other chest: a pool of one place, which a rule that skips one-place pools breaks)
        actor("Obj_TBox_A_02", 51, "Item_Dungeon_Map"),
        actor("Obj_TBox_A_01", 52, "TBox_Rupee_Red"),
    ],
    "KokiriForest/KokiriForest_Child_Dynamic": [
        actor("Obj_TBox_A_01", 41, "TBox_Rupee_Blue"),
        actor("Obj_TBox_A_01", 42, "TBox_Rupee_Green"),
        actor("Obj_TBox_A_01", 43, "Obj_Heart_Piece"),
        actor("Obj_TBox_A_01", 44, "TBox_Rupee_Purple"),
        actor("Obj_TBox_A_01", 45, "Item_Sword_001", "Get_StoneOfForest_GetSword"),   # (an item's own event: stays, or a rupee would play the sword's scene)
        actor("Obj_TBox_A_01", 46, "TBox_Rupee_Red", "KokiriForest_TreasureBox_NaviChatOff"),   # (moves, keeps its event)
    ],
}
DUNGEONS = {"InsideTheDekuTree", "DodongoCavern", "InsideJabuJabuBelly"}
STAYS = {17, 18, 45}


def make_romfs(root):
    zd = zstandard.ZstdCompressionDict(DICT_BYTES, dict_type=zstandard.DICT_TYPE_RAWCONTENT)
    os.makedirs(os.path.join(root, "Pack"))
    zs = zstandard.ZstdCompressor(level=3).compress(sarc([("bcett.byml.zsdic", DICT_BYTES), ("pack.zsdic", b"x" * 64)]))
    open(os.path.join(root, "Pack", "ZsDic.pack.zs"), "wb").write(zs)
    for rel, actors in SCENES.items():
        p = os.path.join(root, "Banc", rel + ".bcett.byml.zs")
        os.makedirs(os.path.dirname(p), exist_ok=True)
        open(p, "wb").write(zstandard.ZstdCompressor(level=3, dict_data=zd).compress(byml_write({"Actors": actors})))
    return zd


DRIVER = r"""
#include "rando.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv) {
    if (argc == 3 && !strcmp(argv[1], "clear")) { rando_clear(argv[2]); return 0; }
    RandoSettings s = {atoi(argv[4]), atoi(argv[5]), (uint32_t)atoi(argv[6])};
    char err[256] = "";
    int n = rando_generate(argv[1], argv[2], argv[3], &s, err, sizeof err);
    printf("%d %u %s\n", n, s.seed, err);
    return n < 0;
}
"""


def main():
    cc = "gcc"
    if "--cc" in sys.argv:
        cc = sys.argv[sys.argv.index("--cc") + 1]
    tmp = tempfile.mkdtemp(prefix="rando-test-")
    try:
        srcs = [os.path.join(tmp, "drv.c"), os.path.join(ROOT, "host", "rando.c")]
        open(srcs[0], "w").write(DRIVER)
        zsrc = [os.path.join(ZSTD, "common", f) for f in sorted(os.listdir(os.path.join(ZSTD, "common"))) if f.endswith(".c")]
        zsrc += [os.path.join(ZSTD, "decompress", f) for f in ("zstd_decompress.c", "zstd_decompress_block.c", "zstd_ddict.c", "huf_decompress.c")]
        exe = os.path.join(tmp, "drv")
        r = subprocess.run([cc, "-O1", "-Wall", "-DZSTD_DISABLE_ASM", "-I", os.path.join(ROOT, "host"), "-o", exe] + srcs + zsrc,
                           capture_output=True, text=True)
        if r.returncode:
            print(r.stderr)
            print("FAIL: host/rando.c does not compile")
            return 1
        romfs = os.path.join(tmp, "romfs")
        zd = make_romfs(romfs)
        dec = zstandard.ZstdDecompressor(dict_data=zd)
        before = {rel: byml_read_actors(byml_write({"Actors": a})) for rel, a in SCENES.items()}

        def run(chests, di, seed, out):
            shutil.rmtree(out, ignore_errors=True)
            r = subprocess.run([exe, romfs, out, os.path.join(out + ".txt"), str(chests), str(di), str(seed)], capture_output=True, text=True)
            check(r.returncode == 0, "rando_generate fails for chests %d, dungeon items %d, seed %d: %s" % (chests, di, seed, r.stdout))
            return r.stdout.split()

        def read_out(out):
            res = {}
            for rel in SCENES:
                p = os.path.join(out, "Banc", rel + ".bcett.byml.zs")
                if os.path.exists(p):
                    raw = open(p, "rb").read()
                    check(raw[:5] == b"\x28\xb5\x2f\xfd\xa0", "%s is not a frame of raw blocks with its content size" % rel)
                    res[rel] = (raw, byml_read_actors(dec.decompress(raw, max_output_size=1 << 24)))
            return res

        # Off writes nothing
        out = os.path.join(tmp, "off")
        run(0, 1, 5, out)
        check(not os.path.exists(os.path.join(out, "Banc")) or not read_out(out), "Shuffle chests Off wrote scenes")

        moved_ever = set()
        for chests in (1, 2):
            for di in (0, 1, 2):
                for seed in (1, 2, 3, 4, 5, 6):
                    out = os.path.join(tmp, "o%d%d%d" % (chests, di, seed))
                    run(chests, di, seed, out)
                    got = read_out(out)
                    tag = "chests %d, dungeon items %d, seed %d" % (chests, di, seed)
                    items_b, items_a = collections.Counter(), collections.Counter()
                    now = {}   # hash -> (scene, item)
                    for rel, acts in before.items():
                        new = got[rel][1] if rel in got else acts
                        check(len(new) == len(acts), "%s: %s has another number of actors" % (tag, rel))
                        for a, b in zip(acts, new):
                            check(a[:2] == b[:2], "%s: %s: an actor's name or hash changed" % (tag, rel))
                            gone = a[2] != b[2] and (a[3] or "").endswith(("_GetMap", "_GetCompass", "_GetBossKey", "_GetSmallKey", "_GetSlingShot"))
                            check(b[3] == (None if gone else a[3]), "%s: %s: actor %s: event %s became %s (an item's get-item event goes with the item, the others stay)" % (tag, rel, a[1], a[3], b[3]))
                            if a[2] != b[2]:
                                check(a[0].startswith("Obj_TBox") and a[1] not in STAYS, "%s: %s: actor %s changed and is no chest to shuffle" % (tag, rel, a[1]))
                                moved_ever.add(a[1])
                            if a[0].startswith("Obj_TBox") and a[1] not in STAYS:
                                items_b[a[2]] += 1
                                items_a[b[2]] += 1
                                now[a[1]] = (rel, b[2])
                    check(items_b == items_a, "%s: the chests hold other items than before" % tag)
                    scope = lambda rel: re.sub(r"_(Static|Dynamic)$", "", rel.split("/")[1])
                    for rel, acts in before.items():
                        for a in acts:
                            if not a[0].startswith("Obj_TBox") or a[1] in STAYS or a[2] is None:
                                continue
                            was_di = a[2].startswith("Item_Dungeon_")
                            if was_di and di == 0:
                                check(now[a[1]][1] == a[2], "%s: dungeon item %d moved with Vanilla" % (tag, a[1]))
                    for h, (rel, item) in now.items():
                        if item.startswith("Item_Dungeon_") and (di == 1 or (chests == 1 and di == 2)):
                            check(rel.split("/")[0] in DUNGEONS, "%s: a dungeon item (%s) left the dungeons" % (tag, item))
                    if di == 1 or (chests == 1 and di == 2):   # (Own dungeon: each dungeon keeps its own dungeon items)
                        for d in DUNGEONS:
                            was = collections.Counter(a[2] for rel, acts in before.items() if rel.startswith(d + "/") for a in acts
                                                      if a[0].startswith("Obj_TBox") and a[1] not in STAYS and (a[2] or "").startswith("Item_Dungeon_"))
                            have = collections.Counter(it for h, (rel, it) in now.items() if rel.startswith(d + "/") and it.startswith("Item_Dungeon_"))
                            check(was == have, "%s: %s's dungeon items are not its own after (%s, then %s)" % (tag, d, dict(was), dict(have)))
                    if chests == 1:   # (a scene's items stay in its chests)
                        for sc in {scope(r) for r in before}:
                            was = collections.Counter(a[2] for rel, acts in before.items() if scope(rel) == sc for a in acts
                                                      if a[0].startswith("Obj_TBox") and a[1] not in STAYS and not (di == 0 and (a[2] or "").startswith("Item_Dungeon_")) or False)
                            have = collections.Counter(it for h, (rel, it) in now.items() if scope(rel) == sc and not (di == 0 and it.startswith("Item_Dungeon_")))
                            check(was == have, "%s: scene %s's items changed (%s, then %s)" % (tag, sc, dict(was), dict(have)))
                    # a changed scene's other content is the same: the pot keeps the item it had
                    if "InsideTheDekuTree/InsideTheDekuTree_Static" in got:
                        pot = [a for a in got["InsideTheDekuTree/InsideTheDekuTree_Static"][1] if a[1] == 15][0]
                        check(pot[2] == "TBox_Rupee_Red", "%s: the pot that shares a chest's dictionary changed" % tag)
        check(moved_ever >= {13, 14, 41, 42}, "no seed moved the chests that share a dictionary / a scene without dungeon items")

        # the same seed twice gives the same bytes; 0 is a new seed, returned
        a, b = os.path.join(tmp, "same_a"), os.path.join(tmp, "same_b")
        run(2, 1, 7, a)
        run(2, 1, 7, b)
        same = all(open(os.path.join(a, "Banc", rel + ".bcett.byml.zs"), "rb").read() == open(os.path.join(b, "Banc", rel + ".bcett.byml.zs"), "rb").read()
                   for rel in SCENES if os.path.exists(os.path.join(a, "Banc", rel + ".bcett.byml.zs")))
        check(same and open(a + ".txt").read() == open(b + ".txt").read(), "the same seed gave other files")
        res = run(2, 1, 0, os.path.join(tmp, "zero"))
        check(len(res) >= 2 and int(res[1]) > 0, "a seed of 0 did not give a seed back")
        # a made-up string the table lacks is added: the Kokiri scene has no dungeon items, so one of them lands there
        lacking = any(any(x[2].startswith("Item_Dungeon_") for x in read_out(os.path.join(tmp, "o2%d%d" % (2, s))).get("KokiriForest/KokiriForest_Child_Dynamic", (b"", []))[1])
                      for s in (1, 2, 3, 4, 5, 6))
        check(lacking, "no seed with Anywhere put a dungeon item (a name its string table lacks) in the Kokiri scene")
        # clear
        subprocess.run([exe, "clear", a], check=True)
        check(not any(f.endswith(".zs") for _, _, fs in os.walk(a) for f in fs), "rando_clear left files")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    if failures:
        print("%d check(s) failed" % len(failures))
        return 1
    print("ok: the generator keeps the chests' items, the dungeon items' rules and every other byte, for every setting")
    return 0


if __name__ == "__main__":
    sys.exit(main())
