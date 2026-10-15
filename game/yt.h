/*
 * SuperTux CD32X - Vic's tiler (YATSSD, yatssd/, MIT) on Hurrican CD32X's screen.
 *
 * WHAT IS VIC'S AND WHAT IS OURS
 *   Vic's code, unchanged (yatssd/), does the drawing:
 *     dtiles.c   draw_handle_layercmd() - draws one tile layer into the canvas and keeps the
 *                "dirty matrix" (which tile each canvas cell holds, so unchanged cells are
 *                skipped); draw_dirtyrect() - marks the cells a sprite covered
 *     dsprite.c  draw_sprite() - clips a sprite and calls the pixel loop; marks its cells dirty
 *     draw.c / draw_inc.h  the pixel loops (16-bit writes; the 32X "overwrite image" window
 *                makes colour 0 see-through), the scissor
 *     font.c     the 8x8 font (msx[]) - for the HUD
 *     sh2_fixed.s  FixedMul / IDiv (dsprite.c's scaled sprites)
 *   Ours (game/yt_layers.c, game/yt_mars.c), in place of Vic's draw_tile_layer() and hw_32x.c:
 *     the camera -> canvas arithmetic of draw_tile_layer(), WITHOUT its three hardware calls,
 *     which cannot run under Hurrican's boot path (docs/R1B_NOTES.md):
 *       - Hw32xUpdateLineTable() / MARS_VDP_SHIFTREG: the platform layer owns the line table
 *         and the shift register (plat_present_scroll writes them, the shift at the swap)
 *       - the COMM4 hand-shake with YATSSD's own slave-SH2 loop: our slave runs Hurrican's
 *         plat_parallel() jobs, not Vic's loop - the master would wait for ever
 *       - HwMd*(): the Genesis planes through YATSSD's own 68000 program - our 68000 runs the
 *         boot path's listener (cd32x/subcpu/hw_md.s), never changed
 *
 * THE SCREEN (Vic's canvas inside Hurrican's frame buffer layout, port/plat_mars.c J15)
 *   frame buffer byte 0x00200  the canvas: 384 bytes a line (canvas_pitch), 200 lines - the
 *                              same place and pitch as Hurrican's ring; CLASSIC uses it without
 *                              wrapping, as YATSSD does; R10's RING wraps it up / down
 *                              (game/yt_layers.c, "the canvas and its record")
 *   0x12E00                    the panel: 24 lines of 320 (the HUD and the timing line)
 *   0x14C00                    Vic's "extrafb" (canvas_yaw = 220 puts it here): R3-R9's dirty
 *                              matrix; R10 keeps its record in SDRAM instead
 *   0x16000                    the platform layer's blank line (the border lines)
 *   R2 ("Sharpened": half size, 16-pixel tiles - the R1 layout, kept). The view is 320 x 176: plat_present_scroll(pal, scroll_y, scroll_x, 176) shows canvas rows
 *   scroll_y .. scroll_y + 175 from column scroll_x, then the 24 panel lines.
 *   As in YATSSD, the canvas holds the view plus 64 columns and 16 rows: when the camera passes
 *   a multiple of 64 sideways or 16 up / down, the canvas is "re-anchored" and the dirty matrix
 *   redraws the cells whose tile changed. scroll_x stays 0..63, scroll_y 0..15, so the line
 *   table entries never end in 0xFF - the hardware shift-register bug cannot happen (that is
 *   why Vic chose 384: "the sweet spot").
 */
#ifndef STX_YT_H
#define STX_YT_H

#include <stdint.h>

#define YT_VIEW_W   320
#define YT_VIEW_H   176          /* the level's part of the screen */
#define YT_PANEL_H  24           /* the panel below it (plat_present_scroll's panel) */
#define YT_PITCH    384
#define YT_ROWS     200          /* canvas lines available (Hurrican's ring area) */
#define YT_YAW      220          /* canvas_yaw: only places the dirty matrix (0x14C00) */
#define YT_TILE     16
#define YT_SCROLL_X 64           /* re-anchor every 64 columns (4 tiles) ... */
#define YT_SCROLL_Y 16           /* ... and every 16 lines (1 tile) */

/* a level for the tiler: ONE layer of YATSSD tile numbers (resource << 2, 0 = nothing), w x h
 * tiles; reslist[r] = the 16x16 pixels of resource r. R3: tools/stx_pack.py merges a level's
 * four layers into one picture a cell (R1/R2 drew four layers: every cell with a tile over the
 * background was redrawn every frame); both SH2s draw the cells (yt_layers.c) */
void yt_level(const uint16_t *layer, int w, int h, uint8_t **reslist);

