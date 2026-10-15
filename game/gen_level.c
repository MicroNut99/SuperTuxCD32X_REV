/*
 * SuperTux CD32X REV4 - OPTIONS - DRAW GENESIS: the level on the Genesis' planes (Sonic CD's way).
 *
 * The Genesis video chip has tile planes that scroll by register: a level on them costs no
 * drawing at all. REV2 / REV4 added the SH2 commands that reach it (62-67, cd32x/subcpu/main.c;
 * docs repo CD32X_COMMAND_LEDGER_ADDITIONS.md). Here:
 *   - plane A (VRAM map 0xE000): the level's tiles, from GENLVL.BIN (tools/stx_genesis.py
 *     genesis_level): palettes 0-2, the level's tiles, each picture's 2 x 2 map entries, the map
 *     as columns (identical ones once), the animations as groups of tiles whose frames are
 *     uploaded over them;
 *   - plane B (0xA000): the background (REV5: always in GENLVL.BIN, kind 1 picture / 0 gradient) - was the picture's GENBG1.BIN (palette 3) or, for a gradient
 *     level, the gradient inside GENLVL.BIN;
 *   - the 32X: only what moves (game/game.c gl_world), on a fixed 320 x 176 sprite layer in the
 *     canvas; everything else in it is colour 192 = 0x8000, the through bit: the Genesis shows
 *     there (32X manual "Priority"; REV3 proved it on the console).
 *
 * VRAM (the same numbers as tools/stx_genesis.py):
 *   0x0000 the boot text's font (tile 0 = blank)   0x1000 background tiles 128-575
 *   0x4800 level tiles 576-1279   0xA000 plane B map   0xB000 level tiles 1408-1535
 *   0xC000 the boot text's plane (never touched)   0xD000 sprite table (empty)
 *   0xD400 H scroll table (plane A, plane B)   0xE000 plane A map; both maps 64 x 32 cells
 *
 * Plane A is 64 Genesis columns (512 pixels) wide and the level up to 2084: the columns around the
 * view are kept loaded - plane column x & 63 holds level column x - and a column that comes near
 * the view is streamed in with command 67 (one per 8 pixels scrolled). The level is 30 Genesis
 * rows (240 lines): it fits the plane's 32 up / down, so up / down is only the scroll register.
 * A cell whose picture changed (a coin taken, a box emptied) gets its 4 map entries written
 * (command 63 from GENLVL.BIN's entry tables); a column streamed in again gets its changed cells
 * written after it.
 *
 * Timing: the 32X shows a frame at the vertical blank after it was handed over; the Genesis
 * shows a scroll value as soon as it is written. So the game loop calls gl_after_flip() right
 * after the flip it waited for - the frame now on screen was drawn for camera (cx, cy), and the
 * Genesis is set to the same camera at once (the first 12 lines are the 32X's border: a write a
 * little late lands there). Columns ahead of the view, animation frames and cell changes follow.
 */
#include "stx.h"
#include "yt.h"

#include <string.h>

#define PLANE_A   0xE000u
#define PLANE_B   0xA000u
#define HSCROLL   0xD400u
#define THROUGH   192                  /* a colour of the see-through range 192-255 = 0x8000 */
#define NCOLS     64
#define SLOTS1    704                  /* level tiles 576-1279, then 1408-1535 */
#define MAX_ERASE 96                   /* rectangles a buffer clears (more: the whole view) */

static uint32_t gl_off[9];             /* GENLVL.BIN's offsets (see tools/stx_genesis.py) */
static int gl_T, gl_R, gl_C, gl_rows, gl_U, gl_G, gl_gbn, gl_kind;
static const uint8_t *gl_seen;         /* the GENLVL.BIN uploaded (0: none) */
static unsigned gl_serial = 0xFFFFFFFFu;
static int gl_on;                      /* the Genesis display shows the level */
static int col_at[NCOLS];              /* plane column -> level column held (-1 none) */
static int grp_frame[16];
/* the camera of the frame handed over, and of the one on screen */
static int drawn_cx[2], drawn_cy[2], pend_cx, pend_cy, pend_valid, drew_level;
static uint16_t sh_a = 0xFFFF, sh_b = 0xFFFF, sv_a = 0xFFFF, sv_b = 0xFFFF;
/* the 32X sprite layer: per buffer what to clear next time */
typedef struct { int16_t x, y, w, h; } Box;
static Box erase[2][MAX_ERASE];
static int n_erase[2], full_clear[2] = { 1, 1 };
static int cell_q[32], n_cell_q;

