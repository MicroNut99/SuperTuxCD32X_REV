/*
 * SuperTux CD32X - the world maps: SuperTux 0.1.3's src/worldmap.cpp. Icyisland (world 1) and
 * the two Bonus Islands; Tux walks along the paths from level to level (one tile at a time;
 * through auto-walk tiles by himself; never past a level he has not solved, except back the
 * way he came), one-way paths, stop tiles, teleporters (some invisible) with their messages,
 * messages shown when walking over a place ("You found a secret place!"), the levels' dots
 * (red: not solved, green: solved), and after a level: solved, Tux's bonus kept, the next
 * direction walked by himself; after the last level its extro text and the credits.
 *
 * Data (tools/stx_convert.py): WMTILES.BIN (the map tiles, 16x16, colours 192-255, their path
 * flags) and WORLDS.BIN (each map's tiles and its level list). The map is drawn by Vic's tiler
 * as a one-layer level (its own 16x16 tiles), the dots and Tux as sprites. See game/stx.h.
 */
#include "stx.h"

enum { D_NONE, D_WEST, D_EAST, D_NORTH, D_SOUTH };
enum { WF_NORTH = 1, WF_SOUTH = 2, WF_EAST = 4, WF_WEST = 8, WF_STOP = 16, WF_AUTO = 32 };
enum { LF_EXTRO = 1, LF_TELEPORT = 2, LF_INVISIBLE = 4, LF_AUTOPATH = 8, LF_PASSIVE = 16,
       LF_APPLY_N = 32, LF_APPLY_S = 64, LF_APPLY_E = 128, LF_APPLY_W = 256 };
#define MAX_MAP_CELLS 4096             /* the biggest map: Bonus Island I, 70 x 50 = 3500 */
#define MAX_MAP_LEVELS 64
#define NO_LEVEL 0xFFFF
#define DISPLAY_MAP_MESSAGE_STEPS 280  /* DISPLAY_MAP_MESSAGE_TIME 2800 ms */
#define WALK_SPEED_MAP FX(2.6)         /* "20 pixels a second" x 1.3, delta in 1/10 s: 2.6 units a step */

typedef struct { int level, x, y, flags, dx, dy, extro; const char *msg; int solved; } MapLevel;

static const uint8_t *wmt;             /* WMTILES.BIN */
static int wm_ntiles;
static const uint8_t *map_bin;
static int map_w, map_h, map_track, map_sx, map_sy, map_nl;
static const uint8_t *map_tiles;       /* w*h big-endian tile ids */
static MapLevel lv[MAX_MAP_LEVELS];
static uint16_t wm_layer[MAX_MAP_CELLS];
static uint8_t *wm_res[256];
static int cur_map;

/* Tux on the map (worldmap.cpp class Tux) */
static int tpx, tpy;                   /* tile_pos: the tile he stands on or walks to */
static int moving, direction, back_direction, input_direction;
static fx offset;                      /* units walked from the last tile (0..32) */
static char passive_msg[48];
static int passive_t;

static const uint8_t *map_at(int m)
{
	const uint8_t *w = need("WORLDS.BIN");
	return w + be32(w + 8 + 4 * m);
}
const char *wmap_name(int m)
{
	static char n[33];
	memcpy(n, map_at(m), 32);
	n[32] = 0;
	return n;
}
int wmap_levels(int m)
{
	const uint8_t *b = map_at(m);
	const int w = be16(b + 38), h = be16(b + 40), nl = be16(b + 42);
	const uint8_t *e = b + 44 + 2 * w * h;
	int n = 0;
	for (int i = 0; i < nl; i++) if (be16(e + 62 * i) != NO_LEVEL) n++;
	return n;
}

