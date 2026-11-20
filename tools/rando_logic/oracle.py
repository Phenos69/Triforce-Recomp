"""Beatability oracle from OoT Randomizer's engine: for every subset (mask) of 15 key-item identities held, which OoTR locations
are reachable (keys all held: K; no keys: N) and is the game beatable. Run: python -I oracle.py out.json"""
import sys, json, itertools, time
sys.path.insert(0, '/home/claude/soh')
import w2
from Item import ItemFactory
from Search import Search

BITS = [('Slingshot', 'Slingshot'), ('Boomerang', 'Boomerang'), ('BombBag', 'Bomb Bag'), ('Bow', 'Bow'),
        ('Hammer', 'Megaton Hammer'), ('Hookshot', 'Progressive Hookshot'), ('Longshot', 'Progressive Hookshot'),
        ('Silver', 'Progressive Strength Upgrade'), ('Gold', 'Progressive Strength Upgrade'), ('Iron', 'Iron Boots'),
        ('Hover', 'Hover Boots'), ('Mirror', 'Mirror Shield'), ('Lens', 'Lens of Truth'), ('Ice', 'Ice Arrows'),
        ('KSword', 'Kokiri Sword')]
DUNG = {'Bottom of the Well': 8, 'Dodongos Cavern': 1, 'Fire Temple': 4, 'Forest Temple': 3, 'Gerudo Training Ground': 11,
        'Ice Cavern': 9, 'Ganons Castle': 13, 'Jabu Jabus Belly': 2, 'Deku Tree': 0, 'Shadow Temple': 7, 'Spirit Temple': 6, 'Water Temple': 5}
KEYS = [('Small Key (Forest Temple)', 5), ('Small Key (Fire Temple)', 8), ('Small Key (Water Temple)', 6), ('Small Key (Shadow Temple)', 5),
        ('Small Key (Spirit Temple)', 5), ('Small Key (Bottom of the Well)', 3), ('Small Key (Gerudo Training Ground)', 9),
        ('Small Key (Ganons Castle)', 2), ('Boss Key (Forest Temple)', 1), ('Boss Key (Fire Temple)', 1), ('Boss Key (Water Temple)', 1),
        ('Boss Key (Shadow Temple)', 1), ('Boss Key (Spirit Temple)', 1), ('Boss Key (Ganons Castle)', 1)]
IDENT_LOCS = {'Slingshot': 'Deku Tree Slingshot Chest', 'Boomerang': 'Jabu Jabus Belly Boomerang Chest', 'BombBag': 'Dodongos Cavern Bomb Bag Chest',
              'Bow': 'Forest Temple Bow Chest', 'Hammer': 'Fire Temple Megaton Hammer Chest', 'Hookshot': 'Graveyard Dampe Race Hookshot Chest',
              'Longshot': 'Water Temple Longshot Chest', 'Silver': 'Spirit Temple Silver Gauntlets Chest',
              'Gold': 'Ganons Castle Shadow Trial Golden Gauntlets Chest', 'Iron': 'Ice Cavern Iron Boots Chest',
              'Hover': 'Shadow Temple Hover Boots Chest', 'Mirror': 'Spirit Temple Mirror Shield Chest', 'Lens': 'Bottom of the Well Lens of Truth Chest',
              'Ice': 'Gerudo Training Ground Maze Path Final Chest', 'KSword': 'KF Kokiri Sword Chest'}


def build():
    w = w2.vanilla_world()
    locs = {l.name: l for l in w.get_locations()}
    shuffled = set(IDENT_LOCS.values())
    sceneids = set(DUNG.values())
    for l in locs.values():
        if l.type == 'Chest' and l.scene in sceneids:
            shuffled.add(l.name)
    for n in shuffled:                                   # the pool's chests hold junk: the held items come from the mask
        l = locs[n]
        l.item = None
        w.push_item(l, ItemFactory('Rupees (5)', w))
    return w, locs, shuffled


def explore(w, names):
    sr = Search([w.state])
    for n in names:
        sr.collect(ItemFactory(n, w))
    while True:
        n = len(sr._cache.visited_locations)
        sr.collect_locations()
        if len(sr._cache.visited_locations) == n:
            break
    return sr


def main(out):
    w, locs, shuffled = build()
    ow = sorted(l.name for l in locs.values() if l.type == 'Chest' and l.scene not in set(DUNG.values()) and l.name not in shuffled)
    watch = sorted(shuffled) + ow
    keyitems = [k for k, c in KEYS for _ in range(c)]
    res = {'ow': ow, 'nshuf': len(shuffled), 'bits': [b for b, _ in BITS], 'locs': watch, 'K': [], 'N': [], 'goal': []}
    t = time.time()
    for mask in range(1 << len(BITS)):
        have = [BITS[i][1] for i in range(len(BITS)) if mask >> i & 1]
        row = {}
        for tag, extra in (('K', keyitems), ('N', [])):
            sr = explore(w, have + extra)
            reach = {l.name for l in sr.iter_reachable_locations([locs[n] for n in watch])}
            row[tag] = sum(1 << i for i, n in enumerate(watch) if n in reach)
            if tag == 'K':
                res['goal'].append(1 if sr.can_beat_game(False) else 0)
        res['K'].append(row['K']); res['N'].append(row['N'])
        if mask % 2048 == 0:
            print(mask, round(time.time() - t), flush=True)
    json.dump(res, open(out, 'w'))


if __name__ == '__main__':
    main(sys.argv[1])
