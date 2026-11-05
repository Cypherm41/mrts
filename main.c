/*
 * Medieval Kingdoms - 2D Isometric RTS (Raylib)
 * BUILD 16 - SPAWN + MOVEMENT FIXED
 *
 * ROOT CAUSE FIXED:
 *   Units spawned INSIDE the building footprint (non-walkable tile).
 *   findPath() rejects any path whose START tile is non-walkable,
 *   so EVERY move order failed instantly.
 *
 * FIXES:
 *   1. findFreeSpawnTile() - searches outward for a walkable free tile
 *   2. Spread index - each new unit spawns on a different tile
 *   3. findPath() now guards against non-walkable start (searches outward)
 *   4. selBuild cleared on empty-ground click (was blocking unit selection)
 *   5. Destination marker drawn so you can SEE orders register
 *   6. Movement uses float fx/fy - smooth, no truncation to zero
 */

#include "raylib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define WINDOW_WIDTH 1280
#define WINDOW_HEIGHT 720
#define MAP_SIZE 48
#define TILE_WIDTH 64
#define TILE_HEIGHT 32
#define MAX_UNITS 512
#define MAX_BUILDINGS 128
#define MAX_DEPOSITS 48
#define MAX_PATH_LEN 96
#define FPS 60

#define POP_BASE 5
#define POP_PER_TOWNHALL 10
#define POP_PER_HOUSE 5
#define COST_VILLAGER_FOOD 50
#define COST_SOLDIER_FOOD 80
#define TRAIN_TIME 9000

typedef enum { FACTION_PLAYER = 1, FACTION_AI = 2 } Faction;
typedef enum { T_GRASS=0, T_DIRT=1, T_FOREST=2, T_ROCK=3, T_WATER=4, T_TREE=5, T_GOLD=6, T_STONE=7 } TileType;
typedef enum { B_NONE=0, B_TOWNCENTER=1, B_HOUSE=2, B_BARRACKS=3, B_FARM=4, B_LUMBER=5, B_MINING=6, B_TOWER=7 } BuildingType;
typedef enum { U_VILLAGER=1, U_SOLDIER=2 } UnitType;
typedef enum { S_IDLE=0, S_MOVING=1, S_GATHER=2, S_BUILD=3, S_DEAD=4 } UnitState;
typedef enum { BS_DONE=0, BS_BUILDING=1 } BuildState;

typedef struct { int x, y; } Vec2i;
typedef struct { int type, walkable, buildable; } Tile;
typedef struct { Vec2i pos; int amount, resType, active; } Deposit;
typedef struct { BuildingType btype; int w, g, s, f, width, height; } BuildInfo;

typedef struct {
    int id, faction, btype;
    Vec2i pos;
    int width, height, hp, maxHp;
    BuildState buildState;
    int progress;
    int selected, trainingUnit, trainingProgress;
} Building;

typedef struct {
    int id, faction, utype;
    int gridX, gridY;      /* rounded position, used for pathfinding */
    float fx, fy;          /* float position, used for smooth movement */
    int screenX, screenY;
    int hp, maxHp, dmg, speed;
    UnitState state;
    int selected;
    Vec2i path[MAX_PATH_LEN];
    int pathLen, pathIdx;
    int gatherId, buildId;
} Unit;

typedef struct {
    Tile map[MAP_SIZE][MAP_SIZE];
    Deposit deps[MAX_DEPOSITS];
    int depCount;
    Unit units[MAX_UNITS];
    int unitCount;
    Building builds[MAX_BUILDINGS];
    int buildCount;
    int food, wood, gold, stone, popCap, popUsed;
    float offsetX, offsetY, zoom;
    int mouseX, mouseY, selecting, selX1, selY1;
    int paused, gameOver, winner, placing;
    BuildingType placeType;
    Vec2i placePos;
    Building* selBuild;
    /* destination marker feedback */
    int markerActive;
    Vec2i markerPos;
    float markerTimer;
} Game;

void updatePopulation(Game* g);

static const BuildInfo INFO[] = {
    {B_NONE, 0, 0, 0, 0, 0, 0},
    {B_TOWNCENTER, 250, 150, 100, 500, 3, 3},
    {B_HOUSE, 50, 0, 0, 0, 2, 1},
    {B_BARRACKS, 150, 100, 0, 0, 2, 2},
    {B_FARM, 50, 0, 0, 100, 2, 1},
    {B_LUMBER, 75, 0, 0, 0, 2, 1},
    {B_MINING, 100, 50, 0, 0, 2, 2},
    {B_TOWER, 200, 0, 150, 0, 1, 1}
};

static const BuildInfo* get_info(BuildingType t) {
    if (t > 0 && t <= 7) return &INFO[t];
    return &INFO[0];
}

static float sqdist(float ax, float ay, float bx, float by) {
    float dx = ax - bx, dy = ay - by;
    return dx*dx + dy*dy;
}

static void worldToScreen(int gx, int gy, int* sx, int* sy, float ox, float oy, float z) {
    *sx = (int)(((gx - gy) * (TILE_WIDTH / 2) + ox) * z);
    *sy = (int)(((gx + gy) * (TILE_HEIGHT / 2) + oy) * z);
}

static void screenToWorld(int sx, int sy, int* gx, int* gy, float ox, float oy, float z) {
    float ix = (float)sx / z - ox;
    float iy = (float)sy / z - oy;
    *gx = (int)((ix / (TILE_WIDTH / 2.0f) + iy / (TILE_HEIGHT / 2.0f)) / 2.0f);
    *gy = (int)((iy / (TILE_HEIGHT / 2.0f) - ix / (TILE_WIDTH / 2.0f)) / 2.0f);
}

static int inBounds(int x, int y) {
    return (x >= 0 && x < MAP_SIZE && y >= 0 && y < MAP_SIZE);
}

static Tile make_tile(TileType t, int w, int b) {
    Tile tile;
    tile.type = t;
    tile.walkable = w;
    tile.buildable = b;
    return tile;
}