static const uint8_t *G(uint32_t off) { return gl_level + off; }

/* REV6: the 32X picture in front of the Genesis' while the Genesis draws a level. 32X hardware
 * manual, VDP bitmap mode register (0x4100) bit 7 PRI: 0 "MEGA Drive has priority" (what the
 * game runs with - the Genesis display is off everywhere else), 1 "32X has priority"; a colour
 * with the through bit (0x8000, our see-through 192) is shown on the other side: with PRI 1,
 * behind the Genesis planes. REV4-REV6 ran levels with PRI 0: the Genesis planes in front, the
 * see-through colour in front of them as black, the sprites behind them (on the console: black,
 * with the level showing only inside Tux's and the snow's outlines). Game code, not the boot
 * path's platform layer (port/ is checked by tools/check_boot.py). */
#ifdef OJ32X_MARS
static void front_32x(int on)
{
	volatile uint16_t *const dispmode = (volatile uint16_t *)0x20004100;
	const uint16_t m = *dispmode;
	*dispmode = on ? (uint16_t)(m | 0x0080) : (uint16_t)(m & ~0x0080);
}
#else
static void front_32x(int on) { (void)on; }   /* the PC model composites with the 32X in front */
#endif

/* ---------------------------------------------------------------- the 32X: a sprite layer */
#ifndef OJ32X_MARS
#include <stdlib.h>
#define GL_REF (getenv("ST_GLREF") != 0)   /* PC check: every frame cleared whole, every column and
                                              changed cell written again - the reference */
#else
#define GL_REF 0
#endif

void gl_begin(int cx, int cy, int in_loop)
{
	yt_fixed();
	const int b = plat_back_index() & 1;
	if (GL_REF) full_clear[b] = 1;
	if (full_clear[b])
	{
		yt_fill(0, 0, YT_VIEW_W, YT_VIEW_H, THROUGH);
		full_clear[b] = 0;
	}
	else
		for (int i = 0; i < n_erase[b]; i++)
			yt_fill(erase[b][i].x, erase[b][i].y, erase[b][i].w, erase[b][i].h, THROUGH);
	n_erase[b] = 0;
	/* a frame drawn for a menu or the level's intro: words go over it - all of it next time */
	if (!in_loop) full_clear[b] = 1;
	drawn_cx[b] = cx;
	drawn_cy[b] = cy;
	drew_level = 1;
}

/* the rectangles drawn since yt_rect_log(1), widened to the level's 16-pixel cells (the tiles
 * drawn over them cover whole cells), to be cleared when this buffer is drawn next */
void gl_collect(int cx, int cy)
{
	const int b = plat_back_index() & 1;
	const int ox = cx % YT_TILE, oy = cy % YT_TILE;
	for (int i = 0; i < yt_rect_count(); i++)
	{
		if (n_erase[b] >= MAX_ERASE) { full_clear[b] = 1; return; }
		int x, y, w, h;
		yt_rect(i, &x, &y, &w, &h);
		const int x0 = ((x + ox) / YT_TILE) * YT_TILE - ox, y0 = ((y + oy) / YT_TILE) * YT_TILE - oy;
		const int x1 = ((x + w - 1 + ox) / YT_TILE + 1) * YT_TILE - ox, y1 = ((y + h - 1 + oy) / YT_TILE + 1) * YT_TILE - oy;
		/* + 2 pixels each side: Vic's loops draw from an even x (draw_clipsprite: x &= ~1) */
		Box *e = &erase[b][n_erase[b]++];
		e->x = (int16_t)(x0 - 2); e->y = (int16_t)y0; e->w = (int16_t)(x1 - x0 + 4); e->h = (int16_t)(y1 - y0);
	}
}

/* ---------------------------------------------------------------- the Genesis */
static unsigned slot_tile(int i) { return i < SLOTS1 ? 576u + (unsigned)i : 1408u + (unsigned)(i - SLOTS1); }

static void read_header(void)
{
	gl_T = be16(G(6)); gl_R = be16(G(8)); gl_C = be16(G(10)); gl_rows = be16(G(12));
	gl_U = be16(G(14)); gl_G = be16(G(16)); gl_gbn = be16(G(18)); gl_kind = be16(G(20));
	for (int i = 0; i < 9; i++) gl_off[i] = be32(G(24 + 4 * i));
	if (gl_G > 16) gl_G = 16;
}

