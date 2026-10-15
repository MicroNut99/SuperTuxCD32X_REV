/*
 * SuperTux CD32X - the menus (SuperTux 0.1.3's src/title.cpp, menu.cpp, setup.cpp, text.cpp,
 * high_scores.cpp), the saves, and main().
 *
 * THE TITLE (title.cpp): SuperTux's own demo - misc/menu.stl with Tux walking and jumping by
 * himself - runs behind the logo and the main menu:
 *   START GAME     SuperTux's five save slots (a new one shows the story first, then Icyisland)
 *   BONUS LEVELS   Bonus Island I and II (world maps, each with its own save), and LEVEL SELECT
 *                  (ours: any of the 76 levels on its own - world 1 and both bonus sets)
 *   OPTIONS        music, lives for a new game, the timing line (REV1: off / on / worst), the
 *                  A/B buttons, background, motion, scroll, draw (REV1: CPU / DMA rows), level
 *                  select, controls, the story, the high scores, the credits
 *   CREDITS        SuperTux's CREDITS over its oiltux picture (LEFT/RIGHT: its songs)
 * In a level, START: CONTINUE, RESTART LEVEL, OPTIONS, ABORT LEVEL (back to the map), MAIN MENU.
 * On a world map, START: CONTINUE, OPTIONS, QUIT GAME (saved).
 *
 * THE SAVES: as Hurrican CD32X's (P1p) - the Sega CD's backup RAM file of the unchanged Sub-CPU
 * program (cd32x/subcpu/main.c K36: command 56 loads it into the Sub-CPU's 4 KB buffer, 57 reads /
 * 58 writes a word of that buffer, 59 saves it). Hurrican keeps its table at byte 3968; SuperTux
 * R2 uses bytes 3072-3439 ("STX2"; R1's 128 bytes at 3840 are no longer read):
 *   0 "STX2"  4 music, timing (byte 5: bit 0 on, 1 moves, 2 steps, 3 classic; REV1: 4 worst,
 *             5 draw DMA; REV3: 6 background genesis), A/B swap, lives  8 ten high scores {name[3], score u32, level u8}
 *   88 seven slots of 40 bytes {used, map, tile x, tile y, lives, bonus, score u32, coins u16,
 *      solved levels 3 x 8 bytes (a bit a level of each world map), pad 4}
 * On the PC: ST_DIR/STXSAVE.BIN.
 */
#include "stx.h"

/* ------------------------------------------------------------------ saves */
#define HS_N 10
#define SAVE_BYTES 368
#define SAVE_WORD0 1536                /* byte 3072 of the backup RAM file's buffer */
typedef struct { char name[4]; uint32_t score; uint8_t level; } HiScore;
static HiScore hs[HS_N];
static int hs_new = -1;
Slot slots[NSLOTS + 2];

static void hs_defaults(void)
{
	static const char *const nm[HS_N] = { "TUX", "PEN", "NOL", "OKS", "ICE", "SNO", "FSH", "SEG", "32X", "CDX" };
	for (int i = 0; i < HS_N; i++)
	{
		memcpy(hs[i].name, nm[i], 4);
		hs[i].score = (uint32_t)(10000 - i * 1000);
		hs[i].level = (uint8_t)(10 - i);
	}
}

