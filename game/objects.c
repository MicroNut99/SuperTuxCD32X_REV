/*
 * SuperTux CD32X - everything that moves besides Tux: the bad guys (SuperTux 0.1.3
 * src/badguy.cpp: Mr. Iceblock, Jumpy, Mr. Bomb and its bomb, the stalactite, the flame, the
 * fish, the bouncing / flying snowballs, Spiky, the snowball), the upgrades and Tux's bullets
 * (special.cpp), bouncy coins, bouncing bricks, brick pieces and floating scores (gameobjs.cpp),
 * and the collisions between them all (world.cpp collision_handler). Fixed point, SuperTux's
 * units, 10 ms steps. See game/stx.h.
 */
#include "stx.h"

int score_multiplier = 1;

/* ------------------------------------------------------------------ bad guys */
enum { M_NORMAL, M_FLAT, M_KICK, M_HELD, M_JUMPY_JUMP, M_BOMB_TICKING, M_BOMB_EXPLODE,
       M_STALACTITE_SHAKING, M_STALACTITE_FALL, M_FISH_WAIT, M_FLY_UP, M_FLY_DOWN };
typedef struct {
	uint8_t used, kind, mode, dying, dir, stay, seen, removable;
	fx x, y, ox, oy;                   /* base, old_base (the flame: its centre) */
	Physic ph;
	int timer;                         /* steps left (-1: not running) */
	int squishcount;
	int sl, sr;                        /* sprites left / right (-1: none) */
	int angle;                         /* the flame: 1/1024 turns */
} BadGuy;
#define MAX_BAD 320
static BadGuy bad[MAX_BAD];
static int nbad;

static int16_t sintab[1024];            /* sine, 1/1024 turns, x 16384 (made at the first level) */
static void make_sintab(void)
{
	/* sine in 1/1024 turns, x 16384: the first quarter by its series (x - x^3/3! + x^5/5! ...,
	 * 2^28 fixed point, exact to the last bit), the rest mirrored */
	for (int i = 0; i <= 256; i++)
	{
		const int64_t x = (int64_t)i * 1686629714LL / 1024;   /* i / 1024 turn in radians x 2^28 */
		int64_t term = x, sum = x;
		for (int k = 1; k < 8; k++)
		{
			term = -(((term * x) >> 28) * x >> 28) / ((2 * k) * (2 * k + 1));
			sum += term;
		}
		const int v = (int)((sum * 16384 + (1 << 27)) >> 28);
		sintab[i] = (int16_t)v;
		sintab[(512 - i) & 1023] = (int16_t)v;
		sintab[(512 + i) & 1023] = (int16_t)-v;
		sintab[(1024 - i) & 1023] = (int16_t)-v;
	}
}

static void set_sprite(BadGuy *b, int l, int r) { b->sl = l; b->sr = r; }

void add_bad_guy(int x, int y, int kind, int stay)
{
	BadGuy *b = 0;
	for (int i = 0; i < MAX_BAD; i++) if (!bad[i].used) { b = &bad[i]; break; }
	if (!b) return;
	memset(b, 0, sizeof *b);
	if (b - bad >= nbad) nbad = (int)(b - bad) + 1;
	b->used = 1;
	b->kind = (uint8_t)kind;
	b->stay = (uint8_t)stay;
	b->x = (fx)x << 16; b->y = (fx)y << 16;
	b->ox = b->x; b->oy = b->y;
	b->dir = 0;
	b->timer = -1;
	b->ph.gravity = 1;
	switch (kind)
	{
	case K_MRBOMB: b->ph.vx = -BADGUY_WALK_SPEED; set_sprite(b, S_MRBOMB_L, S_MRBOMB_R); break;
	case K_MRICEBLOCK: b->ph.vx = -BADGUY_WALK_SPEED; set_sprite(b, S_ICEBLOCK_L, S_ICEBLOCK_R); break;
	case K_JUMPY: set_sprite(b, S_JUMPY_UP, S_JUMPY_UP); break;
	case K_BOMB: set_sprite(b, S_MRBOMB_TICKING_L, S_MRBOMB_TICKING_R); b->dying = DYING_SQUISHED; break;
	case K_FLAME: b->ph.gravity = 0; set_sprite(b, S_FLAME, S_FLAME); break;
	case K_BSNOWBALL: b->ph.vx = -FX(1.3); set_sprite(b, S_BSNOWBALL_L, S_BSNOWBALL_R); break;
	case K_STALACTITE: b->ph.gravity = 0; set_sprite(b, S_STALACTITE, S_STALACTITE); break;
	case K_FISH: set_sprite(b, S_FISH, S_FISH); b->ph.gravity = 1; break;
	case K_FSNOWBALL: set_sprite(b, S_FSNOWBALL, S_FSNOWBALL); b->ph.gravity = 0; break;
	case K_SPIKY: b->ph.vx = -BADGUY_WALK_SPEED; set_sprite(b, S_SPIKY_L, S_SPIKY_R); break;
	case K_SNOWBALL: default: b->ph.vx = -BADGUY_WALK_SPEED; set_sprite(b, S_SNOWBALL_L, S_SNOWBALL_R); break;
	}
	/* "if we're in a solid tile at start correct that now" */
	if (kind != K_FLAME && kind != K_FISH)
		for (int n = 0; n < 480 && box_hits(FX_INT(b->x), FX_INT(b->y), 32, 32); n++) b->y -= ONE;
	b->ox = b->x; b->oy = b->y;
}

static int bx(const BadGuy *b) { return FX_INT(b->x); }
static int by(const BadGuy *b) { return FX_INT(b->y); }

static void apply(BadGuy *b)
{
	const fx g = b->ph.gravity ? grav_fx : 0;
	b->x += b->ph.vx + b->ph.ax;
	b->y += b->ph.vy + b->ph.ay + g;
	b->ph.vx += b->ph.ax;
	b->ph.vy += b->ph.ay + g;
}

