/*
 * SuperTux CD32X - the layer driver for Vic's tiler (see game/yt.h for what is Vic's and what
 * is ours).
 *
 * yt_layer() below is YATSSD's draw_tile_layer() (yatssd/dtiles.c) for one layer without
 * parallax, line by line the same arithmetic, minus its hardware calls:
 *   - MARS_VDP_SHIFTREG = old_camera_x and Hw32xUpdateLineTable(): replaced by the values
 *     yt_scroll_x()/yt_scroll_y() hand to plat_present_scroll (the platform layer writes the
 *     line table, and the shift bit at the buffer swap - OJ32X_FLIP_ASYNC)
 *   - the slave hand-shake (while COMM4 != 0; COMM6 = 0; COMM4 = 3; ... COMM4 == 3): only
 *     "COMM6 = 0" stays (yt_tile_counter_reset) - draw_handle_layercmd() hands out the tiles
 *     through COMM6 (get_next_tile); with the master alone it draws every tile. COMM6 has no
 *     other user in this boot path (cd32x/subcpu/hw_md.s line 1307 is a nop - OpenLara's fix)
 *   - HwMd*() (Genesis planes): not used
 * R5: the cells are listed here and drawn with Vic's pixel loop on both SH2s (yt_layer below).
 * R10: the record of what the canvas holds is ours too ("bands", below) - Vic's dirty matrix in
 * extrafb is not used any more - and the canvas can be a vertical RING (OPTIONS - SCROLL).
 *
 * Built for the 32X (with yatssd/dtiles.c, dsprite.c, draw.c) and for the PC (with
 * game/yt_host.c, which stands in for those three).
 */
#include "yt.h"
#include "types.h"          /* yatssd/types.h: tilemap_t, dtilelayer_t, drawtilelayerscmd_t */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* ---- what YATSSD's own main.c / hw_32x.c define and its files use (yatssd/draw.h) ---- */
tilemap_t tm;                          /* the level (dsprite.c marks sprites in its matrix) */
int window_canvas_x, window_canvas_y;  /* the canvas offset of the view (draw_tile_layer sets it) */
int debug = 0;
int nodraw = 0;                        /* 0: draw (draw_inc.h) */
int32_t canvas_width = YT_VIEW_W;      /* hw_32x.c's values for Hw32xInit - here the view's */
int32_t canvas_height = YT_VIEW_H;
uint32_t canvas_pitch = YT_PITCH;
uint32_t canvas_yaw = YT_YAW;          /* places Vic's extrafb at 0x14C00 (yt.h) */

int draw_handle_layercmd(drawtilelayerscmd_t *cmd);
void draw_setScissor(int16_t x, int16_t y, int16_t w, int16_t h);
int draw_clip(int x, int y, int w, int h, rect_t *cliprect);
void draw_handle_drawspritecmd(drawsprcmd_t *cmd);
void plat_parallel(void (*fn)(void *, int), void *arg);   /* port/plat.h */
bool plat_dma_ok(void);                                   /* port/plat.h; REV: game/yt_mars.c */
void plat_dma_start(void *dst, const void *src, unsigned int bytes);
void plat_dma_wait(void);

static dtilelayer_t layer_desc[1];
/* YATSSD's global_tilemap_id. Starts at an unlikely value (each buffer's record starts with 0,
 * so the first frame of a buffer always draws everything). */
static uint16_t map_id = 0x5301;
static int cam_x, cam_y;               /* this frame's camera, clamped */

void yt_level(const uint16_t *layer, int w, int h, uint8_t **reslist)
{
	/* as init_tilemap() (dtiles.c), without its Genesis plane and line table calls. R3: ONE
	 * layer - tools/stx_pack.py merges a level's four into one picture a cell, so the record
	 * (one picture a cell) skips every cell that has not changed */
	memset(&tm, 0, sizeof tm);
	memset(layer_desc, 0, sizeof layer_desc);
	layer_desc[0].tiles = (uint16_t *)layer;
	layer_desc[0].parallax[0] = layer_desc[0].parallax[1] = 1 << 16;   /* moves with the camera */
	tm.id = map_id++;
	if (map_id == 0 || map_id == 0xFFFF) map_id = 0x5301;
	tm.tw = tm.th = YT_TILE;
	tm.layers = layer_desc;
	tm.numlayers = 1;
	tm.reslist = reslist;
	tm.tiles_hor = (unsigned)w;
	tm.tiles_ver = (unsigned)h;
	tm.canvas_tiles_hor = YT_PITCH / YT_TILE;          /* 24 */
	tm.canvas_tiles_ver = YT_ROWS / YT_TILE;           /* 12 */
	tm.numtiles = w * h;
	draw_setScissor(0, 0, YT_VIEW_W, YT_VIEW_H);
}

void yt_invalidate(void)
{
	tm.id = map_id++;
	if (map_id == 0 || map_id == 0xFFFF) map_id = 0x5301;
}

