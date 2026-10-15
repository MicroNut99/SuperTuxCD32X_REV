/*
 * SuperTux CD32X - a level being played: SuperTux 0.1.3's src/gameloop.cpp (GameSession: the
 * level intro, the steps, the end sequence, time, music, dying and starting again at a reset
 * point, game over) and world.cpp (World: the order of the objects' actions, the camera, the
 * brick / box / coin rules, the particles), with Vic's tiler for the drawing. See game/stx.h.
 *
 * TIME: SuperTux moves everything by frame_ratio = elapsed / 10 ms. Here the world always takes
 * whole 10 ms steps (step_clock: as many as the time since the last frame, at most 10), so the
 * rules give the same results however fast the 32X draws.
 *
 * THE SCREEN: the 32X shows SuperTux's whole width (640 units = 320 pixels) and 352 of its 480
 * lines (176 pixels); the camera follows Tux up and down (ours: SuperTux 0.1.3 never scrolls
 * vertically). Under the view: the panel (score, time, coins, lives, the level, the timing).
 */
#include "stx.h"
#ifndef OJ32X_MARS
#include <stdio.h>
#include <stdlib.h>
#endif

PlayerStatus ps = { 0, 0, START_LIVES, 0 };
fx scroll_x;
int cam_y;
uint32_t game_ms, global_frame;
int opt_music = 1, opt_timing = 0, opt_swap = 0, opt_lives = START_LIVES;
int opt_smooth = 1;                    /* R9: OPTIONS - MOTION (R6's drawing between steps) */
int opt_ring = 1;                      /* R10: OPTIONS - SCROLL RING (1) / CLASSIC (0) */
int opt_dma = 0;                       /* REV: OPTIONS - DRAW <CPU> (0) / <DMA> (1) */
int opt_parallax = 0;                  /* R4: OPTIONS - BACKGROUND FIXED 0 / MOVES 1; REV3: GENESIS 2 */

void ps_reset(void)
{
	ps.score = 0;
	ps.distros = 0;
	ps.lives = opt_lives;
	ps.bonus = 0;
}

/* ------------------------------------------------------------------ music, buttons, time */
static int track_now = -1;

/* only when it changes (OpenLara lesson 5: never restart a track every frame) */
void music(int track)
{
	if (!opt_music && track) track = 0;
	if (track == track_now) return;
	track_now = track;
	plat_music(track);
}
static int track_wanted;
static void music_want(int track) { track_wanted = track; music(track); }
void music_again(void)
{
	track_now = -1;
	music(opt_music ? track_wanted : 0);
}

static uint16_t pad_raw;               /* the pad as the console sent it (R8) */
static uint32_t pad_stale;             /* R9: pad replies that came back old (see pad_read) */
static uint16_t pad_prev = 0xFFFF;     /* nothing counts as pressed until it is let go once */
uint16_t pad_read(uint16_t *hit)
{
	uint16_t p = plat_read_pad();
	pad_raw = p;                       /* R8: shown with the timing line (draw_status) */
	{
		/* R9: each pad reply brings the 68000's frame counter (port/plat_mars.c CMD 48, COMM10).
		 * When a refresh or more has passed since the last request and the counter has not
		 * moved, the console handed back an old pad: counted, and shown with the timing line */
		static uint16_t last_tick;
		static uint32_t last_ms;
		const uint32_t now = plat_ticks_ms();
		const uint16_t tick = yt_pad_tick();
		if (now - last_ms >= 16 && tick == last_tick) pad_stale++;
		last_tick = tick;
		last_ms = now;
	}
	if (opt_swap)
		p = (uint16_t)((p & ~(PAD_A | PAD_B)) | ((p & PAD_A) ? PAD_B : 0) | ((p & PAD_B) ? PAD_A : 0));
	if (hit) *hit = p & ~pad_prev;
	pad_prev = p;
	return p;
}

static uint32_t clock_last, clock_acc;
int interp_a, interp_on;               /* stx.h lerp_fx */
void step_clock_reset(void) { clock_last = plat_ticks_ms(); clock_acc = 0; interp_a = 0; }
int step_clock(void)
{
	const uint32_t now = plat_ticks_ms();
	uint32_t d = now - clock_last;
	clock_last = now;
	if (d > 200) d = 200;
	clock_acc += d;
	int steps = (int)(clock_acc / STEP_MS);
	clock_acc -= (uint32_t)steps * STEP_MS;
	interp_a = (int)(clock_acc * 256u / STEP_MS);   /* R5: how far into the next step */
	return steps > 10 ? 10 : steps;
}

/* ------------------------------------------------------------------ REV3: the Genesis picture
 * OPTIONS - BACKGROUND GENESIS: a level read with it (game/level.c gen_bg) shows its background
 * picture on the Genesis' plane B, behind the 32X (the level's own pictures have through-bit
 * colours where the picture shows - tools/stx_pack.py). The Genesis scrolls it with its own
 * scroll registers at the level's background speed (SuperTux's bkgd_speed): no drawing at all.
 * Each frame that draws the level asks for it (gen_frame); every frame shown decides
 * (gen_present, from present_view): the first level frame uploads the picture (GENBG.BIN:
 * palettes, tiles, plane map - SH2 commands 64 and 63) and switches the Genesis display on; the
 * first frame of anything else switches it off. The picture is 0.8 x the 32X's size
 * (tools/stx_genesis.py), so it moves 0.8 x as many pixels.
 * VRAM as REV4 (game/gen_level.c, tools/stx_genesis.py): sprite table 0xD000, H scroll table
 * 0xD400 (both never written: empty / 0); the picture's map at 0xE000 is plane B AND plane A
 * (both scrolled alike: plane A over plane B shows the same picture) - the green / white boot
 * text stays on 0xC000, hidden (REV3's first version pointed plane A at 0xB000 instead: REV4's
 * tiles live there now). Leaving: display off, md_init_hw's planes, sprites, scroll table. */
static int gen_want, gen_on;
static int gl_in_loop;                 /* REV4: the game loop draws this frame (game/gen_level.c) */
static unsigned gen_up_serial = 0xFFFFFFFFu;
static uint16_t gen_hs = 0xFFFF, gen_vs = 0xFFFF, gen_want_hs, gen_want_vs;
static void gen_frame(int cx, int cy)
{
	if (!gen_bg) return;
	gen_want = 1;
	const int bx = cx * lbkgd_speed / 100 * 4 / 5;
	gen_want_hs = (uint16_t)(-bx & 0x3FF);
	/* up / down: the 192-line picture over the 176-line view, the level's height mapped onto the
	 * 16 lines it can move; the view starts at screen line 12 */
	const int travel = lh * YT_TILE - YT_VIEW_H;
	int by = travel > 0 ? cy * 16 / travel : 0;
	if (by < 0) by = 0;
	if (by > 16) by = 16;
	gen_want_vs = (uint16_t)((by - 12) & 0x3FF);
}
static void gen_present(void)
{
	if (gen_want && gen_bg)
	{
		if (gen_up_serial != gen_bg_serial)
		{
			if (gen_on) { gen_vdp_reg(0x8134); gen_on = 0; }   /* no half-loaded picture */
			const unsigned n = be16(gen_bg + 4);
			gen_cart_to_cram(0, gen_bg + 8, 64);
			gen_cart_to_vram(0xE000, gen_bg + 136, 64 * 32);
			gen_cart_to_vram(0x1000, gen_bg + 4232, n * 16);
			gen_up_serial = gen_bg_serial;
			gen_hs = gen_vs = 0xFFFF;
		}
		if (!gen_on)
		{
			gen_vdp_reg(0x8568);       /* sprite table -> 0xD000 (empty) */
			gen_vdp_reg(0x8D35);       /* H scroll table -> 0xD400 */
			gen_vdp_reg(0x8407);       /* plane B -> 0xE000 (the picture) */
			gen_vdp_reg(0x8238);       /* plane A -> 0xE000 too */
			gen_hs = gen_vs = 0xFFFF;
		}
		if (gen_want_hs != gen_hs)
		{
			gen_vram_word(0xD400, gen_want_hs); gen_vram_word(0xD402, gen_want_hs);   /* A, B */
			gen_hs = gen_want_hs;
		}
		if (gen_want_vs != gen_vs) { gen_vsram(0, gen_want_vs); gen_vsram(2, gen_want_vs); gen_vs = gen_want_vs; }
		if (!gen_on)
		{
			gen_vdp_reg(0x8174);       /* Genesis display on (V interrupt stays on) */
			gen_on = 1;
		}
	}
	else if (gen_on)
	{
		gen_vdp_reg(0x8134);           /* display off, as the game had it */
		gen_vdp_reg(0x8230);           /* md_init_hw's plane A 0xC000, plane B 0xE000, sprites 0xA800, */
		gen_vdp_reg(0x8407);           /* H scroll 0xAC00 */
		gen_vdp_reg(0x8554);
		gen_vdp_reg(0x8D2B);
		gen_on = 0;
	}
	gen_want = 0;
}