/* Is this tile blocked by a building footprint? */
static int blockedByBuilding(Game* g, int x, int y) {
    for (int i = 0; i < g->buildCount; i++) {
        Building* b = &g->builds[i];
        if (x >= b->pos.x && x < b->pos.x + b->width &&
            y >= b->pos.y && y < b->pos.y + b->height) return 1;
    }
    return 0;
}

/* Find a free, walkable tile near (cx,cy) - spirals outward */
static Vec2i findFreeTile(Game* g, int cx, int cy, int ignoreUnitId) {
    for (int r = 0; r <= 12; r++) {
        for (int dy = -r; dy <= r; dy++) {
            for (int dx = -r; dx <= r; dx++) {
                /* only the ring at radius r */
                if (r > 0 && abs(dx) != r && abs(dy) != r) continue;
                int x = cx + dx, y = cy + dy;
                if (!inBounds(x, y)) continue;
                if (!g->map[y][x].walkable) continue;
                if (blockedByBuilding(g, x, y)) continue;

                /* avoid stacking on another unit */
                int occupied = 0;
                for (int u = 0; u < g->unitCount; u++) {
                    if (g->units[u].id == ignoreUnitId) continue;
                    if (g->units[u].state == S_DEAD) continue;
                    if (g->units[u].gridX == x && g->units[u].gridY == y) { occupied = 1; break; }
                }
                if (occupied) continue;

                return (Vec2i){ x, y };
            }
        }
    }
    return (Vec2i){ cx, cy };
}

/* Nearest walkable tile, ignoring occupancy (for move targets) */
static Vec2i nearestWalkable(Game* g, int x, int y) {
    if (!inBounds(x, y)) return (Vec2i){ MAP_SIZE/2, MAP_SIZE/2 };
    if (g->map[y][x].walkable && !blockedByBuilding(g, x, y)) return (Vec2i){ x, y };

    for (int r = 1; r <= 10; r++) {
        for (int dy = -r; dy <= r; dy++) {
            for (int dx = -r; dx <= r; dx++) {
                if (abs(dx) != r && abs(dy) != r) continue;
                int nx = x + dx, ny = y + dy;
                if (!inBounds(nx, ny)) continue;
                if (g->map[ny][nx].walkable && !blockedByBuilding(g, nx, ny))
                    return (Vec2i){ nx, ny };
            }
        }
    }
    return (Vec2i){ x, y };
}

static void drawTile(int sx, int sy, int tt, float z) {
    Color col = (Color){74, 124, 46, 255};
    if (tt == T_GRASS)       col = (Color){74, 124, 46, 255};
    else if (tt == T_DIRT)   col = (Color){139, 90, 43, 255};
    else if (tt == T_FOREST) col = (Color){34, 84, 26, 255};
    else if (tt == T_ROCK)   col = (Color){128, 128, 128, 255};
    else if (tt == T_WATER)  col = (Color){65, 105, 225, 255};
    else if (tt == T_TREE)   col = (Color){34, 139, 34, 255};
    else if (tt == T_GOLD)   col = (Color){255, 215, 0, 255};
    else if (tt == T_STONE)  col = (Color){169, 169, 169, 255};

    int hw = (int)(TILE_WIDTH / 2.0f * z);
    int hh = (int)(TILE_HEIGHT / 2.0f * z);
    if (hw < 1 || hh < 1) return;

    for (int y = sy - hh; y < sy; y++) {
        int d = y - (sy - hh);
        DrawLine(sx - (hw*d/hh), y, sx + (hw*d/hh), y, col);
    }
    for (int y = sy; y <= sy + hh; y++) {
        int d = (sy + hh) - y;
        DrawLine(sx - (hw*d/hh), y, sx + (hw*d/hh), y, col);
    }
}

static int findPath(Game* g, Vec2i from, Vec2i to, Vec2i* path, int maxlen) {
    if (!inBounds(from.x, from.y) || !inBounds(to.x, to.y)) return 0;

    /* Guard: if start is not walkable, snap to nearest free tile */
    if (!g->map[from.y][from.x].walkable || blockedByBuilding(g, from.x, from.y)) {
        Vec2i fixed = nearestWalkable(g, from.x, from.y);
        from = fixed;
    }
    if (!g->map[to.y][to.x].walkable || blockedByBuilding(g, to.x, to.y)) {
        Vec2i fixed = nearestWalkable(g, to.x, to.y);
        to = fixed;
    }

    if (from.x == to.x && from.y == to.y) { path[0] = from; return 1; }

    static Vec2i q[MAP_SIZE * MAP_SIZE];
    static int px[MAP_SIZE][MAP_SIZE];
    static int py[MAP_SIZE][MAP_SIZE];
    static int vis[MAP_SIZE][MAP_SIZE];

    int head = 0, tail = 0;
    memset(vis, 0, sizeof(vis));

    q[tail++] = from;
    vis[from.y][from.x] = 1;

    int dirs[8][2] = {{0,1},{0,-1},{1,0},{-1,0},{1,1},{1,-1},{-1,1},{-1,-1}};

    while (head < tail) {
        Vec2i c = q[head++];
        if (c.x == to.x && c.y == to.y) {
            int len = 0;
            Vec2i cur = to;
            while (cur.x != from.x || cur.y != from.y) {
                if (len >= maxlen) break;
                path[len++] = cur;
                int tx = px[cur.y][cur.x];
                int ty = py[cur.y][cur.x];
                cur.x = tx; cur.y = ty;
            }
            /* reverse so path runs start -> goal */
            for (int i = 0; i < len/2; i++) {
                Vec2i t = path[i];
                path[i] = path[len-1-i];
                path[len-1-i] = t;
            }
            return len;
        }
        for (int i = 0; i < 8; i++) {
            int nx = c.x + dirs[i][0];
            int ny = c.y + dirs[i][1];
            if (!inBounds(nx, ny)) continue;
            if (vis[ny][nx]) continue;
            if (!g->map[ny][nx].walkable) continue;
            if (blockedByBuilding(g, nx, ny)) continue;
            vis[ny][nx] = 1;
            px[ny][nx] = c.x;
            py[ny][nx] = c.y;
            q[tail++] = (Vec2i){ nx, ny };
        }
    }
    return 0;
}