/* ================================================================== R10: the canvas and its record
 *
 * The canvas: frame buffer byte 0x200, 384 bytes a line, 200 lines (up to the panel at 0x12E00);
 * plat_present_scroll(pal, row0, xoff, 176) shows line (row0 + g) % 200 for view line g, from
 * column xoff. 24 columns of 16 pixels; the view needs 21 of them (320 + a partial one).
 *
 * Up / down, two ways (OPTIONS - SCROLL):
 *   CLASSIC (R1-R9, YATSSD's way): canvas line = view line + (cam_y % 16); cell row r of the
 *     canvas holds world row top_v + r at lines 16r..16r+15 (12 rows, 192 lines). When the
 *     camera crosses 16 lines the whole canvas has to follow (a re-anchor, below).
 *   RING (R10): world line L sits at canvas line L % 200, so row0 = cam_y % 200 and the
 *     platform layer's own "% 200" wraps the view round. Up / down needs NO re-anchor ever: the
 *     row coming in is drawn, nothing moves. A cell row starts at (16 ty) % 200 - always a
 *     multiple of 8 - and the row starting at line 192 is drawn in two parts (192-199, 0-7).
 *     (The line table never ends in 0xFF: line x 192 + xoff / 2 + 0x100 never reaches 0xFF in its
 *     low byte for xoff < 64 - the same reason as YATSSD's 384.)
 *
 * Sideways (both): column c of the canvas holds world column top_h + c; window x = cam_x -
 * 16 top_h must stay 0..63. top_h changes in whole cells (a re-anchor, below).
 *
 * THE RECORD ("bands"): what each 8-line band of each canvas column shows - the picture's
 * number x 2 + which half (0 top, 1 bottom), -1 when not known (a sprite was drawn over it, the
 * canvas was moved away, a new level). 25 bands x 24 columns a buffer, in SDRAM (R3-R9: Vic's
 * dirty matrix of 16-line cells in the frame buffer - slow to read, and 16-line cells cannot
 * follow the ring, whose rows start on 8-line steps). A cell is drawn when either of its two
 * bands does not hold its half; a sprite marks the bands under it.
 *
 * RE-ANCHOR (top_h changes, or top_v in CLASSIC): either MOVE the canvas's pixels and the record
 * by the same number of cells (both SH2s), or leave everything where it is - the record then
 * still says what each place shows, and the tiler draws only the cells whose picture differs
 * there (R10: R7's "redraw" marked every cell unknown first, so it drew all of them; on a plain
 * sky most cells now stay). Which way is cheaper is measured on the console (R7). Sideways the
 * re-anchor is done EARLY when it can be (anchor_x below): within the last 16 pixels before it
 * is needed, on a frame that drew few cells - not on top of a busy one. */
#define CANVAS_W   (YT_PITCH)
#define NCOLS      (YT_PITCH / YT_TILE)                /* 24 */
#define NBANDS     (YT_ROWS / 8)                       /* 25 */
#define CLASSIC_H  (YT_ROWS / YT_TILE * YT_TILE)       /* 192: CLASSIC's cell lines */

static int16_t bands[2][NBANDS * NCOLS];   /* per buffer: picture x 2 + half, -1 unknown */
static uint8_t kept[2][NBANDS * NCOLS];    /* per buffer: a kept picture (cloud) is in the band */
static uint8_t drawn[NBANDS * NCOLS];      /* this frame: the tiler drew the band (yt_watch_changed:
                                              a cell is drawn whole, also a band whose record was
                                              right already - its cloud pixels are gone too) */
static uint16_t buf_map[2];                /* per buffer: the tm.id its record belongs to */
static uint8_t buf_ring[2];                /* per buffer: drawn as RING (1) or CLASSIC (0) */
static int16_t buf_top_h[2], buf_top_v[2], buf_cam_x[2];
static uint16_t buf_epoch[2];              /* per buffer: + 1 when its pixels stopped matching the
                                              world where they are (a new record, or a re-anchor
                                              that left them in place) - kept pictures (clouds)
                                              are then drawn again whole (yt_epoch) */
static int cur;                            /* this frame's buffer (plat_back_index) */
static int ring;                           /* this frame: RING */
static int ring_want = 1;                  /* OPTIONS - SCROLL (game: yt_set_ring) */
static int full_frame;                     /* this frame redraws all of the view itself (yt_back) */
static int top_h, top_v;                   /* this frame's anchor (top_v: CLASSIC only) */
static int last_cells;                     /* cells drawn in the last frame (anchor_x: quiet?) */
static int frame_shift;                    /* REV: this frame's re-anchor: 0 none, 1 moved, 2 left */
static uint32_t frame_shift_us;            /* REV: ... and the move's microseconds */
static int dma_want;                       /* REV: OPTIONS - DRAW <DMA> (yt_set_dma) */
static int frame_dma_cells;                /* REV: cells the last frame drew through the DMA unit */
static int16_t *rec = bands[0];            /* bands[cur] */
static uint8_t *kp = kept[0];              /* kept[cur] */
int plat_back_index(void);                 /* port/plat.h */

void yt_set_ring(int on) { ring_want = on != 0; }
void yt_full_frame(int on) { full_frame = on; }
void yt_set_dma(int on) { dma_want = on != 0; }
int yt_dma_state(void) { return !dma_want ? 0 : plat_dma_ok() ? 1 : 2; }
int yt_frame_shift(uint32_t *us) { *us = frame_shift_us; return frame_shift; }
int yt_frame_dma_cells(void) { return frame_dma_cells; }

static int lines(void) { return ring ? YT_ROWS : CLASSIC_H; }
static int nbands(void) { return ring ? NBANDS : CLASSIC_H / 8; }
/* the canvas line of view line vy (vy may be below 0 or past the view, inside the canvas) */
static int line_of(int vy)
{
	const int l = window_canvas_y + vy;
	return ring ? ((l % YT_ROWS) + YT_ROWS) % YT_ROWS : l;
}
/* the canvas line where world cell row ty starts (CLASSIC: -1 when not in the canvas) */
static int row_line(int ty)
{
	if (ring) return (ty * YT_TILE) % YT_ROWS;
	const int r = ty - top_v;
	return r >= 0 && r < CLASSIC_H / YT_TILE ? r * YT_TILE : -1;
}

/* ------------------------------------------------------------------ moving the canvas */
typedef struct { int dx, dy, y0, y1, by_cols; } MoveJob;

static void move_lines(void *arg, int which)
{
	const MoveJob *m = (const MoveJob *)arg;
	uint8_t *canvas = yt_canvas();
	/* destination line y takes source line y + dy; inside it, x takes x + dx */
	int x0 = m->dx < 0 ? -m->dx : 0, x1 = m->dx > 0 ? CANVAS_W - m->dx : CANVAS_W;
	int a = m->y0, b = m->y1;
	if (m->by_cols)
	{
		/* R7: an up / down move (dx 0) shared by columns - each CPU moves every line, its half
		 * of the columns, in the safe order; the lines cannot be shared (they overlap) */
		const int mid = CANVAS_W / 2;
		if (which) x0 = mid; else x1 = mid;
	}
	else
	{
		const int half = (m->y0 + m->y1) / 2;
		if (which) a = half; else b = half;
	}
	const int up = m->dy >= 0;
	for (int k = a; k < b; k++)
	{
		const int y = up ? k : (a + b - 1 - k);
		const int sy = y + m->dy;
		if (sy < 0 || sy >= m->y1) continue;
		yt_move_row(canvas + y * CANVAS_W + x0, canvas + sy * CANVAS_W + x0 + m->dx, x1 - x0);
	}
}