void present_view(void)
{
	gen_present();
	gl_present();                      /* REV4 */
	plat_present_scroll(palette, yt_scroll_y(), yt_scroll_x(), YT_VIEW_H);
}

/* ------------------------------------------------------------------ the world's rules (world.cpp) */
static int counting_distros, distro_counter;

void trybreakbrick(int x, int y, int small, int dir)
{
	const int t = tile_at(x, y);
	const TileDesc *d = &tdesc[t];
	if (d->flags & TF_BRICK)
	{
		if (d->data > 0)
		{
			/* a brick with coins: five of them, then it becomes its next tile */
			add_bouncy_distro(((x + 1) / 32) * 32, (y / 32) * 32);
			if (!counting_distros) { counting_distros = 1; distro_counter = 5; }
			else distro_counter--;
			if (distro_counter <= 0)
			{
				counting_distros = 0;
				level_change(x, y, d->next);
			}
			plat_sfx(SFX_DISTRO);
			ps.score += SCORE_DISTRO;
			ps.distros++;
		}
		else if (!small)
		{
			level_change(x, y, d->next);
			add_broken_brick(t, ((x + 1) / 32) * 32, (y / 32) * 32);
			plat_sfx(SFX_BRICK);
			ps.score += SCORE_BRICK;
		}
	}
	else if (d->flags & TF_FULLBOX) tryemptybox(x, y, dir);
}

void tryemptybox(int x, int y, int dir)
{
	const int t = tile_at(x, y);
	const TileDesc *d = &tdesc[t];
	if (!(d->flags & TF_FULLBOX)) return;
	const int side = dir == 0 ? 1 : 0;   /* the upgrade goes away from the side it was hit on */
	const int px = ((x + 1) / 32) * 32, py = (y / 32) * 32 - 32;
	switch (d->data)
	{
	case 1:                            /* a coin */
		add_bouncy_distro(px, py);
		plat_sfx(SFX_DISTRO);
		ps.score += SCORE_DISTRO;
		ps.distros++;
		break;
	case 2:                            /* small Tux: the egg (grow), big Tux: the ice flower */
		add_upgrade(px, py, side, tux.size == SMALL ? UP_GROWUP : UP_ICEFLOWER);
		plat_sfx(SFX_UPGRADE);
		break;
	case 3: add_upgrade(px, py, side, UP_HERRING); break;
	case 4: add_upgrade(px, py, side, UP_1UP); break;
	default: break;
	}
	level_change(x, y, d->next);
}

void trygrabdistro(int x, int y, int bounce)
{
	const int t = tile_at(x, y);
	if (!(tdesc[t].flags & TF_DISTRO)) return;
	level_change(x, y, tdesc[t].next);
	plat_sfx(SFX_DISTRO);
	if (bounce) add_bouncy_distro(((x + 1) / 32) * 32, (y / 32) * 32);
	ps.score += SCORE_DISTRO;
	ps.distros++;
}

void bumpbrick(int x, int y)           /* gameloop.cpp bumpbrick */
{
	add_bouncy_brick(((x + 1) / 32) * 32, (y / 32) * 32);
	plat_sfx(SFX_BRICK);
}

/* ------------------------------------------------------------------ particles (particlesystem.cpp) */
/* Snow: SuperTux makes 128 flakes over twice the screen's width; every flake drawn makes the
 * tiler redraw the cells under it in the next frame (and the far ones get the level's tiles
 * drawn over them again), so the 32X has 24 (R2: 32; a fifth of SuperTux's density). Clouds: 15 over 2000 units, as SuperTux. Both stay on
 * the screen while the level scrolls under them (x: virtual width, y: the view). */
#define NSNOW   24
#define NCLOUD  15
typedef struct { fx x, y, speed; uint8_t layer, img; } Particle;
static Particle parts[NSNOW > NCLOUD ? NSNOW : NCLOUD];
static int nparts, part_kind, part_vw;

static void clouds_reset(void);   /* R5: the kept clouds (below, with the drawing) */
static int view_cx, view_cy;           /* this frame's camera (half-size pixels) */
static int drew_parallax;              /* the last frame was drawn with BACKGROUND MOVES */

static void particles_start(void)
{
	part_kind = lparticles;
#ifndef OJ32X_MARS
	if (getenv("ST_NOPARTICLES")) part_kind = 0;      /* PC only: to measure what the particles cost */
#endif
	nparts = 0;
	if (part_kind == 1)
	{
		part_vw = VIEW_W_U * 2;        /* SuperTux: virtual_width = screen width x 2 */
		for (int i = 0; i < NSNOW; i++)
		{
			Particle *p = &parts[nparts++];
			p->x = (fx)(rnd() % (uint32_t)part_vw) << 16;
			p->y = (fx)(rnd() % VIEW_H_U) << 16;
			p->layer = (uint8_t)(i % 2);
			p->img = (uint8_t)(rnd() % 3);
			/* speed = size / 60 + (rand % 10) / 300, at least 0.01, times the gravity */
			fx sp;
			do sp = (fx)(p->img * 65536 / 60) + (fx)((rnd() % 10) * 65536 / 300); while (sp < FX(0.01));
			p->speed = sp * lgrav;
		}
	}
	else if (part_kind == 2)
	{
		clouds_reset();
		part_vw = 2000;
		for (int i = 0; i < NCLOUD; i++)
		{
			Particle *p = &parts[nparts++];
			p->x = (fx)(rnd() % 2000u) << 16;
			p->y = (fx)(rnd() % VIEW_H_U) << 16;
			p->layer = 0;
			p->img = 0;
			p->speed = -(fx)((250 + rnd() % 200) * 65536 / 1000);
		}
	}
}

static void particles_step(void)
{
	for (int i = 0; i < nparts; i++)
	{
		Particle *p = &parts[i];
		if (part_kind == 1)
		{
			p->y += p->speed;          /* simulate(frame_ratio): speed a step */
			if (FX_INT(p->y) > VIEW_H_U)
			{
				p->y -= (fx)VIEW_H_U << 16;
				p->x = (fx)(rnd() % (uint32_t)part_vw) << 16;
			}
		}
		else
		{
			p->x += p->speed;
			if (p->x < 0) p->x += (fx)part_vw << 16;
		}
	}
}

/* R5: a far snowflake (behind the level's tiles) is drawn only where the cells under it hold
 * no tile at all (layers 1-3): over a tile it would be hidden anyway, and R3/R4 paid for that by
 * drawing the tiles of its cells again over it (overlay_cells) - and the tiler those cells
 * again in the next frame. A flake can now vanish up to a cell early at the edge of a tile with
 * see-through parts; at this size and speed that does not show. */
