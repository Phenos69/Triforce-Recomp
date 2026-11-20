/* (Triforce) The item randomizer's generator: plain C, no game memory and no Windows beyond the file calls rando.c makes
   itself, so tests/test_rando.py builds it with a host compiler and runs it on a made-up RomFS.

   The game's chests are data: every Banc/<scene>/<scene>_*.bcett.byml.zs holds the scene's actors, and a chest actor names
   what it gives in its Dynamic parameters, Drop__DropActor (a string such as "Item_Range_SlingShot"). The generator reads
   those files from the player's own RomFS, decides which chest gives what, and writes changed copies under a mod folder's
   romfs/ (the layout host/mods.c loads), so the game's own files are never touched and nothing made from them leaves the PC.

   A changed file keeps its layout: the chest's string slot points at another entry of the string table, and an item name
   the table lacks is added by moving the (rebuilt) string table to the end of the file. The copy is written as a zstd frame
   of raw blocks with its content size (libzstd here only decodes, and the game reads such a frame as any other). */
#ifndef HOST_RANDO_H
#define HOST_RANDO_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    int chests;         /* Shuffle chests: 0 Off (nothing changes), 1 Within each scene, 2 Everywhere */
    int dungeon_items;  /* Dungeon items (maps, compasses, small keys, boss keys): 0 Vanilla, 1 Own dungeon, 2 Anywhere
                           (Anywhere is Own dungeon while chests is 1: a scene's chests keep their count) */
    uint32_t seed;      /* 0: a new one each time (rando_generate puts the one it used here) */
} RandoSettings;

/* Reads <romfs>/Pack/ZsDic.pack.zs and <romfs>/Banc/<scene>/ (the .bcett.byml.zs files), writes the scenes it changed under
   <out_romfs>/Banc/<scene>/ and a text list of what went where to spoiler_path (NULL: none). Returns the number of chests
   that give something else now, or a negative number with a reason in err. */
int rando_generate(const char *romfs, const char *out_romfs, const char *spoiler_path, RandoSettings *s,
                   char *err, size_t errn);

/* Removes the files rando_generate wrote under <out_romfs> (a run with the randomizer off) */
void rando_clear(const char *out_romfs);

#endif
