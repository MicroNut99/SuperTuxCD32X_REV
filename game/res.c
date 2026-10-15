/*
 * SuperTux CD32X - the resident pack (IMAGE8.RAW, tools/stx_pack.py): palette, tiles, sprites,
 * SuperTux's letters, the menu pictures, the world maps' data and the texts; drawing sprites,
 * tiles and text with Vic's draw_sprite (game/yt.h). See game/stx.h.
 */
#include "stx.h"

extern const unsigned char msx[];      /* yatssd/font.c: 8 x 8, bit 7 = left */

char *put_str(char *p, const char *s) { while (*s) *p++ = *s++; *p = 0; return p; }
char *put_dec(char *p, uint32_t v, int digits)
{
	char t[12];
	int n = 0;
	do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
	while (n < digits) t[n++] = '0';
	while (n) *p++ = t[--n];
	*p = 0;
	return p;
}

const uint8_t *need(const char *name)
{
	const uint8_t *p;
	uint32_t n;
	if (!pack_file(name, &p, &n))
	{
		static char msg[48];
		put_str(put_str(msg, name), ": NOT IN THE PACKS");
		plat_fatal(msg);
	}
	return p;
}

static uint32_t rng = 12345;
uint32_t rnd(void) { rng = rng * 1103515245u + 12345u; return rng >> 16; }

/* ------------------------------------------------------------------ palette, tiles */
uint16_t palette[256];
int nframes, ntiles;
const uint8_t *tile_px;
TileDesc tdesc[512];
uint8_t col_white = 1, col_black = 1, col_panel = 1, col_yellow = 1;

static int colour_near(int r, int g, int b)
{
	int best = 1, bd = 1 << 30;
	for (int i = 1; i < 192; i++)
	{
		const int c = palette[i];
		const int dr = (c & 31) * 8 - r, dg = (c >> 5 & 31) * 8 - g, db = (c >> 10 & 31) * 8 - b;
		const int d = dr * dr + dg * dg + db * db;
		if (d < bd) { bd = d; best = i; }
	}
	return best;
}

/* ------------------------------------------------------------------ sprites */
Sprite spr[S_N];
static const uint8_t *spr_bin, *spr_frames;
static const char *const spr_names[S_N] = {
	"smalltux-walk-left", "smalltux-walk-right", "smalltux-stand-left", "smalltux-stand-right",
	"smalltux-jump-left", "smalltux-jump-right", "smalltux-gameover", "smalltux-skid-left",
	"smalltux-skid-right", "smalltux-kick-left", "smalltux-kick-right", "smalltux-grab-left",
	"smalltux-grab-right", "smalltux-star",
	"largetux-walk-left", "largetux-walk-right", "largetux-stand-left", "largetux-stand-right",
	"largetux-jump-left", "largetux-jump-right", "largetux-duck-left", "largetux-duck-right",
	"largetux-skid-left", "largetux-skid-right", "largetux-kick-left", "largetux-kick-right",
	"largetux-grab-left", "largetux-grab-right", "largetux-star",
	"firetux-walk-left", "firetux-walk-right", "firetux-stand-left", "firetux-stand-right",
	"firetux-jump-left", "firetux-jump-right", "firetux-duck-left", "firetux-duck-right",
	"firetux-skid-left", "firetux-skid-right", "firetux-kick-left", "firetux-kick-right",
	"firetux-grab-left", "firetux-grab-right",
	"mriceblock-left", "mriceblock-right", "mriceblock-falling-left", "mriceblock-falling-right",
	"mriceblock-flat-left", "mriceblock-flat-right", "snowball-squished-left", "snowball-squished-right",
	"jumpy-left-up", "jumpy-left-down", "jumpy-left-middle", "mrbomb-left", "mrbomb-right",
	"mrbomb-ticking-left", "mrbomb-ticking-right", "mrbomb-explosion", "stalactite",
	"stalactite-broken", "flame", "fish", "fish-down", "bouncingsnowball-left",
	"bouncingsnowball-right", "bouncingsnowball-squished", "flyingsnowball", "flyingsnowball-squished",
	"spiky-left", "spiky-right", "snowball-left", "snowball-right",
	"egg", "1up", "star", "iceflower", "bullet",
	"logo", "wm-smalltux", "wm-tux", "wm-firetux", "leveldot-red", "leveldot-green", "teleporter",
	"snow0", "snow1", "snow2", "cloud", "tux-life",
};