static int far_flake_hidden(int vx, int vy, int w, int h)
{
	const int cx = view_cx, cy = view_cy;
	for (int ty = (vy + cy) / YT_TILE; ty <= (vy + h - 1 + cy) / YT_TILE; ty++)
		for (int tx = (vx + cx) / YT_TILE; tx <= (vx + w - 1 + cx) / YT_TILE; tx++)
			if (level_tile(1, tx, ty) || level_tile(2, tx, ty) || level_tile(3, tx, ty)) return 1;
	return 0;
}

static void particles_draw(int layer)
{
	const int sx = FX_INT(scroll_x);
	for (int i = 0; i < nparts; i++)
	{
		const Particle *p = &parts[i];
		if (p->layer != layer) continue;
		int x = (FX_INT(p->x) - sx) % part_vw;
		if (x < 0) x += part_vw;
		if (x > VIEW_W_U) x -= part_vw;
		const int s = part_kind == 1 ? S_SNOW0 + p->img : S_CLOUD;
		int w, h, dx, dy;
		const uint8_t *px = spr_frame_at(s, 0, &w, &h, &dx, &dy);
		const int vx = px_of(x) + dx, vy = px_of(FX_INT(p->y)) + dy;
		if (layer == 0 && part_kind == 1 && !drew_parallax && far_flake_hidden(vx, vy, w, h)) continue;
		yt_sprite(vx, vy, w, h, px);
	}
}

/* ------------------------------------------------------------------ timing (OPTIONS - TIMING) */
enum { T_LOGIC, T_TILES, T_SPR, T_HUD, T_WAIT, T_N };
static uint32_t tacc[T_N], tshow[T_N], tframes, tms, fps10, tiles_acc, tiles_show;
/* REV, TIMING <WORST>: the averages hide a single slow frame (the sideways shift every 48-64
 * pixels). Each frame's own parts (tfr) are kept too; the slowest frame of the last 64 (L + T + S
 * + H, the time before it can be shown) is latched with what its re-anchor did and how many
 * cells it drew (and how many of them as DMA rows). */
static uint32_t tfr[T_N];
#define TADD(i, d) do { const uint32_t d_ = (d); tacc[i] += d_; tfr[i] += d_; } while (0)
typedef struct { uint32_t t[T_N], busy, shift_us; int shift, cells, dma_cells; } WorstFrame;
static WorstFrame worst_now, worst_show;
static uint32_t worst_frames;
static uint32_t frame_cells;           /* this frame's tiler cells (stat_cells, below) */

static void timing_frame(void)
{
	const uint32_t busy = tfr[T_LOGIC] + tfr[T_TILES] + tfr[T_SPR] + tfr[T_HUD];
	if (busy >= worst_now.busy)
	{
		memcpy(worst_now.t, tfr, sizeof tfr);
		worst_now.busy = busy;
		worst_now.shift = yt_frame_shift(&worst_now.shift_us);
		worst_now.cells = (int)frame_cells;
		worst_now.dma_cells = yt_frame_dma_cells();
	}
	memset(tfr, 0, sizeof tfr);
	if (++worst_frames >= 64)
	{
		worst_show = worst_now;
		memset(&worst_now, 0, sizeof worst_now);
		worst_frames = 0;
	}
	if (++tframes < 16) return;
	const uint32_t ms = plat_ticks_ms();
	fps10 = ms != tms ? 16u * 10000u / (ms - tms) : 0;
	tms = ms;
	for (int i = 0; i < T_N; i++) { tshow[i] = tacc[i] / 16; tacc[i] = 0; }
	tiles_show = tiles_acc / 16;
	tiles_acc = 0;
	tframes = 0;
}

/* ms as "9.9" below 10, "12" from 10 on (the WORST line has to fit 39 characters) */
static char *put_ms(char *q, uint32_t us)
{
	if (us >= 10000) return put_dec(q, us / 1000, 1);
	q = put_dec(q, us / 1000, 1); q = put_str(q, ".");
	return put_dec(q, (us / 100) % 10, 1);
}

/* F frames a second; ms a frame: L the rules, T the tile layers, S the sprites, H the panel,
 * W waiting for the swap; N tiles drawn a frame.
 * REV, TIMING <WORST> (opt_timing 2): the slowest frame of the last 64 -
 *   X its time before it could be shown (L + T + S + H), then its L T S H,
 *   M what its re-anchor did: M- none, Mn.n the canvas moved (ms), ML left in place (its
 *     cells are in T and N), N its cells, D how many of them went through the DMA rows
 *     (shown when any did) */
void timing_line(char *out)
{
	if (opt_timing == 2)
	{
		const WorstFrame *w = &worst_show;
		char *q = put_str(out, "X");
		q = put_ms(q, w->busy);
		static const char *const wn[4] = { " L", " T", " S", " H" };
		for (int i = 0; i < 4; i++) { q = put_str(q, wn[i]); q = put_ms(q, w->t[i]); }
		q = put_str(q, " M");
		if (w->shift == 1) q = put_ms(q, w->shift_us);
		else q = put_str(q, w->shift == 2 ? "L" : "-");
		q = put_dec(put_str(q, " N"), (uint32_t)w->cells, 1);
		if (w->dma_cells) q = put_dec(put_str(q, " D"), (uint32_t)w->dma_cells, 1);
		return;
	}
	char *q = put_str(out, "F");
	q = put_dec(q, fps10 / 10, 2); q = put_str(q, "."); q = put_dec(q, fps10 % 10, 1);
	static const char *const nm[T_N] = { " L", " T", " S", " H", " W" };
	for (int i = 0; i < T_N; i++)
	{
		q = put_str(q, nm[i]);
		q = put_dec(q, tshow[i] / 1000, 1); q = put_str(q, "."); q = put_dec(q, (tshow[i] / 100) % 10, 1);
	}
	q = put_str(q, " N");
	put_dec(q, tiles_show, 1);
}

/* ------------------------------------------------------------------ the camera (World::scrolling) */
#define X_SPACE (400 - 16)
#define CHANGE_DIR_SCROLL_STEPS 200    /* CHANGE_DIR_SCROLL_SPEED 2000 ms */
static int scrolling_timer;

static void scrolling(void)
{
	if (lautoscroll)
	{
		scroll_x += (fx)lautoscroll << 8;   /* hor_autoscroll_speed x frame_ratio */
	}
	else
	{
		const fx tux_pos_x = tux.x + (16 << 16);
		const fx right = tux_pos_x - ((VIEW_W_U - X_SPACE) << 16), left = tux_pos_x - (X_SPACE << 16);
		if (lback_scroll)
		{
			if (tux.old_dir != tux.dir) scrolling_timer = CHANGE_DIR_SCROLL_STEPS;
			if (scrolling_timer)
			{
				scrolling_timer--;
				fx final = scroll_x;
				if (tux.ph.vx > 0) final = right;
				else if (tux.ph.vx < 0) final = left;
				else if (tux.dir == 1) final = right;
				else final = left;
				scroll_x += (final - scroll_x) / (CHANGE_DIR_SCROLL_STEPS / 10) + tux.ph.vx + tux.ph.ax;
			}
			else
			{
				if (tux.ph.vx > 0 && scroll_x < right) scroll_x = right;
				else if (tux.ph.vx < 0 && scroll_x > left) scroll_x = left;
				else if (tux.dir == 1 && scroll_x < right) scroll_x = right;
				else if (tux.dir == 0 && scroll_x > left) scroll_x = left;
			}
		}
		else
		{
			if (tux.ph.vx > 0 && scroll_x < right) scroll_x = right;
			else if (tux.dir == 1 && scroll_x < right) scroll_x = right;
		}
	}
	if (scroll_x < 0) scroll_x = 0;
	if (scroll_x > ((lw * 32 - VIEW_W_U) << 16)) scroll_x = (lw * 32 - VIEW_W_U) << 16;
}