static void swept(BadGuy *b)
{
	fx tx = b->x, ty = b->y;
	b->x = b->ox; b->y = b->oy;
	sweep(&b->x, &b->y, tx, ty, 32, 32);
}

static void fall(BadGuy *b)
{
	if (b->dying != DYING_FALLING)
	{
		if (!issolid(bx(b) + 16, by(b) + 32)) b->ph.gravity = 1;
		else
		{
			if (b->ph.vy > 0)
			{
				b->y = (fx)(((by(b) + 32) / 32) * 32 - 32) << 16;
				b->ph.vy = 0;
			}
			b->ph.gravity = 0;
			if (b->stay && b->mode == M_NORMAL)
			{
				if (!issolid(bx(b) + (b->dir == 0 ? 0 : 32), by(b) + 32))
				{
					if (b->dir == 0) { b->dir = 1; b->ph.vx = fxabs(b->ph.vx); }
					else { b->dir = 0; b->ph.vx = -fxabs(b->ph.vx); }
				}
			}
		}
	}
	else b->ph.gravity = 1;
}

static void check_horizontal_bump(BadGuy *b, int checkcliff)
{
	const int half = 16;
	if (b->dir == 0 && issolid(bx(b), by(b) + half))
	{
		if (b->kind == K_MRICEBLOCK && b->mode == M_KICK) trybreakbrick(bx(b), by(b) + half, 0, b->dir);
		b->dir = 1; b->ph.vx = -b->ph.vx;
		return;
	}
	if (b->dir == 1 && issolid(bx(b) + 32, by(b) + half))
	{
		if (b->kind == K_MRICEBLOCK && b->mode == M_KICK) trybreakbrick(bx(b) + 32, by(b) + half, 0, b->dir);
		b->dir = 0; b->ph.vx = -b->ph.vx;
		return;
	}
	if (!checkcliff) return;
	if (!issolid(bx(b) + 16, by(b) + 32)) return;
	if (b->dir == 0 && !issolid(bx(b), by(b) + 32 + half)) { b->dir = 1; b->ph.vx = -b->ph.vx; return; }
	if (b->dir == 1 && !issolid(bx(b) + 32, by(b) + 32 + half)) { b->dir = 0; b->ph.vx = -b->ph.vx; }
}

static int timer_done(BadGuy *b) { return b->timer == 0; }   /* !timer.check() after it ran out */
static void timer_start(BadGuy *b, int ms) { b->timer = STEPS(ms); }

static void kill_me(BadGuy *b, int score);

static void action_mriceblock(BadGuy *b)
{
	if (b->mode != M_HELD) fall(b);
	if (b->mode != M_HELD)
	{
		apply(b);
		if (b->dying != DYING_FALLING) swept(b);
	}
	else
	{
		/* held by Tux */
		b->dir = (uint8_t)tux.dir;
		const fx hy = tux.y + ((tux.h * 2 / 3) << 16) - (32 << 16);
		b->x = tux.x + (b->dir ? (16 << 16) : -(16 << 16));
		b->y = hy;
		if (box_hits(bx(b), by(b), 32, 32)) { b->x = tux.x; b->y = hy; }
		if (!tux.in_fire)
		{
			/* SHOOT! */
			b->x += b->dir ? (24 << 16) : -(24 << 16);
			b->ox = b->x; b->oy = b->y;
			b->mode = M_KICK;
			tux.kick_t = STEPS(KICKING_TIME);
			set_sprite(b, S_ICEBLOCK_FLAT_L, S_ICEBLOCK_FLAT_R);
			b->ph.vx = b->dir ? FX(3.5) : -FX(3.5);
			plat_sfx(SFX_KICK);
		}
	}
	if (!b->dying)
	{
		const int was = b->dir;
		check_horizontal_bump(b, 0);
		if (b->mode == M_KICK && was != b->dir) plat_sfx(SFX_RICOCHET);
	}
	if (b->mode == M_FLAT && timer_done(b))
	{
		b->mode = M_NORMAL;
		set_sprite(b, S_ICEBLOCK_L, S_ICEBLOCK_R);
		b->ph.vx = b->dir == 0 ? -FX(0.8) : FX(0.8);
		b->ph.vy = 0;
	}
}

static void action_jumpy(BadGuy *b)
{
	const fx vy = fxabs(b->ph.vy);
	if (vy > FX(5.6)) set_sprite(b, S_JUMPY_DOWN, S_JUMPY_DOWN);
	else if (vy > FX(5.3)) set_sprite(b, S_JUMPY_MIDDLE, S_JUMPY_MIDDLE);
	else set_sprite(b, S_JUMPY_UP, S_JUMPY_UP);
	fall(b);
	if (b->dying == DYING_NOT && issolid(bx(b), by(b) + 32))
	{
		b->ph.vy = -FX(6);
		b->ph.gravity = 1;
		b->mode = M_JUMPY_JUMP;
	}
	else if (b->mode == M_JUMPY_JUMP) b->mode = M_NORMAL;
	b->dir = tux.x > b->x ? 1 : 0;
	apply(b);
	if (b->dying == DYING_NOT) swept(b);
}

static void action_mrbomb(BadGuy *b)
{
	if (b->dying == DYING_NOT) check_horizontal_bump(b, 1);
	fall(b);
	apply(b);
	if (b->dying != DYING_FALLING) swept(b);
}

