/*
 * Tyrian 32X - platform layer.
 *
 * Two implementations:
 *   plat_mars.c  the real 32X (SH2 master CPU)
 *   plat_host.c  a PC test build that runs the same game + port code,
 *                reads the same ROM file system image and saves frames to disk
 */
#ifndef T32X_PLAT_H
#define T32X_PLAT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Pad bits as delivered by the 68000 (cart_md.s, command 48), active-high.
 * This is the common Genesis 6-button layout used by D32XR.
 * If Kobo's cart_md.s packs the bits differently, change only these. */
#define PAD_UP     0x0001
#define PAD_DOWN   0x0002
#define PAD_LEFT   0x0004
#define PAD_RIGHT  0x0008
#define PAD_B      0x0010
#define PAD_C      0x0020
#define PAD_A      0x0040
#define PAD_START  0x0080
#define PAD_Z      0x0100
#define PAD_Y      0x0200
#define PAD_X      0x0400
#define PAD_MODE   0x0800

/* Screen geometry: the game draws 320x200; the 32X shows 224 lines (NTSC). */
#define PLAT_SCREEN_W   320
#define PLAT_SCREEN_H   224
#define PLAT_GAME_H     200
#define PLAT_TOP_BORDER ((PLAT_SCREEN_H - PLAT_GAME_H) / 2)

void plat_init(void);

/* Free-running millisecond clock. */
uint32_t plat_ticks_ms(void);

/* Seed for the game's random numbers (replaces time(NULL)). */
uint32_t plat_random_seed(void);

/* Current pad state (PAD_* bits). */
uint16_t plat_read_pad(void);

/* Show one 320x200 8-bit frame with the given 256-entry palette
 * (32X CRAM format 0BBBBBGGGGGRRRRR). Waits for vertical blank and flips. */
void plat_present(const uint8_t *pixels, int pitch, const uint16_t *palette555);

/* Scratch space in the 32X frame buffer (see port/plat_mars.c for the layout).
 * The SH2 only reaches the frame buffer that is NOT on screen, through one fixed
 * address window; every plat_present() swaps which physical buffer that is.
 * So these areas keep their contents only until the next plat_present():
 * afterwards they show the other buffer's contents (from two flips ago).
 *   PLAT_FB_SPARE    64000 bytes, 320 per line: free for the game
 *   PLAT_FB_DISPLAY  where plat_present() puts the picture: usable as scratch
 *                    only for data that is finished before the next present */
#define PLAT_FB_SPARE   0
#define PLAT_FB_DISPLAY 1
uint8_t *plat_fb_area(int area);

/* J5: the game screen drawn straight into the 32X frame buffer (saves the 64,000-byte SDRAM
 * screen and its copy every frame). plat_canvas: where to draw (320 per line, 200 lines), or
 * NULL when the platform has no such thing (PC); plat_present_canvas: show it. */
uint8_t *plat_canvas(void);
void plat_present_canvas(const uint16_t *palette555);
/* J13: the palette entry the border lines above/below the picture show */
void plat_border_colour(int index);

/* J15: the scrolling renderer. Each frame buffer holds a RING (OJ32X_RING_W x OJ32X_RING_H,
 * world row wy at ring row wy % H) and a panel block (320 x 33). plat_back_index: which of the
 * two buffers is being drawn now (0/1). plat_present_scroll: line table = view lines from the
 * ring (starting at ring row row0, column xoff - an odd xoff uses the 32X's 1-pixel shift),
 * then the panel lines; then present as usual. */
uint8_t *plat_scroll_ring(void);
uint8_t *plat_scroll_panel(void);
int plat_back_index(void);
void plat_present_scroll(const uint16_t *palette555, int row0, int xoff, int viewH);
/* J16: wait until the last requested buffer swap has happened (OJ32X_FLIP_ASYNC: presenting
 * only requests it, so the game's logic runs while the 32X waits for the next vblank) */
void plat_wait_flip(void);
/* J17: 0 = Jazz Jackrabbit CD, 1 = Holiday Hare '95 (chosen on the splash; PC: T32X_HH95=1) */
int plat_game(void);
void plat_set_game(int g);           /* J18d */
int plat_hh95_available(void);       /* J18d: OJ600.PAK on the disc */
/* J19: the port's own settings (0 = FPS counter, 1 = A/B swap), kept in the RAM cart */
int plat_port_setting(int key);
void plat_port_setting_set(int key, int value);
/* J13d: both frame buffers' picture areas cleared (32X); PC: nothing */
void plat_clear_screens(void);

/* Like plat_present, but the playfield (x 0..263, rows 0..183) is already in
 * the back buffer's picture area (drawn there directly during levels, see
 * port/video32x.c): only the status bar parts of `pixels` are copied - the
 * right strip (x 264..319, rows 0..183) and rows 184..199. */
#define PLAT_PLAYFIELD_W 264
#define PLAT_PLAYFIELD_H 184
void plat_present_hud(const uint8_t *pixels, int pitch, const uint16_t *palette555);

/* DMA copy into the frame buffer (port/backgrnd.c: layer 1, one screen line
 * at a time). dst and src 4-byte aligned, bytes a multiple of 4.
 * plat_dma_start returns at once on the 32X (the SH2's DMA unit copies while
 * the CPU goes on); plat_dma_wait waits for the last one to finish. If the DMA
 * unit ever does not finish in time, plat_dma_ok() becomes false for good and
 * the caller falls back to CPU copying. PC build: a plain memcpy. */
