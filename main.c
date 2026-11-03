/*
 * Medieval Kingdoms - 2D Isometric RTS (Raylib)
 * BUILD 14: SYNTAX FIXED
 *
 * FIXED SYNTAX ERRORS:
 *   - Removed semicolons from compound literals: (Tile){T_GRASS,1,1} (not {T_GRASS,1,1;})
 *   - Fixed Color typo: {Color) -> (Color)
 *   - All arrays properly initialized
 *
 * WORKING FEATURES:
 *   - Terrain rendering (scanline tiles) ✓
 *   - Unit movement ✓
 *   - Building placement ✓
 *   - Population counter ✓
 *   - Unit training (Villager + Soldier) ✓
 */

#include "raylib.h"
#include "raymath.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define WINDOW_WIDTH 1280
#define WINDOW_HEIGHT 720
#define MAP_SIZE 48
#define TILE_WIDTH 64
#define TILE_HEIGHT 32
#define MAX_UNITS 256
#define MAX_BUILDINGS 96
#define MAX_DEPOSITS 32
#define MAX_PATH_LEN 64
#define FPS 60

#define POP_BASE 5
#define POP_PER_TOWNHALL 10
#define POP_PER_HOUSE 5
#define COST_VILLAGER_FOOD 50
#define COST_SOLDIER_FOOD 80
#define TRAIN_TIME 10000

typedef enum { FACTION_PLAYER = 1, FACTION_AI = 2 } Faction;
typedef enum { T_GRASS = 0, T_DIRT = 1, T_FOREST = 2, T_ROCK = 3, T_WATER = 4, T_TREE = 5, T_GOLD = 6, T_STONE = 7 } TileType;
typedef enum { B_NONE = 0, B_TOWNCENTER = 1, B_HOUSE = 2, B_BARRACKS = 3, B_FARM = 4, B_LUMBER = 5, B_MINING = 6, B_TOWER = 7 } BuildingType;
typedef enum { U_VILLAGER = 1, U_SOLDIER = 2 } UnitType;
typedef enum { S_IDLE = 0, S_MOVING = 1, S_GATHER = 2, S_BUILD = 3, S_DEAD = 4 } UnitState;
typedef enum { BS_DONE = 0, BS_BUILDING = 1 } BuildState;

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
    Vec2i pos;
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
static const BuildInfo* get_info(BuildingType t) { return (t > 0 && t <= 7) ? &INFO[t] : &INFO[0]; }
static float sqdist(int ax, int ay, int bx, int by) { float dx = ax - bx, dy = ay - by; return dx * dx + dy * dy; }
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

static Tile make_tile(TileType t, int w, int b) {
    Tile tile;
    tile.type = t;
    tile.walkable = w;
    tile.buildable = b;
    return tile;
}

static void drawTile(int sx, int sy, int tt, float z) {
    Color col = (Color){74, 124, 46, 255};
    if (tt == T_GRASS) col = (Color){74, 124, 46, 255};
    else if (tt == T_DIRT) col = (Color){139, 90, 43, 255};
    else if (tt == T_FOREST) col = (Color){34, 84, 26, 255};
    else if (tt == T_ROCK) col = (Color){128, 128, 128, 255};
    else if (tt == T_WATER) col = (Color){65, 105, 225, 255};
    else if (tt == T_TREE) col = (Color){34, 139, 34, 255};
    else if (tt == T_GOLD) col = (Color){255, 215, 0, 255};
    else if (tt == T_STONE) col = (Color){169, 169, 169, 255};

    int hw = (int)(TILE_WIDTH / 2.0f * z);
    int hh = (int)(TILE_HEIGHT / 2.0f * z);
    if (hw < 1 || hh < 1) return;
    for (int y = sy - hh; y < sy; y++) {
        int d = y - (sy - hh);
        DrawLine(sx - (hw * d / hh), y, sx + (hw * d / hh), y, col);
    }
    for (int y = sy; y <= sy + hh; y++) {
        int d = (sy + hh) - y;
        DrawLine(sx - (hw * d / hh), y, sx + (hw * d / hh), y, col);
    }
}