static void initMap(Game* g) {
    for (int y = 0; y < MAP_SIZE; y++)
        for (int x = 0; x < MAP_SIZE; x++)
            g->map[y][x] = make_tile(T_GRASS, 1, 1);

    for (int i = 0; i < 12; i++) {
        int cx = rand() % MAP_SIZE;
        int cy = rand() % MAP_SIZE;
        int sz = 2 + rand() % 3;
        for (int dy = -sz; dy <= sz; dy++) {
            for (int dx = -sz; dx <= sz; dx++) {
                int tx = cx + dx, ty = cy + dy;
                if (!inBounds(tx, ty)) continue;
                if (g->map[ty][tx].type != T_GRASS) continue;
                if (rand() % 100 < 65)
                    g->map[ty][tx] = make_tile(T_FOREST, 0, 0);
            }
        }
    }

    g->depCount = 0;
    for (int i = 0; i < 40 && g->depCount < MAX_DEPOSITS; i++) {
        int x = rand() % MAP_SIZE;
        int y = rand() % MAP_SIZE;
        if (x < 14 && y < 14) continue;
        if (x > MAP_SIZE-14 && y > MAP_SIZE-14) continue;
        if (g->map[y][x].type != T_GRASS) continue;

        int rt = rand() % 3;
        g->deps[g->depCount].pos = (Vec2i){ x, y };
        g->deps[g->depCount].amount = 700 + rand() % 600;
        g->deps[g->depCount].resType = rt + 1;
        g->deps[g->depCount].active = 1;

        if (rt == 0)      g->map[y][x] = make_tile(T_TREE, 0, 0);
        else if (rt == 1) g->map[y][x] = make_tile(T_GOLD, 0, 0);
        else              g->map[y][x] = make_tile(T_STONE, 0, 0);

        g->depCount++;
    }
}

static void addBuilding(Game* g, int fac, BuildingType bt, int x, int y, BuildState st) {
    if (g->buildCount >= MAX_BUILDINGS) return;
    Building* b = &g->builds[g->buildCount++];
    const BuildInfo* info = get_info(bt);

    b->id = g->buildCount;
    b->faction = fac;
    b->btype = bt;
    b->pos = (Vec2i){ x, y };
    b->width = info->width;
    b->height = info->height;
    b->maxHp = (bt == B_TOWNCENTER) ? 1200 : 400;
    b->hp = b->maxHp;
    b->buildState = st;
    b->progress = (st == BS_DONE) ? 10000 : 0;
    b->selected = 0;
    b->trainingUnit = 0;
    b->trainingProgress = 0;

    for (int yy = 0; yy < b->height; yy++)
        for (int xx = 0; xx < b->width; xx++)
            if (inBounds(x+xx, y+yy))
                g->map[y+yy][x+xx].walkable = 0;
}

static void addUnit(Game* g, int fac, UnitType ut, int x, int y) {
    if (g->unitCount >= MAX_UNITS) return;
    Unit* u = &g->units[g->unitCount++];

    u->id = g->unitCount;
    u->faction = fac;
    u->utype = ut;
    u->gridX = x;
    u->gridY = y;
    u->fx = (float)x;
    u->fy = (float)y;
    u->screenX = 0;
    u->screenY = 0;

    if (ut == U_VILLAGER) { u->hp = u->maxHp = 40;  u->dmg = 5;  u->speed = 3; }
    else                  { u->hp = u->maxHp = 90;  u->dmg = 18; u->speed = 2; }

    u->state = S_IDLE;
    u->selected = 0;
    u->pathLen = 0;
    u->pathIdx = 0;
    u->gatherId = -1;
    u->buildId = -1;
}

static void spawnFaction(Game* g, int fac, int sx, int sy) {
    addBuilding(g, fac, B_TOWNCENTER, sx, sy, BS_DONE);
    addBuilding(g, fac, B_HOUSE, sx + 4, sy, BS_DONE);

    /* Place starter units on free tiles around the base */
    for (int i = 0; i < 4; i++) {
        Vec2i p = findFreeTile(g, sx + 2 + i, sy + 5, -1);
        addUnit(g, fac, U_VILLAGER, p.x, p.y);
    }
    for (int i = 0; i < 2; i++) {
        Vec2i p = findFreeTile(g, sx + 6 + i, sy + 5, -1);
        addUnit(g, fac, U_SOLDIER, p.x, p.y);
    }
}

static void initGame(Game* g) {
    memset(g, 0, sizeof(Game));
    g->food = 800;
    g->wood = 600;
    g->gold = 300;
    g->stone = 250;
    g->zoom = 1.0f;
    g->offsetX = 520.0f;
    g->offsetY = 120.0f;
    g->markerActive = 0;

    initMap(g);
    spawnFaction(g, FACTION_PLAYER, 10, 10);
    spawnFaction(g, FACTION_AI, MAP_SIZE - 16, MAP_SIZE - 16);
    updatePopulation(g);
}

void updatePopulation(Game* g) {
    int used = 0;
    for (int i = 0; i < g->unitCount; i++)
        if (g->units[i].state != S_DEAD && g->units[i].faction == FACTION_PLAYER)
            used++;
    g->popUsed = used;

    int cap = POP_BASE;
    for (int i = 0; i < g->buildCount; i++) {
        if (g->builds[i].faction != FACTION_PLAYER) continue;
        if (g->builds[i].buildState != BS_DONE) continue;
        if (g->builds[i].btype == B_TOWNCENTER) cap += POP_PER_TOWNHALL;
        else if (g->builds[i].btype == B_HOUSE) cap += POP_PER_HOUSE;
    }
    g->popCap = cap;
}

