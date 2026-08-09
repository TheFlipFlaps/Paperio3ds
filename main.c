// paper.io 2 -- 3DS remake (homebrew prototype)
// Territory-capture game: draw a trail outside your territory, get back
// home to claim everything the trail (plus flood fill) encloses.
// Built with libctru + citro2d/citro3d (devkitPro / devkitARM).

#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>

// ---------------------------------------------------------------------
// World / grid configuration
// ---------------------------------------------------------------------
#define WORLD_W   60
#define WORLD_H   40
#define CELL_PX   6
#define OFFSET_X  ((400 - WORLD_W * CELL_PX) / 2)   // center on top screen
#define OFFSET_Y  ((240 - WORLD_H * CELL_PX) / 2)

// Cell values stored in the grid
#define CELL_EMPTY        0
#define CELL_P1_TERRITORY 1
#define CELL_P1_TRAIL     2
#define CELL_P2_TERRITORY 3
#define CELL_P2_TRAIL     4

#define MAX_TRAIL (WORLD_W * WORLD_H)

typedef struct { int x, y; } Point;

typedef struct {
    int x, y;           // current grid position
    int dx, dy;          // current movement direction
    u8  territoryVal;    // grid value representing this entity's territory
    u8  trailVal;        // grid value representing this entity's trail
    Point trail[MAX_TRAIL];
    int trailLen;
    int spawnX, spawnY;   // used to respawn after death
    int moveTimer;
    int moveEvery;        // lower = faster
    bool isBot;
    int aiTimer;
} Entity;

static u8 grid[WORLD_H][WORLD_W];
static Entity player;
static Entity bot;
static bool visited[WORLD_H][WORLD_W];
static Point bfsQueue[WORLD_W * WORLD_H];

static int playerScore = 0;
static int botScore = 0;
static bool gameOverMsgTimer = 0; // frames left to show a "captured!" flash

// ---------------------------------------------------------------------
// Grid helpers
// ---------------------------------------------------------------------
static inline bool inBounds(int x, int y) {
    return x >= 0 && x < WORLD_W && y >= 0 && y < WORLD_H;
}

// Carve out an initial square of territory for an entity.
static void carveTerritory(u8 val, int cx, int cy, int size) {
    for (int y = cy - size; y <= cy + size; y++) {
        for (int x = cx - size; x <= cx + size; x++) {
            if (inBounds(x, y)) grid[y][x] = val;
        }
    }
}

static void spawnEntity(Entity *e, int cx, int cy, u8 territoryVal,
                         u8 trailVal, int size, bool isBot, int moveEvery) {
    e->x = cx; e->y = cy;
    e->dx = 1; e->dy = 0;
    e->territoryVal = territoryVal;
    e->trailVal = trailVal;
    e->trailLen = 0;
    e->spawnX = cx; e->spawnY = cy;
    e->moveTimer = 0;
    e->moveEvery = moveEvery;
    e->isBot = isBot;
    e->aiTimer = 0;
    carveTerritory(territoryVal, cx, cy, size);
}

// Wipe an entity's in-progress trail cells off the grid (used on death).
static void clearTrailCells(Entity *e) {
    for (int i = 0; i < e->trailLen; i++) {
        Point p = e->trail[i];
        if (grid[p.y][p.x] == e->trailVal) grid[p.y][p.x] = CELL_EMPTY;
    }
    e->trailLen = 0;
}

// Send an entity back to its spawn point after it dies. Territory is kept;
// only the in-flight trail is lost. (A harsher variant could wipe territory
// too -- left as an easy tweak.)
static void respawnEntity(Entity *e) {
    clearTrailCells(e);
    e->x = e->spawnX;
    e->y = e->spawnY;
    e->dx = 1; e->dy = 0;
}

