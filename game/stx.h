/*
 * SuperTux CD32X - the game (R2: the whole of SuperTux 0.1.3, half size, sharpened).
 *
 * Runs from main(), after the boot path (Hurrican CD32X's = the OpenJazz port's J19b files,
 * byte for byte - tools/check_boot.py) has finished, and uses only the platform layer's
 * existing calls (port/plat.h). Drawing: Vic's tiler (YATSSD) - game/yt.h.
 *
 * SuperTux 0.1.3's rules (src/player.cpp, badguy.cpp, special.cpp, gameobjs.cpp, world.cpp,
 * collision.cpp, physic.cpp, gameloop.cpp, worldmap.cpp, title.cpp, particlesystem.cpp) are
 * rebuilt in C in SuperTux's own units - its 640 x 480 screen pixels, 32-pixel tiles - and its
 * own time step (frame_ratio 1 = 10 ms: 100 steps a second), in fixed point (16 bits of
 * fraction: the SH2 has no floating point unit). Drawn at half size ("Sharpened": the pictures
 * halved, then an unsharp mask - tools/stx_convert.py): screen pixel = unit / 2.
 *
 * Files:
 *   res.c     the resident pack: palette, tiles, sprites, letters, pictures, world maps, texts;
 *             drawing sprites and text
 *   level.c   a level: its pack, its layers in SDRAM (animated tiles, coins taken, bricks
 *             broken), the tile rules, collision with the map
 *   tux.c     Tux (player.cpp)
 *   objects.c the bad guys (badguy.cpp), upgrades and bullets (special.cpp), bouncy coins and
 *             bricks, brick pieces, floating scores (gameobjs.cpp), the collisions (world.cpp)
 *   game.c    a level being played (gameloop.cpp, world.cpp): steps, camera, particles, music,
 *             end conditions, drawing, the HUD
 *   wmap.c    the world maps (worldmap.cpp)
 *   menus.c   title (with SuperTux's demo level running behind it), menus, options, texts,
 *             high scores, save slots, main()
 */
#ifndef STX_H
#define STX_H

#include "plat.h"
#include "pack.h"
#include "yt.h"

#include <stdint.h>
#include <string.h>
#ifndef OJ32X_MARS
#include <stdio.h>
#include <stdlib.h>
#endif

/* ------------------------------------------------------------------ helpers */
static inline uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static inline uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
char *put_str(char *p, const char *s);
char *put_dec(char *p, uint32_t v, int digits);
const uint8_t *need(const char *name);
uint32_t rnd(void);

/* ------------------------------------------------------------------ fixed point, units */
typedef int32_t fx;                    /* 16.16, SuperTux's units */
#define FX(v)     ((fx)((v) * 65536.0)) /* constants only: folded by the compiler */
#define FX_INT(v) ((v) >> 16)
#define ONE       (1 << 16)
static inline fx fxabs(fx v) { return v < 0 ? -v : v; }
/* SuperTux units -> screen pixels (half) */
static inline int px_of(int u) { return u >> 1; }
#define VIEW_W_U  640                  /* the view in units: SuperTux's whole width ... */
#define VIEW_H_U  352                  /* ... and 352 of its 480 lines (176 pixels) */
#define STEP_MS   10                   /* SuperTux's frame_ratio 1 */
/* R5: drawing BETWEEN the last two steps. The world takes 10 ms steps (100 a second) but the
 * 32X shows 60 pictures a second, so a frame gets 1 or 2 steps (2, 2, 1, 2, 2, 1 ...): the
 * world moved 20, 20, 10 ms between pictures and scrolling juddered even at 60 frames a second
 * (Micronut99's R5 video: "Not smooth at all"). Before each step the positions are kept
 * (interp_save); a picture draws everything at prev + (now - prev) x interp_a / 256, where
 * interp_a is how far the clock is into the next step - so the world moves evenly with the
 * clock. One step (10 ms) behind; positions that jump (a reset point, snow wrapping round,
 * something new) are drawn where they are. */