static void action_bomb(BadGuy *b)
{
	fall(b);
	if (b->mode == M_NORMAL) { b->mode = M_BOMB_TICKING; timer_start(b, 1000); }
	else if (timer_done(b))
	{
		if (b->mode == M_BOMB_TICKING)
		{
			b->mode = M_BOMB_EXPLODE;
			set_sprite(b, S_MRBOMB_EXPLOSION, S_MRBOMB_EXPLOSION);
			b->dying = DYING_NOT;      /* now the bomb hurts */
			timer_start(b, 1000);
			plat_sfx(SFX_EXPLODE);
		}
		else if (b->mode == M_BOMB_EXPLODE) { b->removable = 1; return; }
	}
	apply(b);
	swept(b);
}

static void action_stalactite(BadGuy *b)
{
	const int RANGE = 40;
	if (b->mode == M_NORMAL)
	{
		if (FX_INT(tux.x) + 32 > bx(b) - RANGE && FX_INT(tux.x) < bx(b) + 32 + RANGE && FX_INT(tux.y) + tux.h > by(b))
		{
			timer_start(b, 800);
			b->mode = M_STALACTITE_SHAKING;
		}
	}
	if (b->mode == M_STALACTITE_SHAKING)
	{
		b->x = b->ox + (((int)(rnd() % 6) - 3) << 16);
		if (timer_done(b)) b->mode = M_STALACTITE_FALL;
	}
	else if (b->mode == M_STALACTITE_FALL)
	{
		fall(b);
		if (issolid(bx(b) + 16, by(b) + 32))
		{
			timer_start(b, 2000);
			b->dying = DYING_SQUISHED;
			b->mode = M_FLAT;
			set_sprite(b, S_STALACTITE_BROKEN, S_STALACTITE_BROKEN);
		}
	}
	else if (b->mode == M_FLAT) fall(b);
	apply(b);
	if (b->dying == DYING_SQUISHED && timer_done(b)) b->removable = 1;
}

static void action_flame(BadGuy *b)
{
	/* a circle of radius 100 around old_base, 0.02 radians a step: 1024 / (2 pi) x 0.02 = 3.26 */
	b->x = b->ox + (fx)((100 * (int32_t)sintab[(b->angle / 16 + 256) & 1023]) << 2);
	b->y = b->oy + (fx)((100 * (int32_t)sintab[(b->angle / 16) & 1023]) << 2);
	b->angle += 52;                    /* 3.26 / 1024 turn in 1/16 steps */
}

static void action_fish(BadGuy *b)
{
	if (b->dying == DYING_NOT && (flags_at(bx(b), by(b) + 32) & TF_WATER) && b->ph.vy >= 0 && b->mode == M_NORMAL)
	{
		b->mode = M_FISH_WAIT;
		set_sprite(b, -1, -1);
		b->ph.vx = b->ph.vy = 0;
		b->ph.gravity = 0;
		timer_start(b, 1000);
	}
	else if (b->mode == M_FISH_WAIT && timer_done(b))
	{
		set_sprite(b, S_FISH, S_FISH);
		b->mode = M_NORMAL;
		b->ph.vx = 0; b->ph.vy = -FX(6);
		b->ph.gravity = 1;
	}
	apply(b);
	if (b->dying == DYING_NOT) swept(b);
	if (b->ph.vy > 0 && b->sl >= 0) set_sprite(b, S_FISH_DOWN, S_FISH_DOWN);
}

static void action_bsnowball(BadGuy *b)
{
	fall(b);
	if (b->dying == DYING_NOT && issolid(bx(b), by(b) + 32)) { b->ph.vy = -FX(4.5); b->ph.gravity = 1; }
	else b->mode = M_NORMAL;
	check_horizontal_bump(b, 0);
	apply(b);
	if (b->dying == DYING_NOT) swept(b);
	if (b->dying == DYING_SQUISHED && timer_done(b)) b->removable = 1;
}

static void action_fsnowball(BadGuy *b)
{
	if (b->dying == DYING_NOT && b->mode == M_NORMAL) { b->mode = M_FLY_UP; b->ph.vy = -FX(1); timer_start(b, 500); }
	if (b->dying == DYING_NOT && timer_done(b))
	{
		if (b->mode == M_FLY_UP) { b->mode = M_FLY_DOWN; b->ph.vy = FX(1); }
		else if (b->mode == M_FLY_DOWN) { b->mode = M_FLY_UP; b->ph.vy = -FX(1); }
		timer_start(b, 1000);
	}
	if (b->dying != DYING_NOT) b->ph.gravity = 1;
	apply(b);
	if (b->dying == DYING_NOT || b->dying == DYING_SQUISHED) swept(b);
	if (b->dying == DYING_SQUISHED && timer_done(b)) b->removable = 1;
}

static void action_walker(BadGuy *b)     /* spiky, snowball */
{
	if (b->dying == DYING_NOT) check_horizontal_bump(b, 0);
	fall(b);
	apply(b);
	if (b->dying != DYING_FALLING) swept(b);
	if (b->kind == K_SNOWBALL && b->dying == DYING_SQUISHED && timer_done(b)) b->removable = 1;
}

static void badguy_action(BadGuy *b)
{
	const int sx = FX_INT(scroll_x);
	if (bx(b) < sx - OFFSCREEN_DISTANCE || by(b) > 480) { b->removable = 1; return; }
	if (bx(b) <= sx + VIEW_W_U + OFFSCREEN_DISTANCE) b->seen = 1;
	if (!b->seen) return;
	if (b->timer > 0) b->timer--;
	if (b->kind != K_FLAME && b->kind != K_STALACTITE) { b->ox = b->x; b->oy = b->y; }
	switch (b->kind)
	{
	case K_MRICEBLOCK: action_mriceblock(b); break;
	case K_JUMPY: action_jumpy(b); break;
	case K_MRBOMB: action_mrbomb(b); break;
	case K_BOMB: action_bomb(b); break;
	case K_STALACTITE: action_stalactite(b); break;
	case K_FLAME: action_flame(b); break;
	case K_FISH: action_fish(b); break;
	case K_BSNOWBALL: action_bsnowball(b); break;
	case K_FSNOWBALL: action_fsnowball(b); break;
	default: action_walker(b); break;
	}
	if (b->kind == K_SPIKY && b->dying == DYING_SQUISHED && timer_done(b)) b->removable = 1;
}

