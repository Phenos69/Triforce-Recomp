"""Per dungeon D: reachability of D's watched locations with every key held except D's own keys (for the key-in-chest rule)."""
import sys, json, time
sys.path.insert(0, '/home/claude/soh/gen'); sys.path.insert(0, '/home/claude/soh')
import oracle
from oracle import BITS, KEYS
dirs = {8: 'BottomOfTheWell', 1: 'DodongoCavern', 4: 'FireTemple', 3: 'ForestTemple', 11: 'GerudoTrainingGround', 9: 'IceCavern',
        13: 'InsideGanonCastle', 2: 'InsideJabuJabuBelly', 0: 'InsideTheDekuTree', 7: 'ShadowTemple', 6: 'SpiritTemple', 5: 'WaterTemple'}
KEYDUNG = {'Bottom of the Well': 8, 'Forest Temple': 3, 'Fire Temple': 4, 'Water Temple': 5, 'Shadow Temple': 7, 'Spirit Temple': 6,
           'Gerudo Training Ground': 11, 'Ganons Castle': 13}
def main(sids, out):
    w, locs, shuf = oracle.build()
    res = {}
    for sid in sids:
        watch = sorted(l.name for l in locs.values() if l.type == 'Chest' and l.scene == sid and l.name in shuf)
        keys = [k for k, c in KEYS for _ in range(c) if KEYDUNG.get(k.split('(')[1][:-1]) != sid]
        tab = []
        for mask in range(1 << 15):
            have = [BITS[i][1] for i in range(15) if mask >> i & 1]
            sr = oracle.explore(w, have + keys)
            reach = {l.name for l in sr.iter_reachable_locations([locs[n] for n in watch])}
            tab.append(sum(1 << i for i, n in enumerate(watch) if n in reach))
        res[dirs[sid]] = {'locs': watch, 'tab': tab}
        print(sid, flush=True)
        json.dump(res, open(out, 'w'))
if __name__ == '__main__':
    main([int(x) for x in sys.argv[2].split(',')], sys.argv[1])