static void save_unpack(const uint8_t *b)
{
	memset(slots, 0, sizeof slots);
	if (memcmp(b, "STX2", 4)) { hs_defaults(); return; }
	opt_music = b[4] & 1; opt_timing = (b[5] & 1) ? 1 + ((b[5] >> 4) & 1) : 0; opt_dma = (b[5] >> 5) & 1; yt_set_dma(opt_dma); opt_parallax = (b[5] >> 6) & 1 ? 2 : (b[5] >> 1) & 1; opt_smooth = !((b[5] >> 2) & 1); opt_ring = !((b[5] >> 3) & 1); yt_set_ring(opt_ring); opt_swap = b[6] & 1;
	opt_lives = b[7] >= 1 && b[7] <= 9 ? b[7] : START_LIVES;
	for (int i = 0; i < HS_N; i++)
	{
		const uint8_t *r = b + 8 + 8 * i;
		for (int k = 0; k < 3; k++) hs[i].name[k] = (char)(r[k] >= 32 && r[k] < 96 ? r[k] : '.');
		hs[i].name[3] = 0;
		hs[i].score = be32(r + 3);
		hs[i].level = r[7];
	}
	for (int i = 0; i < NSLOTS + 2; i++)
	{
		const uint8_t *r = b + 88 + 40 * i;
		Slot *s = &slots[i];
		s->used = r[0] == 1; s->map = r[1] < 3 ? r[1] : 0; s->tx = r[2]; s->ty = r[3];
		s->lives = r[4] <= MAX_LIVES ? r[4] : START_LIVES; s->bonus = r[5] <= 2 ? r[5] : 0;
		s->score = be32(r + 6); s->distros = be16(r + 10);
		memcpy(s->solved, r + 12, 24);
	}
}

static void save_pack(uint8_t *b)
{
	memset(b, 0, SAVE_BYTES);
	memcpy(b, "STX2", 4);
	b[4] = (uint8_t)opt_music; b[5] = (uint8_t)((opt_timing != 0) | (opt_parallax == 1) << 1 | (opt_parallax == 2) << 6 | (!opt_smooth) << 2 | (!opt_ring) << 3 | (opt_timing == 2) << 4 | opt_dma << 5);   /* REV: bits 4 WORST, 5 DMA */ b[6] = (uint8_t)opt_swap; b[7] = (uint8_t)opt_lives;
	for (int i = 0; i < HS_N; i++)
	{
		uint8_t *r = b + 8 + 8 * i;
		memcpy(r, hs[i].name, 3);
		r[3] = (uint8_t)(hs[i].score >> 24); r[4] = (uint8_t)(hs[i].score >> 16);
		r[5] = (uint8_t)(hs[i].score >> 8); r[6] = (uint8_t)hs[i].score;
		r[7] = hs[i].level;
	}
	for (int i = 0; i < NSLOTS + 2; i++)
	{
		uint8_t *r = b + 88 + 40 * i;
		const Slot *s = &slots[i];
		r[0] = s->used; r[1] = s->map; r[2] = s->tx; r[3] = s->ty; r[4] = s->lives; r[5] = s->bonus;
		r[6] = (uint8_t)(s->score >> 24); r[7] = (uint8_t)(s->score >> 16);
		r[8] = (uint8_t)(s->score >> 8); r[9] = (uint8_t)s->score;
		r[10] = (uint8_t)(s->distros >> 8); r[11] = (uint8_t)s->distros;
		memcpy(r + 12, s->solved, 24);
	}
}

#ifdef OJ32X_MARS
#define SV_COMM0  (*(volatile uint16_t *)0x20004020)
#define SV_COMM2  (*(volatile uint16_t *)0x20004022)
#define SV_COMM8  (*(volatile uint16_t *)0x20004028)
#define SV_COMM12 (*(volatile uint16_t *)0x2000402C)
static int sv_cmd(int cmd)             /* Hurrican's hs_cmd: the platform layer's command protocol */
{
	SV_COMM0 = (uint16_t)cmd;
	for (uint32_t n = 0; n < 8000000u && SV_COMM0 != 0; n++) { }
	return SV_COMM0 == 0;
}
static void save_load(void)
{
	static uint8_t b[SAVE_BYTES];
	SV_COMM12 = 0;
	if (!sv_cmd(56)) { save_unpack((const uint8_t *)"none"); return; }
	for (int i = 0; i < SAVE_BYTES / 2; i++)
	{
		SV_COMM2 = (uint16_t)(SAVE_WORD0 + i);
		if (!sv_cmd(57)) { save_unpack((const uint8_t *)"none"); return; }
		const uint16_t v = SV_COMM8;
		b[2 * i] = (uint8_t)(v >> 8); b[2 * i + 1] = (uint8_t)v;
	}
	save_unpack(b);
}
void save_store(void)
{
	static uint8_t b[SAVE_BYTES];
	save_pack(b);
	for (int i = 0; i < SAVE_BYTES / 2; i++)
	{
		SV_COMM8 = (uint16_t)(b[2 * i] << 8 | b[2 * i + 1]);
		SV_COMM2 = (uint16_t)(SAVE_WORD0 + i);
		if (!sv_cmd(58)) return;
	}
	SV_COMM12 = 0;
	sv_cmd(59);
}
#else
static void save_path(char *p, size_t n) { snprintf(p, n, "%s/STXSAVE.BIN", getenv("ST_DIR") ? getenv("ST_DIR") : "."); }
static void save_load(void)
{
	static uint8_t b[SAVE_BYTES];
	char path[512];
	save_path(path, sizeof path);
	FILE *f = fopen(path, "rb");
	if (f) { if (fread(b, 1, SAVE_BYTES, f) != SAVE_BYTES) b[0] = 0; fclose(f); }
	save_unpack(b);
}
void save_store(void)
{
	static uint8_t b[SAVE_BYTES];
	save_pack(b);
	char path[512];
	save_path(path, sizeof path);
	FILE *f = fopen(path, "wb");
	if (f) { fwrite(b, 1, SAVE_BYTES, f); fclose(f); }
}
#endif