/* BadGuy::kill_me */
static void kill_me(BadGuy *b, int score)
{
	if (b->kind == K_BOMB || b->kind == K_STALACTITE || b->kind == K_FLAME) return;
	b->dying = DYING_FALLING;
	if (b->kind == K_MRICEBLOCK)
	{
		set_sprite(b, S_ICEBLOCK_FALLING_L, S_ICEBLOCK_FALLING_R);
		if (b->mode == M_HELD) { b->mode = M_NORMAL; tux.holding = 0; }
	}
	b->ph.gravity = 1;
	add_score(bx(b), by(b), score * score_multiplier);
	plat_sfx(SFX_FALL);
}

static void explode(BadGuy *other)
{
	add_bad_guy(bx(other), by(other), K_BOMB, 0);
	other->removable = 1;
}

static void bump(BadGuy *b)
{
	if (b->kind == K_FLAME || b->kind == K_BOMB || b->kind == K_FISH || b->kind == K_FSNOWBALL) return;
	b->ph.vy = -FX(3);
	kill_me(b, 25);
}

static void squish_me(BadGuy *b)
{
	tux_jump_of_badguy(b->y);
	add_score(bx(b), by(b), 50 * score_multiplier);
	plat_sfx(SFX_SQUISH);
	score_multiplier++;
	b->dying = DYING_SQUISHED;
	timer_start(b, 2000);
	b->ph.vx = b->ph.vy = 0;
}

static void squish(BadGuy *b)
{
	switch (b->kind)
	{
	case K_MRBOMB:
		add_bad_guy(bx(b), by(b), K_BOMB, 0);
		tux_jump_of_badguy(b->y);
		add_score(bx(b), by(b), 50 * score_multiplier);
		plat_sfx(SFX_SQUISH);
		score_multiplier++;
		b->removable = 1;
		return;
	case K_MRICEBLOCK:
		if (b->mode == M_NORMAL || b->mode == M_KICK)
		{
			plat_sfx(SFX_STOMP);
			b->mode = M_FLAT;
			set_sprite(b, S_ICEBLOCK_FLAT_L, S_ICEBLOCK_FLAT_R);
			b->ph.vx = 0;
			timer_start(b, 4000);
		}
		else if (b->mode == M_FLAT)
		{
			plat_sfx(SFX_KICK);
			if (tux.x < b->x + (16 << 16)) { b->ph.vx = FX(5); b->dir = 1; }
			else { b->ph.vx = -FX(5); b->dir = 0; }
			b->mode = M_KICK;
			tux.kick_t = STEPS(KICKING_TIME);
			set_sprite(b, S_ICEBLOCK_FLAT_L, S_ICEBLOCK_FLAT_R);
		}
		tux_jump_of_badguy(b->y);
		score_multiplier++;
		if (++b->squishcount >= 10) kill_me(b, 50);
		return;
	case K_FISH:
		if (b->ph.vy <= 0) return;     /* only when falling down */
		tux_jump_of_badguy(b->y);
		add_score(bx(b), by(b), 25 * score_multiplier);
		score_multiplier++;
		b->removable = 1;
		return;
	case K_BSNOWBALL: squish_me(b); set_sprite(b, S_BSNOWBALL_SQUISHED, S_BSNOWBALL_SQUISHED); return;
	case K_FSNOWBALL: squish_me(b); set_sprite(b, S_FSNOWBALL_SQUISHED, S_FSNOWBALL_SQUISHED); return;
	case K_SNOWBALL: squish_me(b); set_sprite(b, S_SNOWBALL_SQUISHED_L, S_SNOWBALL_SQUISHED_R); return;
	default: return;                   /* spiky, jumpy, flame, stalactite, bomb: not squishable */
	}
}

/* BadGuy::collision with another bad guy */
static void collide_bad(BadGuy *b, BadGuy *o)
{
	if (b->kind == K_MRICEBLOCK && b->mode == M_KICK) kill_me(o, 25);
	else if (b->kind == K_MRICEBLOCK && b->mode == M_HELD) { kill_me(o, 25); kill_me(b, 0); }
	else if (b->kind == K_BOMB && b->dying == DYING_NOT)
	{
		if (o->kind == K_MRBOMB) explode(o);
		else kill_me(o, 50);
	}
	else if (b->kind == K_STALACTITE && b->dying == DYING_NOT)
	{
		if (o->kind == K_MRBOMB) explode(o);
		else kill_me(o, 50);
	}
	else
	{
		if (o->kind == K_JUMPY || o->kind == K_FLAME || o->kind == K_STALACTITE || o->kind == K_FISH) return;
		if (b->y + (32 << 16) < o->y + (32 << 16))
		{
			if (o->dir == 0) { b->dir = 1; b->ph.vx = fxabs(b->ph.vx); b->ph.vy = -FX(2); }
			else { b->dir = 0; b->ph.vx = -fxabs(b->ph.vx); b->ph.vy = -FX(2); }
			return;
		}
		else if (b->y > o->y) return;
		if (o->kind != K_FLAME)
		{
			if (b->dir == 0) { b->dir = 1; b->ph.vx = fxabs(b->ph.vx); }
			else { b->dir = 0; b->ph.vx = -fxabs(b->ph.vx); }
		}
	}
}