/* ours: up and down. The camera moves when Tux leaves the middle band of the view, and not
 * while he falls out of the level (so he leaves the screen, as in SuperTux). */
static void camera_y(int snap)
{
	const int maxy = lh * 32 - VIEW_H_U;
	const int y = FX_INT(tux.y);
	if (snap) cam_y = y + tux.h / 2 - VIEW_H_U / 2;
	else if (tux.dying == DYING_NOT)
	{
		if (y - 96 < cam_y) cam_y = y - 96;
		if (y + tux.h + 112 > cam_y + VIEW_H_U) cam_y = y + tux.h + 112 - VIEW_H_U;
	}
	if (cam_y > maxy) cam_y = maxy;
	if (cam_y < 0) cam_y = 0;
}

/* Player::check_bounds */
#define AUTOSCROLL_DEAD_INTERVAL 300
static void check_bounds(void)
{
	if (tux.x < 0) tux.x = 0;
	if (FX_INT(tux.y) > lh * 32 && tux.dying == DYING_NOT) tux_kill(0);
	if (tux.x < scroll_x && (!lback_scroll || lautoscroll)) tux.x = scroll_x;
	if (lautoscroll)
	{
		const int x = FX_INT(tux.x), y = FX_INT(tux.y);
		if (tux.x == scroll_x && tux.dying == DYING_NOT
		    && (issolid(x + 32, y) || (tux.size != SMALL && !tux.duck && issolid(x + 32, y + 32))))
			tux_kill(0);
		if (tux.x + (32 << 16) > scroll_x + (VIEW_W_U << 16)) tux.x = scroll_x + ((VIEW_W_U - 32) << 16);
	}
}

static int tux_is_dead(void)
{
	return FX_INT(tux.y) > lh * 32 || FX_INT(tux.x) < FX_INT(scroll_x) - AUTOSCROLL_DEAD_INTERVAL;
}

/* World::action */
/* R5: the positions before a step, to draw between steps (stx.h lerp_fx) */
static fx ip_scroll_x, ip_tux_x, ip_tux_y;
static int ip_cam_y;
static fx ip_part[NSNOW > NCLOUD ? NSNOW : NCLOUD][2];
static void interp_save(void)
{
	ip_scroll_x = scroll_x; ip_cam_y = cam_y;
	ip_tux_x = tux.x; ip_tux_y = tux.y;
	for (int i = 0; i < nparts; i++) { ip_part[i][0] = parts[i].x; ip_part[i][1] = parts[i].y; }
	objects_interp_save();
	interp_on = opt_smooth;            /* R9: OPTIONS - MOTION STEPS draws as R5 did */
}

static void world_action(void)
{
	interp_save();
	tux_action();
	check_bounds();
	scrolling();
	objects_action();
	particles_step();
	objects_collisions();
	camera_y(0);
}

/* ------------------------------------------------------------------ the session's state */
enum { NO_ENDSEQUENCE, ENDSEQUENCE_RUNNING, ENDSEQUENCE_WAITING };
enum { MUS_NONE, MUS_LEVEL, MUS_HURRY, MUS_HERRING };
static int end_sequence, end_steps, end_half, last_x_pos, time_left, exit_status, music_type;
static int session_level;

static void play_music_type(int t)
{
	music_type = t;
	switch (t)
	{
	case MUS_LEVEL: music_want(ltrack); break;
	case MUS_HURRY:                    /* the song's "-fast" version: the next track (stx_convert.py MUSIC) */
		music_want(ltrack == 3 || ltrack == 5 || ltrack == 7 || ltrack == 9 || ltrack == 11 ? ltrack + 1 : ltrack);
		break;
	case MUS_HERRING: music_want(TRACK_HERRING); break;
	default: music_want(0); break;
	}
}

/* the level as it starts (World's constructor), Tux at the start or - after a life was lost -
 * at the nearest reset point behind where he was (GameSession::restart_level) */
static void world_start(int from_x)
{
	interp_on = 0;                     /* R5: nothing to draw between until the first step */
	objects_interp_reset();
	level_reset_map();
	counting_distros = 0;
	objects_start();
	tux_init(lstart_x, lstart_y);
	scroll_x = 0;
	scrolling_timer = 0;
	if (from_x >= 0)
	{
		int bx = -1, by = -1;
		for (int i = 0; i < lnresets; i++)
		{
			const int x = (int16_t)be16(lresets + 4 * i), y = (int16_t)be16(lresets + 4 * i + 2);
			if (x - 320 < from_x && bx < x) { bx = x; by = y; }
		}
		if (bx != -1)
		{
			tux.x = tux.px = (fx)bx << 16;
			tux.y = tux.py = (fx)by << 16;
			if (tux.size == BIG) { tux.y -= 32 << 16; tux.py = tux.y; }
			scroll_x = (fx)(bx - VIEW_W_U / 2) << 16;
		}
	}
	scrolling();
	camera_y(1);
	particles_start();
	end_sequence = NO_ENDSEQUENCE;
	time_left = ltime * 100;
	exit_status = ES_NONE;
	music_type = MUS_NONE;
}

/* ------------------------------------------------------------------ drawing */

/* over the sprites logged since yt_rect_log(1): the cells under them get their layers from
 * `first` (1 background-tm, 2 interactive-tm, 3 foreground-tm) drawn again on top (the tiler's
 * merged picture holds them all, under the sprite). Each cell once. */
#define OV_W (YT_VIEW_W / YT_TILE + 2)
#define OV_H (YT_VIEW_H / YT_TILE + 2)
static uint8_t ov_done[OV_W * OV_H];
static void overlay_cells(int first, int cx, int cy)
{
	const int nr = yt_rect_count();
	yt_rect_log(0);                    /* the overlays are not logged */
	if (!nr) return;
	memset(ov_done, 0, sizeof ov_done);
	const int tx0 = cx / YT_TILE, ty0 = cy / YT_TILE;
	for (int i = 0; i < nr; i++)
	{
		int x, y, w, h;
		yt_rect(i, &x, &y, &w, &h);
		for (int ty = (y + cy) / YT_TILE; ty <= (y + h - 1 + cy) / YT_TILE; ty++)
			for (int tx = (x + cx) / YT_TILE; tx <= (x + w - 1 + cx) / YT_TILE; tx++)
			{
				const int ox = tx - tx0, oy = ty - ty0;
				if (ox < 0 || oy < 0 || ox >= OV_W || oy >= OV_H || ov_done[oy * OV_W + ox]) continue;
				ov_done[oy * OV_W + ox] = 1;
				for (int l = first; l <= 3; l++)
				{
					const int t = level_tile(l, tx, ty);
					if (t) tile_draw(t, game_ms, tx * 32, ty * 32, cx, cy);
				}
			}
	}
}

/* R3: the tiler draws one layer (each cell's four merged - tools/stx_pack.py), both SH2s; only
 * cells that changed are drawn. The far snow and the clouds are drawn over it and get the
 * level's tiles drawn again over them (they are behind the tiles in SuperTux); Tux, the bad guys
 * and the rest get the foreground tiles drawn again over them. */
/* R4, OPTIONS - BACKGROUND MOVES (SuperTux's bkgd_speed): the background picture, then the
 * level's tiles one by one as sprites over it - every frame, the whole view (World::draw's own
 * order). The tiler's dirty matrix is not used, so this costs frames (the reason it is an
 * option). Tiles of the layers first..last; the foreground (3) after the sprites. */