// Flood fill from every border cell across anything that ISN'T the given
// territory value. Cells the flood fill can't reach are "enclosed" and get
// captured as new territory (this is what makes closing a trail loop work).
static void claimTerritory(Entity *e) {
    // 1) Bake the trail itself into territory.
    for (int i = 0; i < e->trailLen; i++) {
        Point p = e->trail[i];
        grid[p.y][p.x] = e->territoryVal;
    }
    e->trailLen = 0;

    // 2) BFS flood fill starting from the border, walking through any cell
    //    that is not this entity's territory.
    memset(visited, 0, sizeof(visited));
    int head = 0, tail = 0;

    for (int x = 0; x < WORLD_W; x++) {
        for (int y2 = 0; y2 < WORLD_H; y2 += (WORLD_H - 1 > 0 ? WORLD_H - 1 : 1)) {
            if (grid[y2][x] != e->territoryVal && !visited[y2][x]) {
                visited[y2][x] = true;
                bfsQueue[tail++] = (Point){x, y2};
            }
        }
    }
    for (int y = 0; y < WORLD_H; y++) {
        for (int x2 = 0; x2 < WORLD_W; x2 += (WORLD_W - 1 > 0 ? WORLD_W - 1 : 1)) {
            if (grid[y][x2] != e->territoryVal && !visited[y][x2]) {
                visited[y][x2] = true;
                bfsQueue[tail++] = (Point){x2, y};
            }
        }
    }

    static const int NX[4] = {1, -1, 0, 0};
    static const int NY[4] = {0, 0, 1, -1};

    while (head < tail) {
        Point c = bfsQueue[head++];
        for (int d = 0; d < 4; d++) {
            int nx = c.x + NX[d], ny = c.y + NY[d];
            if (!inBounds(nx, ny)) continue;
            if (visited[ny][nx]) continue;
            if (grid[ny][nx] == e->territoryVal) continue; // blocked by own land
            visited[ny][nx] = true;
            bfsQueue[tail++] = (Point){nx, ny};
        }
    }

    // 3) Anything not reached by the flood fill (and not already ours) was
    //    enclosed -> capture it.
    for (int y = 0; y < WORLD_H; y++) {
        for (int x = 0; x < WORLD_W; x++) {
            if (!visited[y][x] && grid[y][x] != e->territoryVal) {
                grid[y][x] = e->territoryVal;
            }
        }
    }
}

// Advance one entity by one grid cell. Returns false if the entity died
// this step (caller should respawn it).
static bool stepEntity(Entity *e, Entity *other) {
    int nx = e->x + e->dx;
    int ny = e->y + e->dy;

    // Bounce off world edges instead of dying, to keep things playable.
    if (!inBounds(nx, ny)) {
        e->dx = -e->dx;
        e->dy = -e->dy;
        nx = e->x + e->dx;
        ny = e->y + e->dy;
        if (!inBounds(nx, ny)) return true; // corner-stuck, just skip
    }

    u8 target = grid[ny][nx];

    // Crossing the opponent's trail kills you.
    if (target == other->trailVal) {
        return false;
    }
    // Crossing your own trail kills you (classic snake-style self hit).
    if (target == e->trailVal) {
        return false;
    }

    e->x = nx; e->y = ny;
    target = grid[ny][nx];

    if (target == e->territoryVal) {
        // Back on home soil -- if we were out drawing a trail, claim it.
        if (e->trailLen > 0) {
            claimTerritory(e);
        }
    } else {
        // Outside our territory: lay down trail (guard against overflow).
        if (e->trailLen < MAX_TRAIL) {
            grid[ny][nx] = e->trailVal;
            e->trail[e->trailLen++] = (Point){nx, ny};
        }
    }
    return true;
}

// ---------------------------------------------------------------------
// Simple bot AI: mostly goes straight, occasionally turns, and tries to
// avoid an instantly-fatal move (its own trail or the player's trail).
// ---------------------------------------------------------------------
static bool wouldDieMoving(Entity *e, Entity *other, int dx, int dy) {
    int nx = e->x + dx, ny = e->y + dy;
    if (!inBounds(nx, ny)) return true;
    u8 t = grid[ny][nx];
    return (t == e->trailVal || t == other->trailVal);
}