/* BadGuy::collision with Tux (CO_PLAYER, COLLISION_NORMAL) */
static void collide_tux_normal(BadGuy *b)
{
	if (b->mode == M_FLAT && !b->dying)
	{
		plat_sfx(SFX_KICK);
		if (tux.x < b->x) { b->ph.vx = FX(5); b->dir = 1; }
		else { b->ph.vx = -FX(5); b->dir = 0; }
		b->mode = M_KICK;
		tux.kick_t = STEPS(KICKING_TIME);
		set_sprite(b, S_ICEBLOCK_FLAT_L, S_ICEBLOCK_FLAT_R);
	}
}

/* Player::collision with a bad guy */
static void tux_collide(BadGuy *b)
{
	if (b->dying || tux.dying || tux.safe_t || b->mode == M_HELD) return;
	if (b->mode == M_FLAT && tux.in_fire && !tux.holding)
	{
		tux.holding = 1;
		b->mode = M_HELD;
		b->y -= 8 << 16;
	}
	else if (b->mode == M_FLAT) { }
	else if (b->mode == M_KICK)
	{
		if (!tux.invincible_t) tux_kill(1);
		else { b->dying = DYING_FALLING; plat_sfx(SFX_FALL); add_score(bx(b), by(b), 25 * score_multiplier); }
	}
	else
	{
		if (!tux.invincible_t) tux_kill(1);
		else kill_me(b, 25);
	}
	score_multiplier++;
}

/* ------------------------------------------------------------------ upgrades, bullets */
typedef struct { uint8_t used, kind, dir; fx x, y, ox, oy, h; Physic ph; } Upgrade;
#define MAX_UP 12
static Upgrade ups[MAX_UP];

void add_upgrade(int x, int y, int dir, int kind)
{
	Upgrade *u = 0;
	for (int i = 0; i < MAX_UP; i++) if (!ups[i].used) { u = &ups[i]; break; }
	if (!u) return;
	memset(u, 0, sizeof *u);
	u->used = 1; u->kind = (uint8_t)kind; u->dir = (uint8_t)dir;
	u->x = (fx)x << 16; u->y = (fx)y << 16; u->ox = u->x; u->oy = u->y;
	u->h = 0;
	u->ph.gravity = 0;
	if (kind == UP_1UP || kind == UP_HERRING)
	{
		u->ph.vx = dir == 0 ? -FX(1) : FX(1); u->ph.vy = -FX(4);
		u->ph.gravity = 1;
		u->h = 32 << 16;
	}
	else if (kind == UP_ICEFLOWER) { }
	else if (kind == UP_GROWUP) u->ph.vx = dir == 0 ? -FX(1) : FX(1);
	else u->ph.vx = dir == 0 ? -FX(2) : FX(2);
}

static void upgrade_action(Upgrade *u)
{
	if (u->kind == UP_ICEFLOWER || u->kind == UP_GROWUP)
	{
		if (u->h < (32 << 16))
		{
			u->h += FX(0.7);
			if (u->h > (32 << 16)) u->h = 32 << 16;
			return;
		}
	}
	if (FX_INT(u->x) < FX_INT(scroll_x) - OFFSCREEN_DISTANCE || FX_INT(u->y) > 480) { u->used = 0; return; }
	u->ox = u->x; u->oy = u->y;
	const fx g = u->ph.gravity ? grav_fx : 0;
	fx tx = u->x + u->ph.vx + u->ph.ax, ty = u->y + u->ph.vy + g;
	u->ph.vx += u->ph.ax; u->ph.vy += g;
	if (u->kind == UP_GROWUP || u->kind == UP_HERRING) sweep(&u->x, &u->y, tx, ty, 32, FX_INT(u->h));
	else { u->x = tx; u->y = ty; }
	const int x = FX_INT(u->x), y = FX_INT(u->y), h = FX_INT(u->h);
	if (u->kind == UP_GROWUP || u->kind == UP_HERRING)
	{
		if (u->ph.vy != 0)
		{
			if (issolid(x, y + h))
			{
				u->y = (fx)((y / 32) * 32) << 16;
				u->ox = u->x; u->oy = u->y;
				if (u->kind == UP_GROWUP) { u->ph.gravity = 0; u->ph.vx = u->dir == 0 ? -FX(1) : FX(1); u->ph.vy = 0; }
				else { u->ph.vx = u->dir == 0 ? -FX(2) : FX(2); u->ph.vy = -FX(3); }
			}
		}
		else if ((u->ph.vx < 0 && !issolid(x + 32, y + h)) || (u->ph.vx > 0 && !issolid(x, y + h)))
			u->ph.gravity = 1;
		if ((u->ph.vx < 0 && issolid(x, y + h / 2)) || (u->ph.vx > 0 && issolid(x + 32, y + h / 2)))
		{
			u->ph.vx = -u->ph.vx;
			u->dir ^= 1;
		}
	}
}

static int up_sprite(int k) { return k == UP_GROWUP ? S_EGG : k == UP_ICEFLOWER ? S_ICEFLOWER : k == UP_HERRING ? S_STAR : S_1UP; }

typedef struct { uint8_t used; int life; fx x, y, ox, oy, xm, ym; } Bullet;
static Bullet bullets[MAX_BULLETS];

void add_bullet(fx x, fx y, fx xm, int dir)
{
	Bullet *b = 0;
	for (int i = 0; i < MAX_BULLETS; i++) if (!bullets[i].used) { b = &bullets[i]; break; }
	if (!b) return;
	b->used = 1; b->life = 3;
	if (dir) { b->x = x + (32 << 16); b->xm = FX(6) + xm; }
	else { b->x = x; b->xm = -FX(6) + xm; }
	b->y = y; b->ym = 0;
	b->ox = b->x; b->oy = b->y;
	plat_sfx(SFX_SHOOT);
}