extern int interp_a;                   /* 0..256 */
extern int interp_on;                  /* 0 after world_start until the first step */
static inline fx lerp_fx(fx prev, fx now)
{
	const fx d = now - prev;
	if (!interp_on || d > (64 << 16) || d < -(64 << 16)) return now;
	return prev + (fx)(((d >> 4) * interp_a) >> 4);
}
void objects_interp_save(void);
uint16_t yt_pad_tick(void);              /* R9: the 68000's counter that came with the last pad reply */
void objects_interp_reset(void);
#define STEPS(ms) ((ms) / STEP_MS)

/* SuperTux 0.1.3 src/defines.h, player.h, badguy.cpp, special.cpp (per 10 ms step) */
#define WALK_SPEED          FX(1.0)
#define MAX_WALK_XM         FX(2.3)
#define MAX_RUN_XM          FX(3.2)
#define WALK_ACCELERATION_X FX(0.03)
#define RUN_ACCELERATION_X  FX(0.04)
#define SKID_XM             FX(2.0)
#define SKID_TIME           200
#define KICKING_TIME        200
#define TUX_SAFE_TIME       1800
#define TUX_INVINCIBLE_TIME 10000
#define TUX_INVINCIBLE_TIME_WARNING 2000
#define TIME_WARNING        20000
#define MAX_LIVES           99
#define START_LIVES         4
#define DISTROS_LIFEUP      100
#define SCORE_BRICK         5
#define SCORE_DISTRO        25
#define OFFSCREEN_DISTANCE  256
#define MAX_BULLETS         2
#define BADGUY_WALK_SPEED   FX(0.8)

/* tile flags (tools/stx_convert.py TILES.BIN) */
#define TF_SOLID   1
#define TF_ICE     2
#define TF_WATER   4
#define TF_BRICK   8
#define TF_FULLBOX 16
#define TF_DISTRO  32
#define TF_COIN    64
#define TF_GOAL    128

/* sound effects (tools/make_stx_sfx.py SOUNDS, plat_sfx(n) = SOUNDS[n - 1]) */
enum { SFX_JUMP = 1, SFX_SKID, SFX_DISTRO, SFX_LIFEUP, SFX_HURT, SFX_BIGJUMP, SFX_STOMP,
       SFX_SQUISH, SFX_KICK, SFX_BRICK, SFX_BUMP_UPGRADE, SFX_FALL, SFX_EXCELLENT, SFX_COFFEE,
       SFX_HERRING, SFX_UPGRADE, SFX_SHOOT, SFX_EXPLODE, SFX_RICOCHET, SFX_WARP };
/* CD tracks (tools/stx_convert.py MUSIC: track 2 + index; a song's "-fast" version is next) */
#define TRACK_THEME     2
#define TRACK_HERRING   11              /* SALCON.MOD (resources.cpp herring_song) */
#define TRACK_LEVELDONE 13
#define TRACK_FIRST     2
#define TRACK_LAST      13

/* ------------------------------------------------------------------ res.c */
extern uint16_t palette[256];
extern int nframes, ntiles;
extern const uint8_t *tile_px;         /* nframes x 256 (16 x 16) */
typedef struct { uint16_t first, frames, speed, flags, data, next; } TileDesc;
extern TileDesc tdesc[512];
extern uint8_t col_white, col_black, col_panel, col_yellow;