/* R7: MOVE OR LEAVE, measured on the console: what a cell costs the tiler, and what each kind of
 * move costs (sideways; with up / down) - the cheaper way is taken, the move tried again now and
 * then to keep its time current. */
uint32_t plat_prof_us(void);
static uint32_t cell_us16;             /* microseconds a cell x 16, running average (0: unknown) */
static uint32_t move_us[2];            /* a move: [0] sideways only, [1] with up / down (0: unknown) */
static uint8_t leaves_since_move[2];

#ifndef OJ32X_MARS
#include <stdlib.h>
int yt_stat_reanchor[3];               /* PC only (ST_STATS): sideways, up/down, both */
#endif

/* how many view cells would have to be drawn if the canvas stays where it is under the new
 * anchor (their bands do not hold their picture there) */
static int cells_differing(void);

static void reanchor(int dc, int dr)
{
#ifndef OJ32X_MARS
	yt_stat_reanchor[dc && dr ? 2 : dr ? 1 : 0]++;
#endif
	const int nb = nbands();
	const int kind = dr != 0;
	int leave = full_frame || dc <= -NCOLS || dc >= NCOLS || dr * 2 <= -nb || dr * 2 >= nb;
	if (!leave && move_us[kind] && cell_us16 && leaves_since_move[kind] < 16
	    && cell_us16 * (uint32_t)cells_differing() / 16 < move_us[kind])
	{
		leave = 1;                     /* drawing the differing cells is cheaper (measured) */
		leaves_since_move[kind]++;
	}
#ifndef OJ32X_MARS
	{   /* PC checks: ST_REANCHOR=move / draw forces one way (both must give the same picture) */
		const char *e = getenv("ST_REANCHOR");
		if (e && !full_frame && dc > -NCOLS && dc < NCOLS && dr * 2 > -nb && dr * 2 < nb) leave = e[0] == 'd';
	}
#endif
	if (leave)
	{
		/* the pixels stay: the record still says what each place shows. Kept pictures (clouds)
		 * are not in the record - their bands become unknown, so they are drawn clean. */
		for (int i = 0; i < NBANDS * NCOLS; i++)
			if (kp[i]) { rec[i] = -1; kp[i] = 0; }
		buf_epoch[cur]++;
		frame_shift = 2;
		return;
	}
	leaves_since_move[kind] = 0;
	const uint32_t t0 = plat_prof_us();
	MoveJob m = { dc * YT_TILE, dr * YT_TILE, 0, lines(), 0 };
	if (dr == 0) plat_parallel(move_lines, &m);   /* the lines are independent: both SH2s */
	else if (dc == 0) { m.by_cols = 1; plat_parallel(move_lines, &m); }   /* R7: both SH2s */
	else if (dr > 0) { move_lines(&m, 0); move_lines(&m, 1); }   /* both ways at once: one SH2, */
	else { move_lines(&m, 1); move_lines(&m, 0); }                /* the halves in the safe order */
	/* (R10: R4-R9 did the upper half first also when the picture moved DOWN - the lower half then
	 * copied lines the upper half had already overwritten: a 16-line strip of old picture when
	 * the view went sideways and up in the same frame) */
	const uint32_t dt = plat_prof_us() - t0;
	move_us[kind] = move_us[kind] ? (move_us[kind] * 3 + dt) / 4 : dt;
	frame_shift = 1;
	frame_shift_us = dt;
	/* the record the same way: band (b, c) takes (b + 2 dr, c + dc); new ones unknown */
	static int16_t old[NBANDS * NCOLS];
	static uint8_t oldk[NBANDS * NCOLS];
	memcpy(old, rec, sizeof old);
	memcpy(oldk, kp, sizeof oldk);
	for (int b = 0; b < nb; b++)
		for (int c = 0; c < NCOLS; c++)
		{
			const int sc = c + dc, sb = b + 2 * dr;
			const int ok = sc >= 0 && sc < NCOLS && sb >= 0 && sb < nb;
			rec[b * NCOLS + c] = ok ? old[sb * NCOLS + sc] : -1;
			kp[b * NCOLS + c] = ok ? oldk[sb * NCOLS + sc] : 0;
		}
}

/* ------------------------------------------------------------------ the camera */
/* R10: the sideways anchor of this buffer - kept while the view fits (window x 0..63); moved
 * EARLY (shift 3 cells) when the view is in the last 16 pixels on its way and the last frame
 * was quiet, else when it has to (the view would leave the canvas). */
static int anchor_x(int old, int fresh)
{
	const int first = cam_x / YT_TILE;
	if (fresh) return first > 0 ? first - 1 : 0;            /* room both ways */
	const int wx = cam_x - old * YT_TILE;
	const int going = cam_x - buf_cam_x[cur];                /* + right, - left */
	if (wx > 63) return first;                               /* must: right */
	if (wx < 0) return first >= 3 ? first - 3 : 0;          /* must: left */
	const int quiet = last_cells < 16;
	if (quiet && going > 0 && wx >= 48) return first;       /* early: right */
	if (quiet && going < 0 && wx < 16 && old >= 3) return first >= 3 ? first - 3 : 0;   /* early: left */
	return old;
}