/* ------------------------------------------------------------------ screens */
static uint16_t keep_pal[64];
static void pal_keep(void) { memcpy(keep_pal, palette + 192, sizeof keep_pal); }
static void pal_back(void) { memcpy(palette + 192, keep_pal, sizeof keep_pal); yt_invalidate(); }

void bg_picture(void)
{
	yt_screen();
	pic_show(PIC_MENU);
}

/* the panel under a menu: two lines of help, the timing line when it is on */
static void menu_panel(const char *l1, const char *l2)
{
	static char t[48];
	if (opt_timing) timing_line(t);
	else t[0] = 0;
	panel_show(l1, l2, t, col_yellow);
}

static int m_top = 28, m_step = 21, m_rows = 7;   /* where the rows go (the title sets its own) */
static int m_lr, menu_dir;
static void title_style(void) { m_top = 84; m_step = 18; m_rows = 5; }   /* under the logo */
static void list_style(void) { m_top = 28; m_step = 21; m_rows = 7; }             /* m_lr: LEFT / RIGHT also choose (menu_dir -1 / +1) */

static void menu_row(int y, const char *t, int on)
{
	const int big = text_width(F_WHITE, t) <= YT_VIEW_W;
	if (big) text_mid(on ? F_GOLD : F_WHITE, y, t);
	else
	{
		text_mid(F_SMALL, y + 5, t);
		if (on) { text_at(F_GOLD, 0, y, ">"); text_at(F_GOLD, YT_VIEW_W - 16, y, "<"); }
	}
}

int menu_choose(const char *title, const char *const *items, int n, int sel, BgFn bg, const char *hint)
{
	if (sel < 0 || sel >= n) sel = 0;
	for (;;)
	{
		uint16_t hit;
		pad_read(&hit);
		if (hit & PAD_UP) sel = (sel + n - 1) % n;
		if (hit & PAD_DOWN) sel = (sel + 1) % n;
		menu_dir = 0;
		if (m_lr && (hit & (PAD_LEFT | PAD_RIGHT))) { menu_dir = (hit & PAD_LEFT) ? -1 : 1; return sel; }
		if (hit & (PAD_A | PAD_C | PAD_START)) return sel;
		if (hit & PAD_B) return -1;
		plat_wait_flip();
		bg();
		const int top = m_top;
		const int rows = n < m_rows ? n : m_rows;
		int first = sel - rows / 2;
		if (first > n - rows) first = n - rows;
		if (first < 0) first = 0;
		if (title) text_mid(F_GOLD, top - 24, title);
		for (int i = 0; i < rows; i++) menu_row(top + m_step * i, items[first + i], first + i == sel);
		if (first > 0) text_at(F_SMALL, YT_VIEW_W - 12, top, "^");
		if (first + rows < n) text_at(F_SMALL, YT_VIEW_W - 12, top + m_step * (rows - 1) + 8, "v");
		menu_panel("UP/DOWN CHOOSE  A/C/START SELECT", hint ? hint : "B BACK");
		present_view();
	}
}

