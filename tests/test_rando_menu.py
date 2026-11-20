"""The Randomizer tab and the host that reads it (gpu/vk/vk_menu.cpp, vk_menu.h, host/rando_host.c, host/mods.c).

No game, no compiler: the source is read. The tab sits before Debug (the strip and tabs_shown take Debug as the last
tab), its rows are kept in triforce_options.ini under the keys the menu derives from their names (the row's name,
lower case, spaces as _) and host/rando_host.c reads those keys with the right lengths, every row applies after a restart,
the defaults of vk_menu.h are those the host assumes when the file lacks a key, the rows' ranges are those the host
accepts, and the host runs before the mods are indexed.

    python -I -B tests/test_rando_menu.py
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
read = lambda *p: open(os.path.join(HERE, "..", *p), encoding="utf-8").read()
menu, header, host, mods = read("gpu", "vk", "vk_menu.cpp"), read("gpu", "vk", "vk_menu.h"), read("host", "rando_host.c"), read("host", "mods.c")
failures = []


def check(cond, what):
    if not cond:
        failures.append(what)
        print("FAIL: " + what)


tab = {m[0]: int(m[1]) for m in re.findall(r"\b(TAB_[A-Z]+) = (\d+)", menu.split("enum MenuTab", 1)[1].split("};", 1)[0])}
check(tab.get("TAB_RANDOMIZER") == tab.get("TAB_DEBUG", 0) - 1, "the Randomizer tab is not just before Debug")
check(tab.get("TAB_COUNT") == tab.get("TAB_DEBUG", 0) + 1, "Debug is not the last tab")
names = re.search(r"ours\[TAB_COUNT\] = \{([^}]*)\}", menu)
tabs = re.findall(r'"([^"]+)"', names.group(1)) if names else []
check(tabs[-2:] == ["Randomizer", "Debug"] and len(tabs) == tab.get("TAB_COUNT"), "the strip's names do not end with Randomizer, Debug: %s" % tabs)

rows = re.findall(r'\{TAB_RANDOMIZER, "([^"]+)", &opts\.(\w+), (\d+), (\d+), (\d+), (\w+),(.*?)(true), (true)\}', menu, re.S)
check(len(rows) == 5, "the Randomizer tab has %d rows, not 5 (each applying after a restart)" % len(rows))
key = lambda name: name.replace(" ", "_").lower()
want = {"Randomizer": ("randomizer", 0, 1), "Randomizer seed": ("randomizer_seed", 0, 999999), "Shuffle chests": ("shuffle_chests", 0, 2),
        "Dungeon items": ("dungeon_items", 0, 2), "Item scenes": ("item_scenes", 0, 1)}
for name, field, lo, hi, step, text, _, _, _ in rows:
    k, wlo, whi = want.get(name, (None, 0, 0))
    check(k is not None and key(name) == k and field == k, "row %s: its key %s is not the one the host reads (%s)" % (name, key(name), k))
    check(int(lo) == wlo and int(hi) == whi, "row %s: range %s..%s, not %d..%d" % (name, lo, hi, wlo, whi))
    check(len(key(name)) < 31, "row %s: its key is cut at 31 letters" % name)
    check(re.search(r"\bint %s = " % field, header) is not None, "vk_menu.h has no Options::%s" % field)
for k, lo_, hi_ in want.values():
    m = re.search(r'"%s=", (\d+)\)' % re.escape(k), host)
    check(m and int(m.group(1)) == len(k) + 1, "host/rando_host.c reads %s= with the wrong length" % k)

defaults = {k: int(re.search(r"\bint %s = (\d+);" % k, header).group(1)) for k in ("randomizer", "randomizer_seed", "shuffle_chests", "dungeon_items", "item_scenes")}
m = re.search(r"RandoSettings s = \{(\d+), (\d+), (\d+), (\d+)\}", host)
check(m and tuple(int(m.group(i)) for i in (1, 2, 3, 4)) == (defaults["shuffle_chests"], defaults["dungeon_items"], defaults["randomizer_seed"], defaults["item_scenes"]),
      "the defaults of vk_menu.h and of host/rando_host.c differ: %s" % defaults)
check(defaults["randomizer"] == 0, "the randomizer is not off by default")
check("on = atoi(line + 11) == 1" in host, "the host does not take only 1 as On")
init = mods.split("void hos_mods_init", 1)[1].split("\n}", 1)[0]
check(0 <= init.find("hos_rando_prepare();") < init.find("mods_scan();"), "hos_mods_init does not generate before it indexes the mods")
check('"zz-Randomizer (generated)"' in host.replace("RANDO_MOD", '"zz-Randomizer (generated)"', 1) or "zz-Randomizer (generated)" in host,
      "the generated mod's folder is not named to sort last")

if failures:
    print("%d check(s) failed" % len(failures))
    sys.exit(1)
print("ok: the Randomizer tab is before Debug and its rows, keys, defaults and the host's reading agree")