void yt_begin(int cx, int cy)
{
	const int maxx = (int)(tm.tiles_hor * tm.tw) - YT_VIEW_W;
	const int maxy = (int)(tm.tiles_ver * tm.th) - YT_VIEW_H;
	if (cx > maxx) cx = maxx;
	if (cy > maxy) cy = maxy;
	if (cx < 0) cx = 0;
	if (cy < 0) cy = 0;
	cam_x = cx;
	cam_y = cy;
	cur = plat_back_index() & 1;
	rec = bands[cur];
	kp = kept[cur];
	ring = ring_want;
	frame_shift = 0;
	frame_shift_us = 0;
	/* a new tile map, or the other scroll way: this buffer's record is void */
	const int fresh = buf_map[cur] != (uint16_t)tm.id || buf_ring[cur] != ring;
	if (fresh)
	{
		for (int i = 0; i < NBANDS * NCOLS; i++) rec[i] = -1;
		memset(kp, 0, NBANDS * NCOLS);
		buf_map[cur] = (uint16_t)tm.id;
		buf_ring[cur] = (uint8_t)ring;
		buf_epoch[cur]++;
	}
	top_h = anchor_x(buf_top_h[cur], fresh);
	top_v = ring ? 0 : cam_y / YT_TILE;
	window_canvas_x = cam_x - top_h * YT_TILE;
	window_canvas_y = ring ? cam_y % YT_ROWS : cam_y - top_v * YT_TILE;
	if (!fresh && (top_h != buf_top_h[cur] || (!ring && top_v != buf_top_v[cur])))
		reanchor(top_h - buf_top_h[cur], ring ? 0 : top_v - buf_top_v[cur]);
	buf_top_h[cur] = (int16_t)top_h;
	buf_top_v[cur] = (int16_t)top_v;
	buf_cam_x[cur] = (int16_t)cam_x;
	memset(drawn, 0, sizeof drawn);
}

int yt_scroll_x(void) { return window_canvas_x; }
int yt_scroll_y(void) { return window_canvas_y; }
int yt_window_x(void) { return window_canvas_x; }
unsigned yt_epoch(void) { return buf_epoch[cur]; }

/* ------------------------------------------------------------------ R4: the moving background
 * OPTIONS - BACKGROUND MOVES: the view filled from a picture (rows of `stride` bytes, pw x ph,
 * repeating) starting at picture pixel (sx, sy). One pixel of the start may move by one so the
 * copy can use 16-bit (or 32-bit) writes: the picture is far away, it does not show. */
void yt_back(const uint8_t *pic, int stride, int pw, int ph, int sx, int sy)
{
	uint8_t *canvas = yt_canvas();
	sx %= pw; if (sx < 0) sx += pw;
	sy %= ph; if (sy < 0) sy += ph;
	if ((sx ^ window_canvas_x) & 1) sx = (sx + 1) % pw;
	for (int y = 0; y < YT_VIEW_H; y++)
	{
		const uint8_t *row = pic + ((sy + y) % ph) * stride;
		uint8_t *d = canvas + line_of(y) * CANVAS_W + window_canvas_x;
		int n1 = pw - sx;
		if (n1 > YT_VIEW_W) n1 = YT_VIEW_W;
		yt_copy_row(d, row + sx, n1);
		if (n1 < YT_VIEW_W) yt_copy_row(d + n1, row, YT_VIEW_W - n1);
	}
}

/* ------------------------------------------------------------------ the tiler
 * R5: the cells to draw this frame, listed by the master in one pass over the view (a cell is
 * drawn when its bands do not hold its picture), then drawn by both SH2s - each takes every
 * second cell of the list. With only a few cells the second SH2 is not started. Each cell is
 * drawn with Vic's pixel loop (draw_handle_drawspritecmd, what his draw_drawtile does for the
 * base layer). R3's way - Vic's draw_handle_layercmd on both CPUs, a TAS lock and COMM6 for
 * every one of the ~260 view cells - is gone with Vic's dirty matrix in R10. */
typedef struct { int16_t x, y; int16_t idx; int16_t pad; } YtCell;
#define YT_MAX_CELLS (NCOLS * (YT_VIEW_H / YT_TILE + 2))
static YtCell cell_list[YT_MAX_CELLS];
#define YT_SOLO_CELLS 4                /* up to this many: the master alone (no slave start) */

static void draw_part(const uint8_t *px, int flags, int x, int y, int sy, int h)
{
	drawsprcmd_t cmd;
	cmd.flags = (uint16_t)flags;           /* base layer: drawmode 0 | the tile's flip bits */
	cmd.x = (uint16_t)x; cmd.y = (uint16_t)y;
	cmd.w = YT_TILE; cmd.h = (uint16_t)h;
	cmd.sx = 0; cmd.sy = (int16_t)sy;
	cmd.sw = YT_TILE; cmd.sh = YT_TILE;
	cmd.sdata = (void *)px;
	cmd.scale = 0;
	cmd.stride = 0;
	draw_handle_drawspritecmd(&cmd);
}

static void draw_cell(const YtCell *c)
{
	const uint8_t *px = ((uint8_t **)tm.reslist)[c->idx >> 2];
	if (!px) return;
	if (c->y + YT_TILE <= YT_ROWS) draw_part(px, c->idx & 3, c->x, c->y, 0, YT_TILE);
	else
	{
		/* RING: the cell row starting at line 192 - its top half at 192-199, its bottom at 0-7 */
		const int top = YT_ROWS - c->y;
		draw_part(px, c->idx & 3, c->x, c->y, 0, top);
		draw_part(px, c->idx & 3, c->x, 0, top, YT_TILE - top);
	}
}

typedef struct { int n; } CellsJob;
static void cells_job(void *arg, int which)
{
	const int n = ((const CellsJob *)arg)->n;
	for (int i = which & 1; i < n; i += 2) draw_cell(&cell_list[i]);
}