static void bullet_action(Bullet *b)
{
	/* frame_ratio *= 0.5 */
	const fx oldy = b->y;
	fx tx = b->x + b->xm / 2, ty = b->y + b->ym / 2;
	b->ox = b->x; b->oy = b->y;
	sweep(&b->x, &b->y, tx, ty, 4, 4);
	if (issolid(FX_INT(b->x), FX_INT(b->y) + 4) || issolid(FX_INT(b->x), FX_INT(b->y)))
	{
		b->y = oldy;
		b->ym = -b->ym;
		if (b->ym > FX(9)) b->ym = FX(9);
		else if (b->ym < -FX(9)) b->ym = -FX(9);
		b->life--;
	}
	b->ym += FX(0.5) / 2;
	const int sx = FX_INT(scroll_x), x = FX_INT(b->x), y = FX_INT(b->y);
	if (x < sx || x > sx + VIEW_W_U || y > 480 || issolid(x + 4, y + 2) || issolid(x, y + 2) || b->life <= 0)
		b->used = 0;
}

/* ------------------------------------------------------------------ gameobjs.cpp */
typedef struct { uint8_t used; fx y, ym; int x; } BouncyDistro;
typedef struct { uint8_t used; int tile, t; fx x, y, xm, ym; } BrokenBrick;
typedef struct { uint8_t used; int x, y, shape; fx offset, offset_m; } BouncyBrick;
typedef struct { uint8_t used; int x, t, value; fx y; } FloatingScore;
#define MAX_BD 8
#define MAX_BB 16
#define MAX_BOB 4
#define MAX_FS 8
static BouncyDistro bds[MAX_BD];
static BrokenBrick bbs[MAX_BB];
static BouncyBrick bobs[MAX_BOB];
static FloatingScore fss[MAX_FS];
static uint8_t brick_piece[MAX_BB][64];   /* the pieces' pixels (the tiler draws them at once) */
static int coin_tile;                  /* the coin a bouncy distro shows (the first coin tile) */

void add_bouncy_distro(int x, int y)
{
	for (int i = 0; i < MAX_BD; i++) if (!bds[i].used)
	{
		bds[i].used = 1; bds[i].x = x; bds[i].y = (fx)y << 16; bds[i].ym = -FX(2);
		return;
	}
}

static void add_broken_piece(int tile, int x, int y, fx xm, fx ym)
{
	for (int i = 0; i < MAX_BB; i++) if (!bbs[i].used)
	{
		BrokenBrick *b = &bbs[i];
		b->used = 1; b->tile = tile; b->t = STEPS(200);
		b->x = (fx)x << 16; b->y = (fx)y << 16; b->xm = xm; b->ym = ym;
		return;
	}
}
void add_broken_brick(int tile, int x, int y)
{
	add_broken_piece(tile, x, y, -FX(1), -FX(4));
	add_broken_piece(tile, x, y + 16, -FX(1.5), -FX(3));
	add_broken_piece(tile, x + 16, y, FX(1), -FX(4));
	add_broken_piece(tile, x + 16, y + 16, FX(1.5), -FX(3));
}

#define BOUNCY_BRICK_MAX_OFFSET 8
#define BOUNCY_BRICK_SPEED FX(0.9)
void add_bouncy_brick(int x, int y)
{
	for (int i = 0; i < MAX_BOB; i++) if (!bobs[i].used)
	{
		BouncyBrick *b = &bobs[i];
		b->used = 1; b->x = x; b->y = y;
		b->offset = 0; b->offset_m = -BOUNCY_BRICK_SPEED;
		b->shape = tile_at(x, y);
		level_hide(x >> 5, y >> 5, 1);
		return;
	}
}

void add_score(int x, int y, int s)
{
	ps.score += (uint32_t)s;
	for (int i = 0; i < MAX_FS; i++) if (!fss[i].used)
	{
		fss[i].used = 1; fss[i].x = x; fss[i].y = (fx)(y - 16) << 16; fss[i].t = STEPS(1000); fss[i].value = s;
		return;
	}
}

/* World::trybumpbadguy */
void trybumpbadguy(int x, int y)
{
	for (int i = 0; i < nbad; i++)
	{
		BadGuy *b = &bad[i];
		if (!b->used || b->removable) continue;
		if (bx(b) >= x - 32 && bx(b) <= x + 32 && by(b) >= y - 16 && by(b) <= y + 16) bump(b);
	}
	for (int i = 0; i < MAX_UP; i++)
	{
		Upgrade *u = &ups[i];
		if (!u->used || u->h != (32 << 16)) continue;
		const int ux = FX_INT(u->x), uy = FX_INT(u->y);
		if (ux >= x - 32 && ux <= x + 32 && uy >= y - 16 && uy <= y + 16 && u->kind == UP_GROWUP)
		{
			plat_sfx(SFX_BUMP_UPGRADE);
			u->ph.vx = -u->ph.vx; u->ph.vy = -FX(3);
			u->dir ^= 1;
			u->ph.gravity = 1;
		}
	}
}

/* ------------------------------------------------------------------ the world's objects */
void objects_clear(void)
{
	memset(bad, 0, sizeof bad); nbad = 0;
	memset(ups, 0, sizeof ups);
	memset(bullets, 0, sizeof bullets);
	memset(bds, 0, sizeof bds);
	memset(bbs, 0, sizeof bbs);
	memset(bobs, 0, sizeof bobs);
	memset(fss, 0, sizeof fss);
	score_multiplier = 1;
}

void objects_start(void)
{
	static int sin_made;
	if (!sin_made) { make_sintab(); sin_made = 1; }
	coin_tile = 0;
	for (int t = 1; t < ntiles && !coin_tile; t++)
		if ((tdesc[t].flags & TF_DISTRO) && tdesc[t].frames) coin_tile = t;
	objects_clear();
	for (int i = 0; i < lnobjs; i++)
	{
		const uint8_t *o = lobjs + 6 * i;
		add_bad_guy((int16_t)be16(o + 2), (int16_t)be16(o + 4), o[0], o[1]);
	}
}