static int spawnTrainingUnit(Game* g, Building* b, UnitType utype) {
    if (g->unitCount >= MAX_UNITS) { printf("[SPAWN FAIL] unit limit\n"); return 0; }
    if (g->popUsed >= g->popCap)   { printf("[SPAWN FAIL] pop cap %d/%d\n", g->popUsed, g->popCap); return 0; }

    /* KEY FIX: search for a free walkable tile outside the building */
    Vec2i p = findFreeTile(g, b->pos.x, b->pos.y + b->height, -1);

    printf("[SPAWN] %s -> tile (%d,%d)\n",
           utype == U_VILLAGER ? "Villager" : "Soldier", p.x, p.y);

    addUnit(g, FACTION_PLAYER, utype, p.x, p.y);
    g->popUsed++;
    return 1;
}

static void updateUnit(Unit* u, Game* g, float dt) {
    if (u->state == S_DEAD) return;

    /* ---- MOVEMENT ---- */
    if (u->state == S_MOVING && u->pathLen > 0 && u->pathIdx < u->pathLen) {
        Vec2i tgt = u->path[u->pathIdx];
        float dx = (float)tgt.x - u->fx;
        float dy = (float)tgt.y - u->fy;
        float dist = sqrtf(dx*dx + dy*dy);

        if (dist > 0.02f) {
            float step = (float)u->speed * dt * 3.0f;
            if (step > dist) step = dist;
            u->fx += (dx / dist) * step;
            u->fy += (dy / dist) * step;

            u->gridX = (int)floorf(u->fx + 0.5f);
            u->gridY = (int)floorf(u->fy + 0.5f);
        } else {
            /* arrived at this node */
            u->fx = (float)tgt.x;
            u->fy = (float)tgt.y;
            u->gridX = tgt.x;
            u->gridY = tgt.y;
            u->pathIdx++;
            if (u->pathIdx >= u->pathLen) {
                u->state = S_IDLE;
                u->pathLen = 0;
                u->pathIdx = 0;
            }
        }
    }

    /* ---- CONSTRUCTION ---- */
    if (u->state == S_BUILD && u->buildId >= 0 && u->buildId < g->buildCount) {
        Building* b = &g->builds[u->buildId];
        if (b->buildState == BS_BUILDING) {
            float d2 = sqdist(u->fx, u->fy, (float)b->pos.x, (float)b->pos.y);
            if (d2 < 6.0f) {
                b->progress += (int)(dt * 3600.0f);
                if (b->progress >= 10000) {
                    b->buildState = BS_DONE;
                    b->hp = b->maxHp;
                    u->state = S_IDLE;
                    u->buildId = -1;
                    printf("[BUILD] complete\n");
                }
            } else {
                /* walk toward the site */
                Vec2i t = findFreeTile(g, b->pos.x, b->pos.y + b->height, u->id);
                int plen = findPath(g, (Vec2i){u->gridX,u->gridY}, t, u->path, MAX_PATH_LEN);
                if (plen > 0) { u->pathLen = plen; u->pathIdx = 0; u->state = S_MOVING; }
            }
        }
    }

    /* ---- GATHERING ---- */
    if (u->state == S_GATHER && u->gatherId >= 0 && u->gatherId < g->depCount) {
        Deposit* dp = &g->deps[u->gatherId];
        if (!dp->active) { u->state = S_IDLE; u->gatherId = -1; return; }

        float d2 = sqdist(u->fx, u->fy, (float)dp->pos.x, (float)dp->pos.y);
        if (d2 < 6.0f) {
            int amt = 15;
            if (dp->resType == 1)      g->wood  += amt;
            else if (dp->resType == 2) g->gold  += amt;
            else                       g->stone += amt;

            dp->amount -= amt;
            if (dp->amount <= 0) {
                dp->active = 0;
                g->map[dp->pos.y][dp->pos.x] = make_tile(T_GRASS, 1, 1);
            }
            u->state = S_IDLE;
            u->gatherId = -1;
        } else {
            /* walk to a tile beside the deposit */
            Vec2i t = nearestWalkable(g, dp->pos.x, dp->pos.y);
            int plen = findPath(g, (Vec2i){u->gridX,u->gridY}, t, u->path, MAX_PATH_LEN);
            if (plen > 0) { u->pathLen = plen; u->pathIdx = 0; u->state = S_MOVING; }
            else { u->state = S_IDLE; u->gatherId = -1; }
        }
    }
}

static void updateBuilding(Building* b, Game* g, float dt) {
    if (b->buildState != BS_DONE) return;
    if (b->faction != FACTION_PLAYER) return;
    if (b->trainingUnit == 0) return;

    b->trainingProgress += (int)(dt * 2400.0f);
    if (b->trainingProgress >= TRAIN_TIME) {
        if (spawnTrainingUnit(g, b, (UnitType)b->trainingUnit)) {
            printf("[TRAIN] done\n");
        }
        b->trainingUnit = 0;
        b->trainingProgress = 0;
    }
}

static int canTrain(Game* g, UnitType utype) {
    int cost = (utype == U_VILLAGER) ? COST_VILLAGER_FOOD : COST_SOLDIER_FOOD;
    if (g->food < cost) return 0;
    if (g->popUsed >= g->popCap) return 0;
    if (g->unitCount >= MAX_UNITS) return 0;
    return 1;
}

static int canBuildAt(Game* g, BuildingType bt, Vec2i pos) {
    const BuildInfo* info = get_info(bt);
    if (info->width == 0) return 0;
    if (pos.x < 0 || pos.x + info->width > MAP_SIZE) return 0;
    if (pos.y < 0 || pos.y + info->height > MAP_SIZE) return 0;

    for (int yy = 0; yy < info->height; yy++)
        for (int xx = 0; xx < info->width; xx++)
            if (!g->map[pos.y+yy][pos.x+xx].buildable) return 0;

            for (int i = 0; i < g->buildCount; i++) {
                Building* b = &g->builds[i];
                if (pos.x < b->pos.x + b->width && pos.x + info->width > b->pos.x &&
                    pos.y < b->pos.y + b->height && pos.y + info->height > b->pos.y)
                    return 0;
            }
            return 1;
}

