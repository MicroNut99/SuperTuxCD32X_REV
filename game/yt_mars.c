/*
 * SuperTux CD32X - the 32X side of Vic's tiler: the two registers it needs, and what YATSSD's
 * hw_32x.c would have provided (hw_32x.c itself is not linked: it sets up the VDP, the line
 * table, the interrupts and the 68000 link its own way, which is the boot path's job here).
 */
#include "yt.h"
#include "32x.h"            /* yatssd/32x.h: MARS_FRAMEBUFFER, MARS_SYS_COMM6 */
#include "plat.h"

int16_t *yt_extrafb(void)
{
	/* exactly the expression in dtiles.c (draw_dirtyrect, draw_handle_layercmd) */
	return (int16_t *)&MARS_FRAMEBUFFER + 0x100 + ((YT_PITCH * YT_YAW) >> 1);
}

void yt_tile_counter_reset(void)
{
	MARS_SYS_COMM6 = 0;        /* draw_tile_layer: "MARS_SYS_COMM6 = 0" before the layer */
}

uint8_t *yt_canvas(void) { return (uint8_t *)&MARS_FRAMEBUFFER + 0x200; }

/* R9: the low 16 bits of the 68000's frame counter that the Sub-CPU puts in COMM10 with each pad
 * reply (cd32x/subcpu/main.c K32) - the game compares it to see a reply that came back old */
uint16_t yt_pad_tick(void) { return MARS_SYS_COMM10; }

/* a row into the canvas (R4: the moving background) - 16- or 32-bit writes when the two
 * addresses allow it (yt_back makes their parity the same), bytes otherwise */
void yt_copy_row(uint8_t *d, const uint8_t *s, int n)
{
	if (n <= 0) return;
	if ((((uintptr_t)d ^ (uintptr_t)s) & 1) == 0)
	{
		if ((uintptr_t)d & 1) { *d++ = *s++; n--; }
		if (((((uintptr_t)d ^ (uintptr_t)s) & 3) == 0) && n >= 2 && ((uintptr_t)d & 2))
		{
			*(volatile uint16_t *)d = *(const uint16_t *)s; d += 2; s += 2; n -= 2;
		}
		if ((((uintptr_t)d ^ (uintptr_t)s) & 3) == 0)
			for (; n >= 4; n -= 4, d += 4, s += 4) *(volatile uint32_t *)d = *(const uint32_t *)s;
		for (; n >= 2; n -= 2, d += 2, s += 2) *(volatile uint16_t *)d = *(const uint16_t *)s;
	}
	for (; n > 0; n--) *d++ = *s++;
}

/* inside the canvas (R4: re-anchoring), both 4-aligned: 32-bit words, in the safe direction */
void yt_move_row(uint8_t *d, const uint8_t *s, int n)
{
	volatile uint32_t *dw = (volatile uint32_t *)d;
	const volatile uint32_t *sw = (const volatile uint32_t *)s;
	const int w = n >> 2;
	if (d < s) for (int i = 0; i < w; i++) dw[i] = sw[i];
	else for (int i = w - 1; i >= 0; i--) dw[i] = sw[i];
}

void yt_picture(const uint8_t *pixels)
{
	/* 320 x 176 into the canvas (byte 0x200, 384 a line), 32-bit writes; pixels 4-byte aligned */
	volatile uint32_t *d = (volatile uint32_t *)((uint8_t *)&MARS_FRAMEBUFFER + 0x200);
	const uint32_t *s = (const uint32_t *)pixels;
	for (int y = 0; y < YT_VIEW_H; y++, d += YT_PITCH / 4, s += YT_VIEW_W / 4)
		for (int x = 0; x < YT_VIEW_W / 4; x++) d[x] = s[x];
}

/* ---- game/level.c's reader: the boot path's commands, sent as port/plat_mars.c's
 * plat_load_pack sends them (that function always copies a whole file to 0x280000 and reads the
 * splash picture from the disc twice first, for its loading screen):
 *   38  COMM2 = file (9: IMAGE9.RAW -> cart slot 9 = 0x120000; 200 + k: OJk.PAK -> 0x280000);
 *       reply: the file's length in COMM8 (32 bits)
 *   46  COMM2 = piece c | 0x8000 (no read-back), COMM8 = the length: 32 KB from the disc to
 *       slot + c x 32 KB (cd32x/subcpu/main.c); reply COMM12 = 0 ok
 * The progress call may draw and read the pad (which replies in COMM8), so the length is put
 * back before each piece. Afterwards the SH2's cache is purged: the 68000 wrote the cart. */
#define C_COMM0  (*(volatile uint16_t *)0x20004020)
#define C_COMM2  (*(volatile uint16_t *)0x20004022)
#define C_COMM8L (*(volatile uint32_t *)0x20004028)
#define C_COMM12 (*(volatile uint16_t *)0x2000402C)
static int c_cmd(uint16_t c)
{
	C_COMM0 = c;
	for (uint32_t n = 0; n < 2000000u && C_COMM0 != 0; n++)
		for (volatile int k = 0; k < 64; k++) { }
	return C_COMM0 == 0;
}

int cart_load(int fid, uint32_t flen, int first, int n, void (*progress)(int done, int total))
{
	C_COMM2 = (uint16_t)fid;
	C_COMM8L = 0;
	if (!c_cmd(38)) return 0;
	const uint32_t len = C_COMM8L;
	if (len != flen) return 0;
	int bad = 0;
	for (int c = first; c < first + n; c++)
	{
		C_COMM8L = len;
		C_COMM2 = (uint16_t)(c | 0x8000);
		C_COMM12 = 0;
		if (!c_cmd(46) || C_COMM12 != 0) bad++;
		if (progress) progress(c - first + 1, n);
	}
	*(volatile uint8_t *)0xFFFFFE92 = 0x11;   /* purge the cache: the 68000 wrote the cart */
	return !bad;
}

/* dtiles.c's init_tilemap() and draw_tile_layer() / draw_tilemap() refer to these YATSSD
 * hw_32x.c functions. They are not called in this port (game/yt_layers.c replaces those three
 * functions); if one ever were, the error screen names it instead of the program running on. */
void Hw32xUpdateLineTable(int hscroll, int vscroll, int lineskip)
{ (void)hscroll; (void)vscroll; (void)lineskip; plat_fatal("YATSSD HW32XUPDATELINETABLE: NOT USED HERE"); }
void Hw32xSetFGOverlayPriorityBit(int p) { (void)p; plat_fatal("YATSSD HW32XSETFGOVERLAYPRIORITYBIT: NOT USED HERE"); }
void Hw32xSetBGOverlayPriorityBit(int p) { (void)p; plat_fatal("YATSSD HW32XSETBGOVERLAYPRIORITYBIT: NOT USED HERE"); }
void HwMdSetPlaneBitmap(char plane, void *data) { (void)plane; (void)data; plat_fatal("YATSSD HWMDSETPLANEBITMAP: NOT USED HERE"); }
void HwMdClearPlanes(void) { plat_fatal("YATSSD HWMDCLEARPLANES: NOT USED HERE"); }
void HwMdHScrollPlane(char plane, int s) { (void)plane; (void)s; plat_fatal("YATSSD HWMDHSCROLLPLANE: NOT USED HERE"); }
void HwMdVScrollPlane(char plane, int s) { (void)plane; (void)s; plat_fatal("YATSSD HWMDVSCROLLPLANE: NOT USED HERE"); }