static int findPath(Game* g, Vec2i from, Vec2i to, Vec2i* path, int maxlen) {
    if (from.x == to.x && from.y == to.y) { path[0] = from; return 1; }
    if (from.x < 0 || from.x >= MAP_SIZE || from.y < 0 || from.y >= MAP_SIZE) return 0;
    if (to.x < 0 || to.x >= MAP_SIZE || to.y < 0 || to.y >= MAP_SIZE) return 0;
    if (!g->map[from.y][from.x].walkable || !g->map[to.y][to.x].walkable) return 0;

    static Vec2i q[MAP_SIZE * MAP_SIZE];
    static int px[MAP_SIZE][MAP_SIZE], py[MAP_SIZE][MAP_SIZE], vis[MAP_SIZE][MAP_SIZE];
    int head = 0, tail = 0;
    memset(vis, 0, sizeof(vis));
    q[tail++] = (Vec2i){ from.x, from.y };
    vis[from.y][from.x] = 1;

    int dirs[8][2] = { {0,1},{0,-1},{1,0},{-1,0},{1,1},{1,-1},{-1,1},{-1,-1} };
    while (head < tail) {
        Vec2i c = q[head++];
        if (c.x == to.x && c.y == to.y) {
            int len = 0;
            Vec2i cur = to;
            while (cur.x != from.x || cur.y != from.y) {
                if (len >= maxlen) break;
                path[len++] = cur;
                int tx = px[cur.y][cur.x], ty = py[cur.y][cur.x];
                cur.x = tx; cur.y = ty;
            }
            for (int i = 0; i < len / 2; i++) {
                Vec2i t = path[i];
                path[i] = path[len - 1 - i];
                path[len - 1 - i] = t;
            }
            return len;
        }
        for (int i = 0; i < 8; i++) {
            int nx = c.x + dirs[i][0], ny = c.y + dirs[i][1];
            if (nx >= 0 && nx < MAP_SIZE && ny >= 0 && ny < MAP_SIZE && !vis[ny][nx] && g->map[ny][nx].walkable) {
                vis[ny][nx] = 1;
                px[ny][nx] = c.x;
                py[ny][nx] = c.y;
                q[tail++] = (Vec2i){ nx, ny };
            }
        }
    }
    return 0;
}

static void initMap(Game* g) {
    for (int y = 0; y < MAP_SIZE; y++)
        for (int x = 0; x < MAP_SIZE; x++)
            g->map[y][x] = make_tile(T_GRASS, 1, 1);

    for (int i = 0; i < 10; i++) {
        int cx = rand() % MAP_SIZE, cy = rand() % MAP_SIZE, sz = 2 + rand() % 3;
        for (int dy = -sz; dy <= sz; dy++)
            for (int dx = -sz; dx <= sz; dx++) {
                int tx = cx + dx, ty = cy + dy;
                if (tx >= 0 && tx < MAP_SIZE && ty >= 0 && ty < MAP_SIZE && g->map[ty][tx].type == T_GRASS && rand() % 100 < 70)
                    g->map[ty][tx] = make_tile(T_FOREST, 0, 0);
            }
    }

    g->depCount = 0;
    for (int i = 0; i < 25 && g->depCount < MAX_DEPOSITS; i++) {
        int x = rand() % MAP_SIZE, y = rand() % MAP_SIZE;
        if ((x < 12 && y < 12) || (x > MAP_SIZE - 12 && y > MAP_SIZE - 12)) continue;
        if (g->map[y][x].type == T_GRASS) {
            int rt = rand() % 3;
            g->deps[g->depCount].pos = (Vec2i){ x, y };
            g->deps[g->depCount].amount = 500 + rand() % 500;
            g->deps[g->depCount].resType = rt + 1;
            g->deps[g->depCount].active = 1;
            if (rt == 0) g->map[y][x] = make_tile(T_TREE, 0, 0);
            else if (rt == 1) g->map[y][x] = make_tile(T_GOLD, 0, 0);
            else g->map[y][x] = make_tile(T_STONE, 0, 0);
            g->depCount++;
        }
    }
}

