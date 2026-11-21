/* (Triforce) The item randomizer's generator. See rando.h. */
#include "rando.h"
#include "rando_logic.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../third_party/zstd/lib/zstd.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#define MKDIR(p) mkdir(p, 0777)
#endif

/* ---- small things ------------------------------------------------------------------------------------------------ */

typedef struct { uint8_t *d; size_t n, cap; } Buf;

static void fail(char *err, size_t errn, const char *what, const char *a) {
    if (err && errn) snprintf(err, errn, "%s%s%s", what, a ? ": " : "", a ? a : "");
}

static int buf_reserve(Buf *b, size_t cap) {
    if (cap <= b->cap) return 1;
    uint8_t *p = (uint8_t *)realloc(b->d, cap);
    if (!p) return 0;
    memset(p + b->cap, 0, cap - b->cap);
    b->d = p;
    b->cap = cap;
    return 1;
}

static int read_file(const char *path, Buf *b) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0 || !buf_reserve(b, (size_t)n + 1)) { fclose(f); return 0; }
    b->n = fread(b->d, 1, (size_t)n, f);
    fclose(f);
    return b->n == (size_t)n;
}

static void mkdir_p(const char *path) {
    char t[1024];
    snprintf(t, sizeof t, "%s", path);
    for (char *p = t + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            const char c = *p;
            *p = 0;
            MKDIR(t);
            *p = c;
        }
    }
    MKDIR(t);
}

static int write_file(const char *path, const uint8_t *d, size_t n) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    const size_t w = fwrite(d, 1, n, f);
    return fclose(f) == 0 && w == n;
}

/* the names in a folder (not . and ..), sorted; the caller frees the list */
static int list_dir(const char *path, char ***out) {
    char **v = NULL;
    int n = 0, cap = 0;
#ifdef _WIN32
    char pat[1024];
    snprintf(pat, sizeof pat, "%s/*", path);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) { *out = NULL; return 0; }
    do {
        const char *name = fd.cFileName;
#else
    DIR *dir = opendir(path);
    if (!dir) { *out = NULL; return 0; }
    struct dirent *de;
    while ((de = readdir(dir))) {
        const char *name = de->d_name;
#endif
        if (strcmp(name, ".") && strcmp(name, "..")) {
            if (n == cap) {
                cap = cap ? cap * 2 : 32;
                v = (char **)realloc(v, (size_t)cap * sizeof *v);
            }
            v[n++] = strdup(name);
        }
#ifdef _WIN32
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    }
    closedir(dir);
#endif
    for (int i = 1; i < n; i++)   /* (insertion sort: a few hundred names) */
        for (int j = i; j > 0 && strcmp(v[j - 1], v[j]) > 0; j--) { char *t = v[j]; v[j] = v[j - 1]; v[j - 1] = t; }
    *out = v;
    return n;
}

static void free_list(char **v, int n) {
    for (int i = 0; i < n; i++) free(v[i]);
    free(v);
}

static int ends_with(const char *s, const char *suffix) {
    const size_t a = strlen(s), b = strlen(suffix);
    return a >= b && !strcmp(s + a - b, suffix);
}

static int starts_with(const char *s, const char *prefix) { return !strncmp(s, prefix, strlen(prefix)); }

/* ---- zstd --------------------------------------------------------------------------------------------------------- */

/* A frame's content, with the dictionary when there is one (dict NULL: none). 0 on failure */
static int unzstd(const uint8_t *src, size_t n, const void *dict, size_t dict_n, Buf *out) {
    const unsigned long long want = ZSTD_getFrameContentSize(src, n);
    if (want == ZSTD_CONTENTSIZE_ERROR || want > (1ull << 28)) return 0;
    const size_t cap = want == ZSTD_CONTENTSIZE_UNKNOWN ? (size_t)1 << 24 : (size_t)want;
    out->n = 0;
    if (!buf_reserve(out, cap + 8)) return 0;
    ZSTD_DCtx *dctx = ZSTD_createDCtx();
    if (!dctx) return 0;
    const size_t r = dict ? ZSTD_decompress_usingDict(dctx, out->d, cap, src, n, dict, dict_n)
                          : ZSTD_decompressDCtx(dctx, out->d, cap, src, n);
    ZSTD_freeDCtx(dctx);
    if (ZSTD_isError(r)) return 0;
    out->n = r;
    return 1;
}

/* A zstd frame holding d as raw blocks, with its content size (a decoder needs no dictionary for it) */
static uint8_t *zstd_raw_frame(const uint8_t *d, size_t n, size_t *out_n) {
    const size_t blocks = n ? (n + 131071) / 131072 : 1;
    uint8_t *o = (uint8_t *)malloc(4 + 1 + 4 + blocks * 3 + n);
    if (!o) return NULL;
    size_t k = 0;
    o[k++] = 0x28; o[k++] = 0xb5; o[k++] = 0x2f; o[k++] = 0xfd;
    o[k++] = 0xa0;   /* single segment, a 4-byte content size, no dictionary, no checksum */
    o[k++] = (uint8_t)n; o[k++] = (uint8_t)(n >> 8); o[k++] = (uint8_t)(n >> 16); o[k++] = (uint8_t)(n >> 24);
    size_t at = 0;
    for (size_t b = 0; b < blocks; b++) {
        const size_t len = n - at > 131072 ? 131072 : n - at;
        const uint32_t h = (uint32_t)(len << 3) | (b + 1 == blocks ? 1u : 0u);   /* a raw block: type 0 */
        o[k++] = (uint8_t)h; o[k++] = (uint8_t)(h >> 8); o[k++] = (uint8_t)(h >> 16);
        memcpy(o + k, d + at, len);
        k += len;
        at += len;
    }
    *out_n = k;
    return o;
}

/* ---- little-endian reading and writing ---------------------------------------------------------------------------- */

static uint32_t r16(const uint8_t *b, size_t o) { return (uint32_t)b[o] | (uint32_t)b[o + 1] << 8; }
static uint32_t r24(const uint8_t *b, size_t o) { return r16(b, o) | (uint32_t)b[o + 2] << 16; }
static uint32_t r32(const uint8_t *b, size_t o) { return r24(b, o) | (uint32_t)b[o + 3] << 24; }
static uint64_t r64(const uint8_t *b, size_t o) { return (uint64_t)r32(b, o) | (uint64_t)r32(b, o + 4) << 32; }
static void w32(uint8_t *b, size_t o, uint32_t v) {
    b[o] = (uint8_t)v; b[o + 1] = (uint8_t)(v >> 8); b[o + 2] = (uint8_t)(v >> 16); b[o + 3] = (uint8_t)(v >> 24);
}