/* R5: the frames are cut to their visible pixels (tools/stx_convert.py trimmed): (*dx, *dy) is
 * where the cut picture sits in SuperTux's picture, in half-size pixels (dx even) */
const uint8_t *spr_frame_at(int s, uint32_t ms, int *w, int *h, int *dx, int *dy)
{
	const Sprite *sp = &spr[s];
	int f = sp->first;
	if (sp->n > 1) f += (int)((ms * (uint32_t)sp->fps10 / 10000u) % (uint32_t)sp->n);
	const uint8_t *fe = spr_frames + 12 * f;
	*w = be16(fe); *h = be16(fe + 2);
	*dx = (int16_t)be16(fe + 8); *dy = (int16_t)be16(fe + 10);
	return spr_bin + be32(fe + 4);
}

/* the cut picture only (the logo, which is not cut) */
const uint8_t *spr_frame(int s, uint32_t ms, int *w, int *h)
{
	int dx, dy;
	return spr_frame_at(s, ms, w, h, &dx, &dy);
}

/* Sprite::draw(x - hotspot) at half size */
void spr_draw(int s, uint32_t ms, int ux, int uy, int cx, int cy)
{
	int w, h, dx, dy;
	const uint8_t *px = spr_frame_at(s, ms, &w, &h, &dx, &dy);
	yt_sprite(px_of(ux - spr[s].hx) - cx + dx, px_of(uy - spr[s].hy) - cy + dy, w, h, px);
}

/* Surface::draw_part(0, 0, x, y, 32, rows): the top rows_u units of the picture (rising upgrades)
 * - counted from the top of SuperTux's picture, of which the cut one starts dy lines down */
void spr_draw_part(int s, int ux, int uy, int rows_u, int cx, int cy)
{
	int w, h, dx, dy;
	const uint8_t *px = spr_frame_at(s, 0, &w, &h, &dx, &dy);
	int rows = px_of(rows_u) - dy;
	if (rows > h) rows = h;
	if (rows >= 2) yt_sprite(px_of(ux) - cx + dx, px_of(uy) - cy + dy, w, rows, px);
}

/* a tile (its animation frame) as a sprite: bouncing bricks, bouncy coins */
void tile_draw(int tile, uint32_t ms, int ux, int uy, int cx, int cy)
{
	if (tile <= 0 || tile >= ntiles || !tdesc[tile].frames) return;
	const TileDesc *t = &tdesc[tile];
	int f = t->first;
	if (t->frames > 1 && t->speed) f += (int)((ms / t->speed) % t->frames);
	yt_sprite(px_of(ux) - cx, px_of(uy) - cy, YT_TILE, YT_TILE, tile_px + YT_TILE * YT_TILE * f);
}

/* ------------------------------------------------------------------ letters */
typedef struct { int w, h, adv; const uint8_t *px; } Font;
static Font font[F_N];

int text_width(int f, const char *t) { return (int)strlen(t) * font[f].adv; }
int fit_font(const char *t) { return text_width(F_GOLD, t) <= YT_VIEW_W ? F_GOLD : F_SMALL; }

/* a line of text at view (x, y): one YATSSD sprite a letter (see-through background) */
void text_at(int f, int x, int y, const char *t)
{
	const Font *F = &font[f];
	x &= ~1;
	for (; *t; t++, x += F->adv)
	{
		int c = (unsigned char)*t;
		if (c < 33 || c > 127) continue;
		yt_sprite(x, y, F->w, F->h, F->px + (c - 32) * F->w * F->h);
	}
}
void text_mid(int f, int y, const char *t) { text_at(f, (YT_VIEW_W - text_width(f, t)) / 2, y, t); }
void menu_line(int f, int y, const char *t, int on) { text_mid(on ? F_GOLD : f, y, t); }

