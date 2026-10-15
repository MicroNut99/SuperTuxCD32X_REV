/*
 * SuperTux CD32X - the PC stand-in for Vic's drawing code (yatssd/dtiles.c draw_handle_layercmd
 * and draw_dirtyrect, dsprite.c draw_sprite, draw.c draw_setScissor / draw_clip). Vic's files
 * use SH2 assembly and the 32X's fixed addresses, so the PC build has these C copies of the
 * same rules (the dirty matrix rule is transcribed line by line), drawing into game/host.c's
 * frame buffers. They also CHECK the layout: every write must land inside the canvas
 * (lines 0-199, columns 0-383 of the 0x200 area) - the 32X would not complain.
 */
#include "yt.h"
#include "types.h"
#include "plat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern tilemap_t tm;
extern int window_canvas_x, window_canvas_y;
extern uint32_t canvas_pitch, canvas_yaw;

static rect_t scissor;
static uint8_t *canvas(void) { return plat_scroll_ring(); }   /* frame buffer + 0x200 */

int16_t *yt_extrafb(void)
{
	/* the PC's frame buffer is bytes: Vic's int16 matrix lives at the same byte offset */
	return (int16_t *)(canvas() + YT_PITCH * YT_YAW);
}

/* Vic's tile counter (COMM6 on the 32X): yt_layers.c runs draw_handle_layercmd twice through
 * plat_parallel (game/host.c: one after the other) - each takes the cells the other has not */
static int tile_counter;
void yt_tile_counter_reset(void) { tile_counter = 0; }

static void put(int x, int y, int w, int h, const uint8_t *src, int sx, int sy, int stride, int keyed)
{
	if (x < 0 || y < 0 || x + w > YT_PITCH || y + h > YT_ROWS)
	{
		fprintf(stderr, "YT HOST: write outside the canvas: x %d y %d w %d h %d\n", x, y, w, h);
		abort();
	}
	uint8_t *c = canvas();
	for (int r = 0; r < h; r++)
	{
		const uint8_t *s = src + (sy + r) * stride + sx;
		uint8_t *d = c + (y + r) * YT_PITCH + x;
		for (int i = 0; i < w; i++)
			if (!keyed || s[i]) d[i] = s[i];
	}
}

void draw_setScissor(int16_t x, int16_t y, int16_t w, int16_t h)
{
	scissor.x1 = x; scissor.y1 = y; scissor.x2 = (int16_t)(x + w); scissor.y2 = (int16_t)(y + h);
}

/* dtiles.c draw_dirtyrect, as Vic wrote it (including start_tile_ver = y >> 3, then >> 4 again:
 * it marks from line y / 128 - more cells than needed, never fewer) */
void draw_dirtyrect(tilemap_t *t, int x, int y, int w, int h)
{
	int16_t *extrafb = yt_extrafb();
	if (x < 0) x = 0;
	if (y < 0) y = 0;
	if (x >= (int)canvas_pitch || y >= (int)canvas_yaw) return;
	unsigned sth = (unsigned)x / t->tw, eth = (unsigned)(x + w - 1) / t->tw;
	unsigned stv = ((unsigned)y >> 3) / t->th, etv = (unsigned)(y + h - 1) / t->th;
	int ntx = (int)(eth - sth + 1), nty = (int)(etv - stv + 1);
	if ((int)sth + ntx > (int)t->canvas_tiles_hor) ntx = (int)t->canvas_tiles_hor - (int)sth;
	if ((int)stv + nty > (int)t->canvas_tiles_ver) nty = (int)t->canvas_tiles_ver - (int)stv;
	int16_t *dirty = extrafb + 3 + stv * t->canvas_tiles_hor + sth;
	for (int yy = 0; yy < nty; yy++, dirty += t->canvas_tiles_hor)
		for (int xx = 0; xx < ntx; xx++) dirty[xx] = -1;
}