static int hasRes(Game* g, BuildingType bt) {
    const BuildInfo* info = get_info(bt);
    return g->wood >= info->w && g->gold >= info->g &&
    g->stone >= info->s && g->food >= info->f;
}

static void payCost(Game* g, BuildingType bt) {
    const BuildInfo* info = get_info(bt);
    g->wood  -= info->w;
    g->gold  -= info->g;
    g->stone -= info->s;
    g->food  -= info->f;
}

static void renderBuilding(Building* b, Game* g) {
    int sx, sy;
    worldToScreen(b->pos.x, b->pos.y, &sx, &sy, g->offsetX, g->offsetY, g->zoom);

    int w = (int)(b->width  * TILE_WIDTH  / 2 * g->zoom);
    int h = (int)(b->height * TILE_HEIGHT / 2 * g->zoom);

    Color col = (b->faction == FACTION_PLAYER)
    ? (Color){ 70, 180, 70, 255 }
    : (Color){ 235, 190, 30, 255 };

    if (b->buildState == BS_BUILDING) { col.r /= 2; col.g /= 2; col.b /= 2; }

    DrawRectangle(sx - w/2, sy - h/2, w, h, col);
    DrawRectangleLinesEx((Rectangle){ (float)(sx-w/2), (float)(sy-h/2), (float)w, (float)h },
                         1, (Color){0,0,0,90});

    if (b->buildState == BS_BUILDING) {
        int pbw = (int)(w * 0.8f);
        int pbh = (int)(5 * g->zoom);
        int pbx = sx - pbw/2;
        int pby = sy - h/2 - (int)(20 * g->zoom);
        DrawRectangle(pbx, pby, pbw, pbh, (Color){40,40,40,255});
        DrawRectangle(pbx, pby, (b->progress * pbw) / 10000, pbh, (Color){120,200,120,255});
    }

    if (b->trainingUnit > 0) {
        int tbw = (int)(w * 0.8f);
        int tbh = (int)(5 * g->zoom);
        int tbx = sx - tbw/2;
        int tby = sy - h/2 - (int)(36 * g->zoom);
        DrawRectangle(tbx, tby, tbw, tbh, (Color){30,30,40,255});
        DrawRectangle(tbx, tby, (b->trainingProgress * tbw) / 10000, tbh, (Color){120,220,130,255});
        DrawText(b->trainingUnit == U_VILLAGER ? "TRAINING VILLAGER" : "TRAINING SOLDIER",
                 sx - 48, tby - 14, 10, (Color){210,210,220,255});
    }

    if (b->hp < b->maxHp && b->buildState == BS_DONE) {
        int bw = (int)(w * 0.8f);
        int bh = (int)(5 * g->zoom);
        int bx = sx - bw/2;
        int by = sy - h/2 - (int)(14 * g->zoom);
        DrawRectangle(bx, by, bw, bh, RED);
        DrawRectangle(bx, by, (b->hp * bw) / b->maxHp, bh, GREEN);
    }

    if (b->selected)
        DrawRectangleLinesEx((Rectangle){ (float)(sx-w/2-3), (float)(sy-h/2-3),
            (float)(w+6), (float)(h+6) }, 2, WHITE);
}

static void renderUnit(Unit* u, Game* g) {
    if (u->state == S_DEAD) return;

    int sx, sy;
    worldToScreen(u->gridX, u->gridY, &sx, &sy, g->offsetX, g->offsetY, g->zoom);

    /* sub-tile offset for smooth motion */
    int offX = (int)((u->fx - (float)u->gridX) * (TILE_WIDTH  / 2.0f) * g->zoom);
    int offY = (int)((u->fy - (float)u->gridY) * (TILE_HEIGHT / 2.0f) * g->zoom);

    u->screenX = sx + offX;
    u->screenY = sy + offY;

    int r = (int)(9 * g->zoom);
    if (r < 3) r = 3;

    Color col;
    if (u->utype == U_VILLAGER)
        col = (u->faction == FACTION_PLAYER) ? (Color){130,225,140,255} : (Color){248,232,160,255};
    else
        col = (u->faction == FACTION_PLAYER) ? (Color){ 45,120, 55,255} : (Color){195,168, 45,255};

    DrawCircle(u->screenX, u->screenY, r, col);
    DrawCircleLines(u->screenX, u->screenY, r, (Color){0,0,0,120});

    if (u->selected) {
        DrawCircleLines(u->screenX, u->screenY, r + 4, WHITE);
        DrawCircleLines(u->screenX, u->screenY, r + 5, (Color){255,255,255,120});
    }
}

static void renderMarker(Game* g) {
    if (!g->markerActive) return;
    int sx, sy;
    worldToScreen(g->markerPos.x, g->markerPos.y, &sx, &sy, g->offsetX, g->offsetY, g->zoom);
    int r = (int)(14 * g->zoom);
    unsigned char a = (unsigned char)(255 * (g->markerTimer / 0.6f));
    if (a > 255) a = 255;
    DrawCircleLines(sx, sy, r, (Color){255,255,80,a});
    DrawCircleLines(sx, sy, r/2, (Color){255,255,80,a});
}