void message_screen(BgFn bg, const char *l1, const char *l2, const char *l3, int min_ms, int max_ms)
{
	const uint32_t t0 = plat_ticks_ms();
	pad_read(0);
	for (;;)
	{
		uint16_t hit;
		pad_read(&hit);
		const uint32_t ms = plat_ticks_ms() - t0;
		if (ms >= (uint32_t)max_ms || (ms >= (uint32_t)min_ms && (hit & (PAD_A | PAD_B | PAD_C | PAD_START)))) break;
		plat_wait_flip();
		bg();
		menu_panel(l1 ? l1 : "", l2 ? l2 : "");
		(void)l3;
		present_view();
	}
}

/* ------------------------------------------------------------------ text pages (text.cpp) */
/* display_text_file: the file's lines scroll up over the picture; a first character ' ' small
 * letters, TAB normal, '-' big (a heading), anything else normal (SuperTux's blue) */
static int text_lines_draw(const char *t, uint32_t size, int top)
{
	const char *end = t + size;
	int y = top;
	char line[64];
	while (t < end)
	{
		const char *e = t;
		while (e < end && *e != '\n') e++;
		int n = (int)(e - t);
		const char *s = t;
		int f = F_WHITE;
		if (n > 0 && *s == ' ') { s++; n--; f = F_SMALL; }
		else if (n > 0 && *s == '\t') { s++; n--; }
		else if (n > 0 && *s == '-') { s++; n--; f = F_GOLD; }
		if (n > 63) n = 63;
		memcpy(line, s, (size_t)n);
		line[n] = 0;
		if (f != F_SMALL && text_width(f, line) > YT_VIEW_W) f = F_SMALL;
		const int h = (f == F_SMALL ? 9 : 18) + 2;
		if (n && y + h > 0 && y < YT_VIEW_H) text_mid(f, y, line);
		y += h;
		t = e < end ? e + 1 : e;
	}
	return y < 0;
}

static int page_pic;
static const char *page_txt;
static uint32_t page_size;
static int32_t page_pos;               /* the first line's y, 1/16 pixels */
static int page_done;
static void bg_page(void)
{
	yt_screen();
	pic_show(page_pic);
	page_done = text_lines_draw(page_txt, page_size, page_pos / 16);
}

void text_page(const char *file, int pic, int credits)
{
	pal_keep();
	page_pic = pic;
	page_txt = text_file(file, &page_size);
	page_pos = YT_VIEW_H * 16;
	page_done = 0;
	int track = TRACK_FIRST;
	if (credits) music(track);
	step_clock_reset();
	pad_read(0);
	static char l1[48];
	for (;;)
	{
		uint16_t hit;
		pad_read(&hit);
		const int steps = step_clock();
		page_pos -= steps * (credits ? 10 : 8);   /* SCROLL_SPEED_CREDITS 1.2 / _MESSAGE 1.0: 60 / 50 pixels a second */
		if (hit & PAD_UP) page_pos += 16 * 40;
		if (hit & PAD_DOWN) page_pos -= 16 * 40;
		if (credits && (hit & (PAD_LEFT | PAD_RIGHT)))
		{
			/* ours (Hurrican's credits): SuperTux's songs */
			if (hit & PAD_RIGHT) track = track == TRACK_LAST ? TRACK_FIRST : track + 1;
			else track = track == TRACK_FIRST ? TRACK_LAST : track - 1;
			music(track);
		}
		if ((hit & (PAD_A | PAD_B | PAD_C | PAD_START)) || page_done) break;
		plat_wait_flip();
		bg_page();
		if (credits) put_dec(put_str(l1, "LEFT/RIGHT: MUSIC TRACK "), (uint32_t)track, 2);
		else l1[0] = 0;
		menu_panel(l1, "A/B/C: END   UP/DOWN: SCROLL");
		present_view();
	}
	pal_back();
	pad_read(0);
}

/* ------------------------------------------------------------------ high scores */
static int hs_place(uint32_t s)
{
	for (int i = 0; i < HS_N; i++) if (s > hs[i].score) return i;
	return -1;
}