/* level column x into plane column x & 63, then its changed cells */
static void put_cell(int c, int tx, int ty, int which_cols)
{
	const unsigned r = level_pic(c) >> 2;
	if ((int)r >= gl_R) return;
	const unsigned at = PLANE_A + (unsigned)(2 * ty) * 128u + (unsigned)((2 * tx) & (NCOLS - 1)) * 2u;
	if (which_cols == 3)
	{
		gen_cart_to_vram(at, G(gl_off[2] + 4 * r), 2);          /* top entries */
		gen_cart_to_vram(at + 128, G(gl_off[3] + 4 * r), 2);    /* bottom entries */
	}
	else
	{
		const unsigned k = which_cols == 2 ? 1 : 0, a = at + 2 * k;
		gen_vram_word(a, be16(G(gl_off[2] + 4 * r + 2 * k)));
		gen_vram_word(a + 128, be16(G(gl_off[3] + 4 * r + 2 * k)));
	}
}

static void load_col(int x)
{
	if (x < 0 || x >= gl_C) return;
	const int u = be16(G(gl_off[4] + 2 * (unsigned)x));
	gen_cart_to_vram_cols(PLANE_A + (unsigned)(x & (NCOLS - 1)) * 2u, G(gl_off[5] + (uint32_t)u * (uint32_t)gl_rows * 2u),
	                      (unsigned)gl_rows, 1);
	col_at[x & (NCOLS - 1)] = x;
	const int tx = x / 2;
	for (int ty = 0; ty < lh && ty < gl_rows / 2; ty++)
	{
		const int c = ty * lw + tx;
		if (level_pic(c) != level_pic_orig(c)) put_cell(c, tx, ty, (x & 1) ? 2 : 1);
	}
}

/* the columns around the view: 8 to the left, the view's 41, the rest of the 64 to the right */
static void stream(int cx, int max_cols)
{
	const int first = cx / 8 - 8, last = first + NCOLS - 1;
	int done = 0;
	/* the view's own columns first, then outwards */
	for (int d = 0; d < NCOLS && done < max_cols; d++)
	{
		const int x = cx / 8 + (d < 49 ? d : 48 - d);
		if (x < first || x > last || x < 0 || x >= gl_C) continue;
		if (col_at[x & (NCOLS - 1)] != x) { load_col(x); done++; }
	}
}

static void animate(uint32_t ms, int force)
{
	for (int k = 0; k < gl_G; k++)
	{
		const uint8_t *g = G(gl_off[6] + 12u * (unsigned)k);
		const unsigned speed = be16(g), nf = be16(g + 2), first = be16(g + 4), nt = be16(g + 6);
		if (!speed || !nf) continue;
		const int f = (int)((ms / speed) % nf);
		if (!force && f == grp_frame[k]) continue;
		grp_frame[k] = f;
		gen_cart_to_vram(first * 32u, G(be32(g + 8) + (uint32_t)f * nt * 32u), nt * 16u);
	}
}

static void scroll_to(int cx, int cy)
{
	const uint16_t ha = (uint16_t)(-cx & 0x3FF), va = (uint16_t)((cy - 12) & 0x3FF);
	uint16_t hb, vb;
	if (gl_kind == 1)
	{
		/* the picture as REV3: at the level's background speed, 0.8 x the size; the level's
		 * height mapped onto the 16 lines the 192-line picture can move */
		hb = (uint16_t)(-(cx * lbkgd_speed / 100 * 4 / 5) & 0x3FF);
		const int travel = lh * YT_TILE - YT_VIEW_H;
		int by = travel > 0 ? cy * 16 / travel : 0;
		if (by < 0) by = 0;
		if (by > 16) by = 16;
		vb = (uint16_t)((by - 12) & 0x3FF);
	}
	else
	{
		hb = (uint16_t)(-(cx * lbkgd_speed / 100) & 0x3FF);  /* a gradient: only up / down matters */
		vb = va;                                             /* tied to the level's lines */
	}
	if (ha != sh_a) { gen_vram_word(HSCROLL, ha); sh_a = ha; }
	if (va != sv_a) { gen_vsram(0, va); sv_a = va; }
	if (hb != sh_b) { gen_vram_word(HSCROLL + 2, hb); sh_b = hb; }
	if (vb != sv_b) { gen_vsram(2, vb); sv_b = vb; }
}

