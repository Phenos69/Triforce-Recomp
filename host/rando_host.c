/* (Triforce) The item randomizer at the game's start: the Randomizer tab's rows (gpu/vk/vk_menu.cpp) are kept in
   triforce_options.ini (randomizer, randomizer_seed, shuffle_chests, dungeon_items, item_scenes), read here as host/nn_audio.c reads its
   row. With the randomizer on, host/rando.c writes the changed scenes from the game's RomFS into a mod folder of the mods
   folder, "zz-Randomizer (generated)" (the last in the order, so its files win), which mods.c then indexes like any other
   mod; with it off the generated files are removed. The list of what went where is triforce_randomizer.txt beside the game. */
#include "hos.h"
#include "hos_paths.h"
#include "rando.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const char *hos_romfs_dir;   /* nn_fs.c */

#define RANDO_MOD "zz-Randomizer (generated)"

static const char *sep_of(const char *s) {
    const char *b = strrchr(s, '\\'), *f = strrchr(s, '/');
    return b > f ? b : f;
}

void hos_rando_prepare(void) {
    char dir[MAX_PATH], ini[MAX_PATH], mods[MAX_PATH], out[MAX_PATH], spoiler[MAX_PATH];
    if (!hos_game_path_a(dir, sizeof dir)) return;
    char *slash = (char *)sep_of(dir);
    if (!slash) return;
    slash[1] = 0;   /* the folder, with its separator */
    snprintf(ini, sizeof ini, "%striforce_options.ini", dir);
    if (getenv("HOS_MODS")) snprintf(mods, sizeof mods, "%s", getenv("HOS_MODS"));
    else snprintf(mods, sizeof mods, "%smods", dir);
    snprintf(out, sizeof out, "%s/" RANDO_MOD "/romfs", mods);
    snprintf(spoiler, sizeof spoiler, "%striforce_randomizer.txt", dir);

    int on = 0;
    RandoSettings s = {2, 1, 0, 1};   /* (the rows' defaults, vk_menu.h Options: Everywhere, Own dungeon, a new seed each start, scenes moved with their items) */
    FILE *f = fopen(ini, "r");
    if (f) {
        char line[96];
        while (fgets(line, sizeof line, f)) {
            if (!strncmp(line, "randomizer=", 11)) on = atoi(line + 11) == 1;
            else if (!strncmp(line, "randomizer_seed=", 16)) s.seed = (uint32_t)atoi(line + 16);
            else if (!strncmp(line, "shuffle_chests=", 15)) s.chests = atoi(line + 15);
            else if (!strncmp(line, "dungeon_items=", 14)) s.dungeon_items = atoi(line + 14);
            else if (!strncmp(line, "item_scenes=", 12)) s.scenes = atoi(line + 12) == 1;
        }
        fclose(f);
    }
    if (!on) {
        rando_clear(out);
        return;
    }
    if (!hos_romfs_dir) {
        hos_log("randomizer: no RomFS folder: not generated");
        return;
    }
    if (s.chests < 0) s.chests = 0;
    if (s.chests > 2) s.chests = 2;
    if (s.dungeon_items < 0) s.dungeon_items = 0;
    if (s.dungeon_items > 2) s.dungeon_items = 2;
    char err[256] = "";
    rando_clear(out);   /* (a scene this seed leaves alone must not keep an earlier seed's change) */
    const int n = rando_generate(hos_romfs_dir, out, spoiler, &s, err, sizeof err);
    if (n < 0) hos_log("randomizer: not generated: %s", err);
    else hos_log("randomizer: seed %u, shuffle chests %d, dungeon items %d, item scenes %d: %d chests changed (%s)", s.seed, s.chests,
                 s.dungeon_items, s.scenes, n, "triforce_randomizer.txt");
}