static BgFn page_bg;
static void bg_scores(void)
{
	static char t[48];
	page_bg();
	text_mid(F_GOLD, 2, "HIGH SCORES");
	for (int i = 0; i < HS_N; i++)
	{
		char *q = t;
		if (i < 9) *q++ = ' ';
		q = put_dec(q, (uint32_t)i + 1, 1);
		q = put_str(q, ". ");
		q = put_str(q, hs[i].name);
		q = put_str(q, "  ");
		q = put_dec(q, hs[i].score, 7);
		q = put_str(q, "  LEVEL ");
		put_dec(q, hs[i].level, 2);
		text_mid(F_SMALL, 26 + 14 * i, t);
		if (i == hs_new) text_at(F_GOLD, 4, 22 + 14 * i, ">");
	}
}
static void scores_page(BgFn bg)
{
	page_bg = bg;
	message_screen(bg_scores, "ANY BUTTON: BACK", "", 0, 300, 60000);
}

static const char hs_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789. ";
static char hs_name[4];
static int hs_pos;
static uint32_t hs_score;
static void bg_entry(void)
{
	static char t[24];
	bg_picture();
	text_mid(F_GOLD, 30, "A NEW HIGH SCORE!");
	put_dec(t, hs_score, 1);
	text_mid(F_WHITE, 56, t);
	for (int k = 0; k < 3; k++)
	{
		char c[2] = { hs_name[k], 0 };
		text_at(k == hs_pos ? F_GOLD : F_WHITE, YT_VIEW_W / 2 - 24 + 16 * k, 90, c);
	}
	if ((plat_ticks_ms() / 300) & 1) text_at(F_GOLD, YT_VIEW_W / 2 - 24 + 16 * hs_pos, 106, "-");
}

/* the game is over (or a world finished): a name for a place in the table */
static void hs_offer(uint32_t score, int level)
{
	const int p = hs_place(score);
	hs_new = -1;
	if (p < 0) return;
	memcpy(hs_name, "AAA", 4);
	hs_pos = 0;
	hs_score = score;
	pad_read(0);
	for (;;)
	{
		uint16_t hit;
		pad_read(&hit);
		const int n = (int)sizeof hs_chars - 1;
		const char *c = strchr(hs_chars, hs_name[hs_pos]);
		const int k = c ? (int)(c - hs_chars) : 0;
		if (hit & PAD_UP) hs_name[hs_pos] = hs_chars[(k + 1) % n];
		if (hit & PAD_DOWN) hs_name[hs_pos] = hs_chars[(k + n - 1) % n];
		if ((hit & PAD_LEFT) && hs_pos > 0) hs_pos--;
		if (hit & (PAD_RIGHT | PAD_A | PAD_B | PAD_C)) hs_pos++;
		if ((hit & PAD_START) || hs_pos > 2) break;
		plat_wait_flip();
		bg_entry();
		menu_panel("UP/DOWN LETTER  LEFT/RIGHT MOVE", "START DONE");
		present_view();
	}
	for (int i = HS_N - 1; i > p; i--) hs[i] = hs[i - 1];
	memcpy(hs[p].name, hs_name, 4);
	hs[p].score = score;
	hs[p].level = (uint8_t)(level > 99 ? 99 : level);
	hs_new = p;
	save_store();
	scores_page(bg_picture);
}

/* ------------------------------------------------------------------ options */
static void bg_controls(void)
{
	page_bg();
	text_mid(F_GOLD, 2, "CONTROLS");
	static const char *const rows[] = {
		"LEFT / RIGHT      WALK",
		"HOLD B            RUN, CARRY",
		"B (FIRE TUX)      SHOOT",
		"C OR A            JUMP",
		"HOLD THE JUMP     JUMP HIGHER",
		"DOWN (BIG TUX)    DUCK",
		"START             PAUSE",
		"WORLD MAP: PAD WALKS",
		"A OR C ENTERS A LEVEL",
		"MENUS: A/C/START SELECT, B BACK" };
	static const char *const rows_sw[] = { 0, "HOLD A            RUN, CARRY", "A (FIRE TUX)      SHOOT", "C OR B            JUMP" };
	for (int i = 0; i < 10; i++)
		text_mid(F_SMALL, 26 + 14 * i, opt_swap && i >= 1 && i <= 3 ? rows_sw[i] : rows[i]);
}

