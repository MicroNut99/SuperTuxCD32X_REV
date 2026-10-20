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

#if STX_CART_MB > 4

int plat_save_put_word(int index, int value);   /* port/plat_mars.c (CMD 58); game/host.c on the PC */
int plat_save_piece(int piece);                 /* port/plat_mars.c (CMD 61); game/host.c on the PC */
int plat_cart_page(int page);                   /* game/yt_mars.c (CMD 68); game/host.c on the PC */
const volatile uint8_t *plat_cart_through(uint32_t off);   /* the cart, not through the cache */

#define SCRATCH_PIECE 15                        /* free in the save-piece table (docs repo ledger) */
#define SCRATCH_OFF   (0x3F0000u + SCRATCH_PIECE * 4096u)

int cart_mb = 4;                                /* what the probe found (4 until it has run) */
static int cur_page = 1;                        /* window 1 after power-on: page 1 */

int cart_max_page(void) { return cart_mb >= 16 ? 7 : cart_mb >= 8 ? 3 : 1; }
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
static int read_marker(void)
{
	const volatile uint8_t *p = plat_cart_through(SCRATCH_OFF);
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
	const int on1 = read_marker();
	plat_cart_page(6);
	const int on6 = read_marker();
	plat_cart_page(2);
	const int on2 = read_marker();
	plat_cart_page(1);
	cur_page = 1;
	if (on1 != 1) cart_mb = 4;
	else if (on6 != 6 || on2 == 6) cart_mb = 8;
	else if (on2 == 2) cart_mb = 16;
	if (cart_mb > STX_CART_MB) cart_mb = STX_CART_MB;   /* an 8 MB disc uses 8 MB of a 16 MB cart */
	return cart_mb;
}

/* Options -> CART TEST: the probe, then a 4 KB pattern on each page (written with 58 + 61, read
 * back by the SH2), then page 1 again with the saves untouched. Lines of text for the screen. */
int cart_test(char lines[][32], int max)
{
	int n = 0;
	/* the saves (all 13 pieces in use, 0x3F0000-0x3FCFFF + SuperTux's 13) as a sum, not a copy:
	 * the 32X's work RAM has no 4 KB to spare (the start-up check needs its heap) */
	const volatile uint8_t *sv = plat_cart_through(0x3F0000u);
	cart_page(1);
	uint32_t before = 0;
	for (uint32_t i = 0; i < 14u * 4096u; i++) before = before * 31u + sv[i];
	const int mb = cart_probe();
	put_str(put_dec(put_str(put_dec(put_str(lines[n++], "CART "), (uint32_t)mb, 1), " MB (DISC "), STX_CART_MB, 1), " MB)");
	for (int p = 2; p <= cart_max_page() && n < max - 1; p++)
	{
		plat_cart_page(p);
		for (int k = 0; k < 2048; k++) plat_save_put_word(k, (uint16_t)(k * 0x9E37u + p * 0x1111u));
		const int wrote = plat_save_piece(SCRATCH_PIECE);
		const volatile uint8_t *q = plat_cart_through(SCRATCH_OFF);
		int bad = 0;
		for (int k = 0; k < 2048; k++)
			if ((uint16_t)(q[2 * k] << 8 | q[2 * k + 1]) != (uint16_t)(k * 0x9E37u + p * 0x1111u)) bad++;
		put_str(put_dec(put_str(lines[n++], "PAGE "), (uint32_t)p, 1), !bad && wrote ? " OK" : wrote ? " READ BACK BAD" : " WRITE FAILED");
	}
	plat_cart_page(1);
	cur_page = 1;
	uint32_t after = 0;
	for (uint32_t i = 0; i < 14u * 4096u; i++) after = after * 31u + sv[i];
	const int same = after == before;
	if (n < max) put_str(lines[n++], same ? "SAVES UNTOUCHED" : "SAVES CHANGED!");
	return n;
}

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