/* one frame (into the back buffer, after plat_wait_flip):
 *   yt_begin(cx, cy)        the camera (half-size pixels, top-left of the view)
 *   yt_layers(first, last)  draws tile layers first..last (Vic's draw_handle_layercmd)
 *   yt_sprite(...)          a sprite at VIEW coordinates (Vic's draw_sprite, overwrite mode)
 *   yt_scroll_x/y()         what to give plat_present_scroll */
void yt_begin(int cx, int cy);
int  yt_layers(int first, int last);          /* returns the tiles drawn */
int  yt_sprite(int vx, int vy, int w, int h, const uint8_t *pixels);
int  yt_scroll_x(void);
int  yt_scroll_y(void);
/* the cell of world tile (tx, ty) redrawn in this buffer (after yt_begin; animated tiles) */
void yt_mark_tile(int tx, int ty);
/* the sprites' rectangles (view coordinates, clipped) drawn since yt_rect_log(1) - the game draws
 * the foreground tiles again over them */
#define YT_RECTS 96
void yt_rect_log(int on);
int  yt_rect_count(void);
void yt_rect(int i, int *x, int *y, int *w, int *h);
/* R5, kept pictures (the clouds - game.c clouds_draw): yt_keep(1) draws sprites WITHOUT marking
 * them in the record, into the whole kept area (the view + the canvas around it), so they stay
 * in this buffer's canvas until the caller marks them away again (yt_mark_view_rect); yt_keep_box
 * cuts them to a smaller box (view pixels). yt_watch notes the record under a view rectangle
 * (R10: band numbers - 8 lines of a 16-pixel column - into ids) before yt_layers;
 * yt_watch_changed afterwards sets cells[id] for each band the tiler drew and returns how many.
 * yt_cell_box: a band's box in view pixels; yt_cell_tile: the world tile it belongs to.
 * YT_CELLS: band numbers are below it. */
#define YT_CELLS ((YT_PITCH / YT_TILE) * (YT_ROWS / 8))
void yt_keep(int on);
void yt_keep_box(int vx, int vy, int w, int h);
void yt_mark_view_rect(int vx, int vy, int w, int h);
int  yt_watch(int vx, int vy, int w, int h, int16_t *ids, int16_t *vals, int max);
int  yt_watch_changed(const int16_t *ids, const int16_t *vals, int n, uint8_t *cells);
void yt_cell_box(int id, int *vx, int *vy, int *w, int *h);
void yt_cell_tile(int id, int *tx, int *ty);
/* R10: the world tiles the canvas holds (tx0, ty0, nx x ny); the view's start in the canvas */
void yt_canvas_tiles(int *tx0, int *ty0, int *nx, int *ny);
int  yt_window_x(void);
/* R10: this buffer's pixel epoch - changes when its pixels stopped standing where the world
 * says (a new level / scroll way, or a re-anchor that left them in place): kept pictures must
 * be drawn again whole, without marking where they were */
unsigned yt_epoch(void);
/* R10, OPTIONS - SCROLL: 1 RING (up / down never moves the canvas), 0 CLASSIC; takes effect at the
 * next frame (both buffers draw everything once). yt_full_frame(1): this frame draws the whole
 * view itself (BACKGROUND MOVES) - a re-anchor then never moves the canvas. */
void yt_set_ring(int on);
void yt_full_frame(int on);
/* a word of the dirty matrix's tile-map id (forces a full redraw of both buffers) */
void yt_invalidate(void);

/* the menus and texts (not a level): the canvas at its corner (scroll 0, 0); yt_picture copies a
 * whole 320 x 176 picture into it (32-bit writes), yt_sprite then draws letters and the logo.
 * The tile map is invalidated, so a level drawn afterwards starts with a full redraw. */
void yt_screen(void);
void yt_picture(const uint8_t *pixels);       /* platform part: game/yt_mars.c, game/yt_host.c */

/* R4, OPTIONS - BACKGROUND MOVES: the view filled from a repeating picture (pw x ph, rows of
 * stride bytes) from its pixel (sx, sy); the level's tiles are then drawn over it as sprites */
void yt_back(const uint8_t *pic, int stride, int pw, int ph, int sx, int sy);

/* platform parts (game/yt_mars.c on the 32X, game/yt_host.c on the PC) */
uint8_t *yt_canvas(void);                     /* the canvas: frame buffer byte 0x200, 384 a line */
void yt_copy_row(uint8_t *dst, const uint8_t *src, int n);    /* into the canvas (same parity) */
void yt_move_row(uint8_t *dst, const uint8_t *src, int n);    /* inside the canvas, 4-aligned */
int16_t *yt_extrafb(void);                    /* Vic's extrafb in the back buffer */
void yt_tile_counter_reset(void);             /* COMM6 = 0 (Vic's tile counter, master only) */

#endif