void objects_action(void)
{
	for (int i = 0; i < MAX_BD; i++) if (bds[i].used)
	{
		bds[i].y += bds[i].ym; bds[i].ym += FX(0.1);
		if (bds[i].ym >= 0) bds[i].used = 0;
	}
	for (int i = 0; i < MAX_BB; i++) if (bbs[i].used)
	{
		bbs[i].x += bbs[i].xm; bbs[i].y += bbs[i].ym;
		if (--bbs[i].t <= 0) bbs[i].used = 0;
	}
	for (int i = 0; i < MAX_BOB; i++) if (bobs[i].used)
	{
		BouncyBrick *b = &bobs[i];
		b->offset += b->offset_m;
		if (b->offset < -(BOUNCY_BRICK_MAX_OFFSET << 16)) b->offset_m = BOUNCY_BRICK_SPEED;
		if (b->offset >= 0) { b->used = 0; level_hide(b->x >> 5, b->y >> 5, 0); }
	}
	for (int i = 0; i < MAX_FS; i++) if (fss[i].used)
	{
		fss[i].y -= FX(2);
		if (--fss[i].t <= 0) fss[i].used = 0;
	}
	for (int i = 0; i < MAX_BULLETS; i++) if (bullets[i].used) bullet_action(&bullets[i]);
	for (int i = 0; i < MAX_UP; i++) if (ups[i].used) upgrade_action(&ups[i]);
	for (int i = 0; i < nbad; i++) if (bad[i].used && !bad[i].removable) badguy_action(&bad[i]);
}

static int rects(fx ax, fx ay, int aw, int ah, fx bx_, fx by_, int bw, int bh)
{
	const int x1 = FX_INT(ax), y1 = FX_INT(ay), x2 = FX_INT(bx_), y2 = FX_INT(by_);
	return x1 < x2 + bw && x1 + aw > x2 && y1 < y2 + bh && y1 + ah > y2;
}

/* World::collision_handler */
void objects_collisions(void)
{
	/* bullets and bad guys */
	for (int i = 0; i < MAX_BULLETS; i++)
	{
		Bullet *u = &bullets[i];
		if (!u->used) continue;
		for (int j = 0; j < nbad; j++)
		{
			BadGuy *b = &bad[j];
			if (!b->used || b->removable || !b->seen || b->dying != DYING_NOT) continue;
			if (rects(u->x, u->y, 4, 4, b->x, b->y, 32, 32)) { kill_me(b, 10); u->used = 0; break; }
		}
	}
	/* bad guys and bad guys */
	for (int i = 0; i < nbad; i++)
	{
		BadGuy *a = &bad[i];
		if (!a->used || a->removable || !a->seen || a->dying != DYING_NOT) continue;
		for (int j = i + 1; j < nbad; j++)
		{
			BadGuy *b = &bad[j];
			if (!b->used || b->removable || !b->seen || b->dying != DYING_NOT) continue;
			if (rects(a->x, a->y, 32, 32, b->x, b->y, 32, 32)) { collide_bad(b, a); collide_bad(a, b); }
		}
	}
	if (tux.dying == DYING_NOT)
	{
		for (int i = 0; i < nbad; i++)
		{
			BadGuy *b = &bad[i];
			if (!b->used || b->removable || !b->seen || b->dying != DYING_NOT) continue;
			if (rects(b->x, b->y, 32, 32, tux.x, tux.y, tux.w, tux.h))
			{
				if (tux.py < tux.y && FX_INT(tux.py) + tux.h < by(b) + 16 && !tux.invincible_t) squish(b);
				else { tux_collide(b); collide_tux_normal(b); }
			}
		}
		for (int i = 0; i < MAX_UP; i++)
		{
			Upgrade *u = &ups[i];
			if (!u->used || !rects(u->x, u->y, 32, FX_INT(u->h), tux.x, tux.y, tux.w, tux.h)) continue;
			switch (u->kind)
			{
			case UP_GROWUP: plat_sfx(SFX_EXCELLENT); tux_grow(); break;
			case UP_ICEFLOWER: plat_sfx(SFX_COFFEE); tux_grow(); tux.got_coffee = 1; break;
			case UP_HERRING: plat_sfx(SFX_HERRING); tux.invincible_t = STEPS(TUX_INVINCIBLE_TIME); break;
			case UP_1UP: if (ps.lives < MAX_LIVES) { ps.lives++; plat_sfx(SFX_LIFEUP); } break;
			}
			u->used = 0;
		}
	}
	/* cleanup */
	for (int i = 0; i < nbad; i++) if (bad[i].used && bad[i].removable) bad[i].used = 0;
	while (nbad > 0 && !bad[nbad - 1].used) nbad--;
}

/* R5: the positions before the last step (stx.h lerp_fx), valid only for what was in use then */
static fx ip_bad[MAX_BAD][2], ip_up[MAX_UP][3], ip_bul[MAX_BULLETS][2], ip_bd[MAX_BD], ip_bb[MAX_BB][2];
static fx ip_bob[MAX_BOB], ip_fs[MAX_FS];
static uint8_t ip_bad_ok[MAX_BAD], ip_up_ok[MAX_UP], ip_bul_ok[MAX_BULLETS], ip_bd_ok[MAX_BD];
static uint8_t ip_bb_ok[MAX_BB], ip_bob_ok[MAX_BOB], ip_fs_ok[MAX_FS];

