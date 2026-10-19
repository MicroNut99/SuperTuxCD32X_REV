/*
 * SuperTux CD32X - the PC stand-in for port/plat_mars.c (the J19b calls the game uses), for
 * checking the game on the PC: the same game files and game/yt_layers.c, two 128 KB "frame
 * buffers", the line table read back the way the 32X reads it (view lines, then the panel,
 * the 1-pixel shift), frames saved as PPM pictures. Taken from Hurrican CD32X's game/host.c.
 *
 *   ST_DIR=<folder with IMAGE8.RAW and OJnnn.PAK>  ST_OUT=<folder for frames>
 *   ST_FRAMES=<stop after n frames>  ST_SAVE_EVERY=<n>  ST_MS=<ms per frame, default 50>
 *   ST_LEVEL=<level index, 0 = world 1 level 1> (else the title)  ST_MAP=<world map 0-2>
 *   ST_X=<units: Tux's x when a level starts>
 *   ST_PAD="frame:BUTTONS,..."  (UP DOWN LEFT RIGHT A B C X Y Z START, joined with +; "-" = none)
 *   ST_LOG=1  sound effects and music in the log
 *   ST_STATS=1  (R5) a line a frame on stderr: "STAT frame cells sprites sprite-pixels cx cy"
 *   ST_NOPARTICLES=1  (R5) no snow / clouds (to measure what they cost)
 *   ST_PARALLAX  OPTIONS - BACKGROUND MOVES on (any value)
 *   ST_REANCHOR=move / draw  (R7) force the way the view re-anchors (both give the same picture)
 *   ST_TUXY=1  (R8) Tux's y and x a frame on stderr ("TY y x")
 *   ST_TRUTH=1  (R10) every frame drawn from nothing - the reference to compare the others with
 *   ST_CLASSIC=1  (R10) OPTIONS - SCROLL CLASSIC
 */
#include "plat.h"
#include "pack.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint8_t fbmem[2][0x20000];
static int back;                       /* the buffer being drawn */
static uint8_t *cart;                  /* 4 MB */
static uint32_t ticks, frame;
static uint16_t shown_pal[256];
static int border;

int host_genesis_pixel(int x, int line, uint8_t rgb[3]);   /* game/yt_host.c (REV3) */
uint8_t *host_cart(void);
int host_pri;                          /* REV8: the 32X VDP's PRI bit (game/gen_level.c front_32x) */
static const char *env(const char *n, const char *d) { const char *v = getenv(n); return v && *v ? v : d; }

void plat_fatal(const char *msg) { fprintf(stderr, "FATAL: %s\n", msg); exit(1); }
void plat_log(const char *msg) { fprintf(stderr, "[log] %s\n", msg); }
void plat_exit(int code) { exit(code); }
int host_start_level(void) { return atoi(env("ST_LEVEL", "0")); }

static uint32_t read_file(const char *name, uint32_t off, uint32_t max)
{
	char path[1024];
	snprintf(path, sizeof path, "%s/%s", env("ST_DIR", "."), name);
	FILE *f = fopen(path, "rb");
	if (!f) return 0;
	const size_t n = fread(cart + off, 1, max, f);
	fclose(f);
	return (uint32_t)n;
}

uint8_t *host_cart(void) { return cart; }

void plat_init(void)
{
	cart = calloc(1, 0x400000);
	const uint32_t n = read_file("IMAGE8.RAW", 0x100000, 0x180000);
	if (!n || !pack_mount(cart + 0x100000)) plat_fatal("no resident pack IMAGE8.RAW in ST_DIR");
	/* the 32X's frame buffers hold the splash when the game starts: something not zero */
	memset(fbmem, 0xA5, sizeof fbmem);
}

const uint8_t *plat_cart_ptr(uint32_t off) { return cart + off; }

/* 8 / 16 MB test builds (game/cart.c): the 16MB-PCB-CD32X's window 1 on the PC. ST_CARTMB = 4
 * (no mapper: the page writes go nowhere), 8 (the 8 MB GreenPAK image: pages 4-7 are 0-3
 * again) or 16. The cart buffer is what the console sees; window 1's 2 MB are copied out to the
 * page they belong to and in from the new one. Saves / the probe: commands 58 + 61 into window
 * 1's 0x3F0000 + piece x 4 KB, as the Sub-CPU does them. */
