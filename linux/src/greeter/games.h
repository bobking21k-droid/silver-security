/*
 * Games that play right here in SilverOS, drawn with sgfx like everything else:
 *
 *   Snake    eat the red dots, don't bite yourself; it speeds up as it grows
 *   2048     slide the tiles, equal ones merge; reach 2048
 *   Shatter  a paddle, a ball and a wall of glass that breaks into shards
 *
 * Arrow keys play, Enter starts again after a game ends, Esc goes back to the Game Store.
 * Best scores are kept in ~/.config/silver/scores.
 */
#pragma once
static void snd(char ev);

enum { G_SNAKE, G_2048, G_SHATTER, G_N };
static const char *GAME_NAME[G_N] = {"Snake", "2048", "Shatter"};
static const char *GAME_TAG[G_N] = {"Eat, grow, don't bite yourself", "Slide the tiles, merge to 2048", "Break the glass wall with a ball"};
static const char *GAME_DESC[G_N] = {
	"The classic. Arrow keys steer. Every red dot makes you longer and a little faster. Hit a wall or yourself and it is over.",
	"Arrow keys slide every tile. Two equal tiles that meet become one, worth both. Keep the board from filling up and reach 2048.",
	"Left and right move the paddle. Keep the ball in play and smash every pane of glass. Three lives; each wall gets faster."};

static int best[G_N];
static void scores_load(void)
{
#ifndef RENDER
	char p[300]; snprintf(p, sizeof p, "%s/.config/silver/scores", homedir);
	FILE *f = fopen(p, "r");
	if (f) { for (int i = 0; i < G_N; i++) if (fscanf(f, "%d", &best[i]) != 1) best[i] = 0; fclose(f); }
#else
	best[0] = 42; best[1] = 5120; best[2] = 1840;
#endif
}
static void scores_save(void)
{
#ifndef RENDER
	char p[300]; snprintf(p, sizeof p, "%s/.config/silver", homedir); mkdir(p, 0755);
	snprintf(p, sizeof p, "%s/.config/silver/scores", homedir);
	FILE *f = fopen(p, "w");
	if (f) { for (int i = 0; i < G_N; i++) fprintf(f, "%d\n", best[i]); fclose(f); }
#endif
}

static struct {
	int game, over, score, lives;
	float t, acc;
	/* snake */
	int sx[800], sy[800], len, dir, ndir, fx, fy;
	/* 2048 */
	int tile[4][4]; float pop[4][4];
	/* shatter */
	float px, bx, by, vx, vy; int brick[6][10]; int level, served;
	struct { float x, y, vx, vy, r, a, s; } shard[120]; int nsh;
} gm;

#define SN_W 32
#define SN_H 18
static void snake_food(void)
{
	for (int tries = 0; tries < 500; tries++) {
		int x = (int)(sg_frand() * SN_W), y = (int)(sg_frand() * SN_H), hit = 0;
		for (int i = 0; i < gm.len; i++) if (gm.sx[i] == x && gm.sy[i] == y) hit = 1;
		if (!hit) { gm.fx = x; gm.fy = y; return; }
	}
}
static void t2048_add(void)
{
	int free_[16], n = 0;
	for (int i = 0; i < 16; i++) if (!gm.tile[i / 4][i % 4]) free_[n++] = i;
	if (!n) return;
	int k = free_[(int)(sg_frand() * n) % n];
	gm.tile[k / 4][k % 4] = sg_frand() < 0.9f ? 2 : 4; gm.pop[k / 4][k % 4] = 0;
}
static void shatter_wall(void)
{
	for (int r = 0; r < 6; r++) for (int c = 0; c < 10; c++) gm.brick[r][c] = r < 2 ? 2 : 1;
	gm.served = 0; gm.bx = gm.px; gm.by = 0.8f;
}
static void game_start(int g)
{
	memset(&gm, 0, sizeof gm);
	gm.game = g;
	if (g == G_SNAKE) { gm.len = 4; for (int i = 0; i < 4; i++) { gm.sx[i] = 10 - i; gm.sy[i] = SN_H / 2; } gm.dir = gm.ndir = 0; snake_food(); }
	if (g == G_2048) { t2048_add(); t2048_add(); }
	if (g == G_SHATTER) { gm.lives = 3; gm.px = 0.5f; gm.level = 1; shatter_wall(); }
}
static void game_end(void)
{
	gm.over = 1;
	if (gm.score > best[gm.game]) { best[gm.game] = gm.score; scores_save(); }
}

