/*
 * SuperTux CD32X - Tux: SuperTux 0.1.3's src/player.cpp, line by line in fixed point (per 10 ms
 * step, its own units). Small Tux, big Tux, fire Tux (got_coffee: shoots), ducking, skidding,
 * jumping (higher when running; early release; "pressed just before landing"), kicking and
 * carrying Mr. Iceblock, bumping bricks and bonus boxes from below, coins, invincibility (the
 * herring), the safe time after a hit, dying. See game/stx.h.
 */
#include "stx.h"

Tux tux;

static int issolid_u(fx x, fx y) { return issolid(FX_INT(x), FX_INT(y)); }

int tux_on_ground(void)
{
	const int x = FX_INT(tux.x), y = FX_INT(tux.y);
	return issolid(x + tux.w / 2, y + tux.h) || issolid(x + 1, y + tux.h) || issolid(x + tux.w - 1, y + tux.h);
}
static int under_solid(void)
{
	const int x = FX_INT(tux.x), y = FX_INT(tux.y);
	return issolid(x + tux.w / 2, y) || issolid(x + 1, y) || issolid(x + tux.w - 1, y);
}

void tux_init(int x, int y)
{
	memset(&tux, 0, sizeof tux);
	tux.w = 32; tux.h = 32;
	tux.x = (fx)x << 16; tux.y = (fx)y << 16;
	tux.px = tux.x; tux.py = tux.y;
	tux.dir = tux.old_dir = 1;
	tux.can_jump = 1;
	tux.ph.gravity = 1;
	/* World::apply_bonuses: the bonus from the levels before */
	if (ps.bonus >= 1)
	{
		tux.size = BIG; tux.h = 64; tux.y -= 32 << 16; tux.py = tux.y;
		if (ps.bonus == 2) tux.got_coffee = 1;
	}
}

void tux_grow(void)
{
	if (tux.size == BIG) return;
	tux.size = BIG;
	tux.h = 64;
	tux.y -= 32 << 16;
	tux.py = tux.y;
}

void tux_jump_of_badguy(fx by)
{
	tux.ph.vy = tux.in_up ? -FX(5.2) : -FX(2.0);
	tux.y = by - ((tux.h + 2) << 16);
}

/* Player::kill */
void tux_kill(int shrink)
{
	plat_sfx(SFX_HURT);
	tux.ph.vx = 0;
	if (shrink && tux.size == BIG)
	{
		if (tux.got_coffee) tux.got_coffee = 0;
		else { tux.size = SMALL; tux.h = 32; tux.duck = 0; }
		tux.safe_t = STEPS(TUX_SAFE_TIME);
	}
	else
	{
		tux.ph.gravity = 1;
		tux.ph.ax = tux.ph.ay = 0;
		tux.ph.vx = 0;
		tux.ph.vy = -FX(7);
		if (tux.dying != DYING_SQUISHED) ps.lives--;
		tux.dying = DYING_SQUISHED;
	}
}

static void handle_horizontal(void)
{
	fx vx = tux.ph.vx, ax;
	int dirsign = 0;
	const int not_ducking = !tux.duck || tux.ph.vy != 0;
	if (tux.in_left && !tux.in_right && not_ducking) { tux.old_dir = tux.dir; tux.dir = 0; dirsign = -1; }
	else if (!tux.in_left && tux.in_right && not_ducking) { tux.old_dir = tux.dir; tux.dir = 1; dirsign = 1; }
	if (!tux.in_fire)
	{
		ax = dirsign * WALK_ACCELERATION_X;
		if (vx >= MAX_WALK_XM && dirsign > 0) { vx = MAX_WALK_XM; ax = 0; }
		else if (vx <= -MAX_WALK_XM && dirsign < 0) { vx = -MAX_WALK_XM; ax = 0; }
	}
	else
	{
		ax = dirsign * RUN_ACCELERATION_X;
		if (vx >= MAX_RUN_XM && dirsign > 0) { vx = MAX_RUN_XM; ax = 0; }
		else if (vx <= -MAX_RUN_XM && dirsign < 0) { vx = -MAX_RUN_XM; ax = 0; }
	}
	if (dirsign != 0 && fxabs(vx) < WALK_SPEED) vx = dirsign * WALK_SPEED;
	if (tux_on_ground() && ((vx < 0 && dirsign > 0) || (vx > 0 && dirsign < 0)))
	{
		if (fxabs(vx) > SKID_XM && !tux.skid_t)
		{
			tux.skid_t = STEPS(SKID_TIME);
			plat_sfx(SFX_SKID);
			ax = ax * 5 / 2;
		}
		else ax *= 2;
	}
	if (dirsign == 0)
	{
		if (fxabs(vx) < WALK_SPEED) { vx = 0; ax = 0; }
		else if (vx < 0) ax = WALK_ACCELERATION_X * 3 / 2;
		else ax = -WALK_ACCELERATION_X * 3 / 2;
	}
	if ((flags_at(FX_INT(tux.x), FX_INT(tux.y) + tux.h) & TF_ICE) && ax != 0 && vx != 0)
		ax = (fx)(((int64_t)ax << 16) / fxabs(vx));
	tux.ph.vx = vx;
	tux.ph.ax = ax;
}