#include "cart.h"
#if STX_CART_MB > 4
static uint8_t *pages[8];
static int host_page = 1;
int plat_cart_page(int page)
{
	const int mb = atoi(env("ST_CARTMB", "4"));
	if (page < 1 || page > 7) return 0;
	if (mb <= 4) return 1;                         /* no register: the write goes nowhere */
	const int phys = mb == 8 ? page & 3 : page;
	const int old = mb == 8 ? host_page & 3 : host_page;
	if (phys == old) { host_page = page; return 1; }
	for (int i = 0; i < 8; i++) if (!pages[i]) pages[i] = calloc(1, 0x200000);
	memcpy(pages[old], cart + 0x200000, 0x200000);
	memcpy(cart + 0x200000, pages[phys], 0x200000);
	host_page = page;
	if (getenv("ST_LOG")) fprintf(stderr, "[%u] cart window 1 -> page %d\n", frame, page);
	return 1;
}
const volatile uint8_t *plat_cart_through(uint32_t off) { return cart + off; }
static uint8_t save_buf[4096];
int plat_save_put_word(int index, int value)
{
	index &= 2047;
	save_buf[2 * index] = (uint8_t)(value >> 8); save_buf[2 * index + 1] = (uint8_t)value;
	return 1;
}
int plat_save_piece(int piece)
{
	memcpy(cart + 0x3F0000 + (uint32_t)(piece & 15) * 4096u, save_buf, 4096);
	return 1;
}
#endif

int32_t plat_load_pack(int id, uint32_t off, uint32_t max)
{
	char name[32];
	snprintf(name, sizeof name, "OJ%03d.PAK", id - PACK_LEVEL_ID0);
	if (getenv("ST_LOG")) fprintf(stderr, "[%u] disc read: %s\n", frame, name);
	const uint32_t n = read_file(name, off, max);
	return n ? (int32_t)n : -1;
}

/* game/level.c's reader (on the 32X: game/yt_mars.c, commands 38 + 46 as plat_load_pack sends
 * them): pieces first .. first + n - 1 of disc file fid (9 = IMAGE9.RAW at cart 0x120000, 200 + k
 * = OJk.PAK at 0x280000), 32 KB each, to slot + piece x 32 KB */
int cart_load(int fid, uint32_t flen, int first, int n, void (*progress)(int done, int total))
{
	char name[32];
	uint32_t slot;
	if (fid >= PACK_LEVEL_ID0) { snprintf(name, sizeof name, "OJ%03d.PAK", fid - PACK_LEVEL_ID0); slot = 0x280000; }
	else { snprintf(name, sizeof name, "IMAGE%d.RAW", fid); slot = (uint32_t)fid * 0x20000u; }
	char path[1024];
	snprintf(path, sizeof path, "%s/%s", env("ST_DIR", "."), name);
	FILE *f = fopen(path, "rb");
	if (!f) return 0;
	fseek(f, 0, SEEK_END);
	const long len = ftell(f);
	if ((uint32_t)len != flen) { fclose(f); fprintf(stderr, "HOST: %s is %ld bytes, HOMES.BIN says %u\n", name, len, flen); return 0; }
	if (getenv("ST_LOG")) fprintf(stderr, "[%u] disc read: %s pieces %d-%d\n", frame, name, first, first + n - 1);
	for (int c = first; c < first + n; c++)
	{
		const uint32_t off = (uint32_t)c * 32768u;
		if (slot + off + 32768u > 0x3F0000u) { fclose(f); fprintf(stderr, "HOST: a piece past the cart's level area\n"); return 0; }
		fseek(f, (long)off, SEEK_SET);
		if (fread(cart + slot + off, 1, 32768, f) == 0) { fclose(f); return 0; }
		ticks += 300;                  /* about the 32X's time a piece */
		if (progress) progress(c - first + 1, n);
	}
	fclose(f);
	return 1;
}

void plat_sfx(int id) { if (getenv("ST_LOG")) fprintf(stderr, "[%u] sfx %d\n", frame, id); }
void plat_music(int track) { if (getenv("ST_LOG")) fprintf(stderr, "[%u] music %d%s\n", frame, track & 0x7FFF, track & 0x8000 ? " once" : ""); }

uint8_t *plat_scroll_ring(void) { return fbmem[back] + 0x200; }
uint8_t *plat_scroll_panel(void) { return fbmem[back] + 0x12E00; }
int plat_back_index(void) { return back; }
void plat_wait_flip(void) { }
void plat_border_colour(int index) { border = index & 255; }
uint32_t plat_ticks_ms(void) { return ticks; }

uint32_t plat_prof_us(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (uint32_t)(t.tv_sec * 1000000ull + t.tv_nsec / 1000);
}

uint16_t plat_read_pad(void)
{
	const char *s = env("ST_PAD", "");
	uint16_t state = 0;
	while (*s)
	{
		char *colon;
		const unsigned long at = strtoul(s, &colon, 10);
		if (*colon != ':') break;
		const char *v = colon + 1;
		const char *end = strchr(v, ',');
		if (!end) end = v + strlen(v);
		if (at <= frame)
		{
			uint16_t b = 0;
			for (const char *p = v; p < end; )
			{
				const char *q = p;
				while (q < end && *q != '+') q++;
				const size_t l = (size_t)(q - p);
				if (l == 1 && *p == 'A') b |= PAD_A;
				if (l == 1 && *p == 'B') b |= PAD_B;
				if (l == 1 && *p == 'C') b |= PAD_C;
				if (l == 1 && *p == 'X') b |= PAD_X;
				if (l == 1 && *p == 'Y') b |= PAD_Y;
				if (l == 1 && *p == 'Z') b |= PAD_Z;
				if (l == 5 && strncmp(p, "START", 5) == 0) b |= PAD_START;
				if (l == 2 && strncmp(p, "UP", 2) == 0) b |= PAD_UP;
				if (l == 4 && strncmp(p, "DOWN", 4) == 0) b |= PAD_DOWN;
				if (l == 4 && strncmp(p, "LEFT", 4) == 0) b |= PAD_LEFT;
				if (l == 5 && strncmp(p, "RIGHT", 5) == 0) b |= PAD_RIGHT;
				p = q < end ? q + 1 : q;
			}
			state = b;
		}
		s = *end ? end + 1 : end;
	}
	return state;
}