/* the level read: everything into VRAM with the display off */
static void upload(int cx, int cy)
{
	read_header();
	gen_vdp_reg(0x8134);               /* display off */
	gen_vdp_reg(0x8568);               /* sprite table 0xD000 (never written: empty) */
	gen_vdp_reg(0x8D35);               /* H scroll table 0xD400 */
	gen_vdp_reg(0x8238);               /* plane A 0xE000 */
	gen_vdp_reg(0x8405);               /* plane B 0xA000 */
	gen_cart_to_cram(0, G(gl_off[0]), 48);
	const int n1 = gl_T < SLOTS1 ? gl_T : SLOTS1;
	gen_cart_to_vram(slot_tile(0) * 32u, G(gl_off[1]), (unsigned)n1 * 16u);
	if (gl_T > SLOTS1) gen_cart_to_vram(slot_tile(SLOTS1) * 32u, G(gl_off[1] + (uint32_t)n1 * 32u), (unsigned)(gl_T - SLOTS1) * 16u);
	/* REV5: plane B (picture or gradient) is always in GENLVL.BIN */
	gen_cart_to_cram(48, G(gl_off[0] + 96), 16);
	gen_cart_to_vram(PLANE_B, G(gl_off[7]), 64 * 32);
	gen_cart_to_vram(0x1000, G(gl_off[8]), (unsigned)gl_gbn * 16u);
	for (int i = 0; i < NCOLS; i++) col_at[i] = -1;
	for (int k = 0; k < 16; k++) grp_frame[k] = -1;
	n_cell_q = 0;
	sh_a = sh_b = sv_a = sv_b = 0xFFFF;
	animate(game_ms, 1);
	stream(cx, NCOLS);
	scroll_to(cx, cy);
	front_32x(1);                 /* REV6: the 32X in front, its through-bit colour behind the planes */
	gen_vdp_reg(0x8174);               /* display on */
	gl_on = 1;
	gl_seen = gl_level;
	gl_serial = gen_bg_serial;
}

static void off(void)
{
	gen_vdp_reg(0x8134);               /* display off, md_init_hw's planes / sprites / scroll */
	gen_vdp_reg(0x8230);
	gen_vdp_reg(0x8407);
	gen_vdp_reg(0x8554);
	gen_vdp_reg(0x8D2B);
	front_32x(0);                 /* REV6: the priority the rest of the game runs with */
	gl_on = 0;
	full_clear[0] = full_clear[1] = 1; /* the canvas goes back to the tiler */
	yt_invalidate();
}

void gl_cell_changed(int c)
{
	if (!gl_level || !gl_on) return;
	for (int i = 0; i < n_cell_q; i++) if (cell_q[i] == c) return;
	if (n_cell_q < 32) cell_q[n_cell_q++] = c;
}

static int reload_all;
void gl_map_reset(void)
{
	for (int i = 0; i < NCOLS; i++) col_at[i] = -1;
	n_cell_q = 0;
	reload_all = 1;                    /* every column again at the next frame (a restart) */
	pend_valid = 0;                    /* REV6: not at the camera of a frame drawn before it */
}

/* from present_view: a level frame hands its camera over; anything else switches the Genesis off */
void gl_present(void)
{
	const int b = plat_back_index() & 1;
	if (gl_level && drew_level)
	{
		if (!gl_on || gl_seen != gl_level || gl_serial != gen_bg_serial)
			upload(drawn_cx[b], drawn_cy[b]);
		pend_cx = drawn_cx[b];
		pend_cy = drawn_cy[b];
		pend_valid = 1;
	}
	else if (gl_on)
		off();
	drew_level = 0;
}

/* from the game loop, right after the flip: the Genesis follows the frame now on screen */
void gl_after_flip(uint32_t ms)
{
	if (!gl_on || !pend_valid) return;
	scroll_to(pend_cx, pend_cy);
	if (GL_REF) for (int i = 0; i < NCOLS; i++) col_at[i] = -1;
	stream(pend_cx, reload_all || GL_REF ? NCOLS : 6);
	reload_all = 0;
	for (int i = 0; i < n_cell_q; i++)
	{
		const int c = cell_q[i], tx = c % lw, ty = c / lw;
		if (col_at[(2 * tx) & (NCOLS - 1)] == 2 * tx && col_at[(2 * tx + 1) & (NCOLS - 1)] == 2 * tx + 1)
			put_cell(c, tx, ty, 3);
	}
	n_cell_q = 0;
	animate(ms, 0);
}