static void renderUI(Game* g) {
    DrawRectangle(0, WINDOW_HEIGHT - 88, WINDOW_WIDTH, 88, (Color){32,32,46,255});
    DrawRectangleLinesEx((Rectangle){0,(float)(WINDOW_HEIGHT-88),(float)WINDOW_WIDTH,88.0f},
                         2, (Color){70,70,95,255});

    int rx = 25, ry = WINDOW_HEIGHT - 68;
    const char* rn[] = {"Food","Wood","Gold","Stone"};
    int rv[] = {g->food, g->wood, g->gold, g->stone};
    Color rc[] = {
        (Color){255,120,120,255}, (Color){139,90,43,255},
        (Color){255,215,0,255},   (Color){180,180,180,255}
    };

    for (int i = 0; i < 4; i++) {
        DrawRectangle(rx, ry, 30, 30, rc[i]);
        DrawRectangleLines(rx, ry, 30, 30, (Color){0,0,0,130});
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", rv[i]);
        DrawText(buf, rx + 35, ry + 6, 17, (Color){235,235,235,255});
        DrawText(rn[i], rx + 35, ry + 25, 11, (Color){165,165,175,255});
        rx += 88;
    }

    int px = rx + 10;
    DrawRectangle(px - 6, ry - 8, 126, 56, (Color){46,46,66,255});
    DrawRectangleLines(px - 6, ry - 8, 126, 56, (Color){90,95,125,255});
    DrawText("POPULATION", px, ry - 3, 11, (Color){160,168,195,255});
    char pb[32];
    snprintf(pb, sizeof(pb), "%d / %d", g->popUsed, g->popCap);
    Color pc = (g->popUsed >= g->popCap) ? (Color){255,108,108,255} : (Color){150,232,150,255};
    DrawText(pb, px, ry + 13, 21, pc);

    DrawText("BUILD 16 - SPAWN + MOVE FIXED", 10, 10, 14, (Color){110,205,255,255});
    DrawText("Left-click select | Right-click move/gather | WASD pan | Wheel zoom",
             10, 30, 12, (Color){140,145,165,255});

    if (g->placing) {
        DrawText("PLACING - left-click to confirm, ESC to cancel",
                 430, WINDOW_HEIGHT - 116, 15, YELLOW);
    }

    /* ---- TRAINING PANEL ---- */
    if (g->selBuild && g->selBuild->btype == B_TOWNCENTER && g->selBuild->buildState == BS_DONE) {
        int ux = 460, uy = WINDOW_HEIGHT - 208;
        DrawRectangle(ux, uy, 560, 104, (Color){48,48,68,255});
        DrawRectangleLinesEx((Rectangle){(float)ux,(float)uy,560.0f,104.0f}, 2, (Color){90,140,90,255});
        DrawText("TRAIN UNITS", ux + 12, uy + 7, 14, (Color){110,210,110,255});

        Vector2 mp = GetMousePosition();

        Rectangle vbtn = { (float)(ux+12), (float)(uy+28), 264.0f, 66.0f };
        bool vhov = CheckCollisionPointRec(mp, vbtn);
        bool vok  = canTrain(g, U_VILLAGER);
        Color vc = vok ? (vhov ? (Color){100,190,100,255} : (Color){72,150,74,255})
        : (Color){58,56,64,255};
        DrawRectangleRec(vbtn, vc);
        DrawRectangleLinesEx(vbtn, vhov ? 2.0f : 1.0f, vhov ? WHITE : (Color){30,30,42,255});
        DrawText("VILLAGER", ux + 20, uy + 34, 12, vok ? WHITE : (Color){130,130,140,255});
        DrawText("50 Food", ux + 20, uy + 50, 10, vok ? (Color){205,205,215,255} : (Color){120,120,130,255});
        if (vhov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && vok) {
            g->food -= COST_VILLAGER_FOOD;
            g->selBuild->trainingUnit = U_VILLAGER;
            g->selBuild->trainingProgress = 0;
            printf("[QUEUE] villager\n");
        }

        int sx = ux + 284;
        Rectangle sbtn = { (float)sx, (float)(uy+28), 264.0f, 66.0f };
        bool shov = CheckCollisionPointRec(mp, sbtn);
        bool sok  = canTrain(g, U_SOLDIER);
        Color sc = sok ? (shov ? (Color){100,190,100,255} : (Color){72,150,74,255})
        : (Color){58,56,64,255};
        DrawRectangleRec(sbtn, sc);
        DrawRectangleLinesEx(sbtn, shov ? 2.0f : 1.0f, shov ? WHITE : (Color){30,30,42,255});
        DrawText("SOLDIER", sx + 20, uy + 34, 12, sok ? WHITE : (Color){130,130,140,255});
        DrawText("80 Food", sx + 20, uy + 50, 10, sok ? (Color){205,205,215,255} : (Color){120,120,130,255});
        if (shov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && sok) {
            g->food -= COST_SOLDIER_FOOD;
            g->selBuild->trainingUnit = U_SOLDIER;
            g->selBuild->trainingProgress = 0;
            printf("[QUEUE] soldier\n");
        }

        if (g->selBuild->trainingUnit > 0) {
            char tr[64];
            snprintf(tr, sizeof(tr), "Training %s - %d%%",
                     g->selBuild->trainingUnit == U_VILLAGER ? "VILLAGER" : "SOLDIER",
                     (g->selBuild->trainingProgress * 100) / 10000);
            DrawText(tr, ux + 292, uy + 94, 10, (Color){185,205,185,255});
        }
    }

    /* ---- SELECTED UNIT INFO ---- */
    int selUnits = 0;
    for (int i = 0; i < g->unitCount; i++)
        if (g->units[i].selected && g->units[i].state != S_DEAD) selUnits++;

        if (selUnits > 0 && !g->selBuild) {
            DrawRectangle(460, WINDOW_HEIGHT - 132, 380, 40, (Color){48,48,68,235});
            char sb[64];
            snprintf(sb, sizeof(sb), "%d unit%s selected", selUnits, selUnits == 1 ? "" : "s");
            DrawText(sb, 472, WINDOW_HEIGHT - 123, 15, (Color){200,210,230,255});
            DrawText("Right-click ground to move, resource to gather",
                     470, WINDOW_HEIGHT - 137, 11, (Color){150,155,175,255});
        }

        if (g->paused) {
            DrawRectangle(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, (Color){0,0,0,120});
            DrawText("PAUSED", WINDOW_WIDTH/2 - 62, WINDOW_HEIGHT/2, 30, WHITE);
        }
}

