/*
 * SuperTux CD32X - a level: where it is in the RAM cart (and reading it there from the disc),
 * its picture layer for Vic's tiler, the tile rules (level.cpp, collision.cpp) and the changes
 * to the map while it is played (coins taken, boxes emptied, bricks broken: level->change).
 * See game/stx.h; the data format is in tools/stx_pack.py.
 *
 * R3 - THE CART AS A STORE OF LEVELS. Each level has a fixed place in the cart, its "home"
 * (HOMES.BIN, tools/stx_pack.py says why): the title's demo level is in IMAGE8.RAW (read at
 * power-on under the splash), world 1's first levels in area A (kept for good once read), the
 * others in area B, a ring where reading a level drops only those whose homes it overlaps.
 * in_cart[] remembers what is there, so a level played before is usually there already: no
 * disc read, no wait. The reading itself (cart_load: game/yt_mars.c, the boot path's commands
 * 38 + 46) reads only the level's own 32 KB pieces - not the splash picture that the platform
 * layer's plat_load_pack reads twice for its loading screen.
 *
 * R3 - ONE PICTURE A CELL. The tiler draws one layer: every cell's background, background-tm,
 * interactive-tm and foreground-tm merged into one picture by tools/stx_pack.py. When a cell's
 * interactive tile changes, the cell shows the picture made for the new tile (the variants
 * table); animated pictures (coins, water) change their resource's pixels (reslist) and the
 * cells in view that show one are marked for redrawing.
 *
 * Memory: the picture layer and the rules (SuperTux tile ids of interactive-tm) are copied into
 * SDRAM because they change (2 x 31 KB for the widest level, bonus 2's 15,630 cells); the rest
 * is read straight from the cart.
 */
#include "stx.h"

#define MAX_CELLS 15700
#define MAX_RES   2560                 /* tools/stx_pack.py MAX_RES */
#define NLEVELS   77
#define DEMO_LEVEL 76                  /* misc/menu.stl - the title's demo (game/game.c, always in the cart) */
#define PIECE     32768u
int lw, lh, ltrack, ltime, lgrav, lstart_x, lstart_y, lparticles, lback_scroll, lautoscroll, lbkgd_speed;
int level_n = -1;
fx grav_fx;
const uint8_t *lobjs;
int lnobjs;
const uint8_t *lresets;
int lnresets;
void (*level_progress)(int done, int total);

static uint16_t ia_ids[MAX_CELLS];     /* SuperTux tile ids of interactive-tm (the rules) */
static uint16_t pic_layer[MAX_CELLS];  /* the tiler's layer: YATSSD tile numbers */
static const uint8_t *level_bin;       /* LEVEL.BIN in the cart */
static const uint8_t *L_pic, *L_t1, *L_t2, *L_t3, *L_first, *L_anims, *L_frames, *L_vars;
static int n_res, n_anims, n_vars;
static const uint8_t *comp_px;         /* COMP.BIN's picture blocks */
static uint8_t *reslist[MAX_RES + 1];
static uint8_t res_anim[MAX_RES + 1];  /* resources of animated pictures */
static uint8_t anim_gen[2];            /* per frame buffer: the animation step it shows */
static uint32_t anim_step;
/* R5: each animation's frame now, and the frame each buffer last drew - a buffer redraws only
 * the cells of the animations that changed since (R3/R4: all animated cells in view whenever
 * any of them changed: levels full of water and coins drew 30-50 cells a frame for it) */
#define MAX_ANIMS 256                  /* the most in a level: 183 (stx_pack.py) */
static uint8_t anim_k[MAX_ANIMS], anim_seen[2][MAX_ANIMS];
/* bouncing bricks (at most 4 at once, objects.c MAX_BOB): their cells, for the moving-background
 * drawing, which draws the tiles one by one */
static int hidden_cells[8], n_hidden;

/* ------------------------------------------------------------------ the cart */
/* homes[0..76]: the levels; homes[77 + n]: background picture n (R4, the moving background) */
#define MAX_HOMES 96
typedef struct { uint8_t area, back; uint16_t first, n, fid; uint32_t flen; } Home;
static Home homes[MAX_HOMES];
static uint8_t in_cart[MAX_HOMES];
static int homes_read, n_homes;

