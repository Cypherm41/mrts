/*
 * Medieval Kingdoms - 2D Isometric RTS (Raylib)
 * BUILD 8: Guaranteed terrain rendering using rectangles
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
#define MAX_UNITS 128
#define MAX_BUILDINGS 64
#define MAX_DEPOSITS 32
#define MAX_PATH_LEN 64
#define FPS 60

typedef enum { FACTION_PLAYER = 1, FACTION_AI = 2 } Faction;
typedef enum {
    T_GRASS = 0, T_DIRT = 1, T_FOREST = 2, T_ROCK = 3,
    T_WATER = 4, T_TREE = 5, T_GOLD = 6, T_STONE = 7
} TileType;
typedef enum { B_NONE = 0, B_TOWNCENTER = 1, B_HOUSE = 2, B_BARRACKS = 3,
    B_FARM = 4, B_LUMBER = 5, B_MINING = 6, B_TOWER = 7 } BuildingType;
    typedef enum { U_NONE = 0, U_VILLAGER = 1, U_SOLDIER = 2 } UnitType;
    typedef enum { S_IDLE = 0, S_MOVING = 1, S_GATHER = 2, S_BUILD = 3, S_DEAD = 4 } UnitState;
    typedef enum { BS_PLANNED = 0, BS_BUILDING = 1, BS_DONE = 2 } BuildState;

    typedef struct { int x, y; } Vec2i;
    typedef struct { float x, y; } Vec2f;

    typedef struct {
        int type;
        int walkable;
        int buildable;
    } Tile;

    typedef struct {
        Vec2i pos;
        int amount;
        int resType;
        int active;
    } Deposit;

    typedef struct {
        BuildingType type;
        int w, g, s, f;
        int buildTime;
        int width, height;
    } BuildInfo;

    typedef struct {
        int id;
        int faction;
        BuildingType btype;
        Vec2i pos;
        int width, height;
        int hp, maxHp;
        int buildState;
        int progress;
        int selected;
    } Building;

    typedef struct {
        int id;
        int faction;
        int utype;
        Vec2i pos;
        int screenX, screenY;
        int hp, maxHp;
        int dmg;
        int speed;
        int state;
        int selected;
        Vec2i path[MAX_PATH_LEN];
        int pathLen;
        int pathIdx;
        int gatherId;
        int buildId;
    } Unit;

    typedef struct {
        Tile map[MAP_SIZE][MAP_SIZE];
        Deposit deps[MAX_DEPOSITS];
        int depCount;
        Unit units[MAX_UNITS];
        int unitCount;
        Building builds[MAX_BUILDINGS];
        int buildCount;
        int food, wood, gold, stone;
        int popCap;
        int popUsed;
        float offsetX, offsetY;
        float zoom;
        int mouseX, mouseY;
        int selecting;
        int selX1, selY1;
        int paused;
        int gameOver;
        int winner;
        int placing;
        BuildingType placeType;
        Vec2i placePos;
        Building* selBuild;
    } Game;

    static const BuildInfo INFO[] = {
        {B_NONE,   0, 0, 0, 0, 0, 0, 0},
        {B_TOWNCENTER, 250, 150, 100, 500, 1500, 3, 3},
        {B_HOUSE, 50, 0, 0, 0, 100, 2, 1},
        {B_BARRACKS, 150, 100, 0, 0, 500, 2, 2},
        {B_FARM, 50, 0, 0, 100, 100, 2, 1},
        {B_LUMBER, 75, 0, 0, 0, 200, 2, 1},
        {B_MINING, 100, 50, 0, 0, 300, 2, 2},
        {B_TOWER, 200, 0, 150, 0, 600, 1, 1}
    };

    static const BuildInfo* get_info(BuildingType t) {
        if (t <= 0 || t > 7) return &INFO[0];
        return &INFO[t];
    }

    static float sqdist(int ax, int ay, int bx, int by) {
        float dx = (float)(ax - bx), dy = (float)(ay - by);
        return dx*dx + dy*dy;
    }

    static void worldToScreen(int gx, int gy, int* sx, int* sy, float offsetX, float offsetY, float zoom) {
        int isoX = (gx - gy) * (TILE_WIDTH / 2);
        int isoY = (gx + gy) * (TILE_HEIGHT / 2);
        *sx = (int)((isoX + offsetX) * zoom);
        *sy = (int)((isoY + offsetY) * zoom);
    }

    static void screenToWorld(int sx, int sy, int* gx, int* gy, float offsetX, float offsetY, float zoom) {
        float isoX = (float)sx / zoom - offsetX;
        float isoY = (float)sy / zoom - offsetY;
        *gx = (int)((isoX / (TILE_WIDTH / 2.0f) + isoY / (TILE_HEIGHT / 2.0f)) / 2.0f);
        *gy = (int)((isoY / (TILE_HEIGHT / 2.0f) - isoX / (TILE_WIDTH / 2.0f)) / 2.0f);
    }

    /* === GUARANTEED TILE RENDERING USING RECTANGLES === */
    static void drawTile(int sx, int sy, int tileType, float zoom) {
        Color col = (Color){74, 124, 46, 255};
        if (tileType == T_GRASS) col = (Color){74, 124, 46, 255};
        else if (tileType == T_DIRT) col = (Color){139, 90, 43, 255};
        else if (tileType == T_FOREST) col = (Color){34, 84, 26, 255};
        else if (tileType == T_ROCK) col = (Color){128, 128, 128, 255};
        else if (tileType == T_WATER) col = (Color){65, 105, 225, 255};
        else if (tileType == T_TREE) col = (Color){34, 139, 34, 255};
        else if (tileType == T_GOLD) col = (Color){255, 215, 0, 255};
        else if (tileType == T_STONE) col = (Color){169, 169, 169, 255};

        int hw = (int)(TILE_WIDTH / 2.0f * zoom);
        int hh = (int)(TILE_HEIGHT / 2.0f * zoom);

        /* Skip far off-screen tiles */
        if (sx < -300 || sx > WINDOW_WIDTH + 300 || sy < -300 || sy > WINDOW_HEIGHT + 300) return;

        /* DRAW TILE AS TWO DIAMOND HALVES USING RECTANGLES */
        /* Top half: from top point to center */
        int topY = sy - hh;
        int centerX = sx;
        int centerY = sy;

        /* Draw top-left triangle as scanlines */
        for (int y = topY; y < centerY; y++) {
            int lineWidth = (y - topY) * 2;
            int leftX = centerX - (hw * (y - topY) / hh);
            int rightX = centerX + (hw * (y - topY) / hh);

            if (leftX < -300 || rightX > WINDOW_WIDTH + 300) continue;

            DrawLine(leftX, y, rightX, y, col);
        }

        /* Draw bottom-left triangle as scanlines */
        for (int y = centerY; y < sy + hh; y++) {
            int progress = y - centerY;
            int lineWidth = (hh - progress) * 2;
            int leftX = centerX - (hw * (hh - progress) / hh);
            int rightX = centerX + (hw * (hh - progress) / hh);

            if (leftX < -300 || rightX > WINDOW_WIDTH + 300) continue;

            DrawLine(leftX, y, rightX, y, col);
        }
    }

    /* === PATHFINDING === */
    static int findPath(Game* g, Vec2i from, Vec2i to, Vec2i* path, int maxLen) {
        if (from.x == to.x && from.y == to.y) { path[0] = from; return 1; }
        if (from.x < 0 || from.x >= MAP_SIZE || from.y < 0 || from.y >= MAP_SIZE) return 0;
        if (to.x < 0 || to.x >= MAP_SIZE || to.y < 0 || to.y >= MAP_SIZE) return 0;
        if (!g->map[from.y][from.x].walkable || !g->map[to.y][to.x].walkable) return 0;

        Vec2i q[MAP_SIZE * MAP_SIZE];
        int px[MAP_SIZE][MAP_SIZE], py[MAP_SIZE][MAP_SIZE], vis[MAP_SIZE][MAP_SIZE];
        int head = 0, tail = 0;
        memset(vis, 0, sizeof(vis));
        memset(px, -1, sizeof(px));
        memset(py, -1, sizeof(py));

        q[tail++] = from;
        vis[from.y][from.x] = 1;

        int dirs[8][2] = {{0,1},{0,-1},{1,0},{-1,0},{1,1},{1,-1},{-1,1},{-1,-1}};

        while (head < tail) {
            Vec2i cur = q[head++];
            if (cur.x == to.x && cur.y == to.y) {
                int len = 0;
                Vec2i c = to;
                while (c.x != from.x || c.y != from.y) {
                    if (len >= maxLen) break;
                    path[len++] = c;
                    int tx = px[c.y][c.x];
                    int ty = py[c.y][c.x];
                    c.x = tx; c.y = ty;
                }
                for (int i = 0; i < len/2; i++) {
                    Vec2i tmp = path[i];
                    path[i] = path[len-1-i];
                    path[len-1-i] = tmp;
                }
                return len;
            }
            for (int i = 0; i < 8; i++) {
                int nx = cur.x + dirs[i][0];
                int ny = cur.y + dirs[i][1];
                if (nx >= 0 && nx < MAP_SIZE && ny >= 0 && ny < MAP_SIZE) {
                    if (!vis[ny][nx] && g->map[ny][nx].walkable) {
                        vis[ny][nx] = 1;
                        px[ny][nx] = cur.x;
                        py[ny][nx] = cur.y;
                        q[tail++] = (Vec2i){nx, ny};
                    }
                }
            }
        }
        return 0;
    }

    /* === MAP INITIALIZATION === */
    static void initMap(Game* g) {
        for (int y = 0; y < MAP_SIZE; y++) {
            for (int x = 0; x < MAP_SIZE; x++) {
                g->map[y][x].type = T_GRASS;
                g->map[y][x].walkable = 1;
                g->map[y][x].buildable = 1;
            }
        }

        for (int i = 0; i < 10; i++) {
            int cx = rand() % MAP_SIZE;
            int cy = rand() % MAP_SIZE;
            int sz = 2 + rand() % 3;
            for (int dy = -sz; dy <= sz; dy++) {
                for (int dx = -sz; dx <= sz; dx++) {
                    int tx = cx + dx, ty = cy + dy;
                    if (tx >= 0 && tx < MAP_SIZE && ty >= 0 && ty < MAP_SIZE) {
                        if (g->map[ty][tx].type == T_GRASS && rand() % 100 < 70) {
                            g->map[ty][tx].type = T_FOREST;
                            g->map[ty][tx].walkable = 0;
                            g->map[ty][tx].buildable = 0;
                        }
                    }
                }
            }
        }

        g->depCount = 0;
        for (int i = 0; i < 25 && g->depCount < MAX_DEPOSITS; i++) {
            int x = rand() % MAP_SIZE;
            int y = rand() % MAP_SIZE;
            if ((x < 12 && y < 12) || (x > MAP_SIZE-12 && y > MAP_SIZE-12)) continue;
            if (g->map[y][x].type == T_GRASS) {
                int rt = rand() % 3;
                g->deps[g->depCount].pos = (Vec2i){x, y};
                g->deps[g->depCount].resType = rt + 1;
                g->deps[g->depCount].amount = 500 + rand() % 500;
                g->deps[g->depCount].active = 1;
                if (rt == 0) g->map[y][x].type = T_TREE;
                else if (rt == 1) g->map[y][x].type = T_GOLD;
                else g->map[y][x].type = T_STONE;
                g->depCount++;
            }
        }

        for (int i = 0; i < 2; i++) {
            int rx = 15 + rand() % (MAP_SIZE - 30);
            int ry = rand() % MAP_SIZE;
            int dr = (rand() % 2 == 0) ? 2 : -2;
            for (int j = 0; j < 20; j++) {
                if (rx >= 0 && rx < MAP_SIZE && ry >= 0 && ry < MAP_SIZE) {
                    g->map[ry][rx].type = T_WATER;
                    g->map[ry][rx].walkable = 0;
                }
                rx += dr;
                ry += (rand() % 3) - 1;
            }
        }
    }

    /* === SPAWN FACTIONS === */
    static void spawnFaction(Game* g, int fac, int sx, int sy) {
        int bi = g->buildCount;

        if (bi < MAX_BUILDINGS) {
            Building* b = &g->builds[bi++];
            b->id = ++g->buildCount;
            b->faction = fac;
            b->btype = B_TOWNCENTER;
            b->pos = (Vec2i){sx, sy};
            b->width = 3; b->height = 3;
            b->hp = b->maxHp = 1200;
            b->buildState = BS_DONE;
            b->progress = 10000;
            b->selected = 0;
            for (int yy = 0; yy < 3; yy++)
                for (int xx = 0; xx < 3; xx++)
                    if (sx+xx < MAP_SIZE && sy+yy < MAP_SIZE)
                        g->map[sy+yy][sx+xx].walkable = 0;
            g->buildCount++;
        }

        if (bi < MAX_BUILDINGS) {
            Building* b = &g->builds[bi++];
            b->id = ++g->buildCount;
            b->faction = fac;
            b->btype = B_HOUSE;
            b->pos = (Vec2i){sx+4, sy};
            b->width = 2; b->height = 1;
            b->hp = b->maxHp = 300;
            b->buildState = BS_DONE;
            b->progress = 10000;
            b->selected = 0;
            g->buildCount++;
        }

        g->popCap = 15;

        for (int i = 0; i < 4 && g->unitCount < MAX_UNITS; i++) {
            Unit* u = &g->units[g->unitCount++];
            u->id = ++g->unitCount;
            u->faction = fac;
            u->utype = U_VILLAGER;
            u->pos = (Vec2i){sx+2+i, sy+5};
            u->screenX = u->screenY = 0;
            u->hp = u->maxHp = 35;
            u->dmg = 5;
            u->speed = 2;
            u->state = S_IDLE;
            u->selected = 0;
            u->pathLen = 0; u->pathIdx = 0;
            u->gatherId = -1; u->buildId = -1;
        }

        for (int i = 0; i < 2 && g->unitCount < MAX_UNITS; i++) {
            Unit* u = &g->units[g->unitCount++];
            u->id = ++g->unitCount;
            u->faction = fac;
            u->utype = U_SOLDIER;
            u->pos = (Vec2i){sx+4+i, sy+5};
            u->screenX = u->screenY = 0;
            u->hp = u->maxHp = 85;
            u->dmg = 18;
            u->speed = 1;
            u->state = S_IDLE;
            u->selected = 0;
            u->pathLen = 0; u->pathIdx = 0;
            u->gatherId = -1; u->buildId = -1;
        }
    }

    /* === GAME INITIALIZATION === */
    static void initGame(Game* g) {
        memset(g, 0, sizeof(Game));
        g->food = 500; g->wood = 400; g->gold = 200; g->stone = 150;
        g->popCap = 10; g->popUsed = 0;
        g->zoom = 1.0f;
        g->placing = 0;
        g->selBuild = NULL;
        g->paused = 0;
        g->gameOver = 0;
        g->winner = 0;
        g->offsetX = 100.0f;
        g->offsetY = 50.0f;

        initMap(g);
        spawnFaction(g, FACTION_PLAYER, 10, 10);
        spawnFaction(g, FACTION_AI, MAP_SIZE-15, MAP_SIZE-15);
    }

    /* === UNIT UPDATE === */
    static void updateUnit(Unit* u, Game* g, float dt) {
        if (u->state == S_DEAD) return;

        if (u->state == S_MOVING && u->pathLen > 0 && u->pathIdx < u->pathLen) {
            Vec2i target = u->path[u->pathIdx];
            float dx = (float)target.x - (float)u->pos.x;
            float dy = (float)target.y - (float)u->pos.y;
            float dist = sqrtf(dx*dx + dy*dy);

            if (dist > 0.01f) {
                float step = (float)u->speed * dt * 4.0f;
                u->pos.x += (int)(dx/dist * step);
                u->pos.y += (int)(dy/dist * step);
                worldToScreen(u->pos.x, u->pos.y, &u->screenX, &u->screenY, g->offsetX, g->offsetY, g->zoom);
            }

            if (dist < 1.5f) {
                u->pathIdx++;
                if (u->pathIdx >= u->pathLen) {
                    u->state = S_IDLE;
                }
            }
        }

        if (u->state == S_BUILD && u->buildId >= 0 && u->buildId < g->buildCount) {
            Building* b = &g->builds[u->buildId];
            if (b->buildState == BS_BUILDING) {
                float d2 = sqdist(u->pos.x, u->pos.y, b->pos.x, b->pos.y);
                if (d2 < 4.0f) {
                    b->progress += (int)(dt * 1000.0f);
                    if (b->progress >= 10000) {
                        b->buildState = BS_DONE;
                        b->hp = b->maxHp;
                        u->state = S_IDLE;
                        u->buildId = -1;
                    }
                }
            }
        }

        if (u->state == S_GATHER && u->gatherId >= 0 && u->gatherId < g->depCount && g->deps[u->gatherId].active) {
            Deposit* dp = &g->deps[u->gatherId];
            float d2 = sqdist(u->pos.x, u->pos.y, dp->pos.x, dp->pos.y);
            if (d2 < 4.0f) {
                int amt = 20;
                if (dp->resType == 1) g->wood += amt;
                else if (dp->resType == 2) g->gold += amt;
                else if (dp->resType == 3) g->stone += amt;
                dp->amount -= amt;
                if (dp->amount <= 0) {
                    dp->active = 0;
                    g->map[dp->pos.y][dp->pos.x].type = T_GRASS;
                    g->map[dp->pos.y][dp->pos.x].walkable = 1;
                }
                u->state = S_IDLE;
                u->gatherId = -1;
            }
        }
    }

    /* === BUILDING VALIDATION === */
    static int canBuildAt(Game* g, BuildingType bt, Vec2i pos) {
        const BuildInfo* info = get_info(bt);
        if (info->width == 0) return 0;
        if (pos.x < 0 || pos.x + info->width > MAP_SIZE || pos.y < 0 || pos.y + info->height > MAP_SIZE) return 0;

        for (int yy = 0; yy < info->height; yy++) {
            for (int xx = 0; xx < info->width; xx++) {
                if (!g->map[pos.y+yy][pos.x+xx].buildable) return 0;
            }
        }

        for (int i = 0; i < g->buildCount; i++) {
            if (g->builds[i].buildState != BS_DONE) continue;
            if (g->builds[i].faction != FACTION_PLAYER) continue;
            Building* b = &g->builds[i];
            if (pos.x < b->pos.x + b->width && pos.x + info->width > b->pos.x &&
                pos.y < b->pos.y + b->height && pos.y + info->height > b->pos.y) return 0;
        }
        return 1;
    }

    static int hasRes(Game* g, BuildingType bt) {
        const BuildInfo* info = get_info(bt);
        return g->wood >= info->w && g->gold >= info->g && g->stone >= info->s && g->food >= info->f;
    }

    static void payCost(Game* g, BuildingType bt) {
        const BuildInfo* info = get_info(bt);
        g->wood -= info->w;
        g->gold -= info->g;
        g->stone -= info->s;
        g->food -= info->f;
    }

    /* === RENDER BUILDING === */
    static void renderBuilding(Building* b, Game* g) {
        int sx, sy;
        worldToScreen(b->pos.x, b->pos.y, &sx, &sy, g->offsetX, g->offsetY, g->zoom);

        int w = (int)(b->width * TILE_WIDTH / 2 * g->zoom);
        int h = (int)(b->height * TILE_HEIGHT / 2 * g->zoom);

        Color col = (b->faction == FACTION_PLAYER) ? (Color){70, 180, 70, 255} : (Color){255, 215, 0, 255};

        if (b->buildState == BS_BUILDING) {
            col.r /= 2; col.g /= 2; col.b /= 2;
        }

        DrawRectangle(sx - w/2, sy - h/2, w, h, col);

        if (b->buildState == BS_BUILDING) {
            int pbw = (int)(w * 0.8);
            int pbh = (int)(4 * g->zoom);
            int pbx = sx - pbw/2;
            int pby = sy - h/2 - (int)(25 * g->zoom);

            DrawRectangle(pbx, pby, pbw, pbh, (Color){40, 40, 40, 255});
            int pw = (b->progress * pbw) / 10000;
            DrawRectangle(pbx, pby, pw, pbh, GREEN);
        }

        if (b->hp < b->maxHp && b->buildState == BS_DONE) {
            int bw = (int)(w * 0.8);
            int bh = (int)(4 * g->zoom);
            int bx = sx - bw/2;
            int by = sy - h/2 - (int)(12 * g->zoom);
            DrawRectangle(bx, by, bw, bh, RED);
            int hw = (b->hp * bw) / b->maxHp;
            DrawRectangle(bx, by, hw, bh, GREEN);
        }

        if (b->selected) {
            DrawRectangleLinesEx((Rectangle){(float)(sx-w/2-3),(float)(sy-h/2-3),(float)(w+6),(float)(h+6)}, 2, WHITE);
        }
    }

    /* === RENDER UNIT === */
    static void renderUnit(Unit* u, Game* g) {
        if (u->state == S_DEAD) return;

        int r = (int)(8 * g->zoom);
        Color col;
        if (u->utype == U_VILLAGER) {
            col = (u->faction == FACTION_PLAYER) ? (Color){120, 255, 120, 255} : (Color){255, 240, 150, 255};
        } else {
            col = (u->faction == FACTION_PLAYER) ? (Color){60, 140, 60, 255} : (Color){200, 170, 50, 255};
        }

        DrawCircle(u->screenX, u->screenY, r, col);
        if (u->selected) DrawCircleLines(u->screenX, u->screenY, r+4, WHITE);
    }

    /* === BUILDING PREVIEW === */
    static void renderPreview(Game* g) {
        if (!g->placing) return;

        const BuildInfo* info = get_info(g->placeType);
        int valid = canBuildAt(g, g->placeType, g->placePos);
        Color col = valid ? (Color){100, 255, 100, 120} : (Color){255, 100, 100, 120};

        for (int yy = 0; yy < info->height; yy++) {
            for (int xx = 0; xx < info->width; xx++) {
                int tx = g->placePos.x + xx;
                int ty = g->placePos.y + yy;
                if (tx < 0 || tx >= MAP_SIZE || ty < 0 || ty >= MAP_SIZE) continue;

                int sx, sy;
                worldToScreen(tx, ty, &sx, &sy, g->offsetX, g->offsetY, g->zoom);

                /* Draw preview as lines for consistency */
                int hw = (int)(TILE_WIDTH / 2.0f * g->zoom);
                int hh = (int)(TILE_HEIGHT / 2.0f * g->zoom);

                for (int y = sy - hh; y < sy + hh; y++) {
                    if (y < sy) {
                        int lineWidth = (y - (sy - hh)) * 2;
                        int leftX = sx - (hw * (y - (sy - hh)) / hh);
                        int rightX = sx + (hw * (y - (sy - hh)) / hh);
                        DrawLine(leftX, y, rightX, y, col);
                    } else {
                        int progress = y - sy;
                        int lineWidth = (hh - progress) * 2;
                        int leftX = sx - (hw * (hh - progress) / hh);
                        int rightX = sx + (hw * (hh - progress) / hh);
                        DrawLine(leftX, y, rightX, y, col);
                    }
                }
            }
        }
    }

    /* === MINIMAP === */
    static void renderMinimap(Game* g) {
        int mx = WINDOW_WIDTH - 150, my = WINDOW_HEIGHT - 150, ms = 140;
        DrawRectangle(mx, my, ms, ms, (Color){40, 40, 50, 255});

        float scl = (float)ms / MAP_SIZE;

        for (int i = 0; i < g->unitCount; i++) {
            if (g->units[i].state == S_DEAD) continue;
            int x = mx + g->units[i].pos.x * scl;
            int y = my + g->units[i].pos.y * scl;
            Color col = (g->units[i].faction == FACTION_PLAYER) ? (Color){0, 255, 0, 255} : (Color){255, 200, 0, 255};
            DrawCircle(x, y, 2, col);
        }

        for (int i = 0; i < g->buildCount; i++) {
            if (g->builds[i].buildState != BS_DONE) continue;
            int x = mx + g->builds[i].pos.x * scl;
            int y = my + g->builds[i].pos.y * scl;
            int w = (int)(g->builds[i].width * scl);
            int h = (int)(g->builds[i].height * scl);
            Color col = (g->builds[i].faction == FACTION_PLAYER) ? (Color){0, 180, 0, 255} : (Color){200, 170, 0, 255};
            DrawRectangle(x, y, w, h, col);
        }

        DrawRectangleLines(mx, my, ms, ms, WHITE);
        DrawText("Minimap", mx, my - 20, 16, (Color){200, 200, 200, 255});
    }

    /* === UI RENDERING === */
    static void renderUI(Game* g) {
        DrawRectangle(0, WINDOW_HEIGHT - 80, WINDOW_WIDTH, 80, (Color){35, 35, 50, 255});
        DrawRectangleLinesEx((Rectangle){0,(float)(WINDOW_HEIGHT-80),(float)WINDOW_WIDTH,80.0f}, 2, (Color){70, 70, 90, 255});

        int rx = 25, ry = WINDOW_HEIGHT - 65;
        const char* rnames[] = {"Food", "Wood", "Gold", "Stone"};
        int rvals[] = {g->food, g->wood, g->gold, g->stone};
        Color rcols[] = {(Color){255, 120, 120, 255}, (Color){139, 90, 43, 255}, (Color){255, 215, 0, 255}, (Color){180, 180, 180, 255}};

        for (int i = 0; i < 4; i++) {
            DrawRectangle(rx, ry, 30, 30, rcols[i]);
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", rvals[i]);
            DrawText(buf, rx + 35, ry + 8, 16, (Color){230, 230, 230, 255});
            DrawText(rnames[i], rx + 35, ry + 26, 12, (Color){170, 170, 170, 255});
            rx += 85;
        }

        char popbuf[20];
        snprintf(popbuf, sizeof(popbuf), "Pop:%d/%d", g->popUsed, g->popCap);
        DrawText(popbuf, rx + 10, ry + 15, 16, (Color){230, 230, 230, 255});

        /* Build marker */
        DrawText("BUILD 8 - TERRAIN FIXED", 10, 10, 14, (Color){100, 200, 255, 255});

        if (g->placing) {
            DrawText("CLICK MAP TO PLACE, ESC TO CANCEL", 400, WINDOW_HEIGHT - 120, 16, YELLOW);
        }

        if (g->selBuild && g->selBuild->btype == B_TOWNCENTER) {
            int px = 120, py = WINDOW_HEIGHT - 170;
            DrawRectangle(px, py, 600, 90, (Color){50, 50, 70, 255});
            DrawRectangleLinesEx((Rectangle){(float)px,(float)py,600.0f,90.0f}, 2, (Color){90, 140, 90, 255});
            DrawText("BUILD", px + 10, py + 5, 14, (Color){100, 200, 100, 255});

            Vector2 mp = GetMousePosition();
            BuildingType btns[] = {B_HOUSE, B_BARRACKS, B_FARM, B_LUMBER, B_MINING, B_TOWER};
            const char* names[] = {"House", "Barracks", "Farm", "Lumber Mill", "Mining Camp", "Tower"};

            for (int i = 0; i < 6; i++) {
                BuildingType bt = btns[i];
                const BuildInfo* info = get_info(bt);
                int bx = px + 10 + i * 90;
                Rectangle btn = {(float)bx, (float)(py + 25), 85.0f, 55.0f};
                bool hover = CheckCollisionPointRec(mp, btn);

                if (hover) {
                    DrawRectangleLinesEx(btn, 2, WHITE);
                    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                        g->placeType = bt;
                        g->placing = 1;
                        g->selBuild = NULL;
                    }
                }

                Color bcol = hasRes(g, bt) ?
                (hover ? (Color){90, 180, 90, 255} : (Color){70, 150, 70, 255}) :
                (Color){50, 50, 50, 255};
                DrawRectangle(bx, py + 25, 85, 55, bcol);

                DrawText(names[i], bx + 3, py + 30, 9, WHITE);
                char cost[20];
                snprintf(cost, sizeof(cost), "%dW %dG", info->w, info->g);
                DrawText(cost, bx + 3, py + 43, 7, (Color){200, 200, 200, 255});
            }
        }

        if (g->gameOver) {
            DrawRectangle(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, (Color){0, 0, 0, 200});
            const char* msg = (g->winner == FACTION_PLAYER) ? "VICTORY!" : "DEFEAT!";
            int cx = WINDOW_WIDTH/2 - 160, cy = WINDOW_HEIGHT/2 - 40;
            DrawText(msg, cx, cy, 36, (g->winner == FACTION_PLAYER) ? GREEN : RED);
            DrawText("Press ESC to quit", cx + 60, cy + 50, 20, WHITE);
        }

        if (g->paused) {
            DrawText("PAUSED", WINDOW_WIDTH/2 - 50, WINDOW_HEIGHT/2, 24, WHITE);
        }
    }

    /* === INPUT HANDLING === */
    static void handleInput(Game* g) {
        if (IsKeyPressed(KEY_ESCAPE)) {
            if (g->placing) {
                g->placing = 0;
                g->placeType = B_NONE;
            } else {
                CloseWindow();
            }
        }
        if (IsKeyPressed(KEY_SPACE)) g->paused = !g->paused;

        if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP)) g->offsetY += 20.0f;
        if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) g->offsetY -= 20.0f;
        if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) g->offsetX += 20.0f;
        if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) g->offsetX -= 20.0f;

        int wheel = GetMouseWheelMove();
        if (wheel != 0) {
            g->zoom += (wheel > 0) ? 0.1f : -0.1f;
            if (g->zoom < 0.5f) g->zoom = 0.5f;
            if (g->zoom > 2.0f) g->zoom = 2.0f;
        }

        g->mouseX = GetMousePosition().x;
        g->mouseY = GetMousePosition().y;

        if (g->placing) {
            int gx, gy;
            screenToWorld(g->mouseX, g->mouseY, &gx, &gy, g->offsetX, g->offsetY, g->zoom);
            g->placePos = (Vec2i){gx, gy};

            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                if (canBuildAt(g, g->placeType, g->placePos) && hasRes(g, g->placeType)) {
                    payCost(g, g->placeType);

                    if (g->buildCount < MAX_BUILDINGS) {
                        Building* b = &g->builds[g->buildCount++];
                        b->id = ++g->buildCount;
                        b->faction = FACTION_PLAYER;
                        b->btype = g->placeType;
                        b->pos = g->placePos;
                        const BuildInfo* info = get_info(g->placeType);
                        b->width = info->width;
                        b->height = info->height;
                        b->maxHp = (b->btype == B_BARRACKS) ? 600 : 400;
                        b->hp = b->maxHp;
                        b->buildState = BS_BUILDING;
                        b->progress = 0;
                        b->selected = 0;

                        int newIdx = g->buildCount - 1;
                        for (int yy = 0; yy < b->height; yy++) {
                            for (int xx = 0; xx < b->width; xx++) {
                                if (b->pos.x+xx < MAP_SIZE && b->pos.y+yy < MAP_SIZE)
                                    g->map[b->pos.y+yy][b->pos.x+xx].buildable = 0;
                            }
                        }

                        int bestV = -1;
                        float bestDist = 99999.0f;
                        for (int i = 0; i < g->unitCount; i++) {
                            if (g->units[i].faction == FACTION_PLAYER &&
                                g->units[i].utype == U_VILLAGER &&
                                g->units[i].state == S_IDLE) {
                                float d = sqdist(g->units[i].pos.x, g->units[i].pos.y, b->pos.x, b->pos.y);
                            if (d < bestDist) { bestDist = d; bestV = i; }
                                }
                        }
                        if (bestV >= 0) {
                            g->units[bestV].buildId = newIdx;
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
            g->selBuild = NULL;
            for (int i = 0; i < g->unitCount; i++) g->units[i].selected = 0;
        }

        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            g->selecting = 0;
            int minx = (g->selX1 < g->mouseX) ? g->selX1 : g->mouseX;
            int maxx = (g->selX1 < g->mouseX) ? g->mouseX : g->selX1;
            int miny = (g->selY1 < g->mouseY) ? g->selY1 : g->mouseY;
            int maxy = (g->selY1 < g->mouseY) ? g->mouseY : g->selY1;

            if (maxx - minx < 8 && maxy - miny < 8) {
                for (int i = g->buildCount - 1; i >= 0; i--) {
                    if (g->builds[i].faction != FACTION_PLAYER || g->builds[i].buildState != BS_DONE) continue;
                    int sx, sy;
                    worldToScreen(g->builds[i].pos.x, g->builds[i].pos.y, &sx, &sy, g->offsetX, g->offsetY, g->zoom);
                    if (abs(sx - minx) < 50 && abs(sy - miny) < 50) {
                        g->selBuild = &g->builds[i];
                        g->builds[i].selected = 1;
                        break;
                    }
                }
                if (!g->selBuild) {
                    for (int i = 0; i < g->unitCount; i++) {
                        if (g->units[i].faction != FACTION_PLAYER || g->units[i].state == S_DEAD) continue;
                        if (abs(g->units[i].screenX - minx) < 20 && abs(g->units[i].screenY - miny) < 20) {
                            g->units[i].selected = 1;
                            break;
                        }
                    }
                }
            } else {
                for (int i = 0; i < g->unitCount; i++) {
                    if (g->units[i].faction != FACTION_PLAYER || g->units[i].state == S_DEAD) continue;
                    if (g->units[i].screenX >= minx && g->units[i].screenX <= maxx &&
                        g->units[i].screenY >= miny && g->units[i].screenY <= maxy) {
                        g->units[i].selected = 1;
                        }
                }
            }
        }

        if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
            int gx, gy;
            screenToWorld(g->mouseX, g->mouseY, &gx, &gy, g->offsetX, g->offsetY, g->zoom);
            Vec2i click = {gx, gy};

            for (int i = 0; i < g->unitCount; i++) {
                if (!g->units[i].selected || g->units[i].state == S_DEAD || g->units[i].faction != FACTION_PLAYER) continue;

                int gathered = 0;
                for (int d = 0; d < g->depCount; d++) {
                    if (g->deps[d].active && sqdist(gx, gy, g->deps[d].pos.x, g->deps[d].pos.y) < 25.0f) {
                        if (g->units[i].utype == U_VILLAGER) {
                            g->units[i].gatherId = d;
                            g->units[i].state = S_GATHER;
                            gathered = 1;
                            break;
                        }
                    }
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

    /* === MAIN === */
    int main(void) {
        printf("\n=== Medieval Kingdoms ===\nBUILD 8: Rectangle-based terrain\n==========================\n");
        printf("Controls:\n  Left Click: Select / Place\n  Right Click: Move / Gather\n  WASD: Pan Camera\n  Scroll: Zoom\n  Space: Pause\n  ESC: Quit\n\n");

        InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Medieval Kingdoms");
        SetTargetFPS(FPS);

        Game game;
        initGame(&game);

        float targetDt = 1.0f / FPS;
        float accum = 0.0f;

        while (!WindowShouldClose()) {
            float dt = GetFrameTime();
            accum += dt;
            while (accum >= targetDt) {
                if (!game.paused && !game.gameOver) {
                    for (int i = 0; i < game.unitCount; i++) {
                        updateUnit(&game.units[i], &game, targetDt);
                    }
                }
                handleInput(&game);
                accum -= targetDt;
            }

            BeginDrawing();
            ClearBackground((Color){18, 18, 28, 255});

            /* Render terrain tiles */
            for (int gy = 0; gy < MAP_SIZE; gy++) {
                for (int gx = 0; gx < MAP_SIZE; gx++) {
                    int sx, sy;
                    worldToScreen(gx, gy, &sx, &sy, game.offsetX, game.offsetY, game.zoom);
                    drawTile(sx, sy, game.map[gy][gx].type, game.zoom);
                }
            }

            /* Render buildings */
            for (int i = 0; i < game.buildCount; i++) {
                renderBuilding(&game.builds[i], &game);
            }

            /* Render units */
            for (int i = 0; i < game.unitCount; i++) {
                renderUnit(&game.units[i], &game);
            }

            renderPreview(&game);
            renderUI(&game);
            renderMinimap(&game);
            EndDrawing();
        }

        CloseWindow();
        printf("Done!\n");
        return 0;
    }