/* ------------------------------------------------------------------ REV: OPTIONS - DRAW <DMA>
 * The cells the tiler draws in a frame often come as ROWS: a row coming in at the top or the
 * bottom (21 cells side by side), everything after a re-anchor that left the canvas in place,
 * a new level. Such a row of adjacent cells is the same picture line after line, 16 pixels from
 * each cell: the master composes one canvas line of the row in SDRAM (yt_dma_line) and the DMA
 * unit copies it into the canvas while the master composes the next (game/yt_mars.c, the
 * OpenTyrian addendum's "DMA for opaque lines"). Rows of fewer than DMA_RUN_MIN cells, and the
 * single cells of a column coming in at the side, are drawn as before with Vic's pixel loop -
 * by the slave while the master runs its rows, and by the master too when the rows are fewer
 * cells than half of all (a split by count; the timing line shows which way the console likes).
 * The pictures are the same either way: the PC's DMA is a memcpy, ST_TRUTH compares them. */
#define DMA_RUN_MIN 3
typedef struct { int16_t first, n; } CellRun;
static CellRun runs[YT_MAX_CELLS];
static int16_t singles[YT_MAX_CELLS];
static int nruns, nsingles, master_singles;

/* a cell the DMA rows can take: an unflipped picture, 4-byte aligned (stx_pack.py's blocks are) */
static const uint8_t *row_px(const YtCell *c)
{
	const uint8_t *px = ((uint8_t **)tm.reslist)[c->idx >> 2];
	return px && !(c->idx & 3) && !((uintptr_t)px & 3) ? px : 0;
}

/* the list (yt_layer: world rows top to bottom, columns left to right) into rows and singles;
 * returns the cells in rows */
static int dma_split(int n)
{
	nruns = nsingles = 0;
	int in_rows = 0;
	for (int i = 0; i < n; )
	{
		int j = i + 1;
		if (row_px(&cell_list[i]))
			while (j < n && cell_list[j].y == cell_list[i].y
			       && cell_list[j].x == cell_list[j - 1].x + YT_TILE && row_px(&cell_list[j])) j++;
		if (j - i >= DMA_RUN_MIN)
		{
			runs[nruns].first = (int16_t)i;
			runs[nruns].n = (int16_t)(j - i);
			nruns++;
			in_rows += j - i;
		}
		else for (int k = i; k < j; k++) singles[nsingles++] = (int16_t)k;
		i = j;
	}
	const int half = n / 2;
	master_singles = in_rows < half ? half - in_rows : 0;
	if (master_singles > nsingles) master_singles = nsingles;
	return in_rows;
}

static void draw_rows(void)
{
	uint8_t *canvas = yt_canvas();
	int slot = 0;
	for (int r = 0; r < nruns; r++)
	{
		const YtCell *c0 = &cell_list[runs[r].first];
		const int nc = runs[r].n;
		const uint32_t *src[NCOLS];
		for (int i = 0; i < nc; i++) src[i] = (const uint32_t *)row_px(&c0[i]);
		for (int k = 0; k < YT_TILE; k++)
		{
			/* line k of every cell of the row, side by side (explicit words: no memcpy call) */
			volatile uint32_t *d = (volatile uint32_t *)yt_dma_line(slot);
			for (int i = 0; i < nc; i++, d += 4)
			{
				const uint32_t *p = src[i] + k * (YT_TILE / 4);
				d[0] = p[0]; d[1] = p[1]; d[2] = p[2]; d[3] = p[3];
			}
			int l = c0->y + k;
			if (l >= YT_ROWS) l -= YT_ROWS;          /* RING: the row starting at line 192 wraps */
			/* waits for the line before (composed into the other buffer), then starts this one */
			plat_dma_start(canvas + l * CANVAS_W + c0->x, yt_dma_line(slot), (unsigned)(nc * YT_TILE));
			slot ^= 1;
		}
	}
	plat_dma_wait();
}

static void dma_job(void *arg, int which)
{
	(void)arg;
	if (which == 0)
	{
		draw_rows();
		for (int i = 0; i < master_singles; i++) draw_cell(&cell_list[singles[i]]);
	}
	else for (int i = master_singles; i < nsingles; i++) draw_cell(&cell_list[singles[i]]);
}

/* the view's cells: world columns tx0..tx1, rows ty0..ty1 */
static void view_cells(int *tx0, int *tx1, int *ty0, int *ty1)
{
	*tx0 = cam_x / YT_TILE;
	*tx1 = (cam_x + YT_VIEW_W - 1) / YT_TILE;
	*ty0 = cam_y / YT_TILE;
	*ty1 = (cam_y + YT_VIEW_H - 1) / YT_TILE;
	if (*tx1 >= (int)tm.tiles_hor) *tx1 = (int)tm.tiles_hor - 1;
	if (*ty1 >= (int)tm.tiles_ver) *ty1 = (int)tm.tiles_ver - 1;
}

static int cells_differing(void)
{
	/* under the NEW anchor (top_h / top_v / window set by yt_begin before reanchor) */
	if (!tm.layers) return 0;
	const int16_t *tiles = (const int16_t *)tm.layers[0].tiles;
	const int nb = nbands();
	int tx0, tx1, ty0, ty1, n = 0;
	view_cells(&tx0, &tx1, &ty0, &ty1);
	for (int ty = ty0; ty <= ty1; ty++)
	{
		const int l = row_line(ty);
		if (l < 0) { n += tx1 - tx0 + 1; continue; }
		const int b0 = l / 8, b1 = (b0 + 1) % nb;
		for (int tx = tx0; tx <= tx1; tx++)
		{
			const int c = tx - top_h;
			if (c < 0 || c >= NCOLS) { n++; continue; }
			const int16_t v = (int16_t)(tiles[ty * (int)tm.tiles_hor + tx] * 2);
			if (rec[b0 * NCOLS + c] != v || rec[b1 * NCOLS + c] != v + 1) n++;
		}
	}
	return n;
}