static void read_homes(void)
{
	uint32_t size;
	const uint8_t *h = (const uint8_t *)text_file("HOMES.BIN", &size);
	if (size < 12 * NLEVELS) plat_fatal("HOMES.BIN: TOO SHORT");
	n_homes = (int)(size / 12);
	if (n_homes > MAX_HOMES) n_homes = MAX_HOMES;
	for (int i = 0; i < n_homes; i++, h += 12)
	{
		homes[i].area = h[0];
		homes[i].back = h[1];          /* a level's background picture + 1 (0: none); REV3: + 0x80
		                                  when it moves at its own speed */
		homes[i].first = be16(h + 2); homes[i].n = be16(h + 4); homes[i].fid = be16(h + 6);
		homes[i].flen = be32(h + 8);
	}
	homes_read = 1;
}

static uint32_t home_at(int li)
{
	return (homes[li].area == 1 ? 0x120000u : 0x280000u) + homes[li].first * PIECE;
}

static const uint8_t *pack_base(int li)
{
	if (homes[li].area == 0) return need("DEMO.PAK");
	return plat_cart_ptr(home_at(li));
}

/* a file inside one level's pack (OJPK version 2, port/pack.h) */
static const uint8_t *pk_file(const uint8_t *base, const char *name, uint32_t *size)
{
	if (memcmp(base, "OJPK", 4) || be32(base + 4) != 2) return 0;
	const uint32_t n = be32(base + 8);
	for (uint32_t i = 0; i < n; i++)
	{
		const uint8_t *e = base + 24 + 24 * i;
		if (!strncmp((const char *)e, name, 16))
		{
			if (size) *size = be32(e + 20);
			return base + be32(e + 16);
		}
	}
	return 0;
}

static int home_cached(int i)
{
	if (!homes_read) read_homes();
	if (i < 0 || i >= n_homes) return 0;
	return homes[i].area == 0 || in_cart[i];
}

static int homes_overlap(int i, int j)
{
	if (homes[i].area == 0 || homes[j].area == 0) return 0;
	const uint32_t a0 = home_at(i), a1 = a0 + homes[i].n * PIECE;
	const uint32_t b0 = home_at(j), b1 = b0 + homes[j].n * PIECE;
	return a0 < b1 && b0 < a1;
}

/* REV6: no background picture packs (R4's back_home is gone): each level's GENLVL.BIN has its plane B */

/* the level (and the background picture it moves) are in the cart: no disc read */
int level_cached(int li)
{
	return home_cached(li);           /* REV6: no picture packs */
}

/* a level's or a picture's pieces from the disc to its home; what it overwrites is gone */
static void fetch(int li)
{
	music(0);                          /* no CD data reads while CD audio plays (OpenLara lesson 4) */
	const Home *h = &homes[li];
	in_cart[li] = 0;
	for (int j = 0; j < n_homes; j++)
		if (j != li && in_cart[j] && homes_overlap(li, j)) in_cart[j] = 0;
	for (int tries = 0; tries < 3; tries++)
	{
		if (cart_load(h->fid, h->flen, h->first, h->n, level_progress)
		    && pk_file(pack_base(li), "GENLVL.BIN", 0))   /* REV6 */
		{
			in_cart[li] = 1;
			return;
		}
	}
	static char msg[40];
	put_str(put_dec(put_str(msg, li < NLEVELS ? "LEVEL " : "BACKGROUND "), (uint32_t)(li < NLEVELS ? li + 1 : li - NLEVELS), 3),
	        ": NOT READ FROM THE DISC");
	plat_fatal(msg);
}

/* ------------------------------------------------------------------ the level */
void level_palette(void)
{
	const uint8_t *c = comp_px - 128;
	for (int i = 0; i < 64; i++) palette[192 + i] = be16(c + 2 * i);
}

void level_reset_map(void)
{
	const int n = lw * lh;
	n_hidden = 0;
	for (int i = 0; i < n; i++)
	{
		pic_layer[i] = be16(L_pic + 2 * i);
		ia_ids[i] = be16(L_t2 + 2 * i);
	}
	yt_invalidate();
	gl_map_reset();                    /* REV4: the Genesis map columns are read again */
}