typedef struct { int n, fps10, hx, hy, first; } Sprite;
/* the sprites the game draws, by SuperTux's names (res.c spr_names) */
enum {
	S_SMALL_WALK_L, S_SMALL_WALK_R, S_SMALL_STAND_L, S_SMALL_STAND_R, S_SMALL_JUMP_L, S_SMALL_JUMP_R,
	S_SMALL_GAMEOVER, S_SMALL_SKID_L, S_SMALL_SKID_R, S_SMALL_KICK_L, S_SMALL_KICK_R, S_SMALL_GRAB_L,
	S_SMALL_GRAB_R, S_SMALL_STAR,
	S_LARGE_WALK_L, S_LARGE_WALK_R, S_LARGE_STAND_L, S_LARGE_STAND_R, S_LARGE_JUMP_L, S_LARGE_JUMP_R,
	S_LARGE_DUCK_L, S_LARGE_DUCK_R, S_LARGE_SKID_L, S_LARGE_SKID_R, S_LARGE_KICK_L, S_LARGE_KICK_R,
	S_LARGE_GRAB_L, S_LARGE_GRAB_R, S_LARGE_STAR,
	S_FIRE_WALK_L, S_FIRE_WALK_R, S_FIRE_STAND_L, S_FIRE_STAND_R, S_FIRE_JUMP_L, S_FIRE_JUMP_R,
	S_FIRE_DUCK_L, S_FIRE_DUCK_R, S_FIRE_SKID_L, S_FIRE_SKID_R, S_FIRE_KICK_L, S_FIRE_KICK_R,
	S_FIRE_GRAB_L, S_FIRE_GRAB_R,
	S_ICEBLOCK_L, S_ICEBLOCK_R, S_ICEBLOCK_FALLING_L, S_ICEBLOCK_FALLING_R, S_ICEBLOCK_FLAT_L,
	S_ICEBLOCK_FLAT_R, S_SNOWBALL_SQUISHED_L, S_SNOWBALL_SQUISHED_R, S_JUMPY_UP, S_JUMPY_DOWN,
	S_JUMPY_MIDDLE, S_MRBOMB_L, S_MRBOMB_R, S_MRBOMB_TICKING_L, S_MRBOMB_TICKING_R,
	S_MRBOMB_EXPLOSION, S_STALACTITE, S_STALACTITE_BROKEN, S_FLAME, S_FISH, S_FISH_DOWN,
	S_BSNOWBALL_L, S_BSNOWBALL_R, S_BSNOWBALL_SQUISHED, S_FSNOWBALL, S_FSNOWBALL_SQUISHED,
	S_SPIKY_L, S_SPIKY_R, S_SNOWBALL_L, S_SNOWBALL_R,
	S_EGG, S_1UP, S_STAR, S_ICEFLOWER, S_BULLET,
	S_LOGO, S_WM_SMALLTUX, S_WM_TUX, S_WM_FIRETUX, S_DOT_RED, S_DOT_GREEN, S_TELEPORTER,
	S_SNOW0, S_SNOW1, S_SNOW2, S_CLOUD, S_TUX_LIFE,
	S_N
};
extern Sprite spr[S_N];
/* a sprite's frame at time ms: w, h, pixels */
const uint8_t *spr_frame(int s, uint32_t ms, int *w, int *h);
const uint8_t *spr_frame_at(int s, uint32_t ms, int *w, int *h, int *dx, int *dy);   /* + where the cut picture sits */
/* draw sprite s with its hot spot at world position (ux, uy) (units), camera (cx, cy) pixels */
void spr_draw(int s, uint32_t ms, int ux, int uy, int cx, int cy);
/* the first rows only (rising upgrades: draw_part) */
void spr_draw_part(int s, int ux, int uy, int rows_u, int cx, int cy);
void tile_draw(int tile, uint32_t ms, int ux, int uy, int cx, int cy);   /* a tile as a sprite */

enum { F_GOLD, F_WHITE, F_SMALL, F_N };   /* tools/stx_convert.py FONTS */
int text_width(int f, const char *t);
int fit_font(const char *t);
void text_at(int f, int x, int y, const char *t);
void text_mid(int f, int y, const char *t);
void menu_line(int f, int y, const char *t, int on);
enum { PIC_MENU, PIC_EXTRO, PIC_CREDITS, PIC_N };
void pic_show(int p);                  /* the view = that picture, palette 192-255 its colours */
const char *text_file(const char *name, uint32_t *size);
const char *level_title(int li);       /* NAMES.TXT */
const char *level_author(int li);
void res_load(void);

/* the panel (24 lines under the view: three lines of the 8x8 font) */
void panel_show(const char *l1, const char *l2, const char *l3, uint8_t c1);