/* dtiles.c draw_handle_layercmd, the non-parallax branch, one CPU (every tile is "ours") */
int draw_handle_layercmd(drawtilelayerscmd_t *cmd)
{
	tilemap_t *t = cmd->tm;
	const int w = (int)t->tw, h = (int)t->th;
	int16_t *dirty = yt_extrafb() + 3;
	const int cth = (int)t->canvas_tiles_hor;
	const uint8_t **reslist = (const uint8_t **)t->reslist;
	const unsigned l = (unsigned)cmd->startlayer;
	const int16_t *layer = (const int16_t *)t->layers[l].tiles;
	const int16_t *last_layer = (const int16_t *)t->layers[t->numlayers - 1].tiles;
	int drawcnt = 0, stid = cmd->scroll_tile_id, y = cmd->y;
	int i = 0, n = tile_counter++;
	for (int y_tile = cmd->start_tile; y_tile < cmd->end_tile; y_tile += (int)t->tiles_hor)
	{
		int id = stid, x = cmd->x;
		for (int tile = y_tile; tile < y_tile + cmd->num_tiles_x; tile++, i++)
		{
			const int16_t idx = layer[tile];
			if (i != n) { id++; x += w; continue; }
			n = tile_counter++;
			if (dirty[id] != idx && (idx != 0 || (l == 0 && last_layer[tile] != dirty[id])))
			{
				if (l == 0) dirty[id] = idx;
				else dirty[id] = dirty[id] == 0 ? idx : -1;
				put(x + window_canvas_x, y + window_canvas_y, w, h, reslist[idx >> 2], 0, 0, w, l != 0);
				drawcnt++;
			}
			id++;
			x += w;
		}
		y += h;
		stid += cth;
		if (y >= (int)canvas_pitch) break;
	}
	return drawcnt;
}

/* draw.c draw_clip: the clipped amounts on each side; 2 = nothing left */
int draw_clip(int x, int y, int w, int h, rect_t *c)
{
	if (x + w < scissor.x1 || y + h < scissor.y1 || x >= scissor.x2 || y >= scissor.y2) return 2;
	c->x1 = c->x2 = c->y1 = c->y2 = 0;
	if (w + x > scissor.x2) c->x2 = (int16_t)(w + x - scissor.x2);
	if (x < scissor.x1) c->x1 = (int16_t)(scissor.x1 - x);
	if (h + y > scissor.y2) c->y2 = (int16_t)(h + y - scissor.y2);
	if (y < scissor.y1) c->y1 = (int16_t)(scissor.y1 - y);
	if (w <= c->x1 + c->x2 || h <= c->y1 + c->y2) return 2;
	return (c->x1 | c->x2 | c->y1 | c->y2) ? 1 : 0;
}

/* draw.c draw_handle_drawspritecmd for the flags the game uses (overwrite: 0 see-through) */
void draw_handle_drawspritecmd(drawsprcmd_t *cmd)
{
	put(cmd->x, cmd->y, cmd->w, cmd->h, cmd->sdata, cmd->sx, cmd->sy, cmd->stride ? cmd->stride : cmd->sw,
	    (cmd->flags & DRAWSPR_OVERWRITE) != 0);
}

uint8_t *yt_canvas(void) { return canvas(); }

static void canvas_check(const uint8_t *p, int n, const char *what)
{
	if (p < canvas() || p + n > canvas() + YT_PITCH * YT_ROWS)
	{
		fprintf(stderr, "YT HOST: %s outside the canvas\n", what);
		abort();
	}
}
void yt_copy_row(uint8_t *d, const uint8_t *s, int n) { if (n <= 0) return; canvas_check(d, n, "yt_copy_row"); memcpy(d, s, (size_t)n); }
void yt_move_row(uint8_t *d, const uint8_t *s, int n)
{
	if (((d - canvas()) | (s - canvas()) | n) & 3) { fprintf(stderr, "YT HOST: yt_move_row not 4-aligned\n"); abort(); }
	canvas_check(d, n, "yt_move_row dst");
	canvas_check(s, n, "yt_move_row src");
	memmove(d, s, (size_t)n);
}