/* ---- SARC: the dictionary pack ------------------------------------------------------------------------------------ */

/* The file called name in a SARC (the game's packs): where its bytes are, or 0 */
static int sarc_find(const Buf *s, const char *name, size_t *at, size_t *len) {
    const uint8_t *d = s->d;
    if (s->n < 0x20 || memcmp(d, "SARC", 4)) return 0;
    const size_t data0 = r32(d, 0x0c), sfat = r16(d, 4);
    if (memcmp(d + sfat, "SFAT", 4)) return 0;
    const size_t count = r16(d, sfat + 6), sfnt = sfat + 12 + count * 16;
    if (sfnt + 8 > s->n || memcmp(d + sfnt, "SFNT", 4)) return 0;
    for (size_t i = 0; i < count; i++) {
        const size_t e = sfat + 12 + i * 16;
        if (e + 16 > s->n) return 0;
        const size_t name_at = sfnt + 8 + (size_t)(r32(d, e + 4) & 0xffffff) * 4;
        if (name_at >= s->n || strncmp((const char *)d + name_at, name, s->n - name_at)) continue;
        const size_t b = r32(d, e + 8), f = r32(d, e + 12);
        if (data0 + f > s->n || f < b) return 0;
        *at = data0 + b;
        *len = f - b;
        return 1;
    }
    return 0;
}

/* ---- BYML v7: just enough ---------------------------------------------------------------------------------------- */

#define T_STR 0xa0
#define T_ARR 0xc0
#define T_DICT 0xc1
#define T_I64 0xd4
#define T_U64 0xd5

static int in_bounds(const Buf *b, size_t at, size_t len) { return at <= b->n && len <= b->n - at; }

static uint32_t st_count(const Buf *b, uint32_t tbl) { return r24(b->d, tbl + 1); }
static const char *st_str(const Buf *b, uint32_t tbl, uint32_t i) {
    return (const char *)b->d + tbl + r32(b->d, tbl + 4 + (size_t)i * 4);
}
static int st_valid(const Buf *b, uint32_t tbl) {
    if (!in_bounds(b, tbl, 8) || b->d[tbl] != 0xc2) return 0;
    const uint32_t n = st_count(b, tbl);
    if (!in_bounds(b, tbl, 4 + ((size_t)n + 1) * 4)) return 0;
    const uint32_t end = r32(b->d, tbl + 4 + (size_t)n * 4);
    return in_bounds(b, tbl, end) && end >= 4 + ((size_t)n + 1) * 4;
}
static int st_find(const Buf *b, uint32_t tbl, const char *s) {
    const uint32_t n = st_count(b, tbl);
    for (uint32_t i = 0; i < n; i++)
        if (!strcmp(st_str(b, tbl, i), s)) return (int)i;
    return -1;
}

/* A dictionary's entry for key index key: its value type, the value, and where the value's 4 bytes are */
static int dict_find(const Buf *b, uint32_t d, int key, uint8_t *type, uint32_t *val, size_t *slot) {
    if (key < 0 || !in_bounds(b, d, 4) || b->d[d] != T_DICT) return 0;
    const uint32_t n = r24(b->d, d + 1);
    if (!in_bounds(b, d, 4 + (size_t)n * 8)) return 0;
    for (uint32_t i = 0; i < n; i++) {
        const size_t e = d + 4 + (size_t)i * 8;
        if ((int)r24(b->d, e) == key) {
            if (type) *type = b->d[e + 3];
            if (val) *val = r32(b->d, e + 4);
            if (slot) *slot = e + 4;
            return 1;
        }
    }
    return 0;
}

/* ---- the chests --------------------------------------------------------------------------------------------------- */

typedef struct {
    char name[96];   /* "InsideTheDekuTree_Static" */
    char scope[96];  /* the scene without _Static / _Dynamic */
    char dir[64];    /* "InsideTheDekuTree" */
    char rel[256];   /* "Banc/InsideTheDekuTree/InsideTheDekuTree_Static.bcett.byml.zs" */
    Buf b;           /* the file, unpacked */
} Scene;

typedef struct {
    int scene;
    size_t dyn_slot; /* where the actor's Dynamic dictionary's offset is, and that dictionary: BYML shares equal ones */
    uint32_t dyn;
    uint64_t hash;
    char drop[72], gyaml[72], event[96];
    char carry[96];  /* the event that goes with this chest's item when it is moved (empty: none goes) */
    char now[72];    /* what it gives after the shuffle */
    char now_ev[96]; /* and the event it has after it */
    size_t gy_slot;  /* where the actor's Gyaml string index is */
    char now_gy[72]; /* and the chest it is after: a small chest that gets an item's own scene becomes a big one */
    int placed;
    int leader;      /* the same chest in the scene's other age file (same folder and hash): this index, else itself */
} Loc;

static const char *const kDungeons[] = {"BottomOfTheWell", "DodongoCavern", "FireTemple", "ForestTemple", "GerudoTrainingGround",
                                        "IceCavern", "InsideGanonCastle", "InsideJabuJabuBelly", "InsideTheDekuTree",
                                        "ShadowTemple", "SpiritTemple", "WaterTemple", NULL};

static int is_dungeon_item(const char *drop) {
    return starts_with(drop, "Item_Dungeon_");   /* SmallKey, BossKey, Map, Compass */
}

/* What a chest's GetEventName means. The event runs when the chest is opened whatever it holds: the Kokiri Sword chest's played
   the sword's scene and equipped a sword that was never given (found in play). So an item's own event (the swords, the Bomb Bag,
   the Bow, the Hammer, the Gauntlets, the Lens of Truth: kCarry) goes with the item to the chest that gets it, and a chest has
   only the event of the item it holds. The get-item scenes of the dungeon items and of the Slingshot, and Navi's chat switch
   (kPlain), are not tied to anything: they are dropped when the chest's item changes. Any other event is a story scene
   (minigames, Ruto, the sages, Dampe's race, the stones): its chest stays as it is. Returns 0 stays, 1 plain, 2 carried */
static int has_suffix(const char *e, const char *suffix) {
    const size_t n = strlen(e), k = strlen(suffix);
    return n >= k && !strcmp(e + n - k, suffix);
}