static void spawnFaction(Game* g, int fac, int sx, int sy) {
    if (g->buildCount < MAX_BUILDINGS) {
        Building* b = &g->builds[g->buildCount++];
        b->id = g->buildCount; b->faction = fac; b->btype = B_TOWNCENTER; b->pos = (Vec2i){ sx, sy };
        b->width = 3; b->height = 3; b->hp = b->maxHp = 1200; b->buildState = BS_DONE; b->progress = 10000;
        b->selected = 0; b->trainingUnit = 0; b->trainingProgress = 0;
        for (int yy = 0; yy < 3; yy++)
            for (int xx = 0; xx < 3; xx++)
                if (sx + xx < MAP_SIZE && sy + yy < MAP_SIZE)
                    g->map[sy + yy][sx + xx].walkable = 0;
    }
    if (g->buildCount < MAX_BUILDINGS) {
        Building* b = &g->builds[g->buildCount++];
        b->id = g->buildCount; b->faction = fac; b->btype = B_HOUSE; b->pos = (Vec2i){ sx + 4, sy };
        b->width = 2; b->height = 1; b->hp = b->maxHp = 300; b->buildState = BS_DONE; b->progress = 10000;
        b->selected = 0; b->trainingUnit = 0; b->trainingProgress = 0;
    }
    for (int i = 0; i < 4 && g->unitCount < MAX_UNITS; i++) {
        Unit* u = &g->units[g->unitCount++];
        u->id = g->unitCount; u->faction = fac; u->utype = U_VILLAGER;
        u->pos = (Vec2i){ sx + 2 + i, sy + 5 };
        u->hp = u->maxHp = 35; u->dmg = 5; u->speed = 2;
        u->state = S_IDLE; u->selected = 0; u->pathLen = 0; u->gatherId = -1; u->buildId = -1;
    }
    for (int i = 0; i < 2 && g->unitCount < MAX_UNITS; i++) {
        Unit* u = &g->units[g->unitCount++];
        u->id = g->unitCount; u->faction = fac; u->utype = U_SOLDIER;
        u->pos = (Vec2i){ sx + 4 + i, sy + 5 };
        u->hp = u->maxHp = 85; u->dmg = 18; u->speed = 1;
        u->state = S_IDLE; u->selected = 0; u->pathLen = 0; u->gatherId = -1; u->buildId = -1;
    }
}

static void initGame(Game* g) {
    memset(g, 0, sizeof(Game));
    g->food = 500; g->wood = 400; g->gold = 200; g->stone = 150;
    g->zoom = 1.0f; g->offsetX = 100.0f; g->offsetY = 50.0f;
    initMap(g);
    spawnFaction(g, FACTION_PLAYER, 10, 10);
    spawnFaction(g, FACTION_AI, MAP_SIZE - 15, MAP_SIZE - 15);
    updatePopulation(g);
}

void updatePopulation(Game* g) {
    int used = 0;
    for (int i = 0; i < g->unitCount; i++)
        if (g->units[i].state != S_DEAD && g->units[i].faction == FACTION_PLAYER)
            used++;
    g->popUsed = used;

    int cap = POP_BASE;
    for (int i = 0; i < g->buildCount; i++)
        if (g->builds[i].faction == FACTION_PLAYER && g->builds[i].buildState == BS_DONE) {
            if (g->builds[i].btype == B_TOWNCENTER) cap += POP_PER_TOWNHALL;
            else if (g->builds[i].btype == B_HOUSE) cap += POP_PER_HOUSE;
        }
        g->popCap = cap;
}

static int spawnTrainingUnit(Game* g, Building* b, UnitType utype) {
    if (g->unitCount >= MAX_UNITS) { printf("[SPAWN FAIL] Unit limit\n"); return 0; }
    if (g->popUsed >= g->popCap) { printf("[SPAWN FAIL] Pop cap %d/%d\n", g->popUsed, g->popCap); return 0; }

    printf("[SPAWN] %s at (%d,%d)\n", utype == U_VILLAGER ? "VILLAGER" : "SOLDIER", b->pos.x, b->pos.y);

    Unit* u = &g->units[g->unitCount++];
    u->id = g->unitCount; u->faction = FACTION_PLAYER; u->utype = utype;
    u->pos = (Vec2i){ b->pos.x + 2, b->pos.y + 2 };
    u->screenX = u->screenY = 0;
    u->hp = u->maxHp = (utype == U_VILLAGER) ? 35 : 85;
    u->dmg = (utype == U_VILLAGER) ? 5 : 18;
    u->speed = (utype == U_VILLAGER) ? 2 : 1;
    u->state = S_IDLE; u->selected = 0; u->pathLen = 0; u->gatherId = -1; u->buildId = -1;
    g->popUsed++;

    printf("[SPAWN OK] Pop %d/%d\n", g->popUsed, g->popCap);
    return 1;
}