static void handle_vertical(void)
{
	if (tux.in_up && tux.can_jump)
	{
		if (tux_on_ground())
		{
			tux.ph.vy = -(fxabs(tux.ph.vx) > MAX_WALK_XM ? FX(5.8) : FX(5.2));
			tux.y -= ONE;
			tux.jumping = 1;
			tux.can_jump = 0;
			plat_sfx(tux.size == SMALL ? SFX_JUMP : SFX_BIGJUMP);
		}
	}
	else if (!tux.in_up && tux.jumping)
	{
		tux.jumping = 0;
		if (tux.ph.vy < 0) tux.ph.vy = 0;
	}
	const int x = FX_INT(tux.x), y = FX_INT(tux.y);
	if ((issolid(x + tux.w / 2, y + tux.h + 64) || issolid(x + 1, y + tux.h + 64) || issolid(x + tux.w - 1, y + tux.h + 64))
	    && !tux.jumping && !tux.can_jump && tux.in_up && !tux.in_old_up)
		tux.can_jump = 1;
	tux.in_old_up = tux.in_up;
}

static void handle_input(void)
{
	handle_horizontal();
	if (tux_on_ground() && !tux.in_up) tux.can_jump = 1;
	if (tux.in_up || tux.jumping) handle_vertical();
	/* shoot */
	if (tux.in_fire && !tux.in_old_fire && tux.got_coffee)
	{
		add_bullet(tux.x, tux.y, tux.ph.vx, tux.dir);
		tux.in_old_fire = 1;
	}
	/* duck */
	if (tux.in_down && tux.size == BIG && !tux.duck && tux.ph.vy == 0 && tux_on_ground())
	{
		tux.duck = 1; tux.h = 32; tux.y += 32 << 16; tux.py = tux.y;
	}
	else if (!tux.in_down && tux.size == BIG && tux.duck && tux.ph.vy == 0 && tux_on_ground())
	{
		tux.duck = 0; tux.y -= 32 << 16; tux.h = 64; tux.py = tux.y;
	}
}

static void grabdistros(void)
{
	const int x = FX_INT(tux.x), y = FX_INT(tux.y);
	trygrabdistro(x, y, 0); trygrabdistro(x + 31, y, 0);
	trygrabdistro(x, y + tux.h, 0); trygrabdistro(x + 31, y + tux.h, 0);
	if (tux.size == BIG) { trygrabdistro(x, y + tux.h / 2, 0); trygrabdistro(x + 31, y + tux.h / 2, 0); }
	if (ps.distros >= DISTROS_LIFEUP)
	{
		ps.distros -= DISTROS_LIFEUP;
		if (ps.lives < MAX_LIVES) ps.lives++;
		plat_sfx(SFX_LIFEUP);
	}
}