static int yt_layer(int layer)
{
	if (layer != 0 || !tm.layers) return 0;  /* R3 on: one merged layer */
	const int16_t *tiles = (const int16_t *)tm.layers[0].tiles;
	const int nb = nbands();
	int tx0, tx1, ty0, ty1, n = 0;
	view_cells(&tx0, &tx1, &ty0, &ty1);
	for (int ty = ty0; ty <= ty1; ty++)
	{
		const int l = row_line(ty);
		if (l < 0) continue;
		const int b0 = l / 8, b1 = (b0 + 1) % nb;
		int16_t *r0 = rec + b0 * NCOLS, *r1 = rec + b1 * NCOLS;
		for (int tx = tx0; tx <= tx1; tx++)
		{
			const int c = tx - top_h;
			if (c < 0 || c >= NCOLS) continue;
			const int16_t idx = tiles[ty * (int)tm.tiles_hor + tx];
			const int16_t v = (int16_t)(idx * 2);
			if ((r0[c] != v || r1[c] != v + 1) && n < YT_MAX_CELLS)
			{
				r0[c] = v; r1[c] = (int16_t)(v + 1);
				kp[b0 * NCOLS + c] = kp[b1 * NCOLS + c] = 0;
				drawn[b0 * NCOLS + c] = drawn[b1 * NCOLS + c] = 1;
				cell_list[n].x = (int16_t)(c * YT_TILE);
				cell_list[n].y = (int16_t)l;
				cell_list[n].idx = idx;
				n++;
			}
		}
	}
	CellsJob job;
	job.n = n;
	const uint32_t t0 = plat_prof_us();
	frame_dma_cells = 0;
	if (n <= YT_SOLO_CELLS) { cells_job(&job, 0); cells_job(&job, 1); }
	else if (dma_want && (frame_dma_cells = dma_split(n)) > 0) plat_parallel(dma_job, 0);   /* REV */
	else plat_parallel(cells_job, &job);
	if (n >= 32)                       /* R7: what a cell costs (for reanchor's choice) */
	{
		const uint32_t per16 = (plat_prof_us() - t0) * 16u / (uint32_t)n;
		cell_us16 = cell_us16 ? (cell_us16 * 3 + per16) / 4 : per16;
	}
	last_cells = n;
	return n;
}

int yt_layers(int first, int last)
{
	int n = 0;
	for (int l = first; l <= last && l < (int)tm.numlayers; l++)
		n += yt_layer(l);
	return n;
}

/* ------------------------------------------------------------------ marks
 * the bands under canvas columns x..x+w-1 and canvas lines l..l+h-1 (wrapping in RING) */
static void mark_lines(int x, int l, int w, int h, int keep)
{
	if (w <= 0 || h <= 0) return;
	const int nb = nbands();
	int c0 = x / YT_TILE, c1 = (x + w - 1) / YT_TILE;
	if (c0 < 0) c0 = 0;
	if (c1 >= NCOLS) c1 = NCOLS - 1;
	const int nband = (l % 8 + h + 7) / 8;
	for (int k = 0; k < nband && k < nb; k++)
	{
		int b = l / 8 + k;
		if (ring) b %= nb;
		else if (b >= nb) break;
		for (int c = c0; c <= c1; c++)
		{
			if (keep) kp[b * NCOLS + c] = 1;   /* a kept picture: the record stays, the band is flagged */
			else rec[b * NCOLS + c] = -1;
		}
	}
}

static int rect_log_on, rect_n;
static int no_mark;                    /* R5: yt_keep(1) - draw without marking (kept clouds) */
/* R5: kept pictures are drawn into the whole canvas, not only the view: a cloud cut by the edge
 * of the view would otherwise have to be drawn again at every step of the view (the part that
 * scrolls in). The canvas around the view keeps what is drawn in it until the tiler draws there
 * again (then the watch notices) or a re-anchor drops it. Up / down the kept area is the cell rows
 * the tiler covers (view top rounded down to a cell, 192 lines) - in CLASSIC the whole canvas. */
static int kb_x0, kb_y0, kb_x1, kb_y1;  /* the box kept pictures are cut to (view pixels) */
static int keep_top(void) { return -(cam_y % YT_TILE); }
void yt_keep_box(int vx, int vy, int w, int h)
{
	const int ky0 = keep_top(), ky1 = ky0 + CLASSIC_H;
	kb_x0 = vx < -window_canvas_x ? -window_canvas_x : vx;
	kb_y0 = vy < ky0 ? ky0 : vy;
	kb_x1 = vx + w > YT_PITCH - window_canvas_x ? YT_PITCH - window_canvas_x : vx + w;
	kb_y1 = vy + h > ky1 ? ky1 : vy + h;
	if (kb_x1 < kb_x0) kb_x1 = kb_x0;
	if (kb_y1 < kb_y0) kb_y1 = kb_y0;
	draw_setScissor((int16_t)kb_x0, (int16_t)kb_y0, (int16_t)(kb_x1 - kb_x0), (int16_t)(kb_y1 - kb_y0));
}
void yt_keep(int on)
{
	no_mark = on;
	if (on) yt_keep_box(-window_canvas_x, keep_top(), YT_PITCH, CLASSIC_H);
	else draw_setScissor(0, 0, YT_VIEW_W, YT_VIEW_H);
}

/* R5: the bands under a view rectangle (inside the kept area): their numbers into ids[] (at most
 * max), returns how many. A band's number: band x 24 + column. */
static int bands_under(int vx, int vy, int w, int h, int16_t *ids, int max)
{
	if (!tm.tw || w <= 0 || h <= 0) return 0;
	const int ky0 = keep_top(), ky1 = ky0 + CLASSIC_H;
	int x0 = window_canvas_x + vx, x1 = x0 + w - 1;
	int y0 = vy, y1 = vy + h - 1;
	if (x0 < 0) x0 = 0;
	if (x1 >= CANVAS_W) x1 = CANVAS_W - 1;
	if (y0 < ky0) y0 = ky0;
	if (y1 >= ky1) y1 = ky1 - 1;
	if (x0 > x1 || y0 > y1) return 0;
	const int nb = nbands();
	int n = 0;
	/* view line y0 lies in the band of canvas line line_of(y0); count the bands to y1 */
	const int l0 = line_of(y0);
	const int nband = (l0 % 8 + (y1 - y0 + 1) + 7) / 8;
	for (int k = 0; k < nband; k++)
	{
		int b = l0 / 8 + k;
		if (ring) b %= nb;
		else if (b >= nb) break;
		for (int c = x0 / YT_TILE; c <= x1 / YT_TILE; c++)
			if (n < max) ids[n++] = (int16_t)(b * NCOLS + c);
	}
	return n;
}