/* 2048: slide everything one way; returns 1 if anything moved */
static int t2048_slide(int dx, int dy)
{
	int moved = 0;
	for (int line = 0; line < 4; line++) {
		int v[4], n = 0;
		for (int k = 0; k < 4; k++) {                                 /* read the line in the direction of travel */
			int i = dx ? (dx > 0 ? 3 - k : k) : line, j = dy ? (dy > 0 ? 3 - k : k) : line;
			int x = dx ? i : line, y = dy ? j : line;
			if (gm.tile[y][x]) v[n++] = gm.tile[y][x];
		}
		int out[4] = {0}, m = 0, merged[4] = {0};
		for (int k = 0; k < n; k++) {
			if (m && out[m - 1] == v[k] && !merged[m - 1]) { out[m - 1] *= 2; merged[m - 1] = 1; gm.score += out[m - 1]; }
			else out[m++] = v[k];
		}
		for (int k = 0; k < 4; k++) {
			int i = dx ? (dx > 0 ? 3 - k : k) : line, j = dy ? (dy > 0 ? 3 - k : k) : line;
			int x = dx ? i : line, y = dy ? j : line;
			if (gm.tile[y][x] != out[k]) { moved = 1; if (merged[k] || !gm.tile[y][x]) gm.pop[y][x] = 0; }
			gm.tile[y][x] = out[k];
		}
	}
	return moved;
}
static int t2048_stuck(void)
{
	for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) {
		if (!gm.tile[y][x]) return 0;
		if (x < 3 && gm.tile[y][x] == gm.tile[y][x + 1]) return 0;
		if (y < 3 && gm.tile[y][x] == gm.tile[y + 1][x]) return 0;
	}
	return 1;
}

static int game_key(int k)                                          /* returns 1 to leave the game */
{
	if (k == K_ESC) return 1;
	if (gm.over) { if (k == K_ENTER) game_start(gm.game); return 0; }
	if (gm.game == G_SNAKE) {
		int d = k == K_RIGHT ? 0 : k == K_DOWN ? 1 : k == K_LEFT ? 2 : k == K_UP ? 3 : -1;
		if (d >= 0 && (d + 2) % 4 != gm.dir) gm.ndir = d;
	} else if (gm.game == G_2048) {
		int dx = k == K_RIGHT ? 1 : k == K_LEFT ? -1 : 0, dy = k == K_DOWN ? 1 : k == K_UP ? -1 : 0;
		if ((dx || dy) && t2048_slide(dx, dy)) { t2048_add(); snd('m'); if (t2048_stuck()) game_end(); }
	} else if (gm.game == G_SHATTER) {
		if (k == K_LEFT) gm.px = sg_max(0.06f, gm.px - 0.06f);
		else if (k == K_RIGHT) gm.px = sg_min(0.94f, gm.px + 0.06f);
		else if (k == K_UP || k == K_ENTER || k == ' ') { if (!gm.served) { gm.served = 1; gm.vx = 0.35f; gm.vy = -0.55f - 0.08f * gm.level; } }
	}
	return 0;
}

static void game_update(float dt)
{
	gm.t += dt;
	for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) gm.pop[y][x] = sg_min(1, gm.pop[y][x] + dt * 7);
	for (int i = 0; i < gm.nsh; i++) { gm.shard[i].x += gm.shard[i].vx * dt; gm.shard[i].y += gm.shard[i].vy * dt; gm.shard[i].vy += 1.6f * dt; gm.shard[i].a -= dt * 1.4f; gm.shard[i].r += gm.shard[i].s * dt; }
	while (gm.nsh && gm.shard[gm.nsh - 1].a <= 0) gm.nsh--;
	if (gm.over) return;
	if (gm.game == G_SNAKE) {
		float step = sg_max(0.055f, 0.13f - gm.len * 0.0015f);
		gm.acc += dt;
		while (gm.acc >= step && !gm.over) {
			gm.acc -= step;
			gm.dir = gm.ndir;
			int nx = gm.sx[0] + (gm.dir == 0) - (gm.dir == 2), ny = gm.sy[0] + (gm.dir == 1) - (gm.dir == 3);
			if (nx < 0 || ny < 0 || nx >= SN_W || ny >= SN_H) { game_end(); snd('e'); break; }
			for (int i = 0; i < gm.len - 1; i++) if (gm.sx[i] == nx && gm.sy[i] == ny) { game_end(); snd('e'); break; }
			if (gm.over) break;
			int grow = nx == gm.fx && ny == gm.fy;
			if (grow && gm.len < 799) gm.len++;
			for (int i = gm.len - 1; i > 0; i--) { gm.sx[i] = gm.sx[i - 1]; gm.sy[i] = gm.sy[i - 1]; }
			gm.sx[0] = nx; gm.sy[0] = ny;
			if (grow) { gm.score += 10; snake_food(); snd('r'); }
		}
	} else if (gm.game == G_SHATTER) {
		if (!gm.served) { gm.bx = gm.px; gm.by = 0.86f; return; }
		gm.bx += gm.vx * dt; gm.by += gm.vy * dt;
		if (gm.bx < 0.01f) { gm.bx = 0.01f; gm.vx = sg_abs(gm.vx); }
		if (gm.bx > 0.99f) { gm.bx = 0.99f; gm.vx = -sg_abs(gm.vx); }
		if (gm.by < 0.01f) { gm.by = 0.01f; gm.vy = sg_abs(gm.vy); }
		if (gm.by > 0.88f && gm.by < 0.92f && sg_abs(gm.bx - gm.px) < 0.075f && gm.vy > 0) {            /* the paddle: angle by where it hits */
			gm.vy = -sg_abs(gm.vy); gm.vx = (gm.bx - gm.px) / 0.075f * 0.55f; snd('m');
		}
		if (gm.by > 1.02f) {
			snd('e');
			if (--gm.lives <= 0) game_end(); else { gm.served = 0; }
		}
		int r = (int)((gm.by - 0.08f) / 0.05f), c = (int)(gm.bx * 10);
		if (r >= 0 && r < 6 && c >= 0 && c < 10 && gm.brick[r][c]) {
			if (--gm.brick[r][c] == 0) {
				gm.score += 20 * gm.level;
				for (int k = 0; k < 8 && gm.nsh < 120; k++)               /* the pane breaks into shards */
					gm.shard[gm.nsh++] = (typeof(gm.shard[0])){(c + 0.5f) / 10, 0.08f + (r + 0.5f) * 0.05f, (sg_frand() - .5f) * 0.6f, -sg_frand() * 0.4f, sg_frand() * 6, 1, (sg_frand() - .5f) * 10};
				snd('k');
			} else gm.score += 5;
			gm.vy = -gm.vy;
			int left = 0; for (int i = 0; i < 6; i++) for (int j = 0; j < 10; j++) left += gm.brick[i][j];
			if (!left) { gm.level++; shatter_wall(); snd('r'); }
		}
	}
}