static void map_load(int m)
{
	cur_map = m;
	wmt = need("WMTILES.BIN");
	if (memcmp(wmt, "STXW", 4)) plat_fatal("WMTILES.BIN: NOT STXW");
	wm_ntiles = be16(wmt + 4);
	map_bin = map_at(m);
	map_track = be16(map_bin + 32);
	map_sx = be16(map_bin + 34); map_sy = be16(map_bin + 36);
	map_w = be16(map_bin + 38); map_h = be16(map_bin + 40);
	map_nl = be16(map_bin + 42);
	if (map_w * map_h > MAX_MAP_CELLS || map_nl > MAX_MAP_LEVELS) plat_fatal("WORLD MAP TOO BIG");
	map_tiles = map_bin + 44;
	const uint8_t *e = map_tiles + 2 * map_w * map_h;
	for (int i = 0; i < map_nl; i++, e += 62)
	{
		MapLevel *l = &lv[i];
		l->level = be16(e); l->x = be16(e + 2); l->y = be16(e + 4); l->flags = be16(e + 6);
		l->dx = (int16_t)be16(e + 8); l->dy = (int16_t)be16(e + 10); l->extro = be16(e + 12);
		l->msg = (const char *)(e + 14);
		l->solved = 0;
	}
}

static int tile_flags(int x, int y)
{
	if (x < 0 || y < 0 || x >= map_w || y >= map_h) return 0;
	const int t = be16(map_tiles + 2 * (y * map_w + x));
	return t < wm_ntiles ? be16(wmt + 136 + 4 * t) : 0;
}

/* the map for the tiler: one layer, its tiles (resource = tile id + 1) */
static void map_show(void)
{
	for (int i = 0; i < 64; i++) palette[192 + i] = be16(wmt + 8 + 2 * i);
	const uint8_t *px = wmt + 136 + 4 * wm_ntiles;
	wm_res[0] = 0;
	for (int t = 0; t < wm_ntiles && t < 255; t++) wm_res[1 + t] = (uint8_t *)(px + YT_TILE * YT_TILE * t);
	for (int i = 0; i < map_w * map_h; i++)
	{
		int t = be16(map_tiles + 2 * i);
		if (t >= wm_ntiles || t >= 255) t = 0;
		wm_layer[i] = (uint16_t)((t + 1) << 2);
	}
	yt_level(wm_layer, map_w, map_h, wm_res);
}

static MapLevel *at_level(void)
{
	for (int i = 0; i < map_nl; i++) if (lv[i].x == tpx && lv[i].y == tpy) return &lv[i];
	return 0;
}

static int reverse_dir(int d)
{
	return d == D_WEST ? D_EAST : d == D_EAST ? D_WEST : d == D_NORTH ? D_SOUTH : d == D_SOUTH ? D_NORTH : D_NONE;
}
static void next_tile(int d, int *x, int *y)
{
	if (d == D_WEST) (*x)--;
	else if (d == D_EAST) (*x)++;
	else if (d == D_NORTH) (*y)--;
	else if (d == D_SOUTH) (*y)++;
}

/* WorldMap::path_ok */
static int path_ok(int d, int ox, int oy, int *nx, int *ny)
{
	*nx = ox; *ny = oy;
	next_tile(d, nx, ny);
	if (*nx < 0 || *nx >= map_w || *ny < 0 || *ny >= map_h) return 0;
	const int nf = tile_flags(*nx, *ny), of = tile_flags(ox, oy);
	const int one_way = nf >> 8;
	if (one_way)
		return !((one_way == 1 && d != D_SOUTH) || (one_way == 2 && d != D_NORTH)
		         || (one_way == 3 && d != D_WEST) || (one_way == 4 && d != D_EAST));
	switch (d)
	{
	case D_WEST: return (of & WF_WEST) && (nf & WF_EAST);
	case D_EAST: return (of & WF_EAST) && (nf & WF_WEST);
	case D_NORTH: return (of & WF_NORTH) && (nf & WF_SOUTH);
	case D_SOUTH: return (of & WF_SOUTH) && (nf & WF_NORTH);
	}
	return 0;
}

static void tux_stop(void) { offset = 0; direction = D_NONE; moving = 0; }