static void botAI(Entity *bot, Entity *player) {
    bot->aiTimer--;
    bool forcedTurn = wouldDieMoving(bot, player, bot->dx, bot->dy);

    if (bot->aiTimer <= 0 || forcedTurn) {
        int dirs[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
        int order[4] = {0,1,2,3};
        // simple shuffle
        for (int i = 3; i > 0; i--) {
            int j = rand() % (i + 1);
            int tmp = order[i]; order[i] = order[j]; order[j] = tmp;
        }
        bool picked = false;
        for (int i = 0; i < 4; i++) {
            int dx = dirs[order[i]][0], dy = dirs[order[i]][1];
            if (dx == -bot->dx && dy == -bot->dy && bot->trailLen > 0) continue; // avoid instant U-turn into own trail
            if (!wouldDieMoving(bot, player, dx, dy)) {
                bot->dx = dx; bot->dy = dy;
                picked = true;
                break;
            }
        }
        if (!picked) {
            // no safe move found -- just keep going and accept the risk
        }
        bot->aiTimer = 40 + rand() % 60;
    }
}

// ---------------------------------------------------------------------
// Scoring
// ---------------------------------------------------------------------
static void computeScores(void) {
    int p = 0, b = 0;
    for (int y = 0; y < WORLD_H; y++) {
        for (int x = 0; x < WORLD_W; x++) {
            if (grid[y][x] == CELL_P1_TERRITORY) p++;
            if (grid[y][x] == CELL_P2_TERRITORY) b++;
        }
    }
    playerScore = p;
    botScore = b;
}

// ---------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------
static void drawWorld(void) {
    u32 colP1Territory = C2D_Color32(70, 160, 255, 255);
    u32 colP1Trail      = C2D_Color32(150, 210, 255, 255);
    u32 colP2Territory = C2D_Color32(255, 110, 90, 255);
    u32 colP2Trail      = C2D_Color32(255, 175, 150, 255);

    for (int y = 0; y < WORLD_H; y++) {
        for (int x = 0; x < WORLD_W; x++) {
            u8 v = grid[y][x];
            if (v == CELL_EMPTY) continue;
            u32 col;
            switch (v) {
                case CELL_P1_TERRITORY: col = colP1Territory; break;
                case CELL_P1_TRAIL:     col = colP1Trail; break;
                case CELL_P2_TERRITORY: col = colP2Territory; break;
                case CELL_P2_TRAIL:     col = colP2Trail; break;
                default: continue;
            }
            C2D_DrawRectSolid(OFFSET_X + x * CELL_PX, OFFSET_Y + y * CELL_PX,
                               0.0f, CELL_PX, CELL_PX, col);
        }
    }

    // Draw the two entities as slightly larger, bright markers on top.
    C2D_DrawRectSolid(OFFSET_X + player.x * CELL_PX - 1, OFFSET_Y + player.y * CELL_PX - 1,
                       0.5f, CELL_PX + 2, CELL_PX + 2, C2D_Color32(0, 60, 200, 255));
    C2D_DrawRectSolid(OFFSET_X + bot.x * CELL_PX - 1, OFFSET_Y + bot.y * CELL_PX - 1,
                       0.5f, CELL_PX + 2, CELL_PX + 2, C2D_Color32(200, 30, 0, 255));
}

// ---------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------
int main(int argc, char **argv) {
    gfxInitDefault();
    consoleInit(GFX_BOTTOM, NULL);

    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    C3D_RenderTarget *top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    u32 clearColor = C2D_Color32(235, 235, 235, 255);

    srand((unsigned)time(NULL));
    memset(grid, 0, sizeof(grid));

    spawnEntity(&player, 12, WORLD_H / 2, CELL_P1_TERRITORY, CELL_P1_TRAIL, 3, false, 4);
    spawnEntity(&bot,    WORLD_W - 12, WORLD_H / 2, CELL_P2_TERRITORY, CELL_P2_TRAIL, 3, true, 5);

    int frame = 0;

    while (aptMainLoop()) {
        hidScanInput();
        u32 kDown = hidKeysDown();
        u32 kHeld = hidKeysHeld();

        if (kDown & KEY_START) break;

        // --- Player input: D-Pad sets the current direction. ---
        if (kHeld & KEY_DUP)    { player.dx = 0;  player.dy = -1; }
        else if (kHeld & KEY_DDOWN)  { player.dx = 0;  player.dy = 1; }
        else if (kHeld & KEY_DLEFT)  { player.dx = -1; player.dy = 0; }
        else if (kHeld & KEY_DRIGHT) { player.dx = 1;  player.dy = 0; }

        // --- Update ---
        player.moveTimer++;
        if (player.moveTimer >= player.moveEvery) {
            player.moveTimer = 0;
            if (!stepEntity(&player, &bot)) {
                respawnEntity(&player);
            }
        }

        bot.moveTimer++;
        if (bot.moveTimer >= bot.moveEvery) {
            bot.moveTimer = 0;
            botAI(&bot, &player);
            if (!stepEntity(&bot, &player)) {
                respawnEntity(&bot);
            }
        }

        if (frame % 15 == 0) computeScores();

        // --- Bottom screen HUD (plain console text) ---
        consoleSelect(consoleGetDefault());
        printf("\x1b[1;1HPaper.io 3DS -- prototype\n");
        printf("\x1b[3;1HD-Pad: move   START: quit\n");
        printf("\x1b[5;1HYour territory:  %4d cells\n", playerScore);
        printf("\x1b[6;1HBot  territory:  %4d cells\n", botScore);
        printf("\x1b[8;1HDraw a trail outside your\n");
        printf("\x1b[9;1Hzone, then get back home to\n");
        printf("\x1b[10;1Hclaim everything it encloses.\n");
        printf("\x1b[12;1HTouch the enemy trail = you die.\n");
        printf("\x1b[13;1HHit your own trail = you die.\n");

        // --- Render top screen ---
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        C2D_TargetClear(top, clearColor);
        C2D_SceneBegin(top);
        drawWorld();
        C3D_FrameEnd(0);

        frame++;
    }

    C2D_Fini();
    C3D_Fini();
    gfxExit();
    return 0;
}