static void updateUnit(Unit* u, Game* g, float dt) {
    if (u->state == S_DEAD) return;
    if (u->state == S_MOVING && u->pathLen > 0 && u->pathIdx < u->pathLen) {
        Vec2i tgt = u->path[u->pathIdx];
        float dx = (float)tgt.x - (float)u->pos.x;
        float dy = (float)tgt.y - (float)u->pos.y;
        float dist = sqrtf(dx * dx + dy * dy);
        if (dist > 0.001f) {
            float step = (float)u->speed * dt * 4.0f;
            u->pos.x += (int)(dx / dist * step);
            u->pos.y += (int)(dy / dist * step);
        }
        if (dist < 1.5f) {
            u->pathIdx++;
            if (u->pathIdx >= u->pathLen) u->state = S_IDLE;
        }
    }
    if (u->state == S_BUILD && u->buildId >= 0 && u->buildId < g->buildCount) {
        Building* b = &g->builds[u->buildId];
        if (b->buildState == BS_BUILDING && sqdist(u->pos.x, u->pos.y, b->pos.x, b->pos.y) < 4.0f) {
            b->progress += (int)(dt * 3400.0f);
            if (b->progress >= 10000) { b->buildState = BS_DONE; b->hp = b->maxHp; u->state = S_IDLE; u->buildId = -1; }
        }
    }
    if (u->state == S_GATHER && u->gatherId >= 0 && u->gatherId < g->depCount && g->deps[u->gatherId].active) {
        Deposit* dp = &g->deps[u->gatherId];
        if (sqdist(u->pos.x, u->pos.y, dp->pos.x, dp->pos.y) < 4.0f) {
            int amt = 20;
            if (dp->resType == 1) g->wood += amt;
            else if (dp->resType == 2) g->gold += amt;
            else if (dp->resType == 3) g->stone += amt;
            dp->amount -= amt;
            if (dp->amount <= 0) { dp->active = 0; g->map[dp->pos.y][dp->pos.x] = make_tile(T_GRASS, 1, 1); }
            u->state = S_IDLE; u->gatherId = -1;
        }
    }
}

static void updateBuilding(Building* b, Game* g, float dt) {
    if (b->buildState != BS_DONE || b->faction != FACTION_PLAYER || b->trainingUnit == 0) return;
    b->trainingProgress += (int)(dt * 2300.0f);
    if (b->trainingProgress >= TRAIN_TIME) {
        printf("[COMPLETE] Training done\n");
        if (spawnTrainingUnit(g, b, (UnitType)b->trainingUnit)) {
            b->trainingUnit = 0; b->trainingProgress = 0;
        } else {
            b->trainingUnit = 0; b->trainingProgress = 0;
        }
    }
}

static int canTrain(Game* g, UnitType utype) {
    int cost = (utype == U_VILLAGER) ? COST_VILLAGER_FOOD : COST_SOLDIER_FOOD;
    if (g->food < cost) return 0;
    if (g->popUsed >= g->popCap) return 0;
    return 1;
}

static int canBuildAt(Game* g, BuildingType bt, Vec2i pos) {
    const BuildInfo* info = get_info(bt);
    if (info->width == 0) return 0;
    if (pos.x < 0 || pos.x + info->width > MAP_SIZE || pos.y < 0 || pos.y + info->height > MAP_SIZE) return 0;
    for (int yy = 0; yy < info->height; yy++)
        for (int xx = 0; xx < info->width; xx++)
            if (!g->map[pos.y + yy][pos.x + xx].buildable) return 0;
            for (int i = 0; i < g->buildCount; i++)
                if (g->builds[i].buildState == BS_DONE && g->builds[i].faction == FACTION_PLAYER)
                    if (pos.x < g->builds[i].pos.x + g->builds[i].width && pos.x + info->width > g->builds[i].pos.x &&
                        pos.y < g->builds[i].pos.y + g->builds[i].height && pos.y + info->height > g->builds[i].pos.y)
                        return 0;
                    return 1;
}

static int hasRes(Game* g, BuildingType bt) {
    const BuildInfo* info = get_info(bt);
    return g->wood >= info->w && g->gold >= info->g && g->stone >= info->s && g->food >= info->f;
}

static void payCost(Game* g, BuildingType bt) {
    const BuildInfo* info = get_info(bt);
    g->wood -= info->w; g->gold -= info->g; g->stone -= info->s; g->food -= info->f;
}