static void tiles_over_back(int first, int last, int cx, int cy)
{
	const int tx0 = cx / YT_TILE, ty0 = cy / YT_TILE;
	for (int ty = ty0; ty <= (cy + YT_VIEW_H - 1) / YT_TILE && ty < lh; ty++)
		for (int tx = tx0; tx <= (cx + YT_VIEW_W - 1) / YT_TILE && tx < lw; tx++)
			for (int l = first; l <= last; l++)
			{
				if (l == 2 && level_hidden(tx, ty)) continue;
				const int t = level_tile(l, tx, ty);
				if (t) tile_draw(t, game_ms, tx * 32, ty * 32, cx, cy);
			}
}

/* R5: the clouds are KEPT in the canvas. A cloud is 64 x 32 pixels, behind the level's tiles,
 * fixed in the world sideways and drifting slowly (12-22 pixels a second). R3/R4 drew each
 * cloud as an ordinary sprite: its 10-15 cells were drawn again by the tiler in the next frame
 * of that buffer, the cloud again, and the level's tiles over it again (overlay_cells) - every
 * frame, the most expensive picture in 14 levels (N 75-115).
 *
 * Now each buffer keeps its clouds in its canvas - the whole canvas, also the columns and lines
 * around the view, so a cloud at the edge of the view needs nothing when the view scrolls:
 *   - a cloud that moved (its 2-pixel steps; up / down with the camera) or came in: its old
 *     cells are marked (the tiler draws them clean) and it is drawn whole;
 *   - a cloud that did not move: drawn again only in the cells the tiler drew under it this
 *     frame (Tux went past, a coin turned, a cloud in front moved away) - cut to their box;
 *   - in the order of the clouds (the later one in front): a cloud drawn over another one's
 *     cells makes the ones in front of it draw again there;
 *   - then the level's tiles are drawn again over every cell that got cloud pixels.
 * Each buffer remembers where it drew each cloud (world pixels). A cloud's x is kept even in
 * the canvas so that a part drawn in a box lands on the same pixels as the whole cloud. */
typedef struct { int16_t x, y, x0, y0, x1, y1; uint8_t valid; } CloudSeen;
static CloudSeen cseen[2][NCLOUD];
static unsigned cseen_epoch[2];        /* R10: yt_epoch when the buffer's clouds were drawn */
static CloudSeen cnow[NCLOUD];
static uint8_t cneed[NCLOUD];
#define CW_MAX 40
static int16_t cw_ids[NCLOUD][CW_MAX], cw_vals[NCLOUD][CW_MAX];
static uint8_t cw_n[NCLOUD];
static int16_t c_vx[NCLOUD], c_vy[NCLOUD];
static uint8_t c_changed[YT_CELLS], c_touched[YT_CELLS];

static void clouds_reset(void)
{
	memset(cseen, 0, sizeof cseen);
	yt_invalidate();                   /* both buffers drawn clean: no cloud left from before */
}

/* the canvas around the view, in view pixels (yt_begin: the view starts at canvas column
 * cx % 64, line cy % 16; the canvas is 384 x 192) - kept pictures are drawn into all of it */
#define CANVAS_LINES (YT_ROWS / YT_TILE * YT_TILE)
static int kx0(int cx) { (void)cx; return -yt_window_x(); }   /* R10: the anchor is the tiler's */
static int ky0(int cy) { return -(cy % YT_TILE); }

/* after yt_begin, before yt_layers */
static void clouds_before(int cx, int cy)
{
	const int b = plat_back_index() & 1;
	const int sx = FX_INT(scroll_x);
	const int L = kx0(cx), T = ky0(cy), R = L + YT_PITCH, B = T + CANVAS_LINES;
	int w, h, dx, dy;
	spr_frame_at(S_CLOUD, 0, &w, &h, &dx, &dy);
	for (int i = 0; i < nparts; i++)
	{
		const Particle *p = &parts[i];
		int x = (FX_INT(p->x) - sx) % part_vw;
		if (x < 0) x += part_vw;
		if (x > VIEW_W_U) x -= part_vw;
		/* as particles_draw, with the canvas column made even (yt_sprite would round it down) */
		const int vx = ((px_of(x) + dx - L) & ~1) + L, vy = px_of(FX_INT(p->y)) + dy;
		c_vx[i] = (int16_t)vx; c_vy[i] = (int16_t)vy;
		CloudSeen *n = &cnow[i];
		n->x = (int16_t)(vx + cx);        /* world pixels */
		n->y = (int16_t)(vy + cy);
		const int x0 = vx < L ? L : vx, x1 = vx + w > R ? R : vx + w;   /* cut to the canvas */
		const int y0 = vy < T ? T : vy, y1 = vy + h > B ? B : vy + h;
		n->valid = x1 > x0 && y1 > y0;
		n->x0 = (int16_t)(x0 + cx); n->x1 = (int16_t)(x1 + cx);
		n->y0 = (int16_t)(y0 + cy); n->y1 = (int16_t)(y1 + cy);
		const CloudSeen *o = &cseen[b][i];
		const int same_epoch = cseen_epoch[b] == yt_epoch();
		cneed[i] = !same_epoch || !o->valid || o->x != n->x || o->y != n->y || o->x0 != n->x0 || o->x1 != n->x1
		           || o->y0 != n->y0 || o->y1 != n->y1;
		/* moved: the cells it was drawn on are drawn clean (first all marks, then the watches -
		 * a mark after a watch would hide a change from it) */
		if (cneed[i] && o->valid && same_epoch)
			yt_mark_view_rect(o->x0 - cx, o->y0 - cy, o->x1 - o->x0, o->y1 - o->y0);
	}
	for (int i = 0; i < nparts; i++)
	{
		const CloudSeen *n = &cnow[i];
		cw_n[i] = n->valid ? (uint8_t)yt_watch(n->x0 - cx, n->y0 - cy, n->x1 - n->x0, n->y1 - n->y0,
		                                         cw_ids[i], cw_vals[i], CW_MAX) : 0;
	}
}