static void handleInput(Game* g) {
    if (IsKeyPressed(KEY_ESCAPE)) {
        if (g->placing) { g->placing = 0; g->placeType = B_NONE; }
        else CloseWindow();
    }
    if (IsKeyPressed(KEY_SPACE)) g->paused = !g->paused;

    if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP))    g->offsetY += 18.0f;
    if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN))  g->offsetY -= 18.0f;
    if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT))  g->offsetX += 18.0f;
    if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) g->offsetX -= 18.0f;

    float wheel = GetMouseWheelMove();
    if (wheel != 0.0f) {
        g->zoom += (wheel > 0) ? 0.08f : -0.08f;
        if (g->zoom < 0.5f) g->zoom = 0.5f;
        if (g->zoom > 2.2f) g->zoom = 2.2f;
    }

    g->mouseX = (int)GetMousePosition().x;
    g->mouseY = (int)GetMousePosition().y;

    /* ---- BUILDING PLACEMENT MODE ---- */
    if (g->placing) {
        int gx, gy;
        screenToWorld(g->mouseX, g->mouseY, &gx, &gy, g->offsetX, g->offsetY, g->zoom);
        g->placePos = (Vec2i){ gx, gy };

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (canBuildAt(g, g->placeType, g->placePos) && hasRes(g, g->placeType)) {
                payCost(g, g->placeType);
                int idx = g->buildCount;
                addBuilding(g, FACTION_PLAYER, g->placeType, g->placePos.x, g->placePos.y, BS_BUILDING);

                /* send nearest idle villager to build */
                int bestV = -1;
                float bestD = 999999.0f;
                for (int i = 0; i < g->unitCount; i++) {
                    if (g->units[i].faction != FACTION_PLAYER) continue;
                    if (g->units[i].utype != U_VILLAGER) continue;
                    if (g->units[i].state != S_IDLE) continue;
                    float d = sqdist(g->units[i].fx, g->units[i].fy,
                                     (float)g->placePos.x, (float)g->placePos.y);
                    if (d < bestD) { bestD = d; bestV = i; }
                }
                if (bestV >= 0) {
                    g->units[bestV].buildId = idx;
                    g->units[bestV].state = S_BUILD;
                }
            }
            g->placing = 0;
            g->placeType = B_NONE;
        }
        return;
    }

    /* ---- LEFT CLICK: start selection ---- */
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        g->selecting = 1;
        g->selX1 = g->mouseX;
        g->selY1 = g->mouseY;
    }

    /* ---- LEFT RELEASE: finish selection ---- */
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        g->selecting = 0;

        int minx = (g->selX1 < g->mouseX) ? g->selX1 : g->mouseX;
        int maxx = (g->selX1 < g->mouseX) ? g->mouseX : g->selX1;
        int miny = (g->selY1 < g->mouseY) ? g->selY1 : g->mouseY;
        int maxy = (g->selY1 < g->mouseY) ? g->mouseY : g->selY1;

        /* Did the click land on the training panel? */
        int panelHit = 0;
        if (g->selBuild && g->selBuild->btype == B_TOWNCENTER && g->selBuild->buildState == BS_DONE) {
            int ux = 460, uy = WINDOW_HEIGHT - 208;
            if (g->mouseX >= ux && g->mouseX <= ux + 560 &&
                g->mouseY >= uy && g->mouseY <= uy + 104) panelHit = 1;
        }
        if (panelHit) return;

        int isClick = (maxx - minx < 6 && maxy - miny < 6);

        /* clear old selection */
        for (int i = 0; i < g->unitCount; i++) g->units[i].selected = 0;
        if (g->selBuild) { g->selBuild->selected = 0; g->selBuild = NULL; }

        if (isClick) {
            /* try building first */
            int foundBuild = 0;
            for (int i = g->buildCount - 1; i >= 0; i--) {
                if (g->builds[i].faction != FACTION_PLAYER) continue;
                int sx, sy;
                worldToScreen(g->builds[i].pos.x, g->builds[i].pos.y, &sx, &sy,
                              g->offsetX, g->offsetY, g->zoom);
                int w = (int)(g->builds[i].width  * TILE_WIDTH  / 2 * g->zoom);
                int h = (int)(g->builds[i].height * TILE_HEIGHT / 2 * g->zoom);
                if (abs(g->mouseX - sx) < w/2 + 12 && abs(g->mouseY - sy) < h/2 + 12) {
                    g->selBuild = &g->builds[i];
                    g->builds[i].selected = 1;
                    foundBuild = 1;
                    break;
                }
            }
            /* otherwise pick nearest unit */
            if (!foundBuild) {
                int bestU = -1;
                float bestD = 28.0f * 28.0f;
                for (int i = 0; i < g->unitCount; i++) {
                    if (g->units[i].faction != FACTION_PLAYER) continue;
                    if (g->units[i].state == S_DEAD) continue;
                    float d = sqdist((float)g->mouseX, (float)g->mouseY,
                                     (float)g->units[i].screenX, (float)g->units[i].screenY);
                    if (d < bestD) { bestD = d; bestU = i; }
                }
                if (bestU >= 0) g->units[bestU].selected = 1;
            }
        } else {
            /* box select units */
            for (int i = 0; i < g->unitCount; i++) {
                if (g->units[i].faction != FACTION_PLAYER) continue;
                if (g->units[i].state == S_DEAD) continue;
                if (g->units[i].screenX >= minx && g->units[i].screenX <= maxx &&
                    g->units[i].screenY >= miny && g->units[i].screenY <= maxy)
                    g->units[i].selected = 1;
            }
        }
    }

    /* ---- RIGHT CLICK: issue orders ---- */
    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
        int gx, gy;
        screenToWorld(g->mouseX, g->mouseY, &gx, &gy, g->offsetX, g->offsetY, g->zoom);

        /* count selected */
        int selCount = 0;
        for (int i = 0; i < g->unitCount; i++)
            if (g->units[i].selected && g->units[i].state != S_DEAD &&
                g->units[i].faction == FACTION_PLAYER) selCount++;

        if (selCount == 0) return;

        /* destination marker feedback */
        Vec2i dest = nearestWalkable(g, gx, gy);
        g->markerActive = 1;
        g->markerPos = dest;
        g->markerTimer = 0.6f;

        int idx = 0;
        for (int i = 0; i < g->unitCount; i++) {
            Unit* u = &g->units[i];
            if (!u->selected || u->state == S_DEAD || u->faction != FACTION_PLAYER) continue;

            /* gather order if a villager clicked a resource */
            int ordered = 0;
            if (u->utype == U_VILLAGER) {
                for (int d = 0; d < g->depCount; d++) {
                    if (!g->deps[d].active) continue;
                    if (abs(gx - g->deps[d].pos.x) <= 1 && abs(gy - g->deps[d].pos.y) <= 1) {
                        u->gatherId = d;
                        u->state = S_GATHER;
                        Vec2i t = nearestWalkable(g, g->deps[d].pos.x, g->deps[d].pos.y);
                        int plen = findPath(g, (Vec2i){u->gridX,u->gridY}, t, u->path, MAX_PATH_LEN);
                        if (plen > 0) { u->pathLen = plen; u->pathIdx = 0; }
                        ordered = 1;
                        break;
                    }
                }
            }
            if (ordered) { idx++; continue; }

            /* spread formation around destination */
            int offX = 0, offY = 0;
            if (selCount > 1) {
                int cols = (int)ceilf(sqrtf((float)selCount));
                offX = (idx % cols) - cols/2;
                offY = (idx / cols) - cols/2;
            }

            Vec2i want = { dest.x + offX, dest.y + offY };
            Vec2i t = nearestWalkable(g, want.x, want.y);

            int plen = findPath(g, (Vec2i){u->gridX,u->gridY}, t, u->path, MAX_PATH_LEN);
            if (plen > 0) {
                u->pathLen = plen;
                u->pathIdx = 0;
                u->state = S_MOVING;
                u->gatherId = -1;
                u->buildId = -1;
                printf("[ORDER] unit %d -> (%d,%d) pathlen=%d\n", u->id, t.x, t.y, plen);
            } else {
                printf("[ORDER FAIL] unit %d at (%d,%d) -> (%d,%d)\n",
                       u->id, u->gridX, u->gridY, t.x, t.y);
            }
            idx++;
        }
    }
}