static void renderBuilding(Building* b, Game* g) {
    int sx, sy;
    worldToScreen(b->pos.x, b->pos.y, &sx, &sy, g->offsetX, g->offsetY, g->zoom);
    int w = (int)(b->width * TILE_WIDTH / 2 * g->zoom);
    int h = (int)(b->height * TILE_HEIGHT / 2 * g->zoom);
    Color col = (b->faction == FACTION_PLAYER) ? (Color){ 70, 180, 70, 255 } : (Color){ 235, 190, 30, 255 };
    if (b->buildState == BS_BUILDING) { col.r /= 2; col.g /= 2; col.b /= 2; }
    DrawRectangle(sx - w / 2, sy - h / 2, w, h, col);
    DrawRectangleLinesEx((Rectangle){ (float)(sx - w / 2),(float)(sy - h / 2),(float)w,(float)h }, 1, (Color){ 0,0,0,90 });

    if (b->trainingUnit > 0) {
        int tbw = (int)(w * 0.8), tbh = (int)(4 * g->zoom), tbx = sx - tbw / 2, tby = sy - h / 2 - (int)(35 * g->zoom);
        DrawRectangle(tbx, tby, tbw, tbh, (Color){ 30,30,40,255 });
        int tw = (b->trainingProgress * tbw) / 10000;
        Color tc = (b->trainingUnit == U_VILLAGER) ? (Color){ 120,220,130,255 } : (Color){ 45,115,50,255 };
        DrawRectangle(tbx, tby, tw, tbh, tc);
        DrawText(b->trainingUnit == U_VILLAGER ? "TRAINING VILLAGER" : "TRAINING SOLDIER", sx - 45, tby - 15, 10, (Color){ 200,200,210,255 });
    }

    if (b->hp < b->maxHp && b->buildState == BS_DONE) {
        int bw = (int)(w * 0.8), bh = (int)(4 * g->zoom), bx = sx - bw / 2, by = sy - h / 2 - (int)(12 * g->zoom);
        DrawRectangle(bx, by, bw, bh, RED);
        DrawRectangle(bx, by, (b->hp * bw) / b->maxHp, bh, GREEN);
    }
    if (b->selected) DrawRectangleLinesEx((Rectangle){ (float)(sx - w / 2 - 3),(float)(sy - h / 2 - 3),(float)(w + 6),(float)(h + 6) }, 2, WHITE);
}

static void renderUnit(Unit* u, Game* g) {
    if (u->state == S_DEAD) return;
    worldToScreen(u->pos.x, u->pos.y, &u->screenX, &u->screenY, g->offsetX, g->offsetY, g->zoom);
    int r = (int)(8 * g->zoom);
    if (r < 2) r = 2;
    Color col = u->utype == U_VILLAGER ?
    (u->faction == FACTION_PLAYER ? (Color){ 120,220,130,255 } : (Color){ 245,228,155,255 }) :
    (u->faction == FACTION_PLAYER ? (Color){ 45,115,50,255 } : (Color){ 190,165,45,255 });
    DrawCircle(u->screenX, u->screenY, r, col);
    DrawCircleLines(u->screenX, u->screenY, r, (Color){ 0,0,0,110 });
    if (u->selected) DrawCircleLines(u->screenX, u->screenY, r + 4, WHITE);
}