void plat_dma_start(void *dst, const void *src, unsigned int bytes);
void plat_dma_wait(void);
bool plat_dma_ok(void);

/* Sound effect mixer command (port/mixer.h): on the 32X it goes to the slave
 * SH2 through a small ring in uncached SDRAM; the PC build mixes directly. */
#include "mixer.h"
void plat_sound_command(const MixCmd *c);

/* Music: a CD audio track on the Sega CD, played on repeat by the 68000
 * (cart_md.s commands 45 and 54; without a Sega CD the 68000 ignores them).
 * Track 1 is the CD's data track; Tyrian's song n is track n + 2
 * (tools/render_music, tools/make_tyrian_cd.py). */
void plat_music_play(unsigned int track);
void plat_music_stop(void);

/* Files on the Sega CD's data track (ISO 9660 root, 8.3 names; the level
 * files). plat_cd_open: true if found, with the file's first sector and its
 * length. plat_cd_read: `bytes` (<= 4096) from `offset` (a multiple of 2048)
 * into dst. On the 32X: the 68000 (cart/cd_files.c, commands 60/61) reads
 * through D32XR's Sega CD code and hands the bytes over the COMM registers.
 * PC build: files in the folder T32X_CD_DIR. */
bool plat_cd_present(void);  /* a Sega CD is there (no disc access; cached) */
bool plat_cd_open(const char *name, uint32_t *sector, uint32_t *length);
bool plat_cd_read(uint32_t sector, uint32_t offset, uint32_t bytes, void *dst);

/* Sega Mouse in controller port 2: movement since the last call (Y up =
 * positive, as the mouse counts) and buttons (bit 0 left, 1 right, 2 middle,
 * 3 start); false when no mouse is there. 32X: command 63 (cart/mouse.c). */
#define MOUSE_LEFT   1
#define MOUSE_RIGHT  2
#define MOUSE_MIDDLE 4
#define MOUSE_START  8
bool plat_read_mouse(int *dx, int *dy, uint8_t *buttons);

/* Network play over controller port 2 (D32XR's transport, cart/net_link.s):
 * type 1 = link cable, -1 = serial (4800 baud). Bytes can be lost; the game
 * protocol copes (port/sdlnet32x.c, src/network.c). PC build: two instances
 * connected by named pipes (T32X_LINK_IN / T32X_LINK_OUT). */
#define NET_LINK    1
#define NET_SERIAL (-1)
bool plat_net_setup(int type);
void plat_net_cleanup(void);
bool plat_net_send(const uint8_t *bytes, unsigned int count);   /* false: a byte timed out */
unsigned int plat_net_recv(uint8_t *bytes, unsigned int most);   /* bytes waiting, 0 = none */

/* OpenJazz J3: the RAM cart's data area (read-only for the SH2). plat_cart_data returns its
 * start and size; plat_cart_write copies bytes to an offset in it (32X: through the 68000;
 * PC: a read-only mapping opened just for the copy). */
#define PLAT_CART_DATA_OFFSET 0x100000   /* after 1 MB for the engine (J4a estimate: ~0.45 MB) */
#define PLAT_CART_DATA_END    0x3F0000   /* 0x3F0000.. settings and saves (as OpenLara) */
uint8_t *plat_cart_data(uint32_t *size);
void plat_cart_write(uint32_t offset, const void *src, size_t bytes);

/* J5: a level pack from the disc into the cart at <cart_offset> (32X: command 38 number <id>,
 * command 46 chunks, with the loading screen; PC: T32X_PACK_DIR/OJ<id-200>.PAK). Returns its
 * length, or a negative number when there is none. plat_cart_ptr: where the game reads a cart
 * offset. */
int32_t plat_load_pack(int id, uint32_t cart_offset, uint32_t max_bytes);
void plat_packs_ready(void);

/* J10: fn(arg, 1) on the slave SH2 while fn(arg, 0) runs here; returns when both are done
 * (PC: one after the other). fn may only draw and read - no memory allocation, no logging. */
void plat_parallel(void (*fn)(void *, int), void *arg);

/* J10: play sound effect n (OpenJazz SE::Type, 1..) - 32X: Sega CD PCM; PC: nothing */
void plat_sfx(int id);
/* J10: CD audio track n, repeating (0 = stop); only with OJ32X_MUSIC 1 (tracks on the disc) */
void plat_music(int track);
/* J14: a microsecond clock for profiling (wraps after ~71 minutes) */
uint32_t plat_prof_us(void);
/* J14b: slave SH2 drawing on/off at run time (PC: always "off" - one after the other) */
void plat_slave_draws(int on);
int plat_slave_draws_on(void);   /* PC: mount T32X_PACK now if not yet; 32X: nothing */
const uint8_t *plat_cart_ptr(uint32_t cart_offset);

/* Base of the ROM file system image (see tools/mkromfs.py). */
const uint8_t *plat_romfs_base(void);

/* Bytes that malloc can still get (for the tile cache, port/tile_cache.c). */
size_t plat_heap_free(void);

/* Memory report at a point of interest (PC build: prints the heap; 32X: no-op). */
void plat_mem_mark(const char *label);

/* Debug text output (no-op on the 32X). */
void plat_log(const char *msg);

/* Fatal error: show the message and stop. Never returns. */
void plat_fatal(const char *msg) __attribute__((noreturn));

/* Normal program exit. Never returns. */
void plat_exit(int code) __attribute__((noreturn));

#endif