/* after yt_layers: the clouds that need it, then the level's tiles over them - without marks */
static void clouds_draw(int cx, int cy)
{
	const int b = plat_back_index() & 1;
	memset(c_changed, 0, sizeof c_changed);
	memset(c_touched, 0, sizeof c_touched);
	for (int i = 0; i < nparts; i++)
		if (cnow[i].valid) yt_watch_changed(cw_ids[i], cw_vals[i], cw_n[i], c_changed);
	int w, h, dx, dy;
	const uint8_t *px = spr_frame_at(S_CLOUD, 0, &w, &h, &dx, &dy);
	yt_keep(1);
	for (int i = 0; i < nparts; i++)
	{
		if (!cnow[i].valid) continue;
		if (cneed[i])
		{
			yt_keep_box(-YT_PITCH, -YT_ROWS, 3 * YT_PITCH, 3 * YT_ROWS);   /* the whole canvas */
			yt_sprite(c_vx[i], c_vy[i], w, h, px);
			for (int k = 0; k < cw_n[i]; k++) c_touched[cw_ids[i][k]] = 1;
			continue;
		}
		/* not moved: the box of its cells that the tiler drew or a cloud behind it covered */
		int bx0 = 1 << 14, by0 = 1 << 14, bx1 = -(1 << 14), by1 = -(1 << 14);
		for (int k = 0; k < cw_n[i]; k++)
		{
			const int id = cw_ids[i][k];
			if (!c_changed[id] && !c_touched[id]) continue;
			int x, y, bw, bh;
			yt_cell_box(id, &x, &y, &bw, &bh);
			if (x < bx0) bx0 = x;
			if (y < by0) by0 = y;
			if (x + bw > bx1) bx1 = x + bw;
			if (y + bh > by1) by1 = y + bh;
		}
		if (bx1 <= bx0) continue;
		yt_keep_box(bx0, by0, bx1 - bx0, by1 - by0);
		yt_sprite(c_vx[i], c_vy[i], w, h, px);
		for (int k = 0; k < cw_n[i]; k++)            /* every cell of it inside the box */
		{
			int x, y, bw, bh;
			yt_cell_box(cw_ids[i][k], &x, &y, &bw, &bh);
			if (x >= bx0 && x < bx1 && y >= by0 && y < by1) c_touched[cw_ids[i][k]] = 1;
		}
	}
	/* the level's tiles over every cell that got cloud pixels (tile_draw: a whole cell; R10: the
	 * record is in 8-line bands, two a cell - each cell once) */
	yt_keep(1);
	static uint8_t tile_done[(YT_PITCH / YT_TILE) * (YT_VIEW_H / YT_TILE + 4)];
	memset(tile_done, 0, sizeof tile_done);
	int ctx0, cty0, cnx, cny;
	yt_canvas_tiles(&ctx0, &cty0, &cnx, &cny);
	cty0 -= 1;                         /* the kept area starts up to a cell above the view */
	for (int id = 0; id < YT_CELLS; id++)
	{
		if (!c_touched[id]) continue;
		int tx, ty;
		yt_cell_tile(id, &tx, &ty);
		const int ox = tx - ctx0, oy = ty - cty0;
		if (ox < 0 || oy < 0 || ox >= cnx || oy >= YT_VIEW_H / YT_TILE + 4) continue;
		if (tile_done[oy * cnx + ox]) continue;
		tile_done[oy * cnx + ox] = 1;
		for (int l = 1; l <= 3; l++)
		{
			const int t = level_tile(l, tx, ty);
			if (t) tile_draw(t, game_ms, tx * 32, ty * 32, cx, cy);
		}
	}
	yt_keep(0);
	for (int i = 0; i < nparts; i++) cseen[b][i] = cnow[i];
	cseen_epoch[b] = yt_epoch();
}

static uint32_t stat_cells;            /* the cells the tiler drew this frame (N) */

static void draw_world_now(void);
static void particles_draw(int layer);
static void overlay_cells(int first, int cx, int cy);
/* REV4, OPTIONS - DRAW GENESIS: the level is on the Genesis' planes (game/gen_level.c); the 32X
 * draws only what moves - in SuperTux's order: the far snow / clouds with the level's tiles drawn
 * again over them where a tile is (they are behind the tiles), then the bad guys, Tux and the
 * foreground tiles over them, then the near snow and the floating scores. Every rectangle drawn
 * is cleared again (to the see-through colour) when this buffer is drawn next (gl_collect). */
static void gl_world(int cx, int cy, int in_loop)
{
	gl_begin(cx, cy, in_loop);
	yt_rect_log(1);
	particles_draw(0);
	gl_collect(cx, cy);
	overlay_cells(1, cx, cy);
	yt_rect_log(1);
	objects_draw_back(cx, cy, game_ms);
	tux_draw(cx, cy, game_ms, global_frame);
	gl_collect(cx, cy);
	overlay_cells(3, cx, cy);          /* foreground-tm: over Tux */
	yt_rect_log(1);
	particles_draw(1);
	objects_draw_front(cx, cy);
	gl_collect(cx, cy);
	yt_rect_log(0);
}

/* R5: the camera, Tux and the particles moved to where they were interp_a / 256 of the way
 * from the last step's start to now, for the drawing only (the objects do it in objects.c) */
void game_draw_world(void)
{
	const fx keep_scroll_x = scroll_x, keep_tux_x = tux.x, keep_tux_y = tux.y;
	const int keep_cam_y = cam_y;
	static fx keep_part[NSNOW > NCLOUD ? NSNOW : NCLOUD][2];
	if (interp_on)
	{
		scroll_x = lerp_fx(ip_scroll_x, scroll_x);
		cam_y = FX_INT(lerp_fx((fx)ip_cam_y << 16, (fx)cam_y << 16));
		tux.x = lerp_fx(ip_tux_x, tux.x);
		tux.y = lerp_fx(ip_tux_y, tux.y);
		for (int i = 0; i < nparts; i++)
		{
			keep_part[i][0] = parts[i].x; keep_part[i][1] = parts[i].y;
			parts[i].x = lerp_fx(ip_part[i][0], parts[i].x);
			parts[i].y = lerp_fx(ip_part[i][1], parts[i].y);
		}
	}
	draw_world_now();
	if (interp_on)
	{
		scroll_x = keep_scroll_x; cam_y = keep_cam_y;
		tux.x = keep_tux_x; tux.y = keep_tux_y;
		for (int i = 0; i < nparts; i++) { parts[i].x = keep_part[i][0]; parts[i].y = keep_part[i][1]; }
	}
}

static void draw_world_now(void)
{
	uint32_t t0 = plat_prof_us(), t1;
	int cx = px_of(FX_INT(scroll_x)), cy = px_of(cam_y);
	if (cx > lw * YT_TILE - YT_VIEW_W) cx = lw * YT_TILE - YT_VIEW_W;   /* as yt_begin clamps it */
	if (cy > lh * YT_TILE - YT_VIEW_H) cy = lh * YT_TILE - YT_VIEW_H;
	if (cx < 0) cx = 0;
	if (cy < 0) cy = 0;
	view_cx = cx; view_cy = cy;
	frame_cells = 0;
	level_animate(game_ms);
	if (gl_level) { gl_world(cx, cy, gl_in_loop); gl_in_loop = 0; t1 = plat_prof_us(); TADD(T_SPR, t1 - t0); return; }   /* REV4 */
	gen_frame(cx, cy);                 /* REV3: the Genesis picture (no-op unless the level has it) */
	const int parallax = opt_parallax == 1 && back_px && lbkgd_speed != 100;
	if (parallax != drew_parallax)
	{
		yt_invalidate();                 /* the canvas holds the other kind of picture */
		drew_parallax = parallax;
	}
	yt_full_frame(parallax);           /* R10: BACKGROUND MOVES draws the whole view: never move */
#ifndef OJ32X_MARS
	if (getenv("ST_TRUTH")) yt_invalidate();   /* PC: every frame drawn from nothing - the reference */
#endif
	yt_begin(cx, cy);
	if (parallax)
	{
		yt_back(back_px, back_stride, back_w, back_h, cx * lbkgd_speed / 100, cy);
		particles_draw(0);               /* clouds, the far snow: behind the tiles */
		tiles_over_back(1, 2, cx, cy);
		t1 = plat_prof_us(); TADD(T_TILES, t1 - t0); t0 = t1;
		objects_draw_back(cx, cy, game_ms);
		tux_draw(cx, cy, game_ms, global_frame);
		t1 = plat_prof_us(); TADD(T_SPR, t1 - t0); t0 = t1;
		tiles_over_back(3, 3, cx, cy);   /* foreground-tm: over Tux */
		t1 = plat_prof_us(); TADD(T_TILES, t1 - t0); t0 = t1;
		particles_draw(1);
		objects_draw_front(cx, cy);
		t1 = plat_prof_us(); TADD(T_SPR, t1 - t0);
		return;
	}
	level_mark_animated(cx, cy);
	if (part_kind == 2) clouds_before(cx, cy);
	stat_cells = (uint32_t)yt_layers(0, 0);          /* the merged pictures */
	frame_cells = stat_cells;
	tiles_acc += stat_cells;
	t1 = plat_prof_us(); TADD(T_TILES, t1 - t0); t0 = t1;
	if (part_kind == 2) clouds_draw(cx, cy);         /* R5: kept, drawn again only when needed */
	else if (nparts)
		particles_draw(0);                           /* the far snow: only where no tile is (R5) */
	yt_rect_log(1);
	objects_draw_back(cx, cy, game_ms);
	tux_draw(cx, cy, game_ms, global_frame);
	overlay_cells(3, cx, cy);                        /* foreground-tm: over Tux */
	particles_draw(1);                               /* the near snow */
	objects_draw_front(cx, cy);                      /* floating scores */
	t1 = plat_prof_us(); TADD(T_SPR, t1 - t0);
}