/* Tux::update, one step */
static void tux_update(void)
{
	if (!moving)
	{
		if (input_direction != D_NONE)
		{
			const MapLevel *l = at_level();
			int nx, ny;
			if ((!l || l->solved || l->level == NO_LEVEL) && path_ok(input_direction, tpx, tpy, &nx, &ny))
			{
				tpx = nx; tpy = ny;
				moving = 1;
				direction = input_direction;
				back_direction = reverse_dir(direction);
			}
			else if (input_direction == back_direction)
			{
				moving = 1;
				direction = input_direction;
				next_tile(direction, &tpx, &tpy);
				back_direction = reverse_dir(direction);
			}
		}
		return;
	}
	offset += WALK_SPEED_MAP;
	if (offset <= (32 << 16)) return;
	offset -= 32 << 16;                /* the next tile reached: what now? */
	const MapLevel *l = at_level();
	if (l && l->level == NO_LEVEL && l->msg[0] && (l->flags & LF_PASSIVE))
	{
		/* a message for walking over this place (from the directions it applies to) */
		if ((direction == D_NORTH && (l->flags & LF_APPLY_S)) || (direction == D_SOUTH && (l->flags & LF_APPLY_N))
		    || (direction == D_WEST && (l->flags & LF_APPLY_E)) || (direction == D_EAST && (l->flags & LF_APPLY_W)))
		{
			memcpy(passive_msg, l->msg, 47);
			passive_msg[47] = 0;
			passive_t = DISPLAY_MAP_MESSAGE_STEPS;
		}
	}
	const int f = tile_flags(tpx, tpy);
	if ((f & WF_STOP) || (l && (l->level != NO_LEVEL || (l->flags & LF_TELEPORT)))) { tux_stop(); return; }
	if (f & WF_AUTO)
	{
		int d = D_NONE;
		if ((f & WF_NORTH) && back_direction != D_NORTH) d = D_NORTH;
		else if ((f & WF_SOUTH) && back_direction != D_SOUTH) d = D_SOUTH;
		else if ((f & WF_EAST) && back_direction != D_EAST) d = D_EAST;
		else if ((f & WF_WEST) && back_direction != D_WEST) d = D_WEST;
		if (d == D_NONE) { tux_stop(); return; }
		direction = d;
		back_direction = reverse_dir(d);
	}
	int nx, ny;
	if (path_ok(direction, tpx, tpy, &nx, &ny)) { tpx = nx; tpy = ny; }
	else tux_stop();
}

/* Tux::get_pos (units) */
static void tux_pos(int *x, int *y)
{
	*x = tpx * 32; *y = tpy * 32;
	const int o = FX_INT(offset) - 32;
	if (direction == D_WEST) *x -= o;
	else if (direction == D_EAST) *x += o;
	else if (direction == D_NORTH) *y -= o;
	else if (direction == D_SOUTH) *y += o;
}

/* R5: Tux's place before the last step, to draw him between steps (stx.h lerp_fx) */
static int ip_x, ip_y, ip_ok;