/* REV: the DMA unit on the PC (game/yt_mars.c has the 32X one): a checked memcpy. The PC cannot
 * show what DMA costs or saves (the docs: "on the PC the DMA is a memcpy") - it checks that the
 * lines land where the CPU would have drawn them (ST_TRUTH compares the pictures).
 * ST_DMA_FAIL=n: the n-th transfer "never ends" - the fallback path is taken from there on. */
static int dma_bad, dma_count;
bool plat_dma_ok(void) { return !dma_bad; }
void plat_dma_wait(void) { }
void plat_dma_start(void *dst, const void *src, unsigned int bytes)
{
	if ((((uintptr_t)dst - (uintptr_t)canvas()) | (uintptr_t)src | bytes) & 3)
	{
		fprintf(stderr, "YT HOST: DMA not 4-aligned\n");
		abort();
	}
	canvas_check((const uint8_t *)dst, (int)bytes, "plat_dma_start");
	const char *f = getenv("ST_DMA_FAIL");
	if (f && ++dma_count == atoi(f)) dma_bad = 1;
	memcpy(dst, src, bytes);
}
static uint32_t dma_lines[2][YT_PITCH / 4];
uint32_t *yt_dma_line(int i) { return dma_lines[i & 1]; }

/* ---- REV3: a small model of the Genesis video chip for commands 62-66 (game/yt_mars.c on the
 * 32X): VRAM, CRAM, VSRAM and the registers the 68000 listener's md_init_hw sets
 * (cd32x/subcpu/hw_md.s InitVDPRegs), written the way the Sub-CPU / 68000 write them, and
 * host_genesis_pixel() for game/host.c: what plane B shows at a screen pixel (the 32X shows it
 * where its own pixel's colour has the through bit). Only what the game uses: full-screen
 * scrolling, plane B, no sprites; plane A only to check it is pointed at an empty map. */
static uint8_t g_vram[0x10000];
static uint16_t g_cram[64], g_vsram[40];
static uint8_t g_reg[24] = { 0x04, 0x14, 0x30, 0x2C, 0x07, 0x54, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                             0x81, 0x2B, 0x00, 0x01, 0x01, 0x00, 0x00 };
static const uint8_t *g_cart(const void *src, unsigned bytes)
{
	extern uint8_t *host_cart(void);
	const uint8_t *p = (const uint8_t *)src, *c = host_cart();
	if (p < c || p + bytes > c + 0x400000) { fprintf(stderr, "YT HOST: Genesis source not in the cart\n"); abort(); }
	if ((p - c) & 1) { fprintf(stderr, "YT HOST: Genesis source odd\n"); abort(); }
	return p;
}
void gen_vram_word(unsigned vram, unsigned word)
{
	vram &= 0xFFFE;
	g_vram[vram] = (uint8_t)(word >> 8); g_vram[vram + 1] = (uint8_t)word;
}
int gen_cart_to_vram(unsigned vram, const void *src, unsigned words)
{
	const uint8_t *p = g_cart(src, words * 2);
	if (vram & 1) return 0;
	for (unsigned i = 0; i < words; i++) gen_vram_word(vram + 2 * i, (unsigned)p[2 * i] << 8 | p[2 * i + 1]);
	return 1;
}
int gen_cart_to_cram(int first, const void *src, int count)
{
	if (first < 0 || count < 1 || first + count > 64) return 0;
	const uint8_t *p = g_cart(src, (unsigned)count * 2);
	for (int i = 0; i < count; i++) g_cram[first + i] = (uint16_t)(p[2 * i] << 8 | p[2 * i + 1]);
	return 1;
}
void gen_vdp_reg(unsigned word)
{
	const unsigned r = (word >> 8) & 0x1F;
	if ((word & 0xE000) == 0x8000 && r < 24) g_reg[r] = (uint8_t)word;
}
void gen_vsram(unsigned addr, unsigned word) { g_vsram[(addr & 0x7E) / 2 % 40] = (uint16_t)word; }
int gen_cart_to_vram_cols(unsigned vram, const void *src, unsigned rows, unsigned cols)
{
	if (!rows || rows > 255 || !cols || cols > 4 || (vram & 1)) return 0;
	const uint8_t *p = g_cart(src, rows * cols * 2);
	for (unsigned k = 0; k < cols; k++)
		for (unsigned r = 0; r < rows; r++, p += 2)
			gen_vram_word((vram + 2 * k + 128 * r) & 0xFFFF, (unsigned)p[0] << 8 | p[1]);
	return 1;
}

