/*
 * SuperTux CD32X - the 8 / 16 MB test builds (build.sh CART_MB=8|16): the 16MB-PCB-CD32X's paged
 * cart. Game code only; the one boot-stack addition it needs is command 68 (cd32x/subcpu/main.c,
 * docs repo CD32X_CART_PAGING_PROPOSAL.md, the merged form):
 *   - window 0 (cart 0x000000-0x1FFFFF, the program and IMAGE8) is never paged;
 *   - window 1 (0x200000-0x3FFFFF) shows page 1 (the 4 MB cart's upper half and the saves) or one
 *     of pages 2-7 (level packs, docs/CART_8MB_16MB_DESIGN.md);
 *   - after each page change both SH2s purge their caches;
 *   - the size probe writes markers (and their inverse) into save piece 15 of pages 1, 2 and 6
 *     with the existing commands 58 + 61 and reads them back (cart 0x3FF000 in window 1).
 * A 4 MB build (STX_CART_MB 4, the default) compiles none of this.
 */
#include "stx.h"
#include "cart.h"
#include "yt.h"
#include <string.h>

#if STX_CART_MB > 4

int plat_save_put_word(int index, int value);   /* port/plat_mars.c (CMD 58); game/host.c on the PC */
int plat_save_piece(int piece);                 /* port/plat_mars.c (CMD 61); game/host.c on the PC */
int plat_cart_page(int page);                   /* game/yt_mars.c (CMD 68); game/host.c on the PC */
const volatile uint8_t *plat_cart_through(uint32_t off);   /* the cart, not through the cache */

#define SCRATCH_PIECE 15                        /* free in the save-piece table (docs repo ledger) */
#define SCRATCH_OFF   (0x3F0000u + SCRATCH_PIECE * 4096u)

int cart_mb = 4;                                /* what the probe found (4 until it has run) */
static int cur_page = 1;                        /* window 1 after power-on: page 1 */

#ifdef STX_CART_ED
int cart_max_page(void) { return cart_mb >= 16 ? 6 : cart_mb >= 8 ? 3 : 1; }   /* ED: page 7 is the EverDrive's own */
#else
int cart_max_page(void) { return cart_mb >= 16 ? 7 : cart_mb >= 8 ? 3 : 1; }
#endif
int cart_page_now(void) { return cur_page; }

/* window 1 to page p; nothing is sent on a 4 MB cart (or a page it does not have) */
int cart_page(int p)
{
	if (p < 1 || p > cart_max_page()) p = 1;
	if (p == cur_page) return 1;
	if (!plat_cart_page(p)) return 0;
	cur_page = p;
	return 1;
}

static uint16_t marker(int page, int k)
{
	const uint16_t w[2] = { 0x5354, (uint16_t)(0x5000 | page) };   /* "ST", "P" + page */
	return k < 2 ? w[k] : (uint16_t)~w[k - 2];                        /* then the inverse */
}
static void write_marker(int page)
{
	for (int k = 0; k < 4; k++) plat_save_put_word(k, marker(page, k));
	plat_save_piece(SCRATCH_PIECE);
}
void plat_save_read(int piece, int off, uint8_t *dst, int n);   /* port/plat_mars.c: the cart, or the EverDrive's save bank */
static int read_marker(int shown)
{
	uint8_t p[8];
	if (shown == 1) plat_save_read(SCRATCH_PIECE, 0, p, 8);   /* ED-SAVE: page 1's piece is in the bank */
	else { const volatile uint8_t *c = plat_cart_through(SCRATCH_OFF); for (int k = 0; k < 8; k++) p[k] = c[k]; }
	for (int pg = 1; pg <= 7; pg++)
	{
		int same = 1;
		for (int k = 0; k < 4; k++)
			if ((uint16_t)(p[2 * k] << 8 | p[2 * k + 1]) != marker(pg, k)) same = 0;
		if (same) return pg;
	}
	return 0;
}

/* 4, 8 or 16: write markers on pages 1, 2 and 6, read them back. No mapper: every write lands
 * in the same place, page 1 shows page 6's marker. 8 MB: page 6 is page 2 again (the 8 MB
 * GreenPAK image) or has no RAM (the 16 MB image on a one-chip board). Window 1 ends on page 1. */
