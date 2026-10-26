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

/* REV4: a row of the canvas in one colour (16- / 32-bit writes where the address allows) */
void yt_fill_row(uint8_t *d, int n, uint8_t c)
{
	if (n <= 0) return;
	const uint32_t w4 = c * 0x01010101u;
	if ((uintptr_t)d & 1) { *d++ = c; n--; }
	if (((uintptr_t)d & 2) && n >= 2) { *(volatile uint16_t *)d = (uint16_t)w4; d += 2; n -= 2; }
	for (; n >= 4; n -= 4, d += 4) *(volatile uint32_t *)d = w4;
	for (; n >= 2; n -= 2, d += 2) *(volatile uint16_t *)d = (uint16_t)w4;
	if (n) *d = c;
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

/* ---- REV: the master SH2's DMA unit for the tiler's lines (port/plat.h declares these three;
 * nothing in the port defines them, so they live here, in game/ - the boot path is untouched).
 *
 * What the docs say and this follows (OpenTyrian addendum, "Use the SH2's DMA for opaque lines"):
 * compose a line in SDRAM, let DMA channel 0 copy it into the frame buffer (auto request,
 * cycle-steal, 32-bit units) while the CPU composes the next one; give every wait a timeout and
 * fall back to the CPU for good if one ever fails. Tyrian measured 18-19 ms against 24 ms.
 *
 * The 32X manual's rules for this ("1.15 Restrictions", "Please make the following setting ..."):
 *   - auto-request DMA: the interrupts must be masked while it runs. The master's V interrupt
 *     is held off from plat_dma_start until plat_dma_wait has seen the end (one line: well under
 *     0.1 ms) - between two lines it is let through, so a vblank is only ever a line late. The
 *     slave takes no interrupts while it draws (only its command interrupt is on, never raised).
 *   - memory to memory: CHCR = xxxx xx10 1110 0xxx (AR = 1, AM = 0, AL = DS = DL = 1, TB = 0
 *     cycle-steal, TA = 0 dual address); here DM = SM = 01 (increment), TS = 10 (longs), DE = 1.
 *   - only the master runs DMA (two at once: one of them crawls); no PWM, no H interrupt here.
 *   - both addresses in the cache-through area (0x2xxxxxxx): the DMA does not see the cache.
 *     The SH2's cache writes through, so the composed line is in SDRAM before its DMA starts.
 * Channel 0's DREQ use (the 68000 FIFO) is not used by this boot path: DRCR0 = 0 (auto). */
#define DMA_SAR0   (*(volatile uint32_t *)0xFFFFFF80)
#define DMA_DAR0   (*(volatile uint32_t *)0xFFFFFF84)
#define DMA_TCR0   (*(volatile uint32_t *)0xFFFFFF88)
#define DMA_CHCR0  (*(volatile uint32_t *)0xFFFFFF8C)
#define DMA_DRCR0  (*(volatile uint8_t  *)0xFFFFFE71)
#define DMA_DMAOR  (*(volatile uint32_t *)0xFFFFFFB0)
#define CHCR_LINE  0x5AE1u          /* DM 01, SM 01, TS 10, AR, AL, DS, DL, cycle-steal, DE */
#define CHCR_TE    0x0002u
#define DMA_SPIN   100000u          /* ~ 30 ms of polling (one poll ~ 7 cycles): a line takes < 0.2 ms */

static int dma_bad;                  /* a transfer never ended: the CPU copies from now on */
static int dma_busy;                 /* a transfer was started and not yet waited for */
static uint32_t dma_sr;              /* the status register before plat_dma_start masked */
static void *dma_dst; static const void *dma_src; static unsigned dma_len;   /* the one in flight */

static uint32_t sr_get(void) { uint32_t sr; __asm__ __volatile__("stc sr,%0" : "=r"(sr)); return sr; }
static void sr_set(uint32_t sr) { __asm__ __volatile__("ldc %0,sr" : : "r"(sr) : "memory"); }

static void cpu_copy(void *dst, const void *src, unsigned bytes)
{
	volatile uint32_t *d = (volatile uint32_t *)dst;
	const volatile uint32_t *s = (const volatile uint32_t *)src;
	for (unsigned i = 0; i < bytes / 4; i++) d[i] = s[i];
}

static uint32_t through(const void *p) { return (uint32_t)(uintptr_t)p | 0x20000000u; }

bool plat_dma_ok(void) { return !dma_bad; }

void plat_dma_wait(void)
{
	if (!dma_busy) return;
	uint32_t n = 0;
	while (!(DMA_CHCR0 & CHCR_TE) && ++n < DMA_SPIN) { }
	if (!(DMA_CHCR0 & CHCR_TE))
	{
		/* it never ended: stop the channel, copy the line with the CPU, never use DMA again */
		DMA_CHCR0 = 0;
		(void)DMA_DMAOR; DMA_DMAOR = 0;
		dma_bad = 1;
		cpu_copy(dma_dst, dma_src, dma_len);
	}
	dma_busy = 0;
	sr_set(dma_sr);                   /* the vblank may come in now */
}

void plat_dma_start(void *dst, const void *src, unsigned int bytes)
{
	plat_dma_wait();
	if (dma_bad || bytes < 4) { cpu_copy(dst, src, bytes); return; }
	dma_sr = sr_get();
	sr_set(dma_sr | 0xF0);            /* interrupts off while the DMA runs (32X manual 1.15) */
	dma_dst = dst; dma_src = src; dma_len = bytes;
	(void)DMA_CHCR0;
	DMA_CHCR0 = 0;                    /* DE off, TE cleared (read first, then 0) */
	if (DMA_DMAOR & 0x6u) { DMA_DMAOR = 0; }   /* an old address error / NMI flag: clear it */
	DMA_DRCR0 = 0;
	DMA_SAR0 = through(src);
	DMA_DAR0 = through(dst);
	DMA_TCR0 = bytes >> 2;
	DMA_DMAOR = 1;                    /* DME: the DMA unit on, fixed priority */
	DMA_CHCR0 = CHCR_LINE;
	dma_busy = 1;
}

/* the two line buffers the tiler composes into (SDRAM, written through the cache-through
 * address so the tile pictures it reads stay in the cache) */
static uint32_t dma_lines[2][YT_PITCH / 4];
uint32_t *yt_dma_line(int i) { return (uint32_t *)through(dma_lines[i & 1]); }

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
int plat_cmd_rv(uint16_t cmd);           /* port/plat_mars.c: ED / EDSAVE-4 - both SH2s in SDRAM */
static int c_cmd(uint16_t c)
{
	if (c == 46 || c == 68) return plat_cmd_rv(c);   /* the 68000 holds RV = 1 for these */
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

/* ---- 8 / 16 MB test builds (game/cart.c): command 68, window 1's page (cd32x/subcpu/main.c,
 * compiled into the Sub-CPU program only for these builds). Then both SH2s purge their caches:
 * the slave through the platform's parallel job (port/plat_mars.c plat_parallel). */
#include "cart.h"
#if STX_CART_MB > 4
static void purge_cache(void *arg, int cpu) { (void)arg; (void)cpu; *(volatile uint8_t *)0xFFFFFE92 = 0x11; }
int plat_cart_page(int page)
{
	C_COMM2 = (uint16_t)page;
	C_COMM12 = 0;
	const int ok = c_cmd(68) && C_COMM12 == 1;
	plat_parallel(purge_cache, 0);
	return ok;
}
const volatile uint8_t *plat_cart_through(uint32_t off) { return (const volatile uint8_t *)(0x22000000u + off); }
#endif

/* ---- REV3: the Genesis picture, SH2 commands 62-66 (cd32x/subcpu/main.c REV2, boot-proven on
 * the console; docs repo CD32X_COMMAND_LEDGER_ADDITIONS.md). Parameters in COMM2 / COMM8 before
 * COMM0, done = COMM0 cleared (c_cmd above); cart sources as offsets into the 4 MB cart. */
#define C_COMM8W (*(volatile uint16_t *)0x20004028)
static uint32_t cart_off(const void *p)
{
	const uint32_t a = (uint32_t)(uintptr_t)p & 0x1FFFFFFFu;    /* 0x02xxxxxx or 0x22xxxxxx */
	return a >= 0x02000000u && a < 0x02400000u ? a - 0x02000000u : 0xFFFFFFFFu;
}
void gen_vram_word(unsigned vram, unsigned word)
{
	C_COMM2 = (uint16_t)vram;
	C_COMM8W = (uint16_t)word;
	c_cmd(62);
}
int gen_cart_to_vram(unsigned vram, const void *src, unsigned words)
{
	uint32_t off = cart_off(src);
	if (off == 0xFFFFFFFFu || (off & 1) || (vram & 1)) return 0;
	int ok = 1;
	while (words)
	{
		const unsigned k = words > 1023 ? 1023 : words;   /* the command's limit */
		C_COMM2 = (uint16_t)vram;
		C_COMM8L = (uint32_t)k << 22 | off;
		C_COMM12 = 0;
		if (!c_cmd(63) || C_COMM12 != 1) ok = 0;
		vram += k * 2; off += k * 2; words -= k;
	}
	return ok;
}
int gen_cart_to_cram(int first, const void *src, int count)
{
	const uint32_t off = cart_off(src);
	if (off == 0xFFFFFFFFu || (off & 1) || first < 0 || count < 1 || first + count > 64) return 0;
	C_COMM2 = (uint16_t)first;
	C_COMM8L = (uint32_t)count << 22 | off;
	C_COMM12 = 0;
	return c_cmd(64) && C_COMM12 == 1;
}
void gen_vdp_reg(unsigned word) { C_COMM2 = (uint16_t)word; c_cmd(65); }
/* REV4: command 67 - cols (1-4) columns of rows words each, stored one after the other in the
 * cart, down a 64-cell-wide plane map from vram (column k at vram + 2 k). The Sub-CPU reads them
 * through ONE 1 MB bank of the cart window: data crossing a bank goes word by word (62). */
int gen_cart_to_vram_cols(unsigned vram, const void *src, unsigned rows, unsigned cols)
{
	const uint32_t off = cart_off(src);
	if (off == 0xFFFFFFFFu || (off & 1) || (vram & 1) || !rows || rows > 255 || !cols || cols > 4) return 0;
	if ((off & 0xFFFFF) + rows * cols * 2 > 0x100000)
	{
		const uint8_t *p = (const uint8_t *)src;
		for (unsigned k = 0; k < cols; k++)
			for (unsigned r = 0; r < rows; r++, p += 2)
				gen_vram_word(vram + 2 * k + 128 * r, (unsigned)p[0] << 8 | p[1]);
		return 1;
	}
	C_COMM2 = (uint16_t)vram;
	C_COMM8L = (uint32_t)(cols - 1) << 30 | (uint32_t)rows << 22 | off;
	C_COMM12 = 0;
	return c_cmd(67) && C_COMM12 == 1;
}
void gen_vsram(unsigned addr, unsigned word)
{
	C_COMM2 = (uint16_t)addr;
	C_COMM8W = (uint16_t)word;
	c_cmd(66);
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