static void game_draw(void)
{
	memcpy(fb.px, bg_sec, (size_t)W * H * 4);
	/* header: name, score, best */
	sg_text(&fb, &FD, U * 6 + U * 0.35f, U * 11, GAME_NAME[gm.game], U * 6.4f, RED, 0.9f, 0);
	sg_text(&fb, &FD, U * 6, U * 11, GAME_NAME[gm.game], U * 6.4f, WHITE, 1, 0);
	char sc[64]; snprintf(sc, sizeof sc, "%d", gm.score);
	cap_text(W - U * 6, U * 5.5f, "SCORE", U * 1.0f, RGB(170, 170, 176), 1, 1);
	sg_text(&fb, &FT, W - U * 6 - sg_text_w(&FT, sc, U * 4.2f, 0), U * 10.5f, sc, U * 4.2f, WHITE, 1, 0);
	snprintf(sc, sizeof sc, "BEST  %d", best[gm.game] > gm.score ? best[gm.game] : gm.score);
	cap_text(W - U * 24, U * 10, sc, U * 1.0f, RGB(170, 170, 176), 1, 1);
	if (gm.game == G_SHATTER) { snprintf(sc, sizeof sc, "LIVES  %d     WALL  %d", gm.lives, gm.level); cap_text(W - U * 40, U * 10, sc, U * 1.0f, RGB(170, 170, 176), 1, 1); }
	/* the board */
	float bw = W - U * 12, bh = H - U * 26, bx = U * 6, by = U * 15;
	ui_frost(bx, by, bw, bh, U * 2, 0.6f);
	if (gm.game == G_SNAKE) {
		float cs = sg_min((bw - U * 4) / SN_W, (bh - U * 4) / SN_H), ox = bx + (bw - cs * SN_W) / 2, oy = by + (bh - cs * SN_H) / 2;
		sg_rrect(&fb, ox - U * 0.5f, oy - U * 0.5f, cs * SN_W + U, cs * SN_H + U, U * 0.8f, sg_max(1, U * 0.12f), WHITE, 0.15f);
		float pulse = 0.5f + 0.5f * sg_sin(gm.t * 8);
		sg_glow(&fb, ox + (gm.fx + 0.5f) * cs, oy + (gm.fy + 0.5f) * cs, cs * 1.6f, RED, 0.3f + 0.2f * pulse);
		sg_disc(&fb, ox + (gm.fx + 0.5f) * cs, oy + (gm.fy + 0.5f) * cs, cs * 0.36f, RED, 1);
		for (int i = gm.len - 1; i >= 0; i--) {
			float k = 1 - (float)i / (gm.len + 4);
			sg_rrect(&fb, ox + gm.sx[i] * cs + cs * 0.08f, oy + gm.sy[i] * cs + cs * 0.08f, cs * 0.84f, cs * 0.84f, cs * 0.25f, 0, sg_mix(RGB(90, 90, 98), WHITE, k), 1);
		}
	} else if (gm.game == G_2048) {
		float s = sg_min(bw, bh) - U * 6, ts = (s - U * 1.6f * 5) / 4, ox = bx + (bw - s) / 2, oy = by + (bh - s) / 2;
		sg_rrect(&fb, ox, oy, s, s, U * 1.6f, 0, BLACK, 0.4f);
		for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) {
			float tx = ox + U * 1.6f + x * (ts + U * 1.6f), ty = oy + U * 1.6f + y * (ts + U * 1.6f);
			int v = gm.tile[y][x];
			if (!v) { sg_rrect(&fb, tx, ty, ts, ts, U * 1.0f, 0, WHITE, 0.05f); continue; }
			int lv = 0; for (int q = v; q > 2; q >>= 1) lv++;
			float k = sg_sat(lv / 10.0f), sc2 = 0.85f + 0.15f * sg_ease_out(gm.pop[y][x]);
			uint32_t col = lv >= 10 ? RED : sg_mix(RGB(60, 60, 66), WHITE, k * 1.1f);
			float d = ts * (1 - sc2) / 2;
			sg_rrect(&fb, tx + d, ty + d, ts - 2 * d, ts - 2 * d, U * 1.0f, 0, col, 1);
			char nb[16]; snprintf(nb, sizeof nb, "%d", v);
			float fs = ts * (v < 100 ? 0.45f : v < 1000 ? 0.36f : 0.28f);
			sg_text_c(&fb, &FT, tx + ts / 2, ty + ts / 2 + fs * 0.36f, nb, fs, k > 0.55f && lv < 10 ? INK : WHITE, 1, 0);
		}
	} else {
		float ox = bx + U * 2, oy = by + U * 2, ow = bw - U * 4, oh = bh - U * 4;
		for (int r = 0; r < 6; r++) for (int c = 0; c < 10; c++) {
			if (!gm.brick[r][c]) continue;
			float x = ox + c * ow / 10 + U * 0.3f, y = oy + (0.08f + r * 0.05f) * oh + U * 0.2f, w = ow / 10 - U * 0.6f, h = 0.05f * oh - U * 0.4f;
			sg_rrect(&fb, x, y, w, h, U * 0.4f, 0, gm.brick[r][c] == 2 ? WHITE : RGB(150, 152, 160), gm.brick[r][c] == 2 ? 0.85f : 0.55f);
			sg_line(&fb, (V2){x + w * 0.15f, y + h * 0.25f}, (V2){x + w * 0.4f, y + h * 0.25f}, sg_max(1, U * 0.1f), WHITE, 0.6f);
		}
		for (int i = 0; i < gm.nsh; i++) {
			float x = ox + gm.shard[i].x * ow, y = oy + gm.shard[i].y * oh, s = U * 0.8f, a = gm.shard[i].r;
			sg_tri(&fb, (V2){x + sg_cos(a) * s, y + sg_sin(a) * s}, (V2){x + sg_cos(a + 2.2f) * s, y + sg_sin(a + 2.2f) * s}, (V2){x + sg_cos(a + 4.1f) * s * 0.6f, y + sg_sin(a + 4.1f) * s * 0.6f}, WHITE, sg_sat(gm.shard[i].a));
		}
		sg_rrect(&fb, ox + (gm.px - 0.075f) * ow, oy + 0.9f * oh, 0.15f * ow, U * 1.2f, U * 0.6f, 0, WHITE, 1);
		sg_glow(&fb, ox + gm.bx * ow, oy + gm.by * oh, U * 3, RED, 0.35f);
		sg_disc(&fb, ox + gm.bx * ow, oy + gm.by * oh, U * 0.9f, RED, 1);
		if (!gm.served && !gm.over) sg_text_c(&fb, &FT, W / 2.0f, oy + oh * 0.7f, "Press Up to launch", U * 2.0f, RGB(200, 200, 206), 1, 0);
	}
	if (gm.over) {
		sg_rect(&fb, bx, by, bw, bh, BLACK, 0.45f);
		const char *msg = gm.game == G_2048 ? "NO MOVES LEFT" : "GAME OVER";
		sg_text_c(&fb, &FD, W / 2.0f, by + bh / 2, msg, U * 7, WHITE, 1, 0);
		char fin[64]; snprintf(fin, sizeof fin, "Score %d  -  Enter plays again", gm.score);
		sg_text_c(&fb, &FT, W / 2.0f, by + bh / 2 + U * 5, fin, U * 2.2f, RGB(220, 220, 226), 1, 0);
	}
	foot_hints(gm.game == G_SHATTER ? "LEFT RIGHT  move      UP  launch      ESC  back" : "ARROWS  play      ENTER  again      ESC  back");
}