int main(void) {
    printf("\n========================================\n");
    printf("  Medieval Kingdoms - BUILD 16\n");
    printf("  Spawn + Movement fixed\n");
    printf("========================================\n\n");

    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Medieval Kingdoms");
    SetTargetFPS(FPS);

    Game game;
    initGame(&game);

    printf("INIT: food=%d wood=%d gold=%d stone=%d\n",
           game.food, game.wood, game.gold, game.stone);
    printf("Units=%d Buildings=%d Pop=%d/%d\n\n",
           game.unitCount, game.buildCount, game.popUsed, game.popCap);

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        if (dt > 0.1f) dt = 0.1f;

        if (!game.paused) {
            for (int i = 0; i < game.unitCount; i++)
                updateUnit(&game.units[i], &game, dt);
            for (int i = 0; i < game.buildCount; i++)
                updateBuilding(&game.builds[i], &game, dt);

            if (game.markerActive) {
                game.markerTimer -= dt;
                if (game.markerTimer <= 0.0f) game.markerActive = 0;
            }
        }

        handleInput(&game);
        updatePopulation(&game);

        BeginDrawing();
        ClearBackground((Color){16,16,26,255});

        /* terrain */
        for (int gy = 0; gy < MAP_SIZE; gy++) {
            for (int gx = 0; gx < MAP_SIZE; gx++) {
                int sx, sy;
                worldToScreen(gx, gy, &sx, &sy, game.offsetX, game.offsetY, game.zoom);
                drawTile(sx, sy, game.map[gy][gx].type, game.zoom);
            }
        }

        renderMarker(&game);

        /* buildings then units (simple depth order) */
        for (int i = 0; i < game.buildCount; i++)
            renderBuilding(&game.builds[i], &game);
        for (int i = 0; i < game.unitCount; i++)
            renderUnit(&game.units[i], &game);

        /* selection rectangle */
        if (game.selecting) {
            int minx = (game.selX1 < game.mouseX) ? game.selX1 : game.mouseX;
            int maxx = (game.selX1 < game.mouseX) ? game.mouseX : game.selX1;
            int miny = (game.selY1 < game.mouseY) ? game.selY1 : game.mouseY;
            int maxy = (game.selY1 < game.mouseY) ? game.mouseY : game.selY1;
            DrawRectangleLines(minx, miny, maxx-minx, maxy-miny, (Color){120,255,120,200});
        }

        renderUI(&game);

        /* minimap */
        int mmx = WINDOW_WIDTH - 156, mmy = WINDOW_HEIGHT - 224, mms = 136;
        DrawRectangle(mmx, mmy, mms, mms, (Color){38,38,50,255});
        float scl = (float)mms / MAP_SIZE;

        for (int i = 0; i < game.buildCount; i++) {
            int x = mmx + (int)(game.builds[i].pos.x * scl);
            int y = mmy + (int)(game.builds[i].pos.y * scl);
            Color c = (game.builds[i].faction == FACTION_PLAYER)
            ? (Color){0,170,0,255} : (Color){200,170,0,255};
            DrawRectangle(x, y, (int)(game.builds[i].width*scl)+1,
                          (int)(game.builds[i].height*scl)+1, c);
        }
        for (int i = 0; i < game.unitCount; i++) {
            if (game.units[i].state == S_DEAD) continue;
            int x = mmx + (int)(game.units[i].gridX * scl);
            int y = mmy + (int)(game.units[i].gridY * scl);
            Color c = (game.units[i].faction == FACTION_PLAYER)
            ? (Color){80,255,80,255} : (Color){255,210,60,255};
            DrawCircle(x, y, 2, c);
        }
        DrawRectangleLines(mmx, mmy, mms, mms, (Color){120,120,140,255});
        DrawText("MINIMAP", mmx, mmy - 16, 12, (Color){170,175,195,255});

        EndDrawing();
    }

    CloseWindow();
    printf("\nGame closed.\n");
    return 0;
}