/* the moving background (R4): its picture in the cart, 0 when the level does not move one */
const uint8_t *back_px;
int back_w, back_h, back_stride;
/* REV3, OPTIONS - BACKGROUND GENESIS: the level was read with its picture on the Genesis' plane B
 * (GENBG.BIN, tools/stx_genesis.py) and its own pictures see-through where the picture shows
 * (LEVELG.BIN / COMPG.BIN, tools/stx_pack.py); 0 = drawn as before */
const uint8_t *gen_bg;
/* REV4, OPTIONS - DRAW GENESIS: the level read as Genesis tiles and map columns (GENLVL.BIN,
 * tools/stx_genesis.py genesis_level; REV5: its plane B - picture or gradient - inside it too);
 * 0 = drawn by the 32X (game/gen_level.c). gl_fell: Draw: Genesis is on but this level is
 * drawn by the 32X (the timing line shows DX) */
const uint8_t *gl_level;
int gl_fell;
unsigned gen_bg_serial;                /* + 1 each level read (the picture is uploaded again) */

void load_level(int li)
{
	/* REV6: the game is built from Genesis tiles - every level (the title's demo too) is its
	 * GENLVL.BIN (tiles, map columns, plane B) on the Genesis' planes and LEVELG.BIN / COMPG.BIN
	 * (the rules, the pictures the 32X draws over the moving things); nothing else */
	if (!home_cached(li)) fetch(li);
	back_px = 0;
	gen_bg = 0;
	gl_level = 0; gl_fell = 0;
	gen_bg_serial++;
	const uint8_t *base = pack_base(li);
	const uint8_t *g = pk_file(base, "GENLVL.BIN", 0);
	if (!g || memcmp(g, "STGL", 4) || !be16(g + 18)) plat_fatal("GENLVL.BIN: NO GENESIS LEVEL");
	gl_level = g;
	level_n = li;
	uint32_t csize;
	level_bin = pk_file(base, "LEVELG.BIN", 0);
	const uint8_t *comp = pk_file(base, "COMPG.BIN", &csize);
	if (!level_bin || !comp || memcmp(level_bin, "STX3", 4)) plat_fatal("LEVELG.BIN: NOT STX3");
	comp_px = comp + 128;
	lw = be16(level_bin + 4); lh = be16(level_bin + 6);
	ltrack = be16(level_bin + 8); ltime = be16(level_bin + 10); lgrav = be16(level_bin + 12);
	lstart_x = (int16_t)be16(level_bin + 14); lstart_y = (int16_t)be16(level_bin + 16);
	lparticles = level_bin[20]; lback_scroll = level_bin[21];
	lnobjs = be16(level_bin + 22);
	lautoscroll = be16(level_bin + 24);
	lbkgd_speed = be16(level_bin + 26);
	n_res = be16(level_bin + 28); n_anims = be16(level_bin + 30); n_vars = be16(level_bin + 32);
	const int n_frames = be16(level_bin + 36);
	if (lw * lh > MAX_CELLS) plat_fatal("LEVEL TOO BIG");
	if (n_res > MAX_RES) plat_fatal("LEVEL: TOO MANY TILE PICTURES");
	grav_fx = (fx)(((uint32_t)lgrav << 16) / 100u);
	const int cells = lw * lh;
	const uint8_t *p = level_bin + 40;
	L_pic = p; p += 2 * cells;
	L_t1 = p; p += 2 * cells;
	L_t2 = p; p += 2 * cells;
	L_t3 = p; p += 2 * cells;
	L_first = p; p += 2 * (n_res + 1);
	L_anims = p; p += 8 * n_anims;
	L_frames = p; p += 2 * n_frames;
	L_vars = p; p += 6 * n_vars;
	lobjs = p;
	lnresets = be16(lobjs + 6 * lnobjs);
	lresets = lobjs + 6 * lnobjs + 2;
	level_palette();
	reslist[0] = 0;
	for (int r = 1; r <= n_res; r++) reslist[r] = (uint8_t *)(comp_px + YT_TILE * YT_TILE * be16(L_first + 2 * r));
	memset(res_anim, 0, sizeof res_anim);
	for (int a = 0; a < n_anims; a++)
	{
		const int r = be16(L_anims + 8 * a);
		if (r <= n_res) res_anim[r] = 1;
	}
	level_reset_map();
	yt_level(pic_layer, lw, lh, reslist);
	anim_gen[0] = anim_gen[1] = 0xFF;
	anim_step = 0xFFFFFFFFu;
	memset(anim_seen, 0xFF, sizeof anim_seen);   /* no frame 255: each buffer draws them once */
}