/* Player::action */
void tux_action(void)
{
	if (!tux.in_fire) tux.holding = 0;
	tux.px = tux.x; tux.py = tux.y;   /* previous_base */
	if (tux.dying == DYING_NOT) handle_input();

	/* Physic::apply */
	const fx g = tux.ph.gravity ? grav_fx : 0;
	const fx ox = tux.x, oy = tux.y;
	const fx tx = ox + tux.ph.vx + tux.ph.ax;
	const fx ty = oy + tux.ph.vy + tux.ph.ay + g;
	tux.ph.vx += tux.ph.ax;
	tux.ph.vy += tux.ph.ay + g;

	if (tux.dying == DYING_NOT)
	{
		tux.x = ox; tux.y = oy;
		sweep(&tux.x, &tux.y, tx, ty, tux.w, tux.h);
		if (tux.x != tx) tux.ph.vx = 0;
		/* stuck under tiles after ducking: drift out */
		if (!tux.duck && tux_on_ground() && ox == tux.x && oy == tux.y && box_hits(FX_INT(tux.x), FX_INT(tux.y), tux.w, tux.h))
		{
			tux.x += tux.dir ? WALK_SPEED : -WALK_SPEED;
			tux.px = tux.x; tux.py = tux.y;
		}
		int jumped_in_solid = 0;
		if (!tux_on_ground())
		{
			tux.ph.gravity = 1;
			if (under_solid()) { tux.ph.vy = 0; jumped_in_solid = 1; }
		}
		else
		{
			if (tux.ph.vy > 0)
			{
				tux.y = (fx)((FX_INT(tux.y) / 32) * 32) << 16;
				tux.ph.vy = 0;
			}
			tux.ph.gravity = 0;
			score_multiplier = 1;
		}
		if (jumped_in_solid)
		{
			const int x = FX_INT(tux.x), y = FX_INT(tux.y);
			if (flags_at(x, y) & (TF_BRICK | TF_FULLBOX))
			{
				trygrabdistro(x, y - 32, 1);
				trybumpbadguy(x, y - 64);
				trybreakbrick(x, y, tux.size == SMALL, 1);
				bumpbrick(x, y);
				tryemptybox(x, y, 1);
			}
			if (flags_at(x + 31, y) & (TF_BRICK | TF_FULLBOX))
			{
				trygrabdistro(x + 31, y - 32, 1);
				trybumpbadguy(x + 31, y - 64);
				if (tux.size == BIG) trybreakbrick(x + 31, y, tux.size == SMALL, 0);
				bumpbrick(x + 31, y);
				tryemptybox(x + 31, y, 0);
			}
		}
		grabdistros();
		if (jumped_in_solid)
		{
			tux.y += ONE;
			tux.py += ONE;
			if (tux_on_ground()) tux.jumping = 0;
		}
	}
	else { tux.x = tx; tux.y = ty; }

	if (tux.skid_t) tux.skid_t--;
	if (tux.invincible_t) tux.invincible_t--;
	if (tux.safe_t) tux.safe_t--;
	if (tux.kick_t) tux.kick_t--;
	(void)issolid_u;
}

/* Player::draw */
void tux_draw(int cx, int cy, uint32_t ms, uint32_t frame)
{
	if (tux.safe_t && (frame % 2)) return;          /* blinking after a hit */
#ifdef STX_SPRITE_X1
	/* SPRITE_X1: the view is a whole pixel (cx = the camera's units / 2) and so is Tux - each rounded
	 * down on its own, so while the view followed him Tux sat 1 pixel left or right of his place on
	 * the screen, flipping every few frames (measured in ares). Taking the camera's odd unit off his
	 * x rounds both the same way: he stays put on the screen while the view follows him (the level
	 * under him may be 1 pixel off for those frames instead, which the eye, following Tux, does not
	 * see). Off the level's edges (the view clamped) nothing changes. */
	const int odd = FX_INT(scroll_x) - 2 * cx;
	const int x = FX_INT(tux.x) - (odd == 1), y = FX_INT(tux.y);
#else
	const int x = FX_INT(tux.x), y = FX_INT(tux.y);
#endif
	if (tux.dying == DYING_SQUISHED) { spr_draw(S_SMALL_GAMEOVER, ms, x, y, cx, cy); return; }
	const int base = tux.size == SMALL ? S_SMALL_WALK_L : tux.got_coffee ? S_FIRE_WALK_L : S_LARGE_WALK_L;
	/* the sets are in the same order: walk, stand, jump (small: gameover), skid, kick, grab ... */
	int s;
	const int r = tux.dir ? 1 : 0;
	if (tux.size == SMALL)
	{
		if (tux.skid_t) s = S_SMALL_SKID_L + r;
		else if (tux.kick_t) s = S_SMALL_KICK_L + r;
		else if (tux.ph.vy != 0) s = S_SMALL_JUMP_L + r;
		else if (fxabs(tux.ph.vx) < FX(1.0)) s = S_SMALL_STAND_L + r;
		else s = S_SMALL_WALK_L + r;
	}
	else
	{
		const int o = base - S_LARGE_WALK_L;   /* large -> fire: the same layout */
		if (tux.duck) s = S_LARGE_DUCK_L + r;
		else if (tux.skid_t) s = S_LARGE_SKID_L + r;
		else if (tux.kick_t) s = S_LARGE_KICK_L + r;
		else if (tux.ph.vy != 0) s = S_LARGE_JUMP_L + r;
		else if (fxabs(tux.ph.vx) < FX(1.0)) s = S_LARGE_STAND_L + r;
		else s = S_LARGE_WALK_L + r;
		s += o;
	}
	spr_draw(s, ms, x, y, cx, cy);
	if (tux.holding && tux.ph.vy == 0 && !tux.duck)
		spr_draw(tux.size == SMALL ? S_SMALL_GRAB_L + r : (tux.got_coffee ? S_FIRE_GRAB_L : S_LARGE_GRAB_L) + r, ms, x, y, cx, cy);
	if (tux.invincible_t && (tux.invincible_t > STEPS(TUX_INVINCIBLE_TIME_WARNING) || (frame % 3)))
		spr_draw(tux.size == SMALL || tux.duck ? S_SMALL_STAR : S_LARGE_STAR, ms, x, y, cx, cy);
}