/* ------------------------------------------------------------------ level.c */
extern int lw, lh, ltrack, ltime, lgrav, lstart_x, lstart_y, lparticles, lback_scroll, lautoscroll, lbkgd_speed;
extern int level_n;
extern fx grav_fx;
extern const uint8_t *lobjs;           /* n x {kind u8, stay u8, x s16, y s16} (big-endian) */
extern int lnobjs;
extern const uint8_t *lresets;         /* n x {x s16, y s16} */
extern int lnresets;
void load_level(int li);               /* from the cart; from the disc first if it is not there */
void level_reset_map(void);            /* the map as the level starts (coins back, boxes full) */
void level_animate(uint32_t ms);       /* animated tiles: their pictures follow the time */
void level_mark_animated(int cx, int cy);   /* after yt_begin: cells with animated tiles redrawn */
int  tile_at(int x, int y);            /* SuperTux's gettile: interactive tile id at a point */
int  flags_at(int x, int y);
static inline int issolid(int x, int y) { return flags_at(x, y) & TF_SOLID; }
int  box_hits(int bx, int by, int w, int h);   /* collision.cpp collision_object_map */
void level_change(int x, int y, int tile);     /* level->change(x, y, TM_IA, tile) */
void level_hide(int tx, int ty, int hide);     /* a bouncing brick: its cell empty meanwhile */
void level_palette(void);              /* palette 192-255 = the level's background */
int  level_goal_at(int x, int y);
int  level_tile(int layer, int tx, int ty);
int  level_hidden(int tx, int ty);              /* a bouncing brick's cell: drawn empty meanwhile */
/* R4, OPTIONS - BACKGROUND MOVES: the level's background picture (palette 192-255), rows of
 * back_stride bytes; 0 when this level is drawn with the background fixed to it */
extern const uint8_t *back_px;
extern int back_w, back_h, back_stride;    /* 1 background-tm, 2 interactive-tm, 3 foreground-tm */
extern void (*level_progress)(int done, int total);   /* called after each 32 KB read from the disc */
/* the platform part (game/yt_mars.c, game/host.c): pieces first .. first + n - 1 of disc file
 * fid (9 = IMAGE9.RAW, 200 + k = OJk.PAK) into the cart; 0 if it failed */
int  cart_load(int fid, uint32_t flen, int first, int n, void (*progress)(int done, int total));

/* ------------------------------------------------------------------ physics (physic.cpp) */
typedef struct { fx vx, vy, ax, ay; int gravity; } Physic;   /* vy: down positive */
/* collision.cpp collision_swept_object_map, axis by axis: returns 1 if x was stopped */
int sweep(fx *x, fx *y, fx tx, fx ty, int w, int h);

/* ------------------------------------------------------------------ tux.c */
enum { DYING_NOT, DYING_SQUISHED, DYING_FALLING };
enum { SMALL, BIG };
typedef struct {
	fx x, y;                           /* base.x, base.y */
	int w, h;                          /* 32 x 32 small, 32 x 64 big */
	fx px, py;                         /* previous_base (before this step's movement) */
	Physic ph;
	int dir, old_dir;                  /* 0 left, 1 right */
	int size, got_coffee, duck, dying;
	int jumping, can_jump, holding;
	int skid_t, safe_t, invincible_t, kick_t;   /* timers: steps left */
	/* input (player_input_type): 1 = DOWN */
	int in_left, in_right, in_up, in_down, in_fire, in_old_fire, in_old_up;
} Tux;
extern Tux tux;
void tux_init(int x, int y);
void tux_action(void);
void tux_draw(int cx, int cy, uint32_t ms, uint32_t frame);
void tux_grow(void);
void tux_kill(int shrink_only);
void tux_jump_of_badguy(fx badguy_y);
int  tux_on_ground(void);

/* ------------------------------------------------------------------ objects.c */
enum { K_MRICEBLOCK, K_JUMPY, K_MRBOMB, K_BOMB, K_STALACTITE, K_FLAME, K_FISH, K_BSNOWBALL,
       K_FSNOWBALL, K_SPIKY, K_SNOWBALL };
enum { UP_GROWUP, UP_ICEFLOWER, UP_HERRING, UP_1UP };
void objects_start(void);              /* the level's bad guys (activate_bad_guys) */
void objects_clear(void);
void objects_action(void);
void objects_collisions(void);         /* world.cpp collision_handler */
void objects_draw_back(int cx, int cy, uint32_t ms);    /* under Tux: bad guys, upgrades ... */
void objects_draw_front(int cx, int cy);                /* floating scores */
void add_score(int x, int y, int s);
void add_bouncy_distro(int x, int y);
void add_bouncy_brick(int x, int y);
void add_broken_brick(int tile, int x, int y);
void add_upgrade(int x, int y, int dir, int kind);
void add_bullet(fx x, fx y, fx xm, int dir);
void add_bad_guy(int x, int y, int kind, int stay);
void trybumpbadguy(int x, int y);
extern int score_multiplier;