/* a plane (0 A, 1 B) at screen pixel (x, line): its colour index (0 = see-through) */
static unsigned plane_pixel(int plane, int x, int line, unsigned *pal)
{
	const unsigned pw = (g_reg[16] & 3) == 1 ? 64 : (g_reg[16] & 3) == 3 ? 128 : 32;
	const unsigned ph = ((g_reg[16] >> 4) & 3) == 1 ? 64 : ((g_reg[16] >> 4) & 3) == 3 ? 128 : 32;
	const unsigned hs_at = ((unsigned)(g_reg[13] & 0x3F) << 10) + 2 * (unsigned)plane;
	const unsigned hs = ((unsigned)g_vram[hs_at] << 8 | g_vram[hs_at + 1]) & 0x3FF;
	const unsigned vs = g_vsram[plane] & 0x3FF;
	const unsigned px = ((unsigned)x - hs) & (pw * 8 - 1), py = ((unsigned)line + vs) & (ph * 8 - 1);
	const unsigned map = plane ? (unsigned)(g_reg[4] & 7) << 13 : (unsigned)(g_reg[2] & 0x38) << 10;
	const unsigned at = map + 2 * ((py / 8) * pw + px / 8);
	const unsigned e = (unsigned)g_vram[at] << 8 | g_vram[at + 1];
	unsigned tx = px & 7, ty = py & 7;
	if (e & 0x800) tx = 7 - tx;
	if (e & 0x1000) ty = 7 - ty;
	const unsigned t = (e & 0x7FF) * 32 + ty * 4 + tx / 2;
	*pal = (e >> 13) & 3;
	return tx & 1 ? g_vram[t] & 15 : g_vram[t] >> 4;
}

/* what the Genesis shows at screen pixel (x, line), line 0-223: plane A over plane B (REV4: the
 * level over its background; REV3: plane A an empty map); 0 = see-through or display off */
int host_genesis_pixel(int x, int line, uint8_t rgb[3])
{
	if (!(g_reg[1] & 0x40)) return 0;
	unsigned pal, ci = plane_pixel(0, x, line, &pal);
	if (!ci) ci = plane_pixel(1, x, line, &pal);
	if (!ci) return 0;
	const unsigned c = g_cram[pal * 16 + ci];
	rgb[0] = (uint8_t)(((c >> 1) & 7) * 255 / 7); rgb[1] = (uint8_t)(((c >> 5) & 7) * 255 / 7);
	rgb[2] = (uint8_t)(((c >> 9) & 7) * 255 / 7);
	return 1;
}
/* plane A must point at an empty map while the picture shows (the green text is at 0xC000) */
int host_genesis_plane_a_empty(void)
{
	const unsigned map = (unsigned)(g_reg[2] & 0x38) << 10;
	for (unsigned i = 0; i < 64 * 32 * 2; i++) if (g_vram[map + i]) return 0;
	return 1;
}

void yt_fill_row(uint8_t *d, int n, uint8_t c) { if (n <= 0) return; canvas_check(d, n, "yt_fill_row"); memset(d, c, (size_t)n); }

void yt_picture(const uint8_t *pixels)
{
	for (int y = 0; y < YT_VIEW_H; y++) memcpy(canvas() + y * YT_PITCH, pixels + y * YT_VIEW_W, YT_VIEW_W);
}

/* R9: the 68000's counter of a pad reply (game/yt_mars.c): on the PC every reply is fresh */
uint16_t yt_pad_tick(void) { return (uint16_t)(plat_ticks_ms() * 60u / 1000u); }