void yt_mark_view_rect(int vx, int vy, int w, int h)
{
	int16_t ids[96];
	const int n = bands_under(vx, vy, w, h, ids, 96);
	for (int i = 0; i < n; i++) { rec[ids[i]] = -1; kp[ids[i]] = 0; }
}

int yt_watch(int vx, int vy, int w, int h, int16_t *ids, int16_t *vals, int max)
{
	const int n = bands_under(vx, vy, w, h, ids, max);
	for (int i = 0; i < n; i++) vals[i] = rec[ids[i]];
	return n;
}

int yt_watch_changed(const int16_t *ids, const int16_t *vals, int n, uint8_t *cells)
{
	int k = 0;
	for (int i = 0; i < n; i++) if (rec[ids[i]] != vals[i] || drawn[ids[i]]) { cells[ids[i]] = 1; k++; }
	return k;
}

/* a band's box in view pixels (16 x 8), inside the kept area */
void yt_cell_box(int id, int *vx, int *vy, int *w, int *h)
{
	const int b = id / NCOLS, c = id % NCOLS;
	*vx = c * YT_TILE - window_canvas_x;
	int d = b * 8 - window_canvas_y;         /* canvas line -> view line */
	if (ring)
	{
		d %= YT_ROWS;
		if (d < 0) d += YT_ROWS;
		if (d >= keep_top() + CLASSIC_H) d -= YT_ROWS;
	}
	*vy = d;
	*w = YT_TILE;
	*h = 8;
}

/* the world tile a band belongs to now (the tiles drawn over clouds) */
void yt_cell_tile(int id, int *tx, int *ty)
{
	int x, y, w, h;
	yt_cell_box(id, &x, &y, &w, &h);
	*tx = top_h + id % NCOLS;
	*ty = (cam_y + y) / YT_TILE;
}

/* the world tiles the canvas holds now (level.c marks animated cells in all of them) */
void yt_canvas_tiles(int *tx0, int *ty0, int *nx, int *ny)
{
	*tx0 = top_h;
	*nx = NCOLS;
	*ty0 = cam_y / YT_TILE;
	*ny = YT_VIEW_H / YT_TILE + 1;
}

static int16_t rect_xy[YT_RECTS][4];

void yt_rect_log(int on) { rect_log_on = on; if (on) rect_n = 0; }
int yt_rect_count(void) { return rect_n; }
void yt_rect(int i, int *x, int *y, int *w, int *h)
{
	*x = rect_xy[i][0]; *y = rect_xy[i][1]; *w = rect_xy[i][2]; *h = rect_xy[i][3];
}

#ifndef OJ32X_MARS
uint32_t yt_stat_spr, yt_stat_px;      /* PC only (ST_STATS): sprites and their pixels a frame */
#define STAT(n, px) (yt_stat_spr += (n), yt_stat_px += (uint32_t)(px))
#else
#define STAT(n, px) ((void)0)
#endif

/* a sprite at view coordinates: Vic's draw_clip and pixel loop (draw_handle_drawspritecmd,
 * overwrite mode: colour 0 see-through), as his draw_sprite / draw_clipsprite do it - and the
 * bands under it are marked (R3: Vic's draw_dirtyrect marked whole columns from line y / 128).
 * Vic's pixel loops count down an unsigned "units / 4" with do-while: fewer than 4 units after
 * clipping (8 pixels in the 16-bit loops) and the count wraps (Sonic32 T7, VIC_YATSSD.md 6), so
 * a sprite with less than 8 x 2 pixels on the screen is copied here a byte at a time (R4).
 * RING: a sprite over canvas line 199 is drawn in two parts (to 199, from 0). */