/* ------------------------------------------------------------------ game.c */
typedef struct { uint32_t score; int distros, lives, bonus; } PlayerStatus;   /* bonus: 0 none, 1 grow, 2 flower */
extern PlayerStatus ps;
void ps_reset(void);                   /* PlayerStatus::reset (lives: OPTIONS - LIVES) */
extern fx scroll_x;                    /* the camera, units (World::scrolling) */
extern int cam_y;                      /* units (ours: the 32X shows 352 of SuperTux's 480 lines) */
extern uint32_t game_ms, global_frame;
extern int opt_music, opt_timing, opt_swap, opt_lives, opt_parallax, opt_smooth, opt_ring;
void trybreakbrick(int x, int y, int small, int dir);
void tryemptybox(int x, int y, int dir);
void trygrabdistro(int x, int y, int bounce);
void bumpbrick(int x, int y);
void music(int track);                 /* a CD track (0 stops; | 0x8000 once), only on a change */
void music_again(void);                /* OPTIONS - MUSIC switched: the current track again / off */
/* the buttons: pad_read returns the pad (A/B swapped when OPTIONS - BUTTONS says so) and in
 * *hit the buttons pressed since the last call */
uint16_t pad_read(uint16_t *hit);
/* 10 ms steps since the last call (at most 10: the game slows down rather than jumps) */
int  step_clock(void);
void step_clock_reset(void);
/* one level being played (gameloop.cpp GameSession::run): returns when it ends */
enum { ES_NONE, ES_LEVEL_FINISHED, ES_GAME_OVER, ES_LEVEL_ABORT, ES_QUIT };
int  game_play(int level);
void game_draw_world(void);            /* the level as it is now (into the back buffer) */
void present_view(void);               /* plat_present_scroll with the tiler's scroll */
/* the title's demo (title.cpp draw_demo): menu.stl with Tux walking by himself */
void demo_start(void);
void demo_run(void);                   /* the steps since the last frame + game_draw_world */
void timing_line(char *out);           /* OPTIONS - TIMING: where a frame's time goes */
int  level_cached(int li);             /* level.c: its pack is the one in the cart */

/* ------------------------------------------------------------------ menus.c */
typedef void (*BgFn)(void);            /* draws a menu's background into the back buffer */
void bg_picture(void);                 /* SuperTux's title picture */
/* a list (title.cpp / menu.cpp): returns the index chosen, -1 for B (back) */
int  menu_choose(const char *title, const char *const *items, int n, int sel, BgFn bg, const char *hint);
int  pause_menu(void);                 /* in a level: 0 go on, -1 restart, ES_LEVEL_ABORT, ES_QUIT */
void options_page(BgFn bg);
/* text.cpp display_text_file: file from the packs over picture pic; credits: LEFT/RIGHT music */
void text_page(const char *file, int pic, int credits);
void message_screen(BgFn bg, const char *l1, const char *l2, const char *l3, int min_ms, int max_ms);

/* ------------------------------------------------------------------ wmap.c */
/* a world map (worldmap.cpp) for save slot `slot`: returns the score to offer the high score
 * table (game over, or the world finished), or -1 when the player just left */
int  wmap_run(int map, int slot);
const char *wmap_name(int map);
int  wmap_levels(int map);             /* how many levels it has (for the slot list) */

/* ------------------------------------------------------------------ saves (menus.c) */
#define NSLOTS 5                       /* SuperTux's five; 5 and 6: Bonus Island I and II */
typedef struct {
	uint8_t used, map, tx, ty;
	uint8_t lives, bonus;
	uint32_t score;
	uint16_t distros;
	uint8_t solved[3][8];              /* per world map: 64 level bits */
} Slot;
extern Slot slots[NSLOTS + 2];
void save_store(void);
#endif