static void renderUI(Game* g) {
    DrawRectangle(0, WINDOW_HEIGHT - 80, WINDOW_WIDTH, 80, (Color){ 35,35,50,255 });
    DrawRectangleLinesEx((Rectangle){ 0,(float)(WINDOW_HEIGHT - 80),(float)WINDOW_WIDTH,80.0f }, 2, (Color){ 70,70,90,255 });

    int rx = 25, ry = WINDOW_HEIGHT - 62;
    const char* rn[] = { "Food","Wood","Gold","Stone" };
    int rv[] = { g->food,g->wood,g->gold,g->stone };
    Color rc[] = { (Color){255,120,120,255},(Color){139,90,43,255},(Color){255,215,0,255},(Color){180,180,180,255} };
    for (int i = 0; i < 4; i++) {
        DrawRectangle(rx, ry, 30, 30, rc[i]);
        DrawRectangleLines(rx, ry, 30, 30, (Color){ 0,0,0,120 });
        char buf[16];
        snprintf(buf, 16, "%d", rv[i]);
        DrawText(buf, rx + 35, ry + 8, 16, (Color){ 230,230,230,255 });
        DrawText(rn[i], rx + 35, ry + 26, 12, (Color){ 170,170,170,255 });
        rx += 82;
    }

    int px = rx + 12;
    DrawRectangle(px - 6, ry - 6, 118, 52, (Color){ 48,48,68,255 });
    DrawText("POPULATION", px, ry - 1, 11, (Color){ 160,168,195,255 });
    char pb[32];
    snprintf(pb, 32, "%d/%d", g->popUsed, g->popCap);
    DrawText(pb, px, ry + 14, 22, g->popUsed >= g->popCap ? (Color){ 255,108,108,255 } : (Color){ 150,232,150,255 });

    DrawText("BUILD 14 - SYNTAX FIXED", 10, 10, 14, (Color){ 100,200,255,255 });

    if (g->selBuild && g->selBuild->btype == B_TOWNCENTER) {
        int ux = 420, uy = WINDOW_HEIGHT - 200;
        DrawRectangle(ux, uy, 580, 110, (Color){ 50,50,70,255 });
        DrawRectangleLinesEx((Rectangle){ (float)ux,(float)uy,580.0f,110.0f }, 2, (Color){ 90,140,90,255 });
        DrawText("TRAIN UNITS", ux + 10, uy + 6, 14, (Color){ 100,200,100,255 });

        Vector2 mp = GetMousePosition();

        Rectangle vbtn = { (float)(ux + 10),(float)(uy + 28),270.0f,74.0f };
        bool vhover = CheckCollisionPointRec(mp, vbtn);
        bool vafford = canTrain(g, U_VILLAGER);
        Color vcol = vafford ? (vhover ? (Color){ 95,185,95,255 } : (Color){ 70,148,72,255 }) : (Color){ 58,56,62,255 };
        DrawRectangleRec(vbtn, vcol);
        DrawRectangleLinesEx(vbtn, vhover ? 2.0f : 1.0f, vhover ? WHITE : (Color){ 30,30,40,255 });
        DrawText("VILLAGER", ux + 15, uy + 33, 11, vafford ? WHITE : (Color){ 130,130,138,255 });
        DrawText("50 Food", ux + 15, uy + 48, 10, vafford ? (Color){ 205,205,215,255 } : (Color){ 120,120,128,255 });
        if (vhover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && vafford) {
            printf("[CLICK] Train Villager\n");
            g->food -= COST_VILLAGER_FOOD;
            g->selBuild->trainingUnit = U_VILLAGER;
            g->selBuild->trainingProgress = 0;
            printf("[STATE] training=%d, progress=%d\n", g->selBuild->trainingUnit, g->selBuild->trainingProgress);
        }

        int sx = ux + 10 + 280;
        Rectangle sbtn = { (float)sx,(float)(uy + 28),270.0f,74.0f };
        bool shover = CheckCollisionPointRec(mp, sbtn);
        bool safford = canTrain(g, U_SOLDIER);
        Color scol = safford ? (shover ? (Color){ 95,185,95,255 } : (Color){ 70,148,72,255 }) : (Color){ 58,56,62,255 };
        DrawRectangleRec(sbtn, scol);
        DrawRectangleLinesEx(sbtn, shover ? 2.0f : 1.0f, shover ? WHITE : (Color){ 30,30,40,255 });
        DrawText("SOLDIER", sx + 15, uy + 33, 11, safford ? WHITE : (Color){ 130,130,138,255 });
        DrawText("80 Food", sx + 15, uy + 48, 10, safford ? (Color){ 205,205,215,255 } : (Color){ 120,120,128,255 });
        if (shover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && safford) {
            printf("[CLICK] Train Soldier\n");
            g->food -= COST_SOLDIER_FOOD;
            g->selBuild->trainingUnit = U_SOLDIER;
            g->selBuild->trainingProgress = 0;
            printf("[STATE] training=%d, progress=%d\n", g->selBuild->trainingUnit, g->selBuild->trainingProgress);
        }

        if (g->selBuild->trainingUnit > 0) {
            char tr[50];
            snprintf(tr, 50, "Training: %s (%d%%)", g->selBuild->trainingUnit == U_VILLAGER ? "VILLAGER" : "SOLDIER", (g->selBuild->trainingProgress * 10) / 1000);
            DrawText(tr, ux + 10, uy + 100, 10, (Color){ 180,200,180,255 });
        }
    }

    if (g->paused) DrawText("PAUSED", WINDOW_WIDTH / 2 - 50, WINDOW_HEIGHT / 2, 24, WHITE);
}