int yt_sprite(int vx, int vy, int w, int h, const uint8_t *pixels)
{
	/* the area a sprite may cover: the view; kept pictures (yt_keep): the kept box */
	const int bx0 = no_mark ? kb_x0 : 0, bx1 = no_mark ? kb_x1 : YT_VIEW_W;
	const int by0 = no_mark ? kb_y0 : 0, by1 = no_mark ? kb_y1 : YT_VIEW_H;
	const int x0 = vx < bx0 ? bx0 : vx, x1 = vx + w > bx1 ? bx1 : vx + w;
	const int y0 = vy < by0 ? by0 : vy, y1 = vy + h > by1 ? by1 : vy + h;
	if (x1 <= x0 || y1 <= y0) return 0;
	if (x1 - x0 < 8 || y1 - y0 < 2)
	{
		uint8_t *canvas = yt_canvas();
		for (int y = y0; y < y1; y++)
		{
			const uint8_t *s = pixels + (y - vy) * w + (x0 - vx);
			uint8_t *d = canvas + line_of(y) * CANVAS_W + window_canvas_x + x0;
			for (int x = 0; x < x1 - x0; x++) if (s[x]) d[x] = s[x];
		}
		mark_lines(window_canvas_x + x0, line_of(y0), x1 - x0, y1 - y0, no_mark);
		STAT(1, (x1 - x0) * (y1 - y0));
		if (!no_mark && rect_log_on && rect_n < YT_RECTS)
		{
			/* REV4: logged too (the sprite layer clears what it drew; the foreground tiles go
			 * over it like over any sprite) */
			rect_xy[rect_n][0] = (int16_t)x0; rect_xy[rect_n][1] = (int16_t)y0;
			rect_xy[rect_n][2] = (int16_t)(x1 - x0); rect_xy[rect_n][3] = (int16_t)(y1 - y0);
			rect_n++;
		}
		return 1;
	}
	rect_t clip;
	if (draw_clip(vx, vy, w, h, &clip) == 2) return 0;
	/* draw_clipsprite, for DRAWSPR_OVERWRITE without flips or scaling */
	drawsprcmd_t cmd;
	int x = vx + window_canvas_x + clip.x1;
	const int l = line_of(vy + clip.y1);
	int cw = w - (clip.x2 + clip.x1), ch = h - (clip.y2 + clip.y1);
#ifdef STX_SPRITE_X1
	/* SPRITE_X1=1 (the owner, 28 Oct 2026: "Tux is supposed to move smoothly"): Vic's 16-bit loops
	 * write a word = 2 pixels, so a sprite went to the even pixel at or left of its place - Tux moved
	 * on the 32X in 2-pixel steps over a level the Genesis scrolls 1 pixel at a time, and while the
	 * view followed him he jumped back and forth 2 pixels against it every few frames (measured in
	 * ares). At an odd pixel the sprite now goes through YATSSD's own 8-bit path (DRAWSPR_PRECISE):
	 * its odd-x loop writes the first pixel, then words of two pixels shifted by one, then the last -
	 * the same words as before plus two single bytes a line, the overwrite image keeping colour 0
	 * see-through. Even pixels: the 16-bit loop as before. A picture of odd width stays on the even
	 * pixel: that loop reads its lines as words and checks only the first line's address. */
	cmd.flags = DRAWSPR_OVERWRITE;
	if ((x & 1) && !(w & 1)) cmd.flags = DRAWSPR_OVERWRITE | DRAWSPR_PRECISE;
	else { x &= ~1; cw &= ~1; }
	if (cw < 8 || ch < 2) return 0;
#else
	x &= ~1;
	cw &= ~1;
	if (cw < 8 || ch < 2) return 0;
	cmd.flags = DRAWSPR_OVERWRITE;
#endif
	cmd.x = (uint16_t)x;
	cmd.w = (uint16_t)cw;
	cmd.sx = (int16_t)clip.x1;
	cmd.sw = (uint16_t)w; cmd.sh = (uint16_t)h;
	cmd.sdata = (void *)pixels;
	cmd.scale = 0;
	cmd.stride = 0;
	const int first = l + ch <= YT_ROWS ? ch : YT_ROWS - l;   /* rows before the ring wraps */
	cmd.y = (uint16_t)l; cmd.h = (uint16_t)first; cmd.sy = (int16_t)clip.y1;
	if (first >= 2) draw_handle_drawspritecmd(&cmd);
	else if (first == 1)
	{
		uint8_t *d = yt_canvas() + l * CANVAS_W + x;
		const uint8_t *s = pixels + clip.y1 * w + clip.x1;
		for (int i = 0; i < cw; i++) if (s[i]) d[i] = s[i];
	}
	if (first < ch)
	{
		/* RING: the rest from canvas line 0 */
		const int rest = ch - first;
		if (rest >= 2)
		{
			cmd.y = 0; cmd.h = (uint16_t)rest; cmd.sy = (int16_t)(clip.y1 + first);
			draw_handle_drawspritecmd(&cmd);
		}
		else
		{
			uint8_t *d = yt_canvas() + x;
			const uint8_t *s = pixels + (clip.y1 + first) * w + clip.x1;
			for (int i = 0; i < cw; i++) if (s[i]) d[i] = s[i];
		}
	}
	STAT(1, cw * ch);
	mark_lines(x, l, cw, ch, no_mark);
	if (no_mark) return 1;
	if (rect_log_on && rect_n < YT_RECTS)
	{
		rect_xy[rect_n][0] = (int16_t)x0; rect_xy[rect_n][1] = (int16_t)y0;
		rect_xy[rect_n][2] = (int16_t)(x1 - x0); rect_xy[rect_n][3] = (int16_t)(y1 - y0);
		rect_n++;
	}
	return 1;
}

/* REV4, OPTIONS - DRAW GENESIS (game/gen_level.c): the level is on the Genesis; the canvas is a
 * fixed 320 x 176 sprite layer (no scrolling, line g = view line g), cleared where sprites were */
void yt_fixed(void)
{
	cam_x = cam_y = 0;
	top_h = top_v = 0;
	window_canvas_x = window_canvas_y = 0;
	cur = plat_back_index() & 1;
	rec = bands[cur];
	kp = kept[cur];
	ring = 0;
	no_mark = 0;
	draw_setScissor(0, 0, YT_VIEW_W, YT_VIEW_H);
}

/* a rectangle of the view (clipped) filled with one colour */
void yt_fill(int x, int y, int w, int h, uint8_t colour)
{
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > YT_VIEW_W) w = YT_VIEW_W - x;
	if (y + h > YT_VIEW_H) h = YT_VIEW_H - y;
	if (w <= 0 || h <= 0) return;
	uint8_t *c = yt_canvas();
	for (int k = 0; k < h; k++) yt_fill_row(c + (y + k) * CANVAS_W + x, w, colour);
}

void yt_screen(void)
{
	if (tm.tw == 0)
	{
		tm.tw = tm.th = YT_TILE;
		tm.canvas_tiles_hor = YT_PITCH / YT_TILE;
		tm.canvas_tiles_ver = YT_ROWS / YT_TILE;
		tm.tiles_hor = YT_PITCH / YT_TILE;
		tm.tiles_ver = YT_ROWS / YT_TILE;
		draw_setScissor(0, 0, YT_VIEW_W, YT_VIEW_H);
	}
	cam_x = cam_y = 0;
	top_h = top_v = 0;
	window_canvas_x = window_canvas_y = 0;
	cur = plat_back_index() & 1;
	rec = bands[cur];
	kp = kept[cur];
	ring = 0;                          /* the menus' pictures: lines 0..175, not wrapped */
	yt_invalidate();
}

/* the cell of world tile (tx, ty) redrawn in this buffer (animated tiles: a picture that changes
 * keeps its number) */
void yt_mark_tile(int tx, int ty)
{
	const int c = tx - top_h, l = row_line(ty);
	if (c < 0 || c >= NCOLS || l < 0) return;
	const int nb = nbands(), b0 = l / 8, b1 = (b0 + 1) % nb;
	rec[b0 * NCOLS + c] = rec[b1 * NCOLS + c] = -1;
}