static int event_rule(const char *e) {
    static const char *const kPlain[] = {"_GetMap", "_GetCompass", "_GetBossKey", "_GetSmallKey", "_GetSlingShot", "NaviChatOff", NULL};
    static const char *const kCarry[] = {"GetSword", "GetBombBag", "GetBow", "GetHammer", "Get_Gauntlet", "GetGlassTruth", NULL};
    if (!e[0]) return 1;
    if (strstr(e, "Sage") || strstr(e, "SubEvent") || strstr(e, "MiniGame") || strstr(e, "Ruto")) return 0;
    for (int i = 0; kPlain[i]; i++)
        if (has_suffix(e, kPlain[i])) return 1;
    for (int i = 0; kCarry[i]; i++)
        if (strstr(e, kCarry[i])) return 2;
    return 0;
}

static int eligible(Loc *l, int has_check) {
    if (has_check || !l->drop[0]) return 0;
    if (!starts_with(l->gyaml, "Obj_TBox")) return 0;
    if (strstr(l->gyaml, "Pedestal") || strstr(l->gyaml, "Shop")) return 0;
    const int rule = event_rule(l->event);
    if (!rule) return 0;
    if (rule == 2) snprintf(l->carry, sizeof l->carry, "%s", l->event);
    return 1;
}

/* The scene's chests, into locs */
static int scan_scene(const Scene *S, int si, Loc **locs, int *n, int *cap) {
    const Buf *b = &S->b;
    if (b->n < 16 || b->d[0] != 'Y' || b->d[1] != 'B' || r16(b->d, 2) != 7) return 0;
    const uint32_t kt = r32(b->d, 4), st = r32(b->d, 8), root = r32(b->d, 12);
    if (!st_valid(b, kt) || !st_valid(b, st)) return 0;
    const int kActors = st_find(b, kt, "Actors"), kHash = st_find(b, kt, "Hash"), kGyaml = st_find(b, kt, "Gyaml"),
              kDyn = st_find(b, kt, "Dynamic"), kDrop = st_find(b, kt, "Drop__DropActor"),
              kEvent = st_find(b, kt, "GetEventName"), kCheck = st_find(b, kt, "Drop__CheckActor");
    if (kActors < 0 || kGyaml < 0 || kDyn < 0 || kDrop < 0) return 0;
    uint8_t t;
    uint32_t arr, d, dd, v;
    if (!dict_find(b, root, kActors, &t, &arr, NULL) || t != T_ARR || !in_bounds(b, arr, 4)) return 0;
    const uint32_t cnt = r24(b->d, arr + 1), vals = (arr + 4 + cnt + 3) & ~3u;
    if (!in_bounds(b, arr, (size_t)vals - arr + (size_t)cnt * 4)) return 0;
    int added = 0;
    for (uint32_t i = 0; i < cnt; i++) {
        if (b->d[arr + 4 + i] != T_DICT) continue;
        d = r32(b->d, vals + (size_t)i * 4);
        Loc l;
        memset(&l, 0, sizeof l);
        if (!dict_find(b, d, kGyaml, &t, &v, &l.gy_slot) || t != T_STR || v >= st_count(b, st)) continue;
        snprintf(l.gyaml, sizeof l.gyaml, "%s", st_str(b, st, v));
        snprintf(l.now_gy, sizeof l.now_gy, "%s", l.gyaml);
        if (!starts_with(l.gyaml, "Obj_TBox")) continue;
        size_t dyn_slot;
        if (!dict_find(b, d, kDyn, &t, &dd, &dyn_slot) || t != T_DICT) continue;
        if (!dict_find(b, dd, kDrop, &t, &v, NULL) || t != T_STR || v >= st_count(b, st)) continue;
        snprintf(l.drop, sizeof l.drop, "%s", st_str(b, st, v));
        l.dyn_slot = dyn_slot;
        l.dyn = dd;
        if (kEvent >= 0 && dict_find(b, dd, kEvent, &t, &v, NULL) && t == T_STR && v < st_count(b, st))
            snprintf(l.event, sizeof l.event, "%s", st_str(b, st, v));
        if (kHash >= 0 && dict_find(b, d, kHash, &t, &v, NULL) && (t == T_U64 || t == T_I64) && in_bounds(b, v, 8))
            l.hash = r64(b->d, v);
        if (!eligible(&l, kCheck >= 0 && dict_find(b, dd, kCheck, NULL, NULL, NULL))) continue;
        l.scene = si;
        snprintf(l.now, sizeof l.now, "%s", l.drop);
        snprintf(l.now_ev, sizeof l.now_ev, "%s", l.event);
        if (*n == *cap) {
            *cap = *cap ? *cap * 2 : 512;
            *locs = (Loc *)realloc(*locs, (size_t)*cap * sizeof **locs);
        }
        (*locs)[(*n)++] = l;
        added++;
    }
    return added;
}

static void w24(uint8_t *b, size_t o, uint32_t v) { b[o] = (uint8_t)v; b[o + 1] = (uint8_t)(v >> 8); b[o + 2] = (uint8_t)(v >> 16); }

/* A changed chest: its drop or its event is not what the file has */
static int loc_changed(const Loc *l) { return strcmp(l->now, l->drop) || strcmp(l->now_ev, l->event) || strcmp(l->now_gy, l->gyaml); }

/* Key indexes at or above pos move up by one in every dictionary under a node (shared nodes once) */
static void remap_keys(Buf *b, uint32_t off, uint8_t type, int pos, uint8_t *seen, int depth) {
    if (depth > 64 || (type != T_DICT && type != T_ARR) || !in_bounds(b, off, 4) || seen[off]) return;
    seen[off] = 1;
    const uint32_t cnt = r24(b->d, off + 1);
    if (type == T_DICT) {
        if (!in_bounds(b, off, 4 + (size_t)cnt * 8)) return;
        for (uint32_t i = 0; i < cnt; i++) {
            const size_t e = off + 4 + (size_t)i * 8;
            const uint32_t k = r24(b->d, e);
            if ((int)k >= pos) w24(b->d, e, k + 1);
            remap_keys(b, r32(b->d, e + 4), b->d[e + 3], pos, seen, depth + 1);
        }
    } else {
        const uint32_t vals = (off + 4 + cnt + 3) & ~3u;
        if (!in_bounds(b, off, (size_t)vals - off + (size_t)cnt * 4)) return;
        for (uint32_t i = 0; i < cnt; i++) remap_keys(b, r32(b->d, vals + (size_t)i * 4), b->d[off + 4 + i], pos, seen, depth + 1);
    }
}

/* The key table gets a name it lacks: the table is rebuilt at the end of the file with the name in its sorted place (the game
   looks keys up in order), and the keys of every dictionary after it move up. Returns the key's index, or -1 */