/* ------------------------------------------------------------------ pictures, texts */
static const uint8_t *pics[PIC_N];

void pic_show(int p)
{
	for (int i = 0; i < 64; i++) palette[192 + i] = be16(pics[p] + 2 * i);
	yt_picture(pics[p] + 128);
}

const char *text_file(const char *name, uint32_t *size)
{
	const uint8_t *p;
	if (!pack_file(name, &p, size)) { *size = 0; return ""; }
	return (const char *)p;
}

/* NAMES.TXT: "index TAB name TAB author" a line */
static const char *names_txt;
static uint32_t names_n;
static const char *name_field(int li, int field)
{
	static char out[2][48];
	const char *p = names_txt, *end = names_txt + names_n;
	int line = 0;
	while (p < end && line < li)
	{
		while (p < end && *p != '\n') p++;
		p++; line++;
	}
	for (int f = 0; f < field && p < end; f++)
	{
		while (p < end && *p != '\t' && *p != '\n') p++;
		if (p < end && *p == '\t') p++;
	}
	int n = 0;
	while (p < end && *p != '\t' && *p != '\n' && n < 47) out[field - 1][n++] = *p++;
	out[field - 1][n] = 0;
	return out[field - 1];
}
const char *level_title(int li) { return name_field(li, 1); }
const char *level_author(int li) { return name_field(li, 2); }

/* ------------------------------------------------------------------ the panel */
/* R3: R2's panel cost 8 ms a frame on the console (timing line H) whenever the camera moved -
 * the 32X's 1-pixel shift (scroll x odd) applies to the panel's lines too, so the panel was
 * re-made for each buffer at every change of the shift: letters drawn pixel by pixel, then
 * copied a byte pair at a time. Now the text is drawn only when it changes, into both shifted
 * versions at once; a buffer that shows an old version gets the right one copied in 32-bit
 * words (1,920 writes). */
static uint8_t panel_rows[YT_PANEL_H][YT_VIEW_W + 8];
static uint32_t panel_px[2][YT_PANEL_H * YT_VIEW_W / 4];   /* shift 0, shift 1 (4-byte aligned) */
static char panel_have[3][56];
static uint8_t panel_c1 = 0xFF;
static int panel_ver;
static int buf_ver[2] = { -1, -1 }, buf_shift[2] = { -1, -1 };

static void panel_text(int x, int y, const char *t, uint8_t colour)
{
	for (; *t; t++, x += 8)
	{
		const unsigned char *g = msx + (unsigned char)*t * 8;
		for (int r = 0; r < 8; r++)
		{
			const unsigned bits = g[r];
			if (!bits) continue;
			for (int b = 0; b < 8; b++)
				if (bits & (0x80 >> b))
				{
					if (y + r + 1 < YT_PANEL_H && x + b + 1 < YT_VIEW_W) panel_rows[y + r + 1][x + b + 1] = col_black;
					if (y + r < YT_PANEL_H && x + b < YT_VIEW_W) panel_rows[y + r][x + b] = colour;
				}
		}
	}
}

/* three lines under the view (24 lines: three of the 8x8 font, its last row is empty) */
void panel_show(const char *l1, const char *l2, const char *l3, uint8_t c1)
{
	if (c1 != panel_c1 || strncmp(panel_have[0], l1, 55) || strncmp(panel_have[1], l2, 55) || strncmp(panel_have[2], l3, 55))
	{
		panel_c1 = c1;
		strncpy(panel_have[0], l1, 55);
		strncpy(panel_have[1], l2, 55);
		strncpy(panel_have[2], l3, 55);
		memset(panel_rows, col_panel, sizeof panel_rows);
		panel_text(4, 0, l1, c1);
		panel_text(4, 8, l2, col_white);
		panel_text(4, 16, l3, col_white);
		for (int sh = 0; sh < 2; sh++)
		{
			uint8_t *d = (uint8_t *)panel_px[sh];
			for (int y = 0; y < YT_PANEL_H; y++, d += YT_VIEW_W)
			{
				if (sh) { d[0] = col_panel; memcpy(d + 1, panel_rows[y], YT_VIEW_W - 1); }
				else memcpy(d, panel_rows[y], YT_VIEW_W);
			}
		}
		panel_ver++;
	}
	const int shift = yt_scroll_x() & 1;
	const int b = plat_back_index() & 1;
	if (buf_ver[b] == panel_ver && buf_shift[b] == shift) return;
	buf_ver[b] = panel_ver;
	buf_shift[b] = shift;
#ifdef OJ32X_MARS
	volatile uint32_t *d = (volatile uint32_t *)plat_scroll_panel();
	const uint32_t *s = panel_px[shift];
	for (int i = 0; i < YT_PANEL_H * YT_VIEW_W / 4; i++) d[i] = s[i];
#else
	memcpy(plat_scroll_panel(), panel_px[shift], sizeof panel_px[shift]);
#endif
}