static int level_select(void);
static void play_alone(int li);
static int options_played;            /* R11: a level was played from OPTIONS - LEVEL SELECT */

void options_page(BgFn bg)
{
	enum { O_MUSIC, O_LIVES, O_TIMING, O_BUTTONS, O_BACKGROUND, O_MOTION, O_SCROLL, O_DRAW, O_LEVELS, O_CONTROLS, O_STORY, O_SCORES, O_CREDITS, O_BACK, O_N };
	int sel = 0;
	static char rows[O_N][32];
	static const char *items[O_N];
	for (;;)
	{
		put_str(rows[O_MUSIC], opt_music ? "MUSIC  < ON >" : "MUSIC  < OFF >");
		put_str(put_dec(put_str(rows[O_LIVES], "LIVES  < "), (uint32_t)opt_lives, 1), " >");
		/* REV: OFF / ON (the averages, R4-R11) / WORST (the slowest frame of the last 64) */
		put_str(rows[O_TIMING], opt_timing == 2 ? "TIMING <WORST>" : opt_timing ? "TIMING < ON >" : "TIMING < OFF >");
		put_str(rows[O_BUTTONS], opt_swap ? "JUMP   < C/B >" : "JUMP   < C/A >");
		/* R4: SuperTux's background moving at its own speed (costs frames: game/game.c) */
		/* REV3: GENESIS - the picture on the Genesis' plane B, scrolled by the Genesis chip */
		put_str(rows[O_BACKGROUND], opt_parallax == 2 ? "BACKGROUND <GENESIS>" : opt_parallax ? "BACKGROUND <MOVES>" : "BACKGROUND <FIXED>");
		/* R9: R6's drawing between steps on / off (to compare on the console) */
		put_str(rows[O_MOTION], opt_smooth ? "MOTION <SMOOTH>" : "MOTION <STEPS>");
		/* R10: the canvas wrapping up / down (no re-anchor when the view goes up or down) */
		put_str(rows[O_SCROLL], opt_ring ? "SCROLL <RING>" : "SCROLL <CLASSIC>");
		/* REV: rows of new cells copied by the master's DMA unit (game/yt_layers.c) */
		put_str(rows[O_DRAW], opt_dma ? "DRAW   <DMA>" : "DRAW   <CPU>");
		put_str(rows[O_LEVELS], "LEVEL SELECT");
		put_str(rows[O_CONTROLS], "CONTROLS");
		put_str(rows[O_STORY], "STORY");
		put_str(rows[O_SCORES], "HIGH SCORES");
		put_str(rows[O_CREDITS], "CREDITS");
		put_str(rows[O_BACK], "BACK");
		/* R11: LEVEL SELECT only from the title (from a level's pause or the world map it would
		 * start a level inside another one) */
		const int from_title = bg == bg_picture;
		int shown[O_N], ns = 0, at = 0;
		for (int i = 0; i < O_N; i++)
		{
			if (i == O_LEVELS && !from_title) continue;
			if (i == sel) at = ns;
			shown[ns] = i;
			items[ns++] = rows[i];
		}
		list_style();
		m_lr = 1;                      /* LEFT / RIGHT change a setting */
		const int pick = menu_choose("OPTIONS", items, ns, at, bg, "LEFT/RIGHT/A CHANGE  B BACK");
		const int c = pick < 0 ? -1 : shown[pick];
		const int d = menu_dir ? menu_dir : 1;
		m_lr = 0;
		if (c < 0 || (c == O_BACK && !menu_dir)) break;
		sel = c;
		if (menu_dir && c > O_DRAW) continue;   /* LEFT / RIGHT only change the settings */
		switch (c)
		{
		case O_MUSIC: opt_music ^= 1; music_again(); break;
		case O_LIVES: opt_lives = (opt_lives - 1 + d + 9) % 9 + 1; break;
		case O_TIMING: opt_timing = (opt_timing + (d > 0 ? 1 : 2)) % 3; break;
		case O_DRAW: opt_dma ^= 1; yt_set_dma(opt_dma); break;
		case O_BUTTONS: opt_swap ^= 1; break;
		case O_BACKGROUND: opt_parallax = (opt_parallax + (d > 0 ? 1 : 2)) % 3; break;   /* from the next level read on */
		case O_MOTION: opt_smooth ^= 1; break;
		case O_SCROLL: opt_ring ^= 1; yt_set_ring(opt_ring); break;
		case O_LEVELS:
		{
			const int li = level_select();
			if (li >= 0)
			{
				save_store();
				play_alone(li);
				options_played = 1;    /* the title starts its demo again */
				return;
			}
			break;
		}
		case O_CONTROLS: page_bg = bg; message_screen(bg_controls, "ANY BUTTON: BACK", "", 0, 300, 60000); break;
		case O_STORY: text_page("INTRO.TXT", PIC_MENU, 0); break;
		case O_SCORES: hs_new = -1; scores_page(bg); break;
		case O_CREDITS: text_page("CREDITS.TXT", PIC_CREDITS, 1); music_again(); break;
		}
	}
	save_store();
}