static int add_key(Buf *b, const char *name) {
    const uint32_t kt = r32(b->d, 4), cnt = st_count(b, kt);
    int pos = (int)cnt;
    for (uint32_t i = 0; i < cnt; i++)
        if (strcmp(st_str(b, kt, i), name) > 0) { pos = (int)i; break; }
    const size_t hdr = 4 + ((size_t)cnt + 2) * 4, old_first = r32(b->d, kt + 4), old_chars = r32(b->d, kt + 4 + (size_t)cnt * 4) - old_first;
    const size_t base = (b->n + 3) & ~(size_t)3, total = hdr + old_chars + strlen(name) + 1;
    if (!buf_reserve(b, base + total + 8)) return -1;
    uint8_t *o = b->d + base;
    o[0] = 0xc2; w24(o, 1, cnt + 1);
    size_t at = hdr;
    for (uint32_t i = 0; i <= cnt; i++) {
        const char *src = (int)i == pos ? name : st_str(b, kt, (int)i < pos ? i : i - 1);
        w32(b->d, base + 4 + (size_t)i * 4, (uint32_t)at);
        const size_t len = strlen(src) + 1;
        memcpy(o + at, src, len);
        at += len;
    }
    w32(b->d, base + 4 + ((size_t)cnt + 1) * 4, (uint32_t)at);
    b->n = base + at;
    w32(b->d, 4, (uint32_t)base);
    uint8_t *seen = (uint8_t *)calloc(b->n + 1, 1);
    if (!seen) return -1;
    const uint32_t root = r32(b->d, 12);
    remap_keys(b, root, b->d[root], pos, seen, 0);
    free(seen);
    return pos;
}

/* The index of a name in the string table, or one past it for a name added at the end (extra) */
static int str_slot(const Buf *b, uint32_t st, uint32_t cnt, char (**extra)[96], int *extras, const char *name) {
    int idx = st_find(b, st, name);
    if (idx >= 0) return idx;
    for (int j = 0; j < *extras; j++)
        if (!strcmp((*extra)[j], name)) return (int)cnt + j;
    *extra = (char (*)[96])realloc(*extra, (size_t)(*extras + 1) * sizeof **extra);
    if (!*extra) return -1;
    snprintf((*extra)[*extras], 96, "%s", name);
    return (int)cnt + (*extras)++;
}

/* A scene's chests changed: new names go to the end of the file in a rebuilt string table; each changed chest gets a Dynamic
   dictionary of its own (a copy at the end, with its drop and its event as they are now) */
static int apply_scene(Scene *S, int si, Loc *locs, int n) {
    Buf *b = &S->b;
    uint32_t st = r32(b->d, 8);
    const uint32_t cnt = st_count(b, st);
    char (*extra)[96] = NULL;
    int extras = 0, changed = 0, need_event_key = 0;
    int *want = (int *)calloc((size_t)(n ? n : 1), sizeof *want);     /* a loc's new drop string index, -1 not changed */
    int *wantev = (int *)calloc((size_t)(n ? n : 1), sizeof *wantev); /* and its event's, -1 none */
    int *wantgy = (int *)calloc((size_t)(n ? n : 1), sizeof *wantgy); /* and its chest's (the actor's Gyaml), -1 not changed */
    if (!want || !wantev || !wantgy) { free(want); free(wantev); free(wantgy); return -1; }
    for (int i = 0; i < n; i++) {
        want[i] = wantev[i] = wantgy[i] = -1;
        if (locs[i].scene != si || !loc_changed(&locs[i])) continue;
        want[i] = str_slot(b, st, cnt, &extra, &extras, locs[i].now);
        if (locs[i].now_ev[0]) { wantev[i] = str_slot(b, st, cnt, &extra, &extras, locs[i].now_ev); need_event_key = 1; }
        if (strcmp(locs[i].now_gy, locs[i].gyaml)) wantgy[i] = str_slot(b, st, cnt, &extra, &extras, locs[i].now_gy);
        if (want[i] < 0 || (locs[i].now_ev[0] && wantev[i] < 0) || (strcmp(locs[i].now_gy, locs[i].gyaml) && wantgy[i] < 0)) { free(want); free(wantev); free(wantgy); free(extra); return -1; }
        changed++;
    }
    if (extras) {
        const size_t hdr = 4 + ((size_t)cnt + extras + 1) * 4, old_first = r32(b->d, st + 4),
                     old_chars = r32(b->d, st + 4 + (size_t)cnt * 4) - old_first;
        size_t extra_bytes = 0;
        for (int j = 0; j < extras; j++) extra_bytes += strlen(extra[j]) + 1;
        const size_t base = (b->n + 3) & ~(size_t)3, total = hdr + old_chars + extra_bytes;
        if (!buf_reserve(b, base + total + 8)) { free(want); free(wantev); free(extra); return -1; }
        uint8_t *o = b->d + base;
        o[0] = 0xc2; w24(o, 1, cnt + extras);
        for (uint32_t i = 0; i <= cnt; i++)
            w32(b->d, base + 4 + (size_t)i * 4, (uint32_t)(hdr + (r32(b->d, st + 4 + (size_t)i * 4) - old_first)));
        memcpy(o + hdr, b->d + st + old_first, old_chars);
        size_t at = hdr + old_chars;
        for (int j = 0; j < extras; j++) {
            w32(b->d, base + 4 + ((size_t)cnt + j) * 4, (uint32_t)at);
            const size_t len = strlen(extra[j]) + 1;
            memcpy(o + at, extra[j], len);
            at += len;
        }
        w32(b->d, base + 4 + ((size_t)cnt + extras) * 4, (uint32_t)at);
        w32(b->d, 8, (uint32_t)base);
        b->n = base + total;
    }
    int kev = st_find(b, r32(b->d, 4), "GetEventName");
    if (need_event_key && kev < 0 && (kev = add_key(b, "GetEventName")) < 0) { free(want); free(wantev); free(extra); return -1; }
    const int kdrop = st_find(b, r32(b->d, 4), "Drop__DropActor");
    for (int i = 0; i < n; i++) {
        if (want[i] < 0) continue;
        const uint32_t ne = r24(b->d, locs[i].dyn + 1);
        const size_t at = (b->n + 3) & ~(size_t)3;
        if (!buf_reserve(b, at + 4 + (size_t)(ne + 1) * 8 + 8)) { free(want); free(wantev); free(extra); return -1; }
        /* the entries but the event, the drop's value new; the event's entry at its sorted place (keys rise down a dictionary) */
        uint32_t kept = 0;
        int put = wantev[i] < 0;
        for (uint32_t e = 0; e <= ne; e++) {
            if (e < ne) {
                const size_t from = locs[i].dyn + 4 + (size_t)e * 8;
                const int key = (int)r24(b->d, from);
                if (key == kev) continue;   /* (the old event goes) */
                if (!put && key > kev) {
                    const size_t to = at + 4 + (size_t)kept++ * 8;
                    w24(b->d, to, (uint32_t)kev); b->d[to + 3] = T_STR; w32(b->d, to + 4, (uint32_t)wantev[i]);
                    put = 1;
                }
                const size_t to = at + 4 + (size_t)kept++ * 8;
                memcpy(b->d + to, b->d + from, 8);
                if (key == kdrop) w32(b->d, to + 4, (uint32_t)want[i]);
            } else if (!put) {
                const size_t to = at + 4 + (size_t)kept++ * 8;
                w24(b->d, to, (uint32_t)kev); b->d[to + 3] = T_STR; w32(b->d, to + 4, (uint32_t)wantev[i]);
                put = 1;
            }
        }
        b->d[at] = T_DICT; w24(b->d, at + 1, kept);
        b->n = at + 4 + (size_t)kept * 8;
        w32(b->d, locs[i].dyn_slot, (uint32_t)at);
        if (wantgy[i] >= 0) w32(b->d, locs[i].gy_slot, (uint32_t)wantgy[i]);
    }
    free(want);
    free(wantev);
    free(wantgy);
    free(extra);
    return changed;
}