/* ------------------------------------------------------------------ loading */
void res_load(void)
{
	const uint8_t *p = need("PALETTE.BIN");
	for (int i = 0; i < 192; i++) palette[i] = be16(p + 2 * i);
	palette[0] = 0;                    /* the platform layer shows 0 as 0x0400 (black, opaque) */
	p = need("TILES.BIN");
	if (memcmp(p, "STXT", 4)) plat_fatal("TILES.BIN: NOT STXT");
	nframes = be16(p + 4);
	ntiles = be16(p + 6);
	if (ntiles > 512) plat_fatal("TILES.BIN: TOO MANY TILES");
	for (int t = 0; t < ntiles; t++)
	{
		const uint8_t *e = p + 8 + 12 * t;
		tdesc[t].first = be16(e); tdesc[t].frames = be16(e + 2); tdesc[t].speed = be16(e + 4);
		tdesc[t].flags = be16(e + 6); tdesc[t].data = be16(e + 8); tdesc[t].next = be16(e + 10);
	}
	tile_px = p + 8 + 12 * ntiles;

	spr_bin = need("SPRITES.BIN");
	if (memcmp(spr_bin, "STXT", 4)) plat_fatal("SPRITES.BIN: NOT STXT");
	const int n = be16(spr_bin + 4);
	spr_frames = spr_bin + 8 + 36 * n;
	for (int s = 0; s < S_N; s++)
	{
		int found = 0;
		for (int i = 0; i < n && !found; i++)
		{
			const uint8_t *e = spr_bin + 8 + 36 * i;
			if (!strncmp((const char *)e, spr_names[s], 24))
			{
				spr[s].n = be16(e + 24); spr[s].fps10 = be16(e + 26);
				spr[s].hx = (int16_t)be16(e + 28); spr[s].hy = (int16_t)be16(e + 30);
				spr[s].first = be16(e + 32);
				found = 1;
			}
		}
		if (!found || !spr[s].n)
		{
			static char msg[48];
			put_str(put_str(msg, "SPRITE MISSING: "), spr_names[s]);
			plat_fatal(msg);
		}
	}

	p = need("FONTS.BIN");
	if (memcmp(p, "STXF", 4) || be16(p + 4) < F_N) plat_fatal("FONTS.BIN: NOT STXF");
	for (int i = 0; i < F_N; i++)
	{
		const uint8_t *e = p + 8 + 12 * i;
		font[i].w = be16(e); font[i].h = be16(e + 2); font[i].adv = be16(e + 4);
		font[i].px = p + be32(e + 8);
	}
	p = need("PICS.BIN");
	if (memcmp(p, "STXP", 4) || be16(p + 4) < PIC_N) plat_fatal("PICS.BIN: NOT STXP");
	for (int i = 0; i < PIC_N; i++) pics[i] = p + be32(p + 8 + 20 * i + 16);
	names_txt = text_file("NAMES.TXT", &names_n);

	col_white = (uint8_t)colour_near(255, 255, 255);
	col_black = (uint8_t)colour_near(0, 0, 0);
	col_panel = (uint8_t)colour_near(16, 24, 64);
	col_yellow = (uint8_t)colour_near(255, 230, 64);
}