int cart_probe(void)
{
	cart_mb = 4;
	if (!plat_cart_page(1)) { cur_page = 1; return cart_mb; }
	write_marker(1);
	plat_cart_page(2); write_marker(2);
	plat_cart_page(6); write_marker(6);
	plat_cart_page(1);
	const int on1 = read_marker(1);
	plat_cart_page(6);
	const int on6 = read_marker(6);
	plat_cart_page(2);
	const int on2 = read_marker(2);
	plat_cart_page(1);
	cur_page = 1;
	if (on1 != 1) cart_mb = 4;
	else if (on6 != 6 || on2 == 6) cart_mb = 8;
	else if (on2 == 2) cart_mb = 16;
	if (cart_mb > STX_CART_MB) cart_mb = STX_CART_MB;   /* an 8 MB disc uses 8 MB of a 16 MB cart */
	return cart_mb;
}

/* REV11-16MB: the first start copies EVERY level from the disc into its own home in pages 1-7
 * (tools/stx_pack.py --cart-mb 16), with a screen and a bar; the cart keeps them (battery), so
 * later starts only check each level's sum (SUMS.BIN) and copy what is missing or damaged - and
 * no level shows a loading screen again. Only with the whole 16 MB: a smaller cart plays the
 * disc as before (its levels read when played). */
#if STX_CART_MB == 16 && !defined(STX_CART_ED)   /* ED: the levels are kept as the 8 MB build keeps them, pages 2-6 */
#define PRE_LEVELS 77
static uint8_t pre_todo[PRE_LEVELS];
static int pre_base, pre_total, pre_level, pre_levels;
static void pre_draw(int done)
{
	static char t[40];
	game_wait_flip();
	yt_screen();
	bg_picture();
	text_mid(F_GOLD, 40, "16 MB CART");
	text_mid(F_WHITE, 70, "COPYING THE LEVELS");
	text_mid(F_SMALL, 104, "ONLY THE FIRST TIME - AFTER THIS");
	text_mid(F_SMALL, 116, "NO MORE LOADING SCREENS");
	put_str(put_dec(put_str(put_dec(put_str(t, "LEVEL "), (uint32_t)pre_level, 1), " OF "), (uint32_t)pre_levels, 1), "");
	text_mid(F_SMALL, 150, t);
	static uint8_t full[8 * 4], empty[8 * 4];
	memset(full, col_yellow, sizeof full);
	memset(empty, col_black, sizeof empty);
	for (int i = 0; i < 24; i++)
		yt_sprite(64 + 8 * i, 171, 8, 4, pre_total && i * pre_total < done * 24 ? full : empty);
	panel_show("", "", "", col_white);
	present_view();
}
static void pre_progress(int d, int n) { (void)n; pre_draw(pre_base + d); }
void cart_preload(void)
{
	if (cart_mb < 16) return;
	pre_total = 0; pre_levels = 0;
	for (int li = 0; li < PRE_LEVELS; li++)
	{
		const int p = level_preload_pieces(li);
		pre_todo[li] = p > 255 ? 255 : (uint8_t)p;
		pre_total += p;
		if (p) pre_levels++;
	}
	if (!pre_total) return;
	pre_base = 0; pre_level = 0;
	pre_draw(0); pre_draw(0);
	level_progress = pre_progress;
	for (int li = 0; li < PRE_LEVELS; li++)
	{
		if (!pre_todo[li]) continue;
		pre_level++;
		level_preload(li);
		pre_base += pre_todo[li];
	}
	level_progress = 0;
	cart_page(1);
}
#else
void cart_preload(void) { }
#endif

/* the pack at a level's home is this disc's (SUMS.BIN: a sum over each pack, tools/stx_pack.py):
 * after a power-on the cart still holds what was copied in (battery-backed), but the game only
 * trusts what it checks - another disc's or a half-written pack is read again. Here, not in
 * game/level.c: level.c runs from SDRAM, whose bytes the start-up check needs for its heap. */
int cart_pack_valid(const uint8_t *b, uint32_t room, int li)
{
	uint32_t size;
	const uint8_t *sums = (const uint8_t *)text_file("SUMS.BIN", &size);
	if (!sums || size < 4u * (uint32_t)(li + 1) || memcmp(b, "OJPK", 4)) return 0;
	const uint32_t total = be32(b + 16);
	if (total == 0 || total > room) return 0;
	uint32_t sum = total;
	for (uint32_t k = 0; k + 4 <= total; k += 4) sum = sum * 31u + be32(b + k);
	return sum == be32(sums + 4 * li);
}

#endif