/* ------------------------------------------------------------------ the pause in a level */
int pause_menu(void)
{
	static const char *const items[] = { "CONTINUE", "RESTART LEVEL", "OPTIONS", "ABORT LEVEL", "MAIN MENU" };
	for (;;)
	{
		const int c = menu_choose("PAUSE", items, 5, 0, game_draw_world, "B: CONTINUE");
		switch (c)
		{
		case 1: return -1;
		case 2: options_page(game_draw_world); continue;
		case 3: return ES_LEVEL_ABORT;
		case 4: return ES_QUIT;
		default: return 0;
		}
	}
}

/* ------------------------------------------------------------------ the title */
static void bg_title(void)
{
	demo_run();
	int w, h;
	const uint8_t *px = spr_frame(S_LOGO, 0, &w, &h);
	yt_sprite(((YT_VIEW_W - w) / 2) & ~1, 2, w, h, px);
}


/* LEVEL SELECT (ours): every level on its own, as SuperTux's contrib menu plays a level set's */
static int level_select(void)
{
	static char names[76][48];
	static const char *items[76];
	for (int i = 0; i < 76; i++)
	{
		char *q = put_str(names[i], i < 26 ? "1-" : i < 48 ? "B1-" : "B2-");
		q = put_dec(q, (uint32_t)(i < 26 ? i + 1 : i < 48 ? i - 25 : i - 47), 1);
		q = put_str(q, " ");
		const char *t = level_title(i);
		int n = 0;
		while (t[n] && n < 34) { q[n] = t[n]; n++; }
		q[n] = 0;
		items[i] = names[i];
	}
	static int sel;
	const int c = menu_choose("LEVEL SELECT", items, 76, sel, bg_picture, "B BACK");
	if (c >= 0) sel = c;
	return c;
}

static void play_alone(int li)
{
	ps_reset();
	const int r = game_play(li);
	if (r == ES_GAME_OVER || r == ES_LEVEL_FINISHED) hs_offer(ps.score, li + 1);
}

static void after_world(int score, int si)
{
	if (score >= 0) hs_offer((uint32_t)score, slots[si].map == 0 ? 26 : 0);
}