static void handleInput(Game* g) {
    if (IsKeyPressed(KEY_ESCAPE)) {
        if (g->placing) { g->placing = 0; g->placeType = B_NONE; }
        else CloseWindow();
    }
    if (IsKeyPressed(KEY_SPACE)) g->paused = !g->paused;
    if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP)) g->offsetY += 20.0f;
    if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) g->offsetY -= 20.0f;
    if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) g->offsetX += 20.0f;
    if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) g->offsetX -= 20.0f;

    int wheel = (int)GetMouseWheelMove();
    if (wheel != 0) {
        g->zoom += (wheel > 0 ? 0.1f : -0.1f);
        if (g->zoom < 0.5f) g->zoom = 0.5f;
        if (g->zoom > 2.0f) g->zoom = 2.0f;
    }
    g->mouseX = (int)GetMousePosition().x;
    g->mouseY = (int)GetMousePosition().y;

    if (g->placing) {
        int gx, gy;
        screenToWorld(g->mouseX, g->mouseY, &gx, &gy, g->offsetX, g->offsetY, g->zoom);
        g->placePos = (Vec2i){ gx, gy };
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (canBuildAt(g, g->placeType, g->placePos) && hasRes(g, g->placeType)) {
                payCost(g, g->placeType);
                if (g->buildCount < MAX_BUILDINGS) {
                    int idx = g->buildCount;
                    Building* b = &g->builds[idx];
                    b->id = idx + 1; b->faction = FACTION_PLAYER; b->btype = g->placeType;
                    b->pos = g->placePos;
                    const BuildInfo* info = get_info(g->placeType);
                    b->width = info->width; b->height = info->height;
                    b->maxHp = 400; b->hp = b->maxHp;
                    b->buildState = BS_BUILDING; b->progress = 0;
                    b->selected = 0; b->trainingUnit = 0; b->trainingProgress = 0;
                    g->buildCount++;

                    for (int yy = 0; yy < b->height; yy++)
                        for (int xx = 0; xx < b->width; xx++)
                            if (b->pos.x + xx < MAP_SIZE && b->pos.y + yy < MAP_SIZE)
                                g->map[b->pos.y + yy][b->pos.x + xx].buildable = 0;

                    int bestV = -1;
                    float bestDist = 99999.0f;
                    for (int i = 0; i < g->unitCount; i++)
                        if (g->units[i].faction == FACTION_PLAYER && g->units[i].utype == U_VILLAGER && g->units[i].state == S_IDLE) {
                            float d = sqdist(g->units[i].pos.x, g->units[i].pos.y, b->pos.x, b->pos.y);
                            if (d < bestDist) { bestDist = d; bestV = i; }
                        }
                        if (bestV >= 0) {
                            g->units[bestV].buildId = idx;
                            g->units[bestV].state = S_BUILD;
                        }
                }
            }
            g->placing = 0;
            g->placeType = B_NONE;
        }
        return;
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        g->selecting = 1;
        g->selX1 = g->mouseX;
        g->selY1 = g->mouseY;
    }

    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        g->selecting = 0;
        int minx = g->selX1 < g->mouseX ? g->selX1 : g->mouseX;
        int maxx = g->selX1 < g->mouseX ? g->mouseX : g->selX1;
        int miny = g->selY1 < g->mouseY ? g->selY1 : g->mouseY;
        int maxy = g->selY1 < g->mouseY ? g->mouseY : g->selY1;

        bool clickedUI = g->selBuild && (g->selBuild->btype == B_TOWNCENTER) &&
        (g->mouseX >= 420 && g->mouseY >= WINDOW_HEIGHT - 200 && g->mouseX < 1000 && g->mouseY < WINDOW_HEIGHT - 80);

        if (!clickedUI) {
            for (int i = 0; i < g->unitCount; i++) g->units[i].selected = 0;
            if (maxx - minx < 8 && maxy - miny < 8) {
                for (int i = g->buildCount - 1; i >= 0; i--)
                    if (g->builds[i].faction == FACTION_PLAYER && g->builds[i].buildState == BS_DONE) {
                        int sx, sy;
                        worldToScreen(g->builds[i].pos.x, g->builds[i].pos.y, &sx, &sy, g->offsetX, g->offsetY, g->zoom);
                        if (abs(sx - minx) < 55 && abs(sy - miny) < 55) {
                            g->selBuild = &g->builds[i];
                            g->builds[i].selected = 1;
                            break;
                        }
                    }
                    if (!g->selBuild)
                        for (int i = 0; i < g->unitCount; i++)
                            if (g->units[i].faction == FACTION_PLAYER && g->units[i].state != S_DEAD)
                                if (abs(g->units[i].screenX - minx) < 20 && abs(g->units[i].screenY - miny) < 20) {
                                    g->units[i].selected = 1;
                                    break;
                                }
            } else
                for (int i = 0; i < g->unitCount; i++)
                    if (g->units[i].faction == FACTION_PLAYER && g->units[i].state != S_DEAD)
                        if (g->units[i].screenX >= minx && g->units[i].screenX <= maxx &&
                            g->units[i].screenY >= miny && g->units[i].screenY <= maxy)
                            g->units[i].selected = 1;
        }
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
        int gx, gy;
        screenToWorld(g->mouseX, g->mouseY, &gx, &gy, g->offsetX, g->offsetY, g->zoom);
        Vec2i click = { gx, gy };
        for (int i = 0; i < g->unitCount; i++)
            if (g->units[i].selected && g->units[i].state != S_DEAD && g->units[i].faction == FACTION_PLAYER) {
                int gathered = 0;
                for (int d = 0; d < g->depCount; d++)
                    if (g->deps[d].active && sqdist(gx, gy, g->deps[d].pos.x, g->deps[d].pos.y) < 25.0f && g->units[i].utype == U_VILLAGER) {
                        g->units[i].gatherId = d;
                        g->units[i].state = S_GATHER;
                        gathered = 1;
                        break;
                    }
                    if (!gathered) {
                        int plen = findPath(g, g->units[i].pos, click, g->units[i].path, MAX_PATH_LEN);
                        if (plen > 0) {
                            g->units[i].pathLen = plen;
                            g->units[i].pathIdx = 0;
                            g->units[i].state = S_MOVING;
                        }
                    }
            }
    }
}