/* animated pictures: their resource shows the frame of the time (Tile::draw: time / speed) */
void level_animate(uint32_t ms)
{
	const uint32_t step = ms / 25u;    /* SuperTux's global_frame_counter (25 ms) */
	if (step == anim_step) return;
	anim_step = step;
	int changed = 0;
	for (int a = 0; a < n_anims; a++)
	{
		const uint8_t *e = L_anims + 8 * a;
		const int r = be16(e), nf = be16(e + 2), speed = be16(e + 4), first = be16(e + 6);
		if (!speed || !nf) continue;
		const int k = (int)((ms / (uint32_t)speed) % (uint32_t)nf);
		if (a < MAX_ANIMS) anim_k[a] = (uint8_t)k;
		uint8_t *now = (uint8_t *)(comp_px + YT_TILE * YT_TILE * be16(L_frames + 2 * (first + k)));
		if (reslist[r] != now) { reslist[r] = now; changed = 1; }
	}
	if (changed) anim_gen[0]++, anim_gen[1]++;
}

static uint8_t anim_drawn[2] = { 0xFE, 0xFE };
void level_mark_animated(int cx, int cy)
{
	const int b = plat_back_index() & 1;
	if (anim_drawn[b] == anim_gen[b]) return;
	anim_drawn[b] = anim_gen[b];
	/* bit 1 of res_anim: this animation's picture changed since this buffer drew it */
	int any = 0;
	for (int a = 0; a < n_anims; a++)
	{
		const int r = be16(L_anims + 8 * a);
		if (r > n_res) continue;
		if (a >= MAX_ANIMS || anim_seen[b][a] != anim_k[a])
		{
			if (a < MAX_ANIMS) anim_seen[b][a] = anim_k[a];
			res_anim[r] |= 2;
			any = 1;
		}
	}
	if (!any) return;
	/* the whole canvas (view + the columns / lines around it): a cell kept outside the view
	 * with an old frame would come into view with it */
	int tx0, ty0, nx, ny;
	yt_canvas_tiles(&tx0, &ty0, &nx, &ny);
	(void)cx; (void)cy;
	for (int ty = ty0; ty < ty0 + ny && ty < lh; ty++)
		for (int tx = tx0; tx < tx0 + nx && tx < lw; tx++)
		{
			const uint16_t v = pic_layer[ty * lw + tx];
			if (res_anim[v >> 2] & 2) yt_mark_tile(tx, ty);
		}
	for (int a = 0; a < n_anims; a++)
	{
		const int r = be16(L_anims + 8 * a);
		if (r <= n_res) res_anim[r] &= 1;
	}
}

/* the layers' SuperTux tile ids at a cell (1 background-tm, 2 interactive-tm as it is now,
 * 3 foreground-tm): the game draws them again over the far snow / clouds and sprites */
int level_tile(int layer, int tx, int ty)
{
	if (tx < 0 || ty < 0 || tx >= lw || ty >= lh) return 0;
	const int c = ty * lw + tx;
	if (layer == 2) return ia_ids[c];
	return be16((layer == 1 ? L_t1 : L_t3) + 2 * c);
}

int tile_at(int x, int y)
{
	if (x < 0 || y < 0) return 0;
	const int tx = x >> 5, ty = y >> 5;
	if (tx >= lw || ty >= lh) return 0;
	return ia_ids[ty * lw + tx];
}
int flags_at(int x, int y) { const int t = tile_at(x, y); return t < ntiles ? tdesc[t].flags : 0; }
int level_goal_at(int x, int y) { return flags_at(x, y) & TF_GOAL; }

