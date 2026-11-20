Makes host/rando_logic.h (the randomizer's beatability data) from the OoT Randomizer's logic.

The data is derived from the OoT Randomizer (GPL-3.0, github.com/OoTRandomizer/OoT-Randomizer): check that this repository's
license allows it before publishing the header. The scripts here need a clone of that project and its `vanilla_world` helper
(oracle.py imports it as `w2`), so they are kept as a record of how the header was made, not run by the build:

1. oracle.py   - for every set of the 15 key items, which Randomizer locations are reachable (no tricks, vanilla placement,
                 the remake's one extra Water and Spirit key counted as held) and whether the game is beatable
2. oracle3.py  - the same per dungeon with every key held but that dungeon's own (for a key found in a chest)
3. export2.py  - the requirement lists of each chest class and the goal, written as C

The remake's chests share no id with the Randomizer's locations, so a chest that is not one of the 15 key-item chests takes the
requirement of the easiest chest of its dungeon (or of the overworld): see the comment above `beatable` in rando.c.