void objects_interp_reset(void)
{
	memset(ip_bad_ok, 0, sizeof ip_bad_ok); memset(ip_up_ok, 0, sizeof ip_up_ok);
	memset(ip_bul_ok, 0, sizeof ip_bul_ok); memset(ip_bd_ok, 0, sizeof ip_bd_ok);
	memset(ip_bb_ok, 0, sizeof ip_bb_ok); memset(ip_bob_ok, 0, sizeof ip_bob_ok);
	memset(ip_fs_ok, 0, sizeof ip_fs_ok);
}

void objects_interp_save(void)
{
	for (int i = 0; i < nbad; i++)
	{
		ip_bad_ok[i] = bad[i].used;
		ip_bad[i][0] = bad[i].x; ip_bad[i][1] = bad[i].y;
	}
	for (int i = 0; i < MAX_UP; i++)
	{
		ip_up_ok[i] = ups[i].used;
		ip_up[i][0] = ups[i].x; ip_up[i][1] = ups[i].y; ip_up[i][2] = ups[i].h;
	}
	for (int i = 0; i < MAX_BULLETS; i++)
	{
		ip_bul_ok[i] = bullets[i].used;
		ip_bul[i][0] = bullets[i].x; ip_bul[i][1] = bullets[i].y;
	}
	for (int i = 0; i < MAX_BD; i++) { ip_bd_ok[i] = bds[i].used; ip_bd[i] = bds[i].y; }
	for (int i = 0; i < MAX_BB; i++) { ip_bb_ok[i] = bbs[i].used; ip_bb[i][0] = bbs[i].x; ip_bb[i][1] = bbs[i].y; }
	for (int i = 0; i < MAX_BOB; i++) { ip_bob_ok[i] = bobs[i].used; ip_bob[i] = bobs[i].offset; }
	for (int i = 0; i < MAX_FS; i++) { ip_fs_ok[i] = fss[i].used; ip_fs[i] = fss[i].y; }
}

/* where to draw: between the kept position and now (as it is now if it was not in use) */
static fx ip(const fx *prev, uint8_t ok, fx now) { return ok ? lerp_fx(*prev, now) : now; }

void objects_draw_back(int cx, int cy, uint32_t ms)
{
	const int sx = FX_INT(scroll_x);
	for (int i = 0; i < MAX_BD; i++) if (bds[i].used)
		tile_draw(coin_tile, ms, bds[i].x, FX_INT(ip(&ip_bd[i], ip_bd_ok[i], bds[i].y)), cx, cy);
	for (int i = 0; i < MAX_BOB; i++) if (bobs[i].used)
		tile_draw(bobs[i].shape, ms, bobs[i].x, bobs[i].y + FX_INT(ip(&ip_bob[i], ip_bob_ok[i], bobs[i].offset)), cx, cy);
	for (int i = 0; i < nbad; i++)
	{
		BadGuy *b = &bad[i];
		if (!b->used || b->removable || !b->seen) continue;
		const int x = FX_INT(ip(&ip_bad[i][0], ip_bad_ok[i], b->x)), y = FX_INT(ip(&ip_bad[i][1], ip_bad_ok[i], b->y));
		if (x <= sx - 32 || x >= sx + VIEW_W_U) continue;
		const int s = b->dir ? b->sr : b->sl;
		if (s >= 0) spr_draw(s, ms, x, y, cx, cy);
	}
	for (int i = 0; i < MAX_UP; i++)
	{
		Upgrade *u = &ups[i];
		if (!u->used) continue;
		const int x = FX_INT(ip(&ip_up[i][0], ip_up_ok[i], u->x)), y = FX_INT(ip(&ip_up[i][1], ip_up_ok[i], u->y));
		const int h = FX_INT(ip(&ip_up[i][2], ip_up_ok[i], u->h));
		if (h < 32) spr_draw_part(up_sprite(u->kind), x, y + 32 - h, h, cx, cy);
		else spr_draw(up_sprite(u->kind), ms, x, y, cx, cy);
	}
	for (int i = 0; i < MAX_BULLETS; i++) if (bullets[i].used)
		spr_draw(S_BULLET, ms, FX_INT(ip(&ip_bul[i][0], ip_bul_ok[i], bullets[i].x)),
		         FX_INT(ip(&ip_bul[i][1], ip_bul_ok[i], bullets[i].y)), cx, cy);
	for (int i = 0; i < MAX_BB; i++) if (bbs[i].used)
	{
		/* BrokenBrick::draw: a 16x16-unit piece of the brick (8x8 pixels: YATSSD's narrowest
		 * sprite) - which quarter as SuperTux's random offset, fixed per piece here */
		const TileDesc *t = &tdesc[bbs[i].tile < ntiles ? bbs[i].tile : 0];
		if (!t->frames) continue;
		uint8_t *piece = brick_piece[i];
		const uint8_t *src = tile_px + YT_TILE * YT_TILE * t->first;
		const int ox = (i & 1) * 8, oy = (i >> 1 & 1) * 8;
		for (int r = 0; r < 8; r++) memcpy(piece + 8 * r, src + (oy + r) * YT_TILE + ox, 8);
		yt_sprite(px_of(FX_INT(ip(&ip_bb[i][0], ip_bb_ok[i], bbs[i].x))) - cx,
		          px_of(FX_INT(ip(&ip_bb[i][1], ip_bb_ok[i], bbs[i].y))) - cy, 8, 8, piece);
	}
}

void objects_draw_front(int cx, int cy)
{
	char t[12];
	for (int i = 0; i < MAX_FS; i++) if (fss[i].used)
	{
		put_dec(t, (uint32_t)fss[i].value, 1);
		const int x = px_of(fss[i].x + 16) - cx - text_width(F_GOLD, t) / 2;
		const int y = px_of(FX_INT(ip(&ip_fs[i], ip_fs_ok[i], fss[i].y))) - cy;
		text_at(F_GOLD, x, y, t);
	}
}