/* the panel: SuperTux's status line (drawstatus) and the level's name, or the timing line */
static void draw_status(void)
{
	uint32_t t0 = plat_prof_us();
	static char l1[48], l2[48], l3[48];
	char *q = put_dec(put_str(l1, "SCORE "), ps.score, 1);
	q = put_str(q, "  ");
	if (time_left <= 0) q = put_str(q, "TIME'S UP");
	else if (time_left > STEPS(TIME_WARNING) || (global_frame % 10) < 5)
		q = put_dec(put_str(q, "TIME "), (uint32_t)(time_left + 99) / 100, 1);
	else q = put_str(q, "        ");
	q = put_dec(put_str(q, "  COINS "), (uint32_t)ps.distros, 1);
	q = put_dec(put_str(l2, "LIVES "), (uint32_t)(ps.lives < 0 ? 0 : ps.lives), 1);
	q = put_str(q, "  ");
	static char title[48];             /* NAMES.TXT is searched once a level, not every frame */
	static int title_of = -1;
	if (title_of != session_level) { strncpy(title, level_title(session_level), 47); title_of = session_level; }
	const char *t = title;
	int n = 0;
	const int maxn = opt_timing ? 15 : 28;
	while (t[n] && n < maxn) { q[n] = t[n]; n++; }
	q[n] = 0;
	if (opt_timing)
	{
		/* R8: the raw pad in hex after the name (B 0010, C 0020, A 0040, START 0080) */
		static const char hx[] = "0123456789ABCDEF";
		char *e = q + n;
		*e++ = ' '; *e++ = 'P';
		for (int k = 12; k >= 0; k -= 4) *e++ = hx[(pad_raw >> k) & 15];
		*e = 0;
		e = put_dec(put_str(e, " S"), pad_stale % 1000u, 1);   /* R9: old pad replies */
		/* REV: OPTIONS - DRAW: D0 the CPU, D1 DMA rows, D! DMA once failed - the CPU copies;
		 * DG / DX below */
		static const char *const ds[3] = { " D0", " D1", " D!" };
		/* REV5: DG the level is drawn by the Genesis, DX Draw: Genesis is on but this level fell
		 * back to the 32X */
		put_str(e, gl_level ? " DG" : gl_fell ? " DX" : ds[yt_dma_state()]);
	}
	if (opt_timing) timing_line(l3);
	else put_str(l3, "");
	panel_show(l1, l2, l3, end_sequence ? col_yellow : col_white);
	TADD(T_HUD, plat_prof_us() - t0);
}

/* GameSession::levelintro: the name, "TUX x lives", "by author" over the level, 1 to 3 s.
 * Before it, while the level is read from the disc (4-7 s), the same words over the title
 * picture - SuperTux's own loading has no screen of its own. */
static void intro_words(int li, int loading)
{
	static char t[32];
	const char *name = level_title(li);
	text_mid(fit_font(name), 50, name);
	put_dec(put_str(t, "TUX x "), (uint32_t)(ps.lives < 0 ? 0 : ps.lives), 1);
	text_mid(F_WHITE, 74, t);
	const char *a = level_author(li);
	if (*a)
	{
		static char by[56];
		put_str(put_str(by, "by "), a);
		text_mid(F_SMALL, 140, by);
	}
	if (loading) text_mid(F_SMALL, 160, "LOADING ...");
}

static int load_done, load_total;
static void bg_loading(void)
{
	bg_picture();
	intro_words(session_level, 1);
	if (load_total)
	{
		/* a bar: the pieces read so far (yt_sprite needs a picture: one 8 x 4 block a step) */
		static uint8_t full[8 * 4], empty[8 * 4];
		memset(full, col_yellow, sizeof full);
		memset(empty, col_black, sizeof empty);
		for (int i = 0; i < 24; i++)
			yt_sprite(64 + 8 * i, 171, 8, 4, i * load_total < load_done * 24 ? full : empty);
	}
}
/* level.c's reader calls this after each 32 KB piece */
static void load_progress(int done, int total)
{
	load_done = done;
	load_total = total;
	plat_wait_flip();
	yt_screen();
	bg_loading();
	panel_show("", "", "", col_white);
	present_view();
}
static void level_read(int li)
{
	if (!level_cached(li))
	{
		load_done = load_total = 0;
		load_progress(0, 0);
		load_progress(0, 0);       /* both buffers: the words stay while the disc is read */
		level_progress = load_progress;
	}
	load_level(li);
	level_progress = 0;
}
/* levelintro draws the level's background picture alone under the words */
static void bg_intro(void)
{
	int cx = px_of(FX_INT(scroll_x)), cy = px_of(cam_y);
	if (cx > lw * YT_TILE - YT_VIEW_W) cx = lw * YT_TILE - YT_VIEW_W;
	if (cy > lh * YT_TILE - YT_VIEW_H) cy = lh * YT_TILE - YT_VIEW_H;
	if (cx < 0) cx = 0;
	if (cy < 0) cy = 0;
	if (gl_level) gl_begin(cx, cy, 0); /* REV4: the level on the Genesis, nothing moving yet */
	else
	{
		yt_begin(cx, cy);
		yt_layers(0, 0);
		gen_frame(cx, cy);             /* REV3 */
	}
	intro_words(session_level, 0);
}

static void level_intro(void)
{
	music(0);                          /* halt_music */
	message_screen(bg_intro, 0, 0, 0, 1000, 3000);
}

static void bg_gameover(void)
{
	static char t[40];
	game_draw_world();
	text_mid(F_GOLD, 56, "GAMEOVER");
	put_dec(put_str(t, "SCORE: "), ps.score, 1);
	text_mid(F_GOLD, 80, t);
	put_dec(put_str(t, "COINS: "), (uint32_t)ps.distros, 1);
	text_mid(F_GOLD, 100, t);
}

/* ------------------------------------------------------------------ GameSession::check_end_conditions */
static int goal_tile(void)
{
	const int bx = FX_INT(tux.x), by = FX_INT(tux.y);
	for (int x = bx / 32; x * 32 < bx + tux.w; x++)
		for (int y = by / 32; y * 32 < by + tux.h; y++)
		{
			const int t = tile_at(x * 32, y * 32);
			if (tdesc[t].flags & TF_GOAL) return t;
		}
	return 0;
}

static void start_end_sequence(int kind)
{
	end_sequence = kind;
	last_x_pos = -1;
	music(TRACK_LEVELDONE | 0x8000);   /* played once */
	end_steps = STEPS(7000);
	tux.invincible_t = STEPS(7000);
}

static void check_end_conditions(void)
{
	const int endpos = (lw - 5) * 32;
	const int goal = goal_tile();
	if (!end_sequence && FX_INT(tux.x) >= endpos) start_end_sequence(ENDSEQUENCE_WAITING);
	else if (end_sequence && end_steps <= 0) exit_status = ES_LEVEL_FINISHED;
	else if (end_sequence == ENDSEQUENCE_RUNNING && goal && tdesc[goal].data >= 1) end_sequence = ENDSEQUENCE_WAITING;
	else if (!end_sequence && goal && tdesc[goal].data == 0) start_end_sequence(ENDSEQUENCE_RUNNING);
	else if (!end_sequence && tux_is_dead())
	{
		ps.bonus = 0;
		if (ps.lives < 0)
		{
			music(0);
			message_screen(bg_gameover, 0, 0, 0, 2000, 5000);
			exit_status = ES_GAME_OVER;
		}
		else
		{
			world_start(FX_INT(tux.x));
			level_intro();
			step_clock_reset();
		}
	}
}