/* what the 32X shows: 224 lines through the line table of the buffer just finished; palette
 * entry 0 shows as 0x0400 (port/plat_mars.c) */
static void save_frame(const uint8_t *buf, int shift)
{
	char path[1024];
	snprintf(path, sizeof path, "%s/frame_%05u.ppm", env("ST_OUT", "."), frame);
	FILE *f = fopen(path, "wb");
	if (!f) return;
	fprintf(f, "P6\n320 224\n255\n");
	for (int line = 0; line < 224; line++)
	{
		const uint32_t word = (uint32_t)buf[2 * line] << 8 | buf[2 * line + 1];
		const uint8_t *px = buf + word * 2 + shift;
		for (int x = 0; x < 320; x++)
		{
			const uint16_t c = shown_pal[px[x]] ? shown_pal[px[x]] : 0x0400;
			uint8_t rgb[3] = { (uint8_t)((c & 31) << 3), (uint8_t)((c >> 5 & 31) << 3), (uint8_t)((c >> 10 & 31) << 3) };
			/* REV8: the 32X manual's "Priority" (REV6 showed the old model was wrong): PRI 0 the
			 * Genesis has priority, PRI 1 the 32X; a colour with the through bit goes to the other
			 * side. The side in front shows, unless it is the Genesis with a see-through pixel
			 * (colour 0 of its planes) - then the 32X colour behind it. */
			const int front32 = (host_pri != 0) ^ ((c & 0x8000) != 0);
			if (!front32) host_genesis_pixel(x, line, rgb);
			fwrite(rgb, 1, 3, f);
		}
	}
	fclose(f);
}

/* port/plat_mars.c plat_present_scroll (J19b): 12 border lines, view lines from the ring
 * (384 per row, 200 rows), the panel below (320 per line), the blank line beyond */
void plat_present_scroll(const uint16_t *palette555, int row0, int xoff, int viewH)
{
	uint8_t *fb = fbmem[back];
	for (int line = 0; line < 256; line++)
	{
		const int g = line - 12;
		uint32_t byte;
		if (g < 0 || g >= 200) byte = 0x16000;
		else if (g < viewH) byte = 0x200 + (uint32_t)((row0 + g) % 200) * 384u + (uint32_t)(xoff & ~1);
		else byte = 0x12E00 + (uint32_t)(g - viewH) * 320u;
		if ((byte >> 1 & 0xFF) == 0xFF && (xoff & 1))
		{
			fprintf(stderr, "HOST: line %d's table entry ends in 0xFF with the shift on (the 32X's shift-register bug)\n", line);
			exit(1);
		}
		fb[2 * line] = (uint8_t)(byte >> 9);
		fb[2 * line + 1] = (uint8_t)(byte >> 1);
	}
	memset(fb + 0x16000, border, 320);
	/* REV4: a frame is saved when the NEXT one is handed over - on the console the 32X shows it
	 * from the vertical blank on, and the game sets the Genesis' scroll for it right after that
	 * blank (game/gen_level.c gl_after_flip): the Genesis state at the next hand-over is what was
	 * on screen with it */
	static uint8_t pend_fb[0x20000];
	static uint16_t pend_pal[256];
	static int pend_shift, pend_have;
	static uint32_t pend_frame;
	const unsigned every = (unsigned)atoi(env("ST_SAVE_EVERY", "10"));
	if (pend_have)
	{
		memcpy(shown_pal, pend_pal, sizeof shown_pal);
		const uint32_t keep = frame;
		frame = pend_frame;
		save_frame(pend_fb, pend_shift);
		frame = keep;
		pend_have = 0;
	}
	if (every && frame % every == 0)
	{
		memcpy(pend_fb, fb, sizeof pend_fb);
		memcpy(pend_pal, palette555, sizeof pend_pal);
		pend_shift = xoff & 1;
		pend_frame = frame;
		pend_have = 1;
	}
	frame++;
	ticks += (uint32_t)atoi(env("ST_MS", "50"));
	back ^= 1;
	if (frame >= (uint32_t)atoi(env("ST_FRAMES", "300")) + 1) exit(0);
}

void plat_parallel(void (*fn)(void *, int), void *arg) { fn(arg, 0); fn(arg, 1); }