static void start_game(void)
{
	static char rows[NSLOTS + 1][48];
	static const char *items[NSLOTS + 1];
	for (;;)
	{
		for (int i = 0; i < NSLOTS; i++)
		{
			const Slot *s = &slots[i];
			char *q = put_dec(put_str(rows[i], "SLOT "), (uint32_t)i + 1, 1);
			if (!s->used) put_str(q, " - FREE");
			else
			{
				int solved = 0;
				for (int k = 0; k < 64; k++) solved += (s->solved[s->map][k >> 3] >> (k & 7)) & 1;
				q = put_str(put_str(q, " - "), wmap_name(s->map));
				q = put_dec(put_str(q, " "), (uint32_t)solved, 1);
				put_dec(put_str(q, "/"), (uint32_t)wmap_levels(s->map), 1);
			}
			items[i] = rows[i];
		}
		put_str(rows[NSLOTS], "ERASE A SLOT");
		items[NSLOTS] = rows[NSLOTS];
		list_style();
		const int c = menu_choose("START GAME", items, NSLOTS + 1, 0, bg_picture, "B BACK");
		if (c < 0) return;
		if (c == NSLOTS)
		{
			static const char *const er[NSLOTS] = { "ERASE SLOT 1", "ERASE SLOT 2", "ERASE SLOT 3", "ERASE SLOT 4", "ERASE SLOT 5" };
			const int e = menu_choose("ERASE A SLOT", er, NSLOTS, 0, bg_picture, "B: ERASE NOTHING");
			if (e >= 0) { memset(&slots[e], 0, sizeof slots[e]); save_store(); }
			continue;
		}
		if (!slots[c].used)
		{
			/* a new game: the story first (title.cpp: intro.txt), then Icyisland */
			memset(&slots[c], 0, sizeof slots[c]);
			music(TRACK_THEME);
			text_page("INTRO.TXT", PIC_MENU, 0);
		}
		after_world(wmap_run(slots[c].map, c), c);
		return;
	}
}

static void bonus_levels(void)
{
	static const char *const items[] = { "BONUS ISLAND I", "BONUS ISLAND II", "LEVEL SELECT" };
	for (;;)
	{
		list_style();
		const int c = menu_choose("BONUS LEVELS", items, 3, 0, bg_picture, "B BACK");
		if (c < 0) return;
		if (c < 2)
		{
			/* each Bonus Island keeps its own save (SuperTux: bonusisland1.stsg, ...) */
			const int si = NSLOTS + c;
			slots[si].map = (uint8_t)(c + 1);
			after_world(wmap_run(c + 1, si), si);
			return;
		}
		const int li = level_select();
		if (li >= 0) { play_alone(li); return; }
	}
}

int main(int argc, char *argv[])
{
	(void)argc; (void)argv;
	plat_init();
	res_load();
	save_load();
	plat_border_colour(0);
	ps_reset();
#ifndef OJ32X_MARS
	/* PC checks: straight into a level (ST_LEVEL) or a world map (ST_MAP); ST_TIMING=1 the
	 * timing line */
	if (getenv("ST_TIMING")) opt_timing = atoi(getenv("ST_TIMING")) == 2 ? 2 : 1;
	if (getenv("ST_DMA")) { opt_dma = 1; yt_set_dma(1); }   /* REV PC checks: DRAW <DMA> */
	if (getenv("ST_PARALLAX")) opt_parallax = 1;
	if (getenv("ST_GENESIS")) opt_parallax = 2;   /* REV3 PC checks: BACKGROUND GENESIS */
	if (getenv("ST_CLASSIC")) { opt_ring = 0; yt_set_ring(0); }   /* R10 PC checks: SCROLL CLASSIC */
	if (getenv("ST_LEVEL")) { for (;;) play_alone(atoi(getenv("ST_LEVEL"))); }
	if (getenv("ST_MAP")) { for (;;) wmap_run(atoi(getenv("ST_MAP")), 0); }
#endif
	int sel = 0;
	for (;;)
	{
		demo_start();
		for (;;)
		{
			static const char *const items[] = { "START GAME", "BONUS LEVELS", "OPTIONS", "CREDITS", "HIGH SCORES" };
			title_style();
			const int c = menu_choose(0, items, 5, sel, bg_title, "SUPERTUX 0.1.3 - SEGA CD 32X");
			list_style();
			if (c < 0) continue;
			sel = c;
			if (c == 0) { start_game(); break; }
			if (c == 1) { bonus_levels(); break; }
			list_style();
			if (c == 2)
			{
				options_played = 0;
				options_page(bg_picture);
				if (options_played) break;   /* R11: back from a level: the demo again */
				music_again();
			}
			if (c == 3) { text_page("CREDITS.TXT", PIC_CREDITS, 1); music_again(); }
			if (c == 4) { hs_new = -1; scores_page(bg_picture); }
		}
	}
}