/* ---- the shuffle -------------------------------------------------------------------------------------------------- */

static uint64_t rng_state;
static uint64_t rng(void) {   /* splitmix64 */
    uint64_t z = (rng_state += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}
static uint32_t below(uint32_t n) { return (uint32_t)(rng() % n); }

static int dungeon_listed(const Scene *sc) {
    for (int i = 0; kDungeons[i]; i++)
        if (!strcmp(sc->dir, kDungeons[i])) return 1;
    return 0;
}

/* the chest actor "place" becomes when it holds the item that "origin" (a chest actor) held */
static const char *chest_for(const char *place, const char *origin) {
    static char out[72];
    const char *cls = starts_with(origin, "Obj_TBox_A_02") ? "A_02" : starts_with(origin, "Obj_TBox_A_03") ? "A_03" : "A_01";
    int ok = 0;
    for (const char *k = "1236"; *k; k++) {
        char base[32];
        snprintf(base, sizeof base, "Obj_TBox_A_0%c", *k);
        if (starts_with(place, base) && (!place[strlen(base)] || !strcmp(place + strlen(base), "_NoBake"))) ok = 1;
    }
    if (!ok) return place;
    snprintf(out, sizeof out, "Obj_TBox_%s%s", cls, ends_with(place, "_NoBake") ? "_NoBake" : "");
    return out;
}

/* The items shuffled among the places, one each: items (the chests they are in now) and places have the same count. An item
   keeps the event that goes with it */
static void deal(Loc *locs, const int *places, int *items, int n) {
    for (int i = n - 1; i > 0; i--) { const int j = (int)below((uint32_t)i + 1); const int t = items[i]; items[i] = items[j]; items[j] = t; }
    for (int i = 0; i < n; i++) {
        Loc *to = &locs[places[i]];
        const Loc *from = &locs[items[i]];
        snprintf(to->now, sizeof to->now, "%s", from->drop);
        snprintf(to->now_ev, sizeof to->now_ev, "%s", from->carry);
        /* A chest's size goes with its item: an item that is in a big chest in the game (a compass, a map, a boss key, the sword...)
           makes the chest it goes to a big one, an item of a small chest a small one (the actor's name: A_01 small, A_02 big,
           A_03 the boss key's). The lighting variant (_NoBake) and chests of another kind (the Lens of Truth's hidden one,
           heart pieces' memo chests) stay as they are */
        snprintf(to->now_gy, sizeof to->now_gy, "%s", chest_for(to->gyaml, from->gyaml));
        to->placed = 1;
    }
}

/* One pool: the places and the items of the chests in the scope (NULL: all), after the dungeon items kept in their dungeons
   have taken theirs. A dungeon item that stays vanilla is in neither list. */
static void shuffle_pool(Loc *locs, int nl, const Scene *sc, const char *scope, const RandoSettings *s, int di_mode) {
    int *places = (int *)malloc((size_t)(nl ? nl : 1) * sizeof *places);
    int *items = (int *)malloc((size_t)(nl ? nl : 1) * sizeof *items);
    int np = 0, ni = 0;
    for (int i = 0; i < nl; i++) {
        if (locs[i].leader != i) continue;   /* (a chest that is another file's copy follows its leader) */
        if (scope && strcmp(sc[locs[i].scene].scope, scope)) continue;
        const int di = is_dungeon_item(locs[i].drop), listed = dungeon_listed(&sc[locs[i].scene]);
        if (!locs[i].placed && !(s->dungeon_items == 0 && di)) places[np++] = i;
        if (!di || di_mode == 2 || (di_mode == 1 && !listed)) items[ni++] = i;
    }
    if (np == ni && np > 0) deal(locs, places, items, np);   /* (a count that differs would lose an item: leave the pool alone) */
    free(places);
    free(items);
}

static void shuffle_all(Loc *locs, int nl, const Scene *sc, int nsc, const RandoSettings *s) {
    /* Within each scene, "Anywhere" is "Own dungeon": the scene keeps the chests it has */
    const int di_mode = (s->chests == 1 && s->dungeon_items == 2) ? 1 : s->dungeon_items;
    if (di_mode == 1) {   /* the dungeon items go to random chests of their own dungeon (both its scenes' chests) */
        for (int d = 0; kDungeons[d]; d++) {
            int *slots = (int *)malloc((size_t)(nl ? nl : 1) * sizeof *slots);
            int *items = (int *)malloc((size_t)(nl ? nl : 1) * sizeof *items);
            int ns = 0, ni = 0;
            for (int i = 0; i < nl; i++) {
                if (locs[i].leader != i || strcmp(sc[locs[i].scene].dir, kDungeons[d])) continue;
                slots[ns++] = i;
                if (is_dungeon_item(locs[i].drop)) items[ni++] = i;
            }
            for (int i = ns - 1; i > 0; i--) { const int j = (int)below((uint32_t)i + 1), t = slots[i]; slots[i] = slots[j]; slots[j] = t; }
            if (ni) deal(locs, slots, items, ni);   /* (the first ni of the shuffled places) */
            free(slots);
            free(items);
        }
    }
    if (s->chests == 2) {
        shuffle_pool(locs, nl, sc, NULL, s, di_mode);
    } else {
        for (int a = 0; a < nsc; a++) {
            int first = 1;
            for (int b = 0; b < a; b++) if (!strcmp(sc[b].scope, sc[a].scope)) first = 0;
            if (first) shuffle_pool(locs, nl, sc, sc[a].scope, s, di_mode);
        }
    }
    for (int i = 0; i < nl; i++) {   /* a chest in the other age's file has what its leader has */
        if (locs[i].leader == i) continue;
        const Loc *l = &locs[locs[i].leader];
        snprintf(locs[i].now, sizeof locs[i].now, "%s", l->now);
        snprintf(locs[i].now_ev, sizeof locs[i].now_ev, "%s", l->now_ev);
        snprintf(locs[i].now_gy, sizeof locs[i].now_gy, "%s", l->now_gy);
        locs[i].placed = l->placed;
    }
}

/* ---- beatability -------------------------------------------------------------------------------------------------- */

/* The game is beatable when the fifteen key items (kRlItems: the Bow, the Hookshot, the Mirror Shield and the like, every item the
   game's logic asks for) can all be reached. Sphere by sphere: a chest opens once the items already found meet its class's
   requirement (rando_logic.h, made from the OoT Randomizer's logic with no tricks), and the game is won once the held items meet
   kRlGoal. A chest holding a key is held to the stricter list (no key of its dungeon). A chest of the remake that is not one of
   the fifteen takes the requirement of the EASIEST chest of its dungeon (or of the overworld): the remake's chests are not matched
   with the Randomizer's locations one by one (no id in common), so a key item in a chest deeper in its dungeon than that is not
   known to be out of reach, and a seed that passes is very likely beatable, not proved. The key-item chests the shuffle leaves
   alone (story ones: the Hookshot, the Boomerang...) count where they are. Returns 1 beatable, 0 not, -1 out of memory */
static int req_met(const RlReq *r, uint32_t held) {
    for (int i = 0; i < r->n; i++)
        if ((held & r->m[i]) == r->m[i]) return 1;
    return 0;
}

static int rl_item(const char *drop) {
    for (int i = 0; i < 15; i++)
        if (!strcmp(drop, kRlItems[i])) return i;
    return -1;
}

static int rl_is_key(const char *drop) {
    return starts_with(drop, "Item_Dungeon_") && (strstr(drop, "SmallKey") || strstr(drop, "BossKey"));
}

static int rl_class(const Loc *l, const Scene *sc) {
    for (int i = 0; i < 15; i++)
        if (l->hash == kRlHash[i]) return 13 + i;
    for (int d = 0; d < 12; d++)
        if (!strcmp(sc[l->scene].dir, kRlDirs[d])) return d;
    return 12;
}

static int beatable(const Loc *locs, int nl, const Scene *sc) {
    uint32_t held = 0, fixed = 0x7fff;   /* (a key-item chest the shuffle does not touch stays as it is: it holds its own item) */
    char *open = (char *)calloc((size_t)(nl ? nl : 1), 1);
    int *cls = (int *)malloc((size_t)(nl ? nl : 1) * sizeof *cls);
    if (!open || !cls) { free(open); free(cls); return -1; }
    for (int i = 0; i < nl; i++) {
        cls[i] = rl_class(&locs[i], sc);
        if (locs[i].leader == i && cls[i] >= 13) fixed &= ~(1u << (cls[i] - 13));
    }
    for (int again = 1; again;) {
        again = 0;
        for (int i = 0; i < 15; i++)
            if ((fixed >> i & 1) && !(held >> i & 1) && req_met(&kRlAny[13 + i], held)) { held |= 1u << i; again = 1; }
        for (int i = 0; i < nl; i++) {
            if (open[i] || locs[i].leader != i) continue;
            const RlReq *need = rl_is_key(locs[i].now) ? &kRlKey[cls[i]] : &kRlAny[cls[i]];
            if (!req_met(need, held)) continue;
            open[i] = 1;
            again = 1;
            const int b = rl_item(locs[i].now);
            if (b >= 0) held |= 1u << b;
        }
    }
    free(open);
    free(cls);
    return req_met(&kRlGoal, held);
}

/* ---- the scenes' warps ------------------------------------------------------------------------------------------- */

/* A scene that plays when an item is found (a BFEVFL event flow: the Kokiri Sword's, Dm_Kokiri_0063) warps the player and Navi
   to a fixed spot, the place it was made: the old chest's. Moved to another chest it would take the player there. Every action
   that warps the player or Navi to a position (WarpToTargetPosAndRot) becomes an EventWait of the file's own (the event's actor
   and action indexes and its parameters are changed: the parameters of the file's shortest wait), so the scene plays where the
   player stands. Warps to an actor of the scene (the chest) are not touched. Returns the number changed, or -1 */
static int evfl_name(const Buf *b, uint64_t p, char *out, size_t n) {
    out[0] = 0;
    if (!p || p > b->n || b->n - p < 2) return 0;
    size_t len = r16(b->d, p);
    if (p + 2 + len > b->n) return 0;
    if (len >= n) len = n - 1;
    memcpy(out, b->d + p + 2, len);
    out[len] = 0;
    return 1;
}

/* The "Frames" int of a params container (type 1: its names are the DIC's after the root, one item pointer each) */
static int evfl_frames(const Buf *b, uint64_t c, int *frames) {
    if (!c || c + 16 > b->n || b->d[c] != 1) return 0;
    const uint32_t n = r16(b->d, c + 2);
    const uint64_t dic = r64(b->d, c + 8);
    if (!dic || dic + 8 + (uint64_t)(n + 1) * 16 > b->n || memcmp(b->d + dic, "DIC ", 4) || c + 16 + (uint64_t)n * 8 > b->n) return 0;
    char nm[16];
    for (uint32_t i = 0; i < n; i++) {
        if (!evfl_name(b, r64(b->d, dic + 8 + (size_t)(i + 1) * 16 + 8), nm, sizeof nm) || strcmp(nm, "Frames")) continue;
        const uint64_t item = r64(b->d, c + 16 + (size_t)i * 8);
        if (!item || item + 20 > b->n || b->d[item] != 2) return 0;
        *frames = (int)r32(b->d, item + 16);
        return 1;
    }
    return 0;
}

static int evfl_neutralize_warps(Buf *b) {
    if (b->n < 0x48 || memcmp(b->d, "BFEVFL\0\0", 8)) return -1;
    const size_t esize = r16(b->d, 8) >= 0x400 ? 0x30 : 0x28;
    const uint64_t fpo = r64(b->d, 0x28);
    if (!fpo || fpo + 8 > b->n) return -1;
    const uint64_t fc = r64(b->d, fpo);
    if (!fc || fc + 0x48 > b->n || memcmp(b->d + fc, "EVFL", 4)) return -1;
    const uint32_t na = r16(b->d, fc + 0x10), ne = r16(b->d, fc + 0x16);
    const uint64_t actors = r64(b->d, fc + 0x28), events = r64(b->d, fc + 0x30);
    if (!actors || !events || actors + (uint64_t)na * 0x38 > b->n || events + (uint64_t)ne * esize > b->n) return -1;
    int wa = -1, wi = -1;   /* an actor with an EventWait action, and its index in the actor's actions */
    char nm[96];
    for (uint32_t a = 0; a < na && wa < 0; a++) {
        const uint8_t *ar = b->d + actors + (size_t)a * 0x38;
        const uint64_t acts = r64(ar, 0x18);
        const uint32_t n = r16(ar, 0x30);
        if (!acts || acts + (uint64_t)n * 8 > b->n) continue;
        for (uint32_t j = 0; j < n; j++)
            if (evfl_name(b, r64(b->d, acts + (size_t)j * 8), nm, sizeof nm) && !strcmp(nm, "EventWait")) { wa = (int)a; wi = (int)j; break; }
    }
    if (wa < 0) return 0;   /* (no wait to use: the file stays as it is) */
    uint64_t wparams = 0;
    int wframes = 0;
    for (uint32_t e = 0; e < ne; e++) {
        const uint8_t *ev = b->d + events + (size_t)e * esize;
        int f;
        if (ev[8] != 0 || (int)r16(ev, 12) != wa || (int)r16(ev, 14) != wi || !evfl_frames(b, r64(ev, 16), &f) || f < 0) continue;
        if (!wparams || f < wframes) { wparams = r64(ev, 16); wframes = f; }
    }
    int changed = 0;
    for (uint32_t e = 0; e < ne; e++) {
        uint8_t *ev = b->d + events + (size_t)e * esize;
        if (ev[8] != 0) continue;   /* (an action event) */
        const uint32_t ai = r16(ev, 12), ac = r16(ev, 14);
        if (ai >= na || (int)ai == wa) continue;
        const uint8_t *ar = b->d + actors + (size_t)ai * 0x38;
        if (!evfl_name(b, r64(ar, 0), nm, sizeof nm) || (strcmp(nm, "Player_Event") && strcmp(nm, "Npc_Navi"))) continue;
        const uint64_t acts = r64(ar, 0x18);
        if (!acts || ac >= r16(ar, 0x30) || acts + (uint64_t)(ac + 1) * 8 > b->n) continue;
        if (!evfl_name(b, r64(b->d, acts + (size_t)ac * 8), nm, sizeof nm) || !strstr(nm, "WarpToTargetPosAndRot")) continue;
        ev[12] = (uint8_t)wa; ev[13] = (uint8_t)(wa >> 8);
        ev[14] = (uint8_t)wi; ev[15] = (uint8_t)(wi >> 8);
        if (wparams) for (int k = 0; k < 8; k++) ev[16 + k] = (uint8_t)(wparams >> (8 * k));
        changed++;
    }
    return changed;
}

/* The scenes of the items that go with them (under <romfs>/Event/EventFlow), written without the warps */
static const char *const kScenes[] = {"Dm_Kokiri_0063", NULL};

static int patch_scenes(const char *romfs, const char *out_romfs, const Buf *dict, FILE *sp) {
    int n = 0;
    for (int i = 0; kScenes[i]; i++) {
        char path[1024];
        Buf tmp = {0}, ev = {0};
        snprintf(path, sizeof path, "%s/Event/EventFlow/%s.bfevfl.zs", romfs, kScenes[i]);
        if (!read_file(path, &tmp) || !unzstd(tmp.d, tmp.n, dict->d, dict->n, &ev)) { free(tmp.d); free(ev.d); continue; }
        const int c = evfl_neutralize_warps(&ev);
        if (c > 0) {
            size_t zn = 0;
            uint8_t *z = zstd_raw_frame(ev.d, ev.n, &zn);
            snprintf(path, sizeof path, "%s/Event/EventFlow", out_romfs);
            mkdir_p(path);
            snprintf(path, sizeof path, "%s/Event/EventFlow/%s.bfevfl.zs", out_romfs, kScenes[i]);
            if (z && write_file(path, z, zn)) {
                n++;
                if (sp) fprintf(sp, "scene %s: %d warps to a fixed place removed\n", kScenes[i], c);
            }
            free(z);
        }
        free(tmp.d);
        free(ev.d);
    }
    return n;
}

/* ---- the run ------------------------------------------------------------------------------------------------------ */

int rando_generate(const char *romfs, const char *out_romfs, const char *spoiler_path, RandoSettings *s, char *err,
                   size_t errn) {
    char path[1024];
    Buf pack = {0}, dict = {0}, tmp = {0};
    Scene *sc = NULL;
    int nsc = 0, capsc = 0, nl = 0, capl = 0, result = -1;
    Loc *locs = NULL;
    if (!s || s->chests < 0 || s->chests > 2 || s->dungeon_items < 0 || s->dungeon_items > 2) {
        fail(err, errn, "bad settings", NULL);
        return -1;
    }
    if (!s->seed) s->seed = (uint32_t)(((uint64_t)time(NULL) * 2654435761u) % 999999u) + 1;
    snprintf(path, sizeof path, "%s/Pack/ZsDic.pack.zs", romfs);
    if (!read_file(path, &tmp)) { fail(err, errn, "cannot read", path); goto done; }
    if (!unzstd(tmp.d, tmp.n, NULL, 0, &pack)) { fail(err, errn, "cannot unpack", path); goto done; }
    size_t at = 0, len = 0;
    if (!sarc_find(&pack, "bcett.byml.zsdic", &at, &len)) { fail(err, errn, "no bcett dictionary in", path); goto done; }
    dict.d = pack.d + at;
    dict.n = len;

    snprintf(path, sizeof path, "%s/Banc", romfs);
    char **dirs;
    const int ndirs = list_dir(path, &dirs);
    for (int di = 0; di < ndirs; di++) {
        if (!strcmp(dirs[di], "DreamWorld")) continue;   /* the boss rush's stages */
        char sub[1024];
        snprintf(sub, sizeof sub, "%s/Banc/%s", romfs, dirs[di]);
        char **files;
        const int nf = list_dir(sub, &files);
        for (int fi = 0; fi < nf; fi++) {
            if (!ends_with(files[fi], ".bcett.byml.zs")) continue;
            if (nsc == capsc) {
                capsc = capsc ? capsc * 2 : 128;
                sc = (Scene *)realloc(sc, (size_t)capsc * sizeof *sc);
            }
            Scene *S = &sc[nsc];
            memset(S, 0, sizeof *S);
            snprintf(S->dir, sizeof S->dir, "%s", dirs[di]);
            snprintf(S->rel, sizeof S->rel, "Banc/%s/%s", dirs[di], files[fi]);
            snprintf(S->name, sizeof S->name, "%.*s", (int)(strlen(files[fi]) - 14), files[fi]);
            snprintf(S->scope, sizeof S->scope, "%s", S->name);
            if (ends_with(S->scope, "_Static")) S->scope[strlen(S->scope) - 7] = 0;
            else if (ends_with(S->scope, "_Dynamic")) S->scope[strlen(S->scope) - 8] = 0;
            snprintf(path, sizeof path, "%s/%s", romfs, S->rel);
            if (!read_file(path, &tmp) || !unzstd(tmp.d, tmp.n, dict.d, dict.n, &S->b)) continue;   /* (not a scene we can read) */
            if (scan_scene(S, nsc, &locs, &nl, &capl)) nsc++;
            else { free(S->b.d); }
        }
        free_list(files, nf);
    }
    free_list(dirs, ndirs);
    if (!nl) { fail(err, errn, "no chests found under", romfs); goto done; }

    /* A chest is in both of a scene's age files (Child and Adult, same folder and hash): one place, one item. Only an exact copy
       follows (same drop, same event) */
    for (int i = 0; i < nl; i++) {
        locs[i].leader = i;
        for (int j = 0; j < i && locs[i].hash; j++)
            if (locs[j].leader == j && locs[j].hash == locs[i].hash && !strcmp(sc[locs[j].scene].dir, sc[locs[i].scene].dir) &&
                !strcmp(locs[j].drop, locs[i].drop) && !strcmp(locs[j].event, locs[i].event)) { locs[i].leader = j; break; }
    }
    if (!s->scenes)   /* (no event goes with an item: a moved item's own scene would warp the player to its old chest) */
        for (int i = 0; i < nl; i++) locs[i].carry[0] = 0;
    rng_state = (uint64_t)s->seed * 0x9e3779b97f4a7c15ull + 12345;
    int tries = 0, checked = 0;
    if (s->chests) {
        for (;;) {   /* (a shuffle the game's logic says cannot be finished is dealt again) */
            shuffle_all(locs, nl, sc, nsc, s);
            tries++;
            const int ok = beatable(locs, nl, sc);
            if (ok < 0) break;
            checked = 1;
            if (ok) break;
            if (tries >= 2000) { fail(err, errn, "no beatable shuffle found (try other settings)", NULL); goto done; }
            for (int i = 0; i < nl; i++) {
                snprintf(locs[i].now, sizeof locs[i].now, "%s", locs[i].drop);
                snprintf(locs[i].now_ev, sizeof locs[i].now_ev, "%s", locs[i].event);
                snprintf(locs[i].now_gy, sizeof locs[i].now_gy, "%s", locs[i].gyaml);
                locs[i].placed = 0;
            }
        }
    }

    int changed = 0;
    FILE *sp = NULL;
    if (spoiler_path) sp = fopen(spoiler_path, "w");
    if (sp) fprintf(sp, "Triforce randomizer\nseed %u\nshuffle chests %d, dungeon items %d\n%d chests found\n\n", s->seed,
                    s->chests, s->dungeon_items, nl);
    if (sp && s->chests) fprintf(sp, checked ? "beatable by the logic: yes (shuffle %d)\n\n" : "beatable: not checked\n\n", tries);
    for (int a = 0; a < nsc; a++) {
        int here = 0;
        for (int i = 0; i < nl; i++) here += locs[i].scene == a && loc_changed(&locs[i]);
        if (!here) continue;
        if (apply_scene(&sc[a], a, locs, nl) < 0) { fail(err, errn, "out of memory", NULL); if (sp) fclose(sp); goto done; }
        size_t zn = 0;
        uint8_t *z = zstd_raw_frame(sc[a].b.d, sc[a].b.n, &zn);
        if (!z) { fail(err, errn, "out of memory", NULL); goto done; }
        snprintf(path, sizeof path, "%s/Banc/%s", out_romfs, sc[a].dir);
        mkdir_p(path);
        snprintf(path, sizeof path, "%s/%s", out_romfs, sc[a].rel);
        const int ok = write_file(path, z, zn);
        free(z);
        if (!ok) { fail(err, errn, "cannot write", path); if (sp) fclose(sp); goto done; }
        changed += here;
    }
    if (s->scenes && s->chests) {   /* (the scenes that go with the items play where the player is) */
        Buf zd = {0};
        size_t zat = 0, zlen = 0;
        if (sarc_find(&pack, "zs.zsdic", &zat, &zlen)) { zd.d = pack.d + zat; zd.n = zlen; patch_scenes(romfs, out_romfs, &zd, sp); }
    }
    if (sp) {
        for (int i = 0; i < nl; i++)
            if (loc_changed(&locs[i])) {
                fprintf(sp, "%s  chest %llu: %s -> %s\n", sc[locs[i].scene].name, (unsigned long long)locs[i].hash, locs[i].drop, locs[i].now);
                if (strcmp(locs[i].now_ev, locs[i].event))
                    fprintf(sp, "    event: %s -> %s\n", locs[i].event[0] ? locs[i].event : "(none)", locs[i].now_ev[0] ? locs[i].now_ev : "(none)");
            }
        fclose(sp);
    }
    result = changed;
done:
    for (int i = 0; i < nsc; i++) free(sc[i].b.d);
    free(sc);
    free(locs);
    free(pack.d);
    free(tmp.d);
    return result;
}

void rando_clear(const char *out_romfs) {
    char path[1024];
    snprintf(path, sizeof path, "%s/Banc", out_romfs);
    char **dirs;
    const int nd = list_dir(path, &dirs);
    for (int i = 0; i < nd; i++) {
        char sub[1024];
        snprintf(sub, sizeof sub, "%s/Banc/%s", out_romfs, dirs[i]);
        char **files;
        const int nf = list_dir(sub, &files);
        for (int j = 0; j < nf; j++) {
            if (!ends_with(files[j], ".bcett.byml.zs")) continue;
            snprintf(path, sizeof path, "%.700s/%.250s", sub, files[j]);
            remove(path);
        }
        free_list(files, nf);
    }
    free_list(dirs, nd);
    for (int i = 0; kScenes[i]; i++) {
        snprintf(path, sizeof path, "%s/Event/EventFlow/%s.bfevfl.zs", out_romfs, kScenes[i]);
        remove(path);
    }
}
