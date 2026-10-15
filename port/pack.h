/*
 * OpenJazz CD32X - data packs in the RAM cart (J4c).
 *
 * The SH2 cannot write the cart (OpenLara lesson 1), so everything the game needs there is
 * prepared on the PC and copied in from the disc with the proven commands 38 + 46 (a pack is
 * the disc file IMAGE<n>.RAW; main.c puts it at cart slot n * 128 KB).
 *
 * A pack holds:
 *   - game files (PANEL.000, MENU.000, STARTUP.0SC ...), read in place through fmemopen
 *   - pictures already decoded by the PC build (the exact pixels the game builds at run time),
 *     found by width, height and content; the game uses the cart copy instead of keeping its
 *     own in SDRAM
 *
 * Layout (all numbers big-endian, all offsets from the start of the pack, 4-byte aligned):
 *   0   "OJPK"
 *   4   version (2)
 *   8   number of files      F
 *   12  number of pictures   P
 *   16  total size in bytes
 *   20  number of file pictures Q
 *   24  F file entries:    name[16] (upper case, NUL padded), offset, size       (24 bytes)
 *   ..  P picture entries: hash, width (16 bit), height (16 bit), offset         (12 bytes),
 *       sorted by hash
 *   ..  Q file pictures:   name[16], start, end (positions in that game file), width (16 bit),
 *       height (16 bit), offset of the picture                                   (32 bytes)
 *       = "the RLE picture at <start> in <name>, already decoded": the game skips to <end>
 *       instead of decoding it in SDRAM (64 KB for a full-screen picture)
 *   ..  data
 */
#ifndef OJ32X_PACK_H
#define OJ32X_PACK_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PACK_MAX 4

/* J5: level packs. Disc file OJ<k>.PAK (k = world * 10 + level, three digits) = command 38
 * number PACK_LEVEL_ID0 + k; copied to cart PACK_LEVEL_CART (slot 20). The common pack (cart
 * 0x100000) must therefore end before 0x280000 (1.5 MB) - build.sh checks both sizes. */
#define PACK_LEVEL_ID0   200
#define PACK_LEVEL_CART  0x280000u
#define PACK_LEVEL_MAX   0x170000u   /* up to 0x3F0000, where saves begin */
#define PACK_BONUS_K     800         /* J9: OJ800.PAK = the bonus levels (all BONUSMAP.* files) */

/* Makes a pack in memory (the cart) known to the game. false: not a pack. */
bool pack_mount(const uint8_t *base);
void pack_unmount_all(void);
/* J5: mount at a given place (0 = the common pack, 1 = the current level's pack); NULL removes
 * the last one */
bool pack_mount_at(int index, const uint8_t *base);
int  pack_count(void);

/* Game files: name without directories, any case. */
bool pack_file(const char *name, const uint8_t **data, uint32_t *size);
FILE *pack_fopen(const char *name);               /* read-only stream over the cart copy */
bool pack_exists(const char *name);               /* in a pack, or listed as on the disc */

/* Pictures: the cart copy of these exact pixels, or NULL. */
uint32_t pack_hash(const uint8_t *pixels, int w, int h);
const uint8_t *pack_picture(const uint8_t *pixels, int w, int h);

/* A picture stored at <start> in a game file (file pictures): the decoded copy, and where
 * the file continues after it; NULL if the pack does not have it. */
const uint8_t *pack_file_picture(const char *name, uint32_t start, int w, int h, uint32_t *end);

/* the same, any size: width and height come back */
const uint8_t *pack_file_picture_any(const char *name, uint32_t start, int *w, int *h, uint32_t *end);

/* true if p lies inside a mounted pack (such data is never freed) */
bool pack_contains(const void *p);

/* PC build only (port/pack_bake.c): T32X_BAKE=<file> records every game file the game opens
 * and every picture it builds, and writes them as one pack when the program ends. */
void pack_bake_file(const char *name, const char *path);
void pack_bake_picture(const uint8_t *pixels, int w, int h);
void pack_bake_file_picture(const char *name, uint32_t start, uint32_t end,
                            const uint8_t *pixels, int w, int h);

#ifdef __cplusplus
}
#endif

#endif