/* ------------------------------------------------------------------ drawing */
static void wmap_draw(void)
{
	int x, y;
	tux_pos(&x, &y);
	if (ip_ok && opt_smooth)
	{
		const int on = interp_on;
		interp_on = 1;
		x = FX_INT(lerp_fx((fx)ip_x << 16, (fx)x << 16));
		y = FX_INT(lerp_fx((fx)ip_y << 16, (fx)y << 16));
		interp_on = on;
	}
	int cx = x - VIEW_W_U / 2, cy = y - VIEW_H_U / 2;
	if (cx > map_w * 32 - VIEW_W_U) cx = map_w * 32 - VIEW_W_U;
	if (cy > map_h * 32 - VIEW_H_U) cy = map_h * 32 - VIEW_H_U;
	if (cx < 0) cx = 0;
	if (cy < 0) cy = 0;
	cx = px_of(cx); cy = px_of(cy);
	yt_begin(cx, cy);
	yt_layers(0, 0);
	for (int i = 0; i < map_nl; i++)
	{
		const MapLevel *l = &lv[i];
		int s;
		if (l->level == NO_LEVEL)
		{
			if ((l->flags & LF_TELEPORT) && !(l->flags & LF_INVISIBLE)) s = S_TELEPORTER;
			else continue;
		}
		else s = l->solved ? S_DOT_GREEN : S_DOT_RED;
		spr_draw(s, game_ms, l->x * 32, l->y * 32, cx, cy);
	}
	spr_draw(ps.bonus == 1 ? S_WM_TUX : ps.bonus == 2 ? S_WM_FIRETUX : S_WM_SMALLTUX, game_ms, x, y, cx, cy);

	/* draw_status: the level's name at the bottom, a message above it */
	if (!moving)
	{
		const MapLevel *l = at_level();
		if (l)
		{
			if (l->level != NO_LEVEL)
			{
				const char *t = level_title(l->level);
				text_mid(text_width(F_WHITE, t) <= YT_VIEW_W ? F_WHITE : F_SMALL, YT_VIEW_H - 20, t);
			}
			else if ((l->flags & LF_TELEPORT) && l->msg[0]) text_mid(fit_font(l->msg), YT_VIEW_H - 20, l->msg);
			if (l->msg[0] && !(l->flags & LF_PASSIVE) && !(l->flags & LF_TELEPORT)) text_mid(fit_font(l->msg), YT_VIEW_H - 40, l->msg);
		}
	}
	if (passive_t) text_mid(fit_font(passive_msg), YT_VIEW_H - 40, passive_msg);

	static char l1[48], l2[48], l3[48];
	char *q = put_dec(put_str(l1, "SCORE "), ps.score, 1);
	q = put_dec(put_str(q, "  COINS "), (uint32_t)ps.distros, 1);
	put_dec(put_str(q, "  LIVES "), (uint32_t)(ps.lives < 0 ? 0 : ps.lives), 1);
	put_str(l2, wmap_name(cur_map));
	if (opt_timing) timing_line(l3);
	else put_str(l3, "A OR C ENTER     START MENU");
	panel_show(l1, l2, l3, col_yellow);
}

static void frame(void)
{
	game_wait_flip();
	wmap_draw();
	present_view();
}

/* ------------------------------------------------------------------ the save slot */
static void slot_store(int si)
{
	Slot *s = &slots[si];
	s->used = 1;
	s->map = (uint8_t)cur_map;
	s->tx = (uint8_t)tpx; s->ty = (uint8_t)tpy;
	s->lives = (uint8_t)(ps.lives < 0 ? 0 : ps.lives > MAX_LIVES ? MAX_LIVES : ps.lives);
	s->bonus = (uint8_t)ps.bonus;
	s->score = ps.score;
	s->distros = (uint16_t)ps.distros;
	memset(s->solved[cur_map], 0, 8);
	for (int i = 0; i < map_nl; i++) if (lv[i].solved) s->solved[cur_map][i >> 3] |= (uint8_t)(1 << (i & 7));
	save_store();
}