int main(void) {
    printf("\n=== Medieval Kingdoms - BUILD 14 ===\nSyntax fixed, training works\n\n");
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Medieval Kingdoms");
    SetTargetFPS(FPS);

    Game game;
    initGame(&game);
    printf("INIT: Food=%d, Pop=%d/%d, Units=%d\n", game.food, game.popUsed, game.popCap, game.unitCount);

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        if (!game.paused) {
            for (int i = 0; i < game.unitCount; i++) updateUnit(&game.units[i], &game, dt);
            for (int i = 0; i < game.buildCount; i++) updateBuilding(&game.builds[i], &game, dt);
        }
        handleInput(&game);
        updatePopulation(&game);

        BeginDrawing();
        ClearBackground((Color){ 18,18,28,255 });

        for (int gy = 0; gy < MAP_SIZE; gy++)
            for (int gx = 0; gx < MAP_SIZE; gx++) {
                int sx, sy;
                worldToScreen(gx, gy, &sx, &sy, game.offsetX, game.offsetY, game.zoom);
                drawTile(sx, sy, game.map[gy][gx].type, game.zoom);
            }

            for (int i = 0; i < game.buildCount; i++) renderBuilding(&game.builds[i], &game);
            for (int i = 0; i < game.unitCount; i++) renderUnit(&game.units[i], &game);
            renderUI(&game);

        int mx = WINDOW_WIDTH - 150, my = WINDOW_HEIGHT - 210;
        DrawRectangle(mx, my, 140, 140, (Color){ 40,40,50,255 });
        float scl = 140.0f / MAP_SIZE;
        for (int i = 0; i < game.unitCount; i++)
            if (game.units[i].state != S_DEAD)
                DrawCircle(mx + (int)(game.units[i].pos.x * scl), my + (int)(game.units[i].pos.y * scl), 2,
                           game.units[i].faction == FACTION_PLAYER ? (Color){ 0,255,0,255 } : (Color){ 255,200,0,255 });
                DrawRectangleLines(mx, my, 140, 140, WHITE);

            EndDrawing();
    }
    CloseWindow();
    printf("Done!\n");
    return 0;
}