/* ------------------------------------------------------------------ GameSession::run */
static void read_input(uint16_t pad)
{
	tux.in_old_fire = tux.in_fire;
	if (end_sequence != NO_ENDSEQUENCE)
	{
		/* the end sequence: Tux walks right by himself and jumps when something stops him */
		tux.in_fire = 0; tux.in_left = 0; tux.in_right = 1; tux.in_down = 0;
		tux.in_up = FX_INT(tux.x) == last_x_pos;
		last_x_pos = FX_INT(tux.x);
		return;
	}
	tux.in_left = (pad & PAD_LEFT) != 0;
	tux.in_right = (pad & PAD_RIGHT) != 0;
	tux.in_down = (pad & PAD_DOWN) != 0;
	/* jump. R8: let go only after two pad reads in a row without it. Micronut99 (R6/R7 on the
	 * console): with A, Tux "jumps a tiny bit"; with C he jumps fully. On the PC A and C are
	 * the same bit test, so on the console the A bit drops out for a read just after the jump
	 * starts (A shares the pad's TH-low read with START, the 6-button read sequence) - and a
	 * jump let go stops climbing (handle_vertical: vy = 0). One read missing no longer counts;
	 * a real let-go is seen one frame later (about 17 ms). */
	static int up_off = 2;             /* pad reads in a row without a jump button */
	if (pad & (PAD_C | PAD_A)) up_off = 0;
	else if (up_off < 2) up_off++;
	tux.in_up = up_off < 2;
	tux.in_fire = (pad & PAD_B) != 0;                /* run, shoot, carry */
}

int game_play(int li)
{
	session_level = li;
	level_read(li);                    /* from the cart; from the disc first if it is not there */
	world_start(-1);
#ifndef OJ32X_MARS
	if (getenv("ST_X")) { tux.x = tux.px = (fx)atoi(getenv("ST_X")) << 16; tux.y = tux.py = 0; scrolling(); }   /* PC checks */
#endif
	level_intro();
	step_clock_reset();
	uint32_t frame_25 = 0;
	while (exit_status == ES_NONE)
	{
		uint32_t t0 = plat_prof_us(), t1;
		uint16_t hit;
		const uint16_t pad = pad_read(&hit);
		if (hit & PAD_START)
		{
			const int r = pause_menu();
			step_clock_reset();
			if (r == ES_LEVEL_ABORT || r == ES_QUIT) { exit_status = r; break; }
			if (r == -1) { world_start(-1); level_intro(); step_clock_reset(); continue; }
			music_again();
		}
		read_input(pad);
		const int steps = step_clock();
		for (int s = 0; s < steps && exit_status == ES_NONE; s++)
		{
			check_end_conditions();
			if (exit_status != ES_NONE) break;
			if (end_sequence == ENDSEQUENCE_RUNNING) { if ((end_half ^= 1) != 0) world_action(); }   /* frame_ratio / 2 */
			else if (end_sequence == NO_ENDSEQUENCE) world_action();
			if (end_sequence) end_steps--;
			if (time_left > 0) time_left--;
			if (time_left <= 0 && tux.dying == DYING_NOT && !end_sequence) tux_kill(0);
			game_ms += STEP_MS;
			/* the music: the herring's, hurry up (less than 20 s), the level's */
			if (!end_sequence)
			{
				if (tux.invincible_t) { if (music_type != MUS_HERRING) play_music_type(MUS_HERRING); }
				else if (time_left < STEPS(TIME_WARNING)) { if (music_type != MUS_HURRY) play_music_type(MUS_HURRY); }
				else if (music_type != MUS_LEVEL) play_music_type(MUS_LEVEL);
			}
		}
		if (exit_status != ES_NONE) break;
		frame_25 = game_ms / 25;       /* SuperTux's global_frame_counter: one every 25 ms */
		global_frame = frame_25;
		t1 = plat_prof_us(); TADD(T_LOGIC, t1 - t0); t0 = t1;
		plat_wait_flip();
		t1 = plat_prof_us(); TADD(T_WAIT, t1 - t0);
		if (gl_level) { gl_after_flip(game_ms); t1 = plat_prof_us(); TADD(T_HUD, t1 - t0); }   /* REV4: the Genesis follows the frame now shown */
		gl_in_loop = 1;
		game_draw_world();
#ifndef OJ32X_MARS
		if (getenv("ST_TUXY")) fprintf(stderr, "TY %d %d\n", FX_INT(tux.y), FX_INT(tux.x));   /* PC: Tux's place a frame */
#endif
		draw_status();
		timing_frame();
#ifndef OJ32X_MARS
		if (getenv("ST_STATS"))
		{
			/* PC only: what this frame drew - the merged cells, the sprites and their pixels */
			extern uint32_t yt_stat_spr, yt_stat_px;
			extern int yt_stat_reanchor[3];
			static uint32_t f;
			fprintf(stderr, "STAT %u %u %u %u %d %d %d %d %d %d\n", f++, stat_cells, yt_stat_spr, yt_stat_px, view_cx, view_cy,
			        yt_stat_reanchor[0], yt_stat_reanchor[1], yt_stat_reanchor[2], yt_frame_dma_cells());   /* REV: + DMA row cells */
		}
		{ extern uint32_t yt_stat_spr, yt_stat_px; yt_stat_spr = yt_stat_px = 0; }
#endif
		present_view();
	}
	if (exit_status == ES_LEVEL_FINISHED)
	{
		/* worldmap.cpp: what Tux has at the end goes into the next level */
		ps.bonus = tux.got_coffee ? 2 : tux.size == BIG ? 1 : 0;
	}
	return exit_status;
}

/* ------------------------------------------------------------------ the title's demo (title.cpp) */
static int demo_walking = 1, demo_timer;
#define DEMO_LEVEL 76                  /* misc/menu.stl (tools/stx_convert.py LEVELS.TXT) */

void demo_start(void)
{
	session_level = DEMO_LEVEL;
	const PlayerStatus keep = ps;
	level_read(DEMO_LEVEL);            /* R3: always in the cart (IMAGE8.RAW's DEMO.PAK) */
	world_start(-1);
	ps = keep;
	demo_timer = 0;
	demo_walking = 1;
	music_want(ltrack);
	step_clock_reset();
}

void demo_run(void)
{
	const PlayerStatus keep = ps;      /* the demo's coins and lives are not the player's */
	const int steps = step_clock();
	for (int s = 0; s < steps; s++)
	{
		tux.in_old_fire = tux.in_fire;
		tux.in_right = 1; tux.in_left = 0; tux.in_fire = 0; tux.in_down = 0;
		if (demo_timer > 0)
		{
			demo_timer--;
			tux.in_up = !demo_walking;
		}
		else
		{
			demo_timer = (int)STEPS(rnd() % 3000 + 3000);
			demo_walking = !demo_walking;
		}
		/* back to the start at the level's end (or when he fell) */
		if (lw * 32 - 320 < FX_INT(tux.x) || tux_is_dead())
		{
			tux_init(lstart_x, lstart_y);
			scroll_x = 0;
			camera_y(1);
			gl_map_reset();             /* REV6: the Genesis map jumps back with the view */
		}
		tux.can_jump = 1;
		const fx last = tux.x;
		world_action();
		if (last == tux.x) demo_walking = 0;
		game_ms += STEP_MS;
	}
	global_frame = game_ms / 25;
	ps = keep;
	game_draw_world();
}