/* ------------------------------------------------------------------ WorldMap::display */
int wmap_run(int m, int si)
{
	map_load(m);
	Slot *s = &slots[si];
	tpx = map_sx; tpy = map_sy;
	if (s->used && s->map == m)
	{
		/* WorldMap::loadgame */
		tpx = s->tx; tpy = s->ty;
		ps.lives = s->lives; ps.bonus = s->bonus; ps.score = s->score; ps.distros = s->distros;
		for (int i = 0; i < map_nl; i++) lv[i].solved = (s->solved[m][i >> 3] >> (i & 7)) & 1;
	}
	else
	{
		ps_reset();
		if (s->used) for (int i = 0; i < map_nl; i++) lv[i].solved = (s->solved[m][i >> 3] >> (i & 7)) & 1;
	}
	if (tpx >= map_w || tpy >= map_h) { tpx = map_sx; tpy = map_sy; }
	tux_stop();
	back_direction = D_NONE;
	input_direction = D_NONE;
	passive_t = 0;
	slot_store(si);
	map_show();
	music(map_track);
	step_clock_reset();
	ip_ok = 0;                     /* R5: nothing to draw between yet */
	for (;;)
	{
		uint16_t hit;
		const uint16_t p = pad_read(&hit);
		int in = D_NONE;
		if (p & PAD_LEFT) in = D_WEST;
		else if (p & PAD_RIGHT) in = D_EAST;
		else if (p & PAD_UP) in = D_NORTH;
		else if (p & PAD_DOWN) in = D_SOUTH;
		if (hit & PAD_START)
		{
			/* the world map's menu: Continue, Options, Quit Game */
			static const char *const items[] = { "CONTINUE", "OPTIONS", "QUIT GAME" };
			for (;;)
			{
				const int c = menu_choose("PAUSE", items, 3, 0, wmap_draw, "B: BACK TO THE MAP");
				if (c == 1) { options_page(wmap_draw); music(map_track); continue; }
				if (c == 2) { slot_store(si); return -1; }
				break;
			}
			step_clock_reset();
			ip_ok = 0;                     /* R5: nothing to draw between yet */
			continue;
		}
		const int steps = step_clock();
		const int enter = (hit & (PAD_A | PAD_C)) != 0;
		MapLevel *l = at_level();
		if (enter && !moving && l && l->level != NO_LEVEL)
		{
			/* WorldMap::update: enter the level */
			const int r = game_play(l->level);
			int result = -1;
			if (r == ES_LEVEL_FINISHED)
			{
				const int was = l->solved;
				l->solved = 1;
				if (!was && (l->flags & LF_AUTOPATH))
				{
					/* walk on by itself: the first way that is not where he came from */
					const int f = tile_flags(tpx, tpy);
					int d = D_NONE;
					if ((f & WF_NORTH) && back_direction != D_NORTH) d = D_NORTH;
					else if ((f & WF_SOUTH) && back_direction != D_SOUTH) d = D_SOUTH;
					else if ((f & WF_EAST) && back_direction != D_EAST) d = D_EAST;
					else if ((f & WF_WEST) && back_direction != D_WEST) d = D_WEST;
					input_direction = d;
				}
				if (l->flags & LF_EXTRO)
				{
					/* the world is done: its extro text, then the credits, then the title */
					slot_store(si);
					music(TRACK_THEME);
					static const char *const ex[3] = { "EXTRO.TXT", "EXTRO1.TXT", "EXTRO2.TXT" };
					text_page(ex[l->extro < 3 ? l->extro : 0], PIC_EXTRO, 0);
					text_page("CREDITS.TXT", PIC_CREDITS, 1);
					music(TRACK_THEME);
					return (int)ps.score;
				}
			}
			else if (r == ES_GAME_OVER)
			{
				result = (int)ps.score;
				ps_reset();            /* PlayerStatus::reset; the solved levels stay */
			}
			slot_store(si);
			if (r == ES_GAME_OVER) return result;
			if (r == ES_QUIT) return -1;
			map_show();
			music(map_track);
			step_clock_reset();
			ip_ok = 0;                     /* R5: nothing to draw between yet */
			if (r != ES_LEVEL_FINISHED) input_direction = D_NONE;
			continue;
		}
		if (enter && !moving && l && (l->flags & LF_TELEPORT) && l->dx >= 0 && l->dy >= 0)
		{
			plat_sfx(SFX_WARP);
			back_direction = D_NONE;
			tpx = l->dx; tpy = l->dy;
			const uint32_t t0 = plat_ticks_ms();
			while (plat_ticks_ms() - t0 < 1000) frame();   /* SDL_Delay(1000) */
			pad_read(0);
			step_clock_reset();
			ip_ok = 0;                     /* R5: nothing to draw between yet */
			continue;
		}
		/* tux->update(delta); tux->set_direction(input): the direction held (or the one chosen
		 * by itself after a level, until a step has used it) */
		if (in != D_NONE) input_direction = in;
		for (int i = 0; i < steps; i++)
		{
			tux_pos(&ip_x, &ip_y);
			ip_ok = 1;
			tux_update();
			input_direction = in;
			if (passive_t) passive_t--;
			game_ms += STEP_MS;
		}
		frame();
	}
}