/* collision.cpp collision_object_map: a box at whole units, "1 pixel smaller" */
int box_hits(int bx, int by, int w, int h)
{
	for (int tx = (bx + 1) >> 5; tx * 32 < bx + w; tx++)
		for (int ty = (by + 1) >> 5; ty * 32 < by + h; ty++)
		{
			if (tx < 0 || ty < 0 || tx >= lw || ty >= lh) continue;
			const int t = ia_ids[ty * lw + tx];
			if (t < ntiles && (tdesc[t].flags & TF_SOLID)) return 1;
		}
	return 0;
}

/* the picture made for cell c with interactive tile t (tools/stx_pack.py variants: sorted by
 * cell, then tile); -1 if there is none */
static int variant(int c, int t)
{
	int lo = 0, hi = n_vars - 1;
	while (lo <= hi)
	{
		const int mid = (lo + hi) >> 1;
		const uint8_t *e = L_vars + 6 * mid;
		const int vc = be16(e), vt = be16(e + 2);
		if (vc == c && vt == t) return be16(e + 4);
		if (vc < c || (vc == c && vt < t)) lo = mid + 1;
		else hi = mid - 1;
	}
	return -1;
}

void level_change(int x, int y, int tile)
{
	if (x < 0 || y < 0 || (x >> 5) >= lw || (y >> 5) >= lh) return;
	const int c = (y >> 5) * lw + (x >> 5);
	if (tile >= ntiles) tile = 0;
	ia_ids[c] = (uint16_t)tile;
	const int v = variant(c, tile);
	if (v >= 0) { pic_layer[c] = (uint16_t)v; gl_cell_changed(c); }   /* the tiler sees the number change and redraws the cell; REV4: the Genesis map too */
#ifndef OJ32X_MARS
	else fprintf(stderr, "LEVEL %d: no picture for cell %d with tile %d\n", level_n, c, tile);
#endif
}

/* REV4 (game/gen_level.c): a cell's picture number now and as the level was read */
unsigned level_pic(int c) { return pic_layer[c]; }
unsigned level_pic_orig(int c) { return be16(L_pic + 2 * c); }

int level_hidden(int tx, int ty)
{
	const int c = ty * lw + tx;
	for (int i = 0; i < n_hidden; i++) if (hidden_cells[i] == c) return 1;
	return 0;
}

void level_hide(int tx, int ty, int hide)
{
	if (tx < 0 || ty < 0 || tx >= lw || ty >= lh) return;
	const int c = ty * lw + tx;
	if (hide && n_hidden < 8) hidden_cells[n_hidden++] = c;
	if (!hide)
		for (int i = 0; i < n_hidden; i++)
			if (hidden_cells[i] == c) { hidden_cells[i] = hidden_cells[--n_hidden]; break; }
	const int v = variant(c, hide ? 0 : ia_ids[c]);
	if (v >= 0) { pic_layer[c] = (uint16_t)v; gl_cell_changed(c); }
}

/* ------------------------------------------------------------------ the sweep */
/* collision.cpp collision_swept_object_map, axis by axis (x, then y): the box moves unit by unit
 * and stops at the last free one ("current = old - d; while (collision) current -= d") */
static int move_axis(fx *pos, fx target, int horizontal, int ox, int oy, int w, int h)
{
	const int from = FX_INT(*pos), to = FX_INT(target);
	if (from == to) { *pos = target; return 0; }
	const int d = to > from ? 1 : -1;
	for (int p = from + d; ; p += d)
	{
		const int hit = horizontal ? box_hits(p, oy, w, h) : box_hits(ox, p, w, h);
		if (hit) { *pos = (fx)(p - d) << 16; return 1; }
		if (p == to) break;
	}
	*pos = target;
	return 0;
}

int sweep(fx *x, fx *y, fx tx, fx ty, int w, int h)
{
	const int sx = move_axis(x, tx, 1, 0, FX_INT(*y), w, h);
	move_axis(y, ty, 0, FX_INT(*x), 0, w, h);
	return sx;
}
