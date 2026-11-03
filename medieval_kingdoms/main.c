/*
 * Medieval Kingdoms - 2D Isometric RTS (Raylib Version)
 * FIXED: Unit movement, training clicks, pathfinding
 *
 * Compile: gcc -std=c11 -O2 -o medieval_rts main.c -lraylib -lm
 */

#include "raylib.h"
#include "raymath.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

/* === Constants === */
#define WINDOW_WIDTH 1280
#define WINDOW_HEIGHT 720
#define MAP_SIZE 48
#define TILE_WIDTH 64
#define TILE_HEIGHT 32
#define MAX_UNITS 128
#define MAX_BUILDINGS 32
#define MAX_DEPOSITS 32
#define MAX_PATH_LEN 64
#define FPS 60

/* === Factions === */
#define FACTION_PLAYER 1
#define FACTION_AI 2

/* === Tile Types === */
typedef enum {
    TILE_GRASS = 0,
    TILE_DIRT = 1,
    TILE_FOREST = 2,
    TILE_ROCK = 3,
    TILE_WATER = 4,
    TILE_TREE = 5,
    TILE_GOLD_DEP = 6,
    TILE_STONE_DEP = 7
} TileType;

/* === Resources === */
typedef enum {
    RESOURCE_NONE = 0,
    RESOURCE_WOOD = 1,
    RESOURCE_GOLD = 2,
    RESOURCE_STONE = 3
} ResourceType;

/* === Unit Types === */
typedef enum {
    UNIT_NONE = 0,
    UNIT_VILLAGER = 1,
    UNIT_SOLDIER = 2
} UnitType;

/* === Building Types === */
typedef enum {
    BUILDING_NONE = 0,
    BUILDING_TOWN_CENTER = 1,
    BUILDING_HOUSE = 2,
    BUILDING_BARRACKS = 3
} BuildingType;

/* === Unit States === */
typedef enum {
    STATE_IDLE = 0,
    STATE_MOVING = 1,
    STATE_GATHERING = 2,
    STATE_DEAD = 3
} UnitState;

/* === Data Structures === */
typedef struct { int x, y; } Vec2i;
typedef struct { float x, y; } Vec2f;

typedef struct {
    uint8_t type;
    uint8_t walkable;
    uint8_t buildable;
} MapTile;

typedef struct {
    Vec2i pos;
    int amount;
    int resource_type;
    uint8_t active;
} ResourceDeposit;

typedef struct {
    int id;
    int faction;
    int unit_type;
    Vec2i grid_pos;
    Vec2f pixel_pos;
    Vec2f target_pos;
    int health, max_health;
    int damage, armor, range;
    float move_speed;
    float attack_cooldown;
    float max_cooldown;
    int state;
    int selected;
    Vec2i path[MAX_PATH_LEN];
    int path_length;
    int path_index;
    int gather_target_id;
} Unit;

typedef struct {
    int id;
    int faction;
    int building_type;
    Vec2i grid_pos;
    int width, height;
    int health, max_health;
    int constructed;
    int selected;
} Building;

typedef struct {
    int food, wood, gold, stone;
    int population_capacity;
    int population_used;
} Resources;

typedef struct {
    MapTile map[MAP_SIZE][MAP_SIZE];
    ResourceDeposit deposits[MAX_DEPOSITS];
    int deposit_count;

    Unit units[MAX_UNITS];
    int unit_count;
    Building buildings[MAX_BUILDINGS];
    int building_count;

    Resources resources;

    Vec2i camera;
    float zoom;

    int mouse_x, mouse_y;
    int selecting;
    int sel_start_x, sel_start_y;

    int paused;
    int game_over;
    int winner;

    /* UI State */
    Building* selected_building;
    int camera_panned;
} Game;

/* === Forward Declarations === */
static void draw_isometric_tile(int sx, int sy, uint8_t tile_type, float zoom);

/* === Helper Functions === */
static inline float distance_squared(int x1, int y1, int x2, int y2) {
    int dx = x1 - x2, dy = y1 - y2;
    return (float)(dx * dx + dy * dy);
}

static inline int clamp_int(int val, int min_val, int max_val) {
    if (val < min_val) return min_val;
    if (val > max_val) return max_val;
    return val;
}

static inline float clamp_float(float val, float min_val, float max_val) {
    if (val < min_val) return min_val;
    if (val > max_val) return max_val;
    return val;
}

/* === Coordinate Conversion (Isometric) === */
void world_to_screen(int gx, int gy, int* sx, int* sy, Vec2i cam, float zoom) {
    *sx = ((gx - gy) * TILE_WIDTH / 2 + WINDOW_WIDTH / 2 - cam.x) * zoom;
    *sy = ((gx + gy) * TILE_HEIGHT / 2 + WINDOW_HEIGHT / 4 - cam.y) * zoom;
}

void screen_to_world(int sx, int sy, int* gx, int* gy, Vec2i cam, float zoom) {
    float cx = (float)(sx - WINDOW_WIDTH / 2 + cam.x) / zoom;
    float cy = (float)(sy - WINDOW_HEIGHT / 4 + cam.y) / zoom;
    *gy = (int)((cx / (TILE_WIDTH / 2.0f) + cy / (TILE_HEIGHT / 2.0f)) / 2.0f);
    *gx = (int)((cx / (TILE_WIDTH / 2.0f) - cy / (TILE_HEIGHT / 2.0f)) / 2.0f);
}

/* === Pathfinding (BFS) === */
int find_path(Game* game, Vec2i start, Vec2i goal, Vec2i* path, int max_len) {
    if (start.x == goal.x && start.y == goal.y) {
        path[0] = start;
        return 1;
    }

    if (start.x < 0 || start.x >= MAP_SIZE || start.y < 0 || start.y >= MAP_SIZE) return 0;
    if (goal.x < 0 || goal.x >= MAP_SIZE || goal.y < 0 || goal.y >= MAP_SIZE) return 0;
    if (!game->map[start.y][start.x].walkable || !game->map[goal.y][goal.x].walkable) return 0;

    Vec2i queue[MAP_SIZE * MAP_SIZE];
    int parent_x[MAP_SIZE][MAP_SIZE];
    int parent_y[MAP_SIZE][MAP_SIZE];
    int visited[MAP_SIZE][MAP_SIZE];
    int front = 0, back = 0;

    memset(visited, 0, sizeof(visited));
    memset(parent_x, -1, sizeof(parent_x));
    memset(parent_y, -1, sizeof(parent_y));

    queue[back++] = start;
    visited[start.y][start.x] = 1;

    int dirs[8][2] = {{0,1},{0,-1},{1,0},{-1,0},{1,1},{1,-1},{-1,1},{-1,-1}};

    while (front < back) {
        Vec2i current = queue[front++];

        if (current.x == goal.x && current.y == goal.y) {
            int len = 0;
            Vec2i cur = goal;
            while (cur.x != start.x || cur.y != start.y) {
                if (len >= max_len) break;
                path[len++] = cur;
                int px = parent_x[cur.y][cur.x];
                int py = parent_y[cur.y][cur.x];
                cur.x = px; cur.y = py;
            }
            /* Reverse path so it goes from start to goal */
            for (int i = 0; i < len / 2; i++) {
                Vec2i tmp = path[i];
                path[i] = path[len - 1 - i];
                path[len - 1 - i] = tmp;
            }
            return len;
        }

        for (int d = 0; d < 8; d++) {
            int nx = current.x + dirs[d][0];
            int ny = current.y + dirs[d][1];

            if (nx >= 0 && nx < MAP_SIZE && ny >= 0 && ny < MAP_SIZE) {
                if (!visited[ny][nx] && game->map[ny][nx].walkable) {
                    visited[ny][nx] = 1;
                    parent_y[ny][nx] = current.y;
                    parent_x[ny][nx] = current.x;
                    queue[back++] = (Vec2i){nx, ny};
                }
            }
        }
    }

    return 0;
}

/* === Drawing Functions === */
void draw_isometric_tile(int sx, int sy, uint8_t tile_type, float zoom) {
    int hw = (int)(TILE_WIDTH / 2.0f * zoom);
    int hh = (int)(TILE_HEIGHT / 2.0f * zoom);

    Vector2 points[4];
    points[0] = (Vector2){(float)sx, (float)(sy - hh)};
    points[1] = (Vector2){(float)(sx + hw), (float)sy};
    points[2] = (Vector2){(float)sx, (float)(sy + hh)};
    points[3] = (Vector2){(float)(sx - hw), (float)sy};

    Color col = GREEN;
    switch (tile_type) {
        case TILE_GRASS: col = (Color){74, 124, 46, 255}; break;
        case TILE_DIRT: col = (Color){139, 90, 43, 255}; break;
        case TILE_FOREST: col = (Color){34, 84, 26, 255}; break;
        case TILE_ROCK: col = (Color){128, 128, 128, 255}; break;
        case TILE_WATER: col = (Color){65, 105, 225, 255}; break;
        case TILE_TREE: col = (Color){34, 139, 34, 255}; break;
        case TILE_GOLD_DEP: col = (Color){255, 215, 0, 255}; break;
        case TILE_STONE_DEP: col = (Color){169, 169, 169, 255}; break;
    }

    Vector2 tri1[3] = {points[0], points[1], points[2]};
    Vector2 tri2[3] = {points[2], points[3], points[0]};

    DrawTriangle(tri1[0], tri1[1], tri1[2], col);
    DrawTriangle(tri2[0], tri2[1], tri2[2], col);

    Color darker = (Color){col.r * 0.7f, col.g * 0.7f, col.b * 0.7f, 255};
    DrawPoly(points[0], 4, (float)hw, 0.0f, darker);
}

/* === Map Generation === */
void init_map(Game* game) {
    for (int y = 0; y < MAP_SIZE; y++) {
        for (int x = 0; x < MAP_SIZE; x++) {
            game->map[y][x].type = TILE_GRASS;
            game->map[y][x].walkable = 1;
            game->map[y][x].buildable = 1;
        }
    }

    /* Forests */
    for (int i = 0; i < 12; i++) {
        int cx = rand() % MAP_SIZE;
        int cy = rand() % MAP_SIZE;
        int size = 2 + rand() % 3;

        for (int dy = -size; dy <= size; dy++) {
            for (int dx = -size; dx <= size; dx++) {
                int tx = cx + dx;
                int ty = cy + dy;
                if (tx >= 0 && tx < MAP_SIZE && ty >= 0 && ty < MAP_SIZE) {
                    if (game->map[ty][tx].type == TILE_GRASS && rand() % 100 < 60) {
                        game->map[ty][tx].type = TILE_FOREST;
                        game->map[ty][tx].walkable = 0;
                        game->map[ty][tx].buildable = 0;
                    }
                }
            }
        }
    }

    /* Deposits */
    game->deposit_count = 0;
    for (int i = 0; i < 20 && game->deposit_count < MAX_DEPOSITS; i++) {
        int x = rand() % MAP_SIZE;
        int y = rand() % MAP_SIZE;

        if ((x < 10 && y < 10) || (x > MAP_SIZE - 10 && y > MAP_SIZE - 10)) continue;

        if (game->map[y][x].type == TILE_GRASS) {
            int rtype = rand() % 3;
            game->deposits[game->deposit_count].pos = (Vec2i){x, y};
            game->deposits[game->deposit_count].resource_type = rtype + 1;
            game->deposits[game->deposit_count].amount = 400 + rand() % 600;
            game->deposits[game->deposit_count].active = 1;
            game->map[y][x].type = (rtype == 0) ? TILE_TREE : (rtype == 1) ? TILE_GOLD_DEP : TILE_STONE_DEP;
            game->deposit_count++;
        }
    }

    /* Rivers */
    for (int i = 0; i < 2; i++) {
        int rx = 10 + rand() % (MAP_SIZE - 20);
        int ry = rand() % MAP_SIZE;
        int dr = (rand() % 2 == 0) ? 2 : -2;

        for (int j = 0; j < 15; j++) {
            if (rx >= 0 && rx < MAP_SIZE && ry >= 0 && ry < MAP_SIZE) {
                game->map[ry][rx].type = TILE_WATER;
                game->map[ry][rx].walkable = 0;
            }
            rx += dr;
            ry += (rand() % 3) - 1;
        }
    }
}

/* === Spawn Faction === */
void spawn_faction(Game* game, int faction, int start_x, int start_y) {
    int bi = game->building_count;

    /* Town Center */
    if (bi < MAX_BUILDINGS) {
        Building* tc = &game->buildings[bi++];
        tc->id = ++game->building_count;
        tc->faction = faction;
        tc->building_type = BUILDING_TOWN_CENTER;
        tc->grid_pos = (Vec2i){start_x, start_y};
        tc->width = 3; tc->height = 3;
        tc->max_health = 1200; tc->health = 1200;
        tc->constructed = 1;
        tc->selected = 0;

        for (int yy = 0; yy < 3; yy++)
            for (int xx = 0; xx < 3; xx++)
                if (start_x + xx < MAP_SIZE && start_y + yy < MAP_SIZE)
                    game->map[start_y + yy][start_x + xx].walkable = 0;
        game->building_count++;
    }

    /* House */
    if (bi < MAX_BUILDINGS) {
        Building* hb = &game->buildings[bi++];
        hb->id = ++game->building_count;
        hb->faction = faction;
        hb->building_type = BUILDING_HOUSE;
        hb->grid_pos = (Vec2i){start_x + 4, start_y};
        hb->width = 2; hb->height = 1;
        hb->max_health = 300; hb->health = 300;
        hb->constructed = 1;
        hb->selected = 0;
        game->building_count++;
    }

    game->resources.population_capacity = 15;

    /* Villagers */
    for (int vi = 0; vi < 4 && game->unit_count < MAX_UNITS; vi++) {
        Unit* vu = &game->units[game->unit_count++];
        vu->id = ++game->unit_count;
        vu->faction = faction;
        vu->unit_type = UNIT_VILLAGER;
        vu->grid_pos = (Vec2i){start_x + 2 + vi, start_y + 5};
        vu->target_pos = (Vec2f){(float)vu->grid_pos.x, (float)vu->grid_pos.y};
        vu->health = vu->max_health = 35;
        vu->damage = 5; vu->armor = 0; vu->range = 1;
        vu->move_speed = 2.5f;
        vu->attack_cooldown = 0; vu->max_cooldown = 800;
        vu->state = STATE_IDLE;
        vu->selected = 0;
        vu->gather_target_id = -1;
        vu->path_length = 0;
        vu->path_index = 0;
    }

    /* Soldiers */
    for (int si = 0; si < 2 && game->unit_count < MAX_UNITS; si++) {
        Unit* su = &game->units[game->unit_count++];
        su->id = ++game->unit_count;
        su->faction = faction;
        su->unit_type = UNIT_SOLDIER;
        su->grid_pos = (Vec2i){start_x + 4 + si, start_y + 5};
        su->target_pos = (Vec2f){(float)su->grid_pos.x, (float)su->grid_pos.y};
        su->health = su->max_health = 85;
        su->damage = 18; su->armor = 4; su->range = 1;
        su->move_speed = 1.8f;
        su->attack_cooldown = 0; su->max_cooldown = 1200;
        su->state = STATE_IDLE;
        su->selected = 0;
        su->gather_target_id = -1;
        su->path_length = 0;
        su->path_index = 0;
    }
}

/* === Initialize Game === */
void init_game(Game* game) {
    memset(game, 0, sizeof(Game));

    game->paused = 0;
    game->game_over = 0;
    game->zoom = 1.0f;
    game->selected_building = NULL;

    game->resources.food = 350;
    game->resources.wood = 250;
    game->resources.gold = 150;
    game->resources.stone = 100;
    game->resources.population_capacity = 10;
    game->resources.population_used = 0;

    init_map(game);
    spawn_faction(game, FACTION_PLAYER, 8, 8);
    spawn_faction(game, FACTION_AI, MAP_SIZE - 12, MAP_SIZE - 12);

    /* Center camera on player base */
    int sx, sy;
    world_to_screen(8, 8, &sx, &sy, (Vec2i){0, 0}, 1.0f);
    game->camera.x = WINDOW_WIDTH / 2 - sx;
    game->camera.y = WINDOW_HEIGHT / 4 - sy;
}

/* === Update Unit === */
void update_unit(Unit* u, Game* game, float dt) {
    (void)game;
    if (u->state == STATE_DEAD) return;

    if (u->attack_cooldown > 0) {
        u->attack_cooldown -= dt * 1000.0f;
    }

    if (u->state == STATE_MOVING) {
        if (u->path_length > 0 && u->path_index < u->path_length) {
            Vec2i target = u->path[u->path_index];

            float dx = (float)target.x - u->target_pos.x;
            float dy = (float)target.y - u->target_pos.y;
            float m = sqrtf(dx * dx + dy * dy);

            if (m > 0.0f) {
                float step = u->move_speed * dt * 3.0f; /* Movement speed multiplier */
                u->target_pos.x += (dx / m) * step;
                u->target_pos.y += (dy / m) * step;
            }

            /* Check if reached this path node */
            float dist_to_node = sqrtf((target.x - (int)u->target_pos.x) * (target.x - (int)u->target_pos.x) +
            (target.y - (int)u->target_pos.y) * (target.y - (int)u->target_pos.y));

            if (dist_to_node < 0.5f) {
                u->path_index++;
                if (u->path_index >= u->path_length) {
                    u->state = STATE_IDLE;
                    u->target_pos = (Vec2f){(float)u->grid_pos.x, (float)u->grid_pos.y};
                    u->grid_pos = target; /* Update grid position */
                }
            }
        } else {
            u->state = STATE_IDLE;
            u->target_pos = (Vec2f){(float)u->grid_pos.x, (float)u->grid_pos.y};
        }
    }

    if (u->state == STATE_GATHERING && u->gather_target_id >= 0) {
        if (u->gather_target_id < game->deposit_count && game->deposits[u->gather_target_id].active) {
            ResourceDeposit* dep = &game->deposits[u->gather_target_id];
            int d2 = (int)distance_squared(u->grid_pos.x, u->grid_pos.y, dep->pos.x, dep->pos.y);

            if (d2 < 4) {
                int amount = 15;
                if (dep->resource_type == RESOURCE_WOOD) game->resources.wood += amount;
                else if (dep->resource_type == RESOURCE_GOLD) game->resources.gold += amount;
                else if (dep->resource_type == RESOURCE_STONE) game->resources.stone += amount;

                dep->amount -= amount;
                if (dep->amount <= 0) {
                    dep->active = 0;
                    game->map[dep->pos.y][dep->pos.x].type = TILE_GRASS;
                    game->map[dep->pos.y][dep->pos.x].walkable = 1;
                }
                u->state = STATE_IDLE;
                u->gather_target_id = -1;
            }
        } else {
            u->state = STATE_IDLE;
            u->gather_target_id = -1;
        }
    }
}

/* === AI System === */
unsigned int last_ai_action = 0;

void update_ai(Game* game, float dt) {
    (void)dt;
    unsigned int now = (unsigned int)(GetTime() * 1000);
    if (now - last_ai_action < 2500) return;
    last_ai_action = now;

    int villagers = 0, town_center_idx = -1;
    for (int i = 0; i < game->unit_count; i++) {
        if (game->units[i].faction == FACTION_AI && game->units[i].state != STATE_DEAD) {
            if (game->units[i].unit_type == UNIT_VILLAGER) villagers++;
        }
    }
    for (int i = 0; i < game->building_count; i++) {
        if (game->buildings[i].faction == FACTION_AI &&
            game->buildings[i].building_type == BUILDING_TOWN_CENTER &&
            game->buildings[i].constructed) {
            town_center_idx = i;
        break;
            }
    }

    /* Train villagers */
    if (villagers < 8 && town_center_idx >= 0 && game->resources.food >= 50 && game->unit_count < MAX_UNITS) {
        Unit* vu = &game->units[game->unit_count++];
        vu->id = ++game->unit_count;
        vu->faction = FACTION_AI;
        vu->unit_type = UNIT_VILLAGER;
        vu->grid_pos = game->buildings[town_center_idx].grid_pos;
        vu->target_pos = (Vec2f){(float)vu->grid_pos.x, (float)vu->grid_pos.y};
        vu->health = vu->max_health = 35;
        vu->damage = 5;
        vu->move_speed = 2.5f;
        vu->max_cooldown = 800;
        vu->attack_cooldown = 0;
        vu->state = STATE_IDLE;
        vu->gather_target_id = -1;
        vu->path_length = 0;
        vu->path_index = 0;
        vu->selected = 0;
        game->resources.food -= 50;
    }

    /* Assign villagers to gather */
    if (villagers >= 3) {
        int assigned = 0;
        for (int i = 0; i < game->unit_count && assigned < 2; i++) {
            if (game->units[i].faction == FACTION_AI &&
                game->units[i].unit_type == UNIT_VILLAGER &&
                game->units[i].state == STATE_IDLE &&
                game->units[i].gather_target_id < 0) {

                int best_deposit = -1;
            float best_dist = 999999.0f;
            for (int j = 0; j < game->deposit_count; j++) {
                if (game->deposits[j].active && game->deposits[j].resource_type == RESOURCE_WOOD) {
                    float d = distance_squared(game->units[i].grid_pos.x, game->units[i].grid_pos.y,
                                               game->deposits[j].pos.x, game->deposits[j].pos.y);
                    if (d < best_dist) {
                        best_dist = d;
                        best_deposit = j;
                    }
                }
            }

            if (best_deposit >= 0) {
                game->units[i].gather_target_id = best_deposit;
                game->units[i].state = STATE_GATHERING;
                assigned++;
            }
                }
        }
    }
}

/* === Combat === */
void update_combat(Game* game) {
    for (int i = 0; i < game->unit_count; i++) {
        Unit* attacker = &game->units[i];
        if (attacker->state == STATE_DEAD || attacker->attack_cooldown > 0) continue;

        for (int j = 0; j < game->unit_count; j++) {
            if (i == j) continue;
            Unit* defender = &game->units[j];
            if (defender->faction == attacker->faction || defender->state == STATE_DEAD) continue;

            float dist = distance_squared((int)attacker->target_pos.x, (int)attacker->target_pos.y,
                                          (int)defender->target_pos.x, (int)defender->target_pos.y);

            if (dist < 100) {
                int dmg = attacker->damage - defender->armor;
                if (dmg < 1) dmg = 1;
                defender->health -= dmg;
                attacker->attack_cooldown = attacker->max_cooldown;

                if (defender->health <= 0) {
                    defender->state = STATE_DEAD;
                    defender->health = 0;
                }
                break;
            }
        }
    }
}

void check_win_condition(Game* game) {
    if (game->game_over) return;

    int player_has_tc = 0, ai_has_tc = 0;
    for (int i = 0; i < game->building_count; i++) {
        if (game->buildings[i].building_type == BUILDING_TOWN_CENTER) {
            if (game->buildings[i].faction == FACTION_PLAYER && game->buildings[i].constructed)
                player_has_tc = 1;
            if (game->buildings[i].faction == FACTION_AI && game->buildings[i].constructed)
                ai_has_tc = 1;
        }
    }

    if (!player_has_tc) {
        game->game_over = 1;
        game->winner = FACTION_AI;
    } else if (!ai_has_tc) {
        game->game_over = 1;
        game->winner = FACTION_PLAYER;
    }
}

/* === Rendering === */
void render_map(Game* game) {
    for (int gy = 0; gy < MAP_SIZE; gy++) {
        for (int gx = 0; gx < MAP_SIZE; gx++) {
            int sx, sy;
            world_to_screen(gx, gy, &sx, &sy, game->camera, game->zoom);

            if (sx < -100 || sx > WINDOW_WIDTH + 100 || sy < -100 || sy > WINDOW_HEIGHT + 100)
                continue;

            draw_isometric_tile(sx, sy, game->map[gy][gx].type, game->zoom);
        }
    }
}

void render_building(Building* b, Game* game) {
    if (!b->constructed) return;

    int sx, sy;
    world_to_screen(b->grid_pos.x, b->grid_pos.y, &sx, &sy, game->camera, game->zoom);

    int w = (int)(b->width * TILE_WIDTH / 2.0f * game->zoom);
    int h = (int)(b->height * TILE_HEIGHT / 2.0f * game->zoom);

    Color col = b->faction == FACTION_PLAYER ?
    (Color){70, 180, 70, 255} :
    (Color){255, 215, 0, 255};

    Rectangle rect = {(float)(sx - w / 2), (float)(sy - h / 2), (float)w, (float)h};
    DrawRectangleRec(rect, col);

    /* Health bar */
    if (b->health < b->max_health) {
        int bw = (int)(w * 0.8);
        int bh = (int)(4 * game->zoom);
        int bx = sx - bw / 2;
        int by = sy - h / 2 - (int)(12 * game->zoom);

        Rectangle hb = {(float)bx, (float)by, (float)bw, (float)bh};
        DrawRectangleRec(hb, RED);

        int hw = (b->health * bw) / b->max_health;
        hb.width = (float)hw;
        DrawRectangleRec(hb, GREEN);
    }

    /* Selection indicator */
    if (b->selected) {
        Rectangle ob = {(float)(sx - w / 2 - 3), (float)(sy - h / 2 - 3), (float)(w + 6), (float)(h + 6)};
        DrawRectangleLinesEx(ob, 3, WHITE);
    }

    /* Building label */
    const char* label = b->building_type == BUILDING_TOWN_CENTER ? "TC" :
    b->building_type == BUILDING_HOUSE ? "H" : "B";
    DrawText(label, sx - 8, sy - h/2 - 20, 12, WHITE);
}

void render_unit(Unit* u, Game* game) {
    if (u->state == STATE_DEAD) return;

    int sx, sy;
    world_to_screen((int)u->target_pos.x, (int)u->target_pos.y, &sx, &sy, game->camera, game->zoom);

    int radius = (int)(8 * game->zoom);

    Color col;
    if (u->unit_type == UNIT_VILLAGER) {
        col = u->faction == FACTION_PLAYER ?
        (Color){100, 230, 100, 255} :
        (Color){255, 240, 100, 255};
    } else {
        col = u->faction == FACTION_PLAYER ?
        (Color){70, 130, 70, 255} :
        (Color){200, 170, 0, 255};
    }

    /* Draw unit circle */
    DrawCircle(sx, sy, radius, col);

    /* Health bar */
    if (u->health < u->max_health) {
        int bw = (int)(radius * 2 * game->zoom);
        int bh = (int)(4 * game->zoom);
        int bx = sx - bw / 2;
        int by = sy - radius - (int)(8 * game->zoom);

        Rectangle hb = {(float)bx, (float)by, (float)bw, (float)bh};
        DrawRectangleRec(hb, RED);

        int hw = (u->health * bw) / u->max_health;
        hb.width = (float)hw;
        DrawRectangleRec(hb, GREEN);
    }

    /* Selection indicator */
    if (u->selected) {
        int rr = radius + (int)(4 * game->zoom);
        DrawCircleLines(sx, sy, rr, WHITE);
    }
}

void render_minimap(Game* game) {
    int mmx = WINDOW_WIDTH - 140, mmy = WINDOW_HEIGHT - 140, mms = 130;

    Rectangle bg = {(float)mmx, (float)mmy, (float)mms, (float)mms};
    DrawRectangleRec(bg, (Color){50, 50, 50, 255});

    float scl = mms / (float)MAP_SIZE;

    /* Units */
    for (int i = 0; i < game->unit_count; i++) {
        if (game->units[i].state == STATE_DEAD) continue;
        int x = mmx + game->units[i].grid_pos.x * scl;
        int y = mmy + game->units[i].grid_pos.y * scl;
        Color col = game->units[i].faction == FACTION_PLAYER ?
        (Color){0, 255, 0, 255} :
        (Color){255, 200, 0, 255};
        DrawCircle(x, y, 2, col);
    }

    /* Buildings */
    for (int i = 0; i < game->building_count; i++) {
        if (!game->buildings[i].constructed) continue;
        int x = mmx + game->buildings[i].grid_pos.x * scl;
        int y = mmy + game->buildings[i].grid_pos.y * scl;
        int w = (int)(game->buildings[i].width * scl);
        int h = (int)(game->buildings[i].height * scl);
        Color col = game->buildings[i].faction == FACTION_PLAYER ?
        (Color){0, 150, 0, 255} :
        (Color){200, 170, 0, 255};
        Rectangle blk = {(float)x, (float)y, (float)w, (float)h};
        DrawRectangleRec(blk, col);
    }

    DrawRectangleLinesEx(bg, 1, WHITE);
    DrawText("Minimap", mmx, mmy - 20, 16, (Color){200, 200, 200, 255});
}

void render_ui(Game* game) {
    /* Bottom panel */
    Rectangle pnl = {0, (float)(WINDOW_HEIGHT - 80), (float)WINDOW_WIDTH, 80.0f};
    DrawRectangleRec(pnl, (Color){40, 40, 50, 255});
    DrawRectangleLinesEx(pnl, 2, (Color){80, 80, 100, 255});

    /* Resources */
    int rx = 30, ry = WINDOW_HEIGHT - 65;
    const char* res_names[4] = {"Food", "Wood", "Gold", "Stone"};
    int res_vals[4] = {game->resources.food, game->resources.wood,
        game->resources.gold, game->resources.stone};
        Color res_colors[4] = {
            (Color){255, 150, 150, 255},
            (Color){139, 90, 43, 255},
            (Color){255, 215, 0, 255},
            (Color){169, 169, 169, 255}
        };

        for (int i = 0; i < 4; i++) {
            Rectangle ibox = {(float)rx, (float)ry, 35.0f, 35.0f};
            DrawRectangleRec(ibox, res_colors[i]);

            char buf[16];
            snprintf(buf, sizeof(buf), "%d", res_vals[i]);
            DrawText(buf, rx + 40, ry + 10, 16, (Color){220, 220, 220, 255});
            DrawText(res_names[i], rx + 40, ry + 35, 14, (Color){180, 180, 180, 255});
            rx += 90;
        }

        rx += 15;
        char pop_buf[16];
        snprintf(pop_buf, sizeof(pop_buf), "Pop:%d/%d", game->resources.population_used,
                 game->resources.population_capacity);
        DrawText(pop_buf, rx, ry + 15, 16, (Color){220, 220, 220, 255});

        /* Training panel (appears when Town Center is selected) */
        if (game->selected_building && game->selected_building->building_type == BUILDING_TOWN_CENTER) {
            int px = 120, py = WINDOW_HEIGHT - 140;
            Rectangle tpnl = {(float)px, (float)py, 200.0f, 60.0f};
            DrawRectangleRec(tpnl, (Color){60, 60, 80, 255});
            DrawRectangleLinesEx(tpnl, 2, (Color){100, 150, 100, 255});

            DrawText("TRAIN UNITS", px + 20, py + 5, 14, (Color){100, 200, 100, 255});

            Vector2 mouse_pos = GetMousePosition();

            /* Train Villager button */
            Rectangle vb = {(float)px + 10, (float)py + 20, 80.0f, 30.0f};
            bool hover_vb = CheckCollisionPointRec(mouse_pos, vb);

            if (hover_vb) {
                DrawRectangleLinesEx(vb, 2, WHITE);
                if (game->resources.food >= 50) {
                    DrawRectangleRec(vb, (Color){100, 180, 100, 255});
                    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                        game->resources.food -= 50;
                        /* Spawn villager */
                        if (game->unit_count < MAX_UNITS && game->resources.population_used < game->resources.population_capacity) {
                            Unit* vu = &game->units[game->unit_count++];
                            vu->id = ++game->unit_count;
                            vu->faction = FACTION_PLAYER;
                            vu->unit_type = UNIT_VILLAGER;
                            vu->grid_pos = game->selected_building->grid_pos;
                            vu->target_pos = (Vec2f){(float)vu->grid_pos.x, (float)vu->grid_pos.y};
                            vu->health = vu->max_health = 35;
                            vu->damage = 5; vu->move_speed = 2.5f;
                            vu->attack_cooldown = 0; vu->max_cooldown = 800;
                            vu->state = STATE_IDLE;
                            vu->gather_target_id = -1;
                            vu->path_length = 0;
                            vu->path_index = 0;
                            vu->selected = 0;
                            game->resources.population_used++;
                        }
                    }
                }
            } else {
                if (game->resources.food >= 50) {
                    DrawRectangleRec(vb, (Color){80, 150, 80, 255});
                } else {
                    DrawRectangleRec(vb, (Color){50, 50, 50, 255});
                }
            }
            DrawText("Villager (50 Food)", px + 15, py + 25, 12, (Color){255, 255, 255, 255});

            /* Train Soldier button */
            Rectangle sb = {(float)px + 110, (float)py + 20, 80.0f, 30.0f};
            bool hover_sb = CheckCollisionPointRec(mouse_pos, sb);

            if (hover_sb) {
                DrawRectangleLinesEx(sb, 2, WHITE);
                if (game->resources.food >= 80) {
                    DrawRectangleRec(sb, (Color){100, 180, 100, 255});
                    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                        game->resources.food -= 80;
                        if (game->unit_count < MAX_UNITS && game->resources.population_used < game->resources.population_capacity) {
                            Unit* su = &game->units[game->unit_count++];
                            su->id = ++game->unit_count;
                            su->faction = FACTION_PLAYER;
                            su->unit_type = UNIT_SOLDIER;
                            su->grid_pos = game->selected_building->grid_pos;
                            su->target_pos = (Vec2f){(float)su->grid_pos.x, (float)su->grid_pos.y};
                            su->health = su->max_health = 85;
                            su->damage = 18; su->armor = 4; su->range = 1;
                            su->move_speed = 1.8f;
                            su->attack_cooldown = 0; su->max_cooldown = 1200;
                            su->state = STATE_IDLE;
                            su->gather_target_id = -1;
                            su->path_length = 0;
                            su->path_index = 0;
                            su->selected = 0;
                            game->resources.population_used++;
                        }
                    }
                }
            } else {
                if (game->resources.food >= 80) {
                    DrawRectangleRec(sb, (Color){80, 150, 80, 255});
                } else {
                    DrawRectangleRec(sb, (Color){50, 50, 50, 255});
                }
            }
            DrawText("Soldier (80 Food)", px + 115, py + 25, 12, (Color){255, 255, 255, 255});
        }

        /* Game Over screen */
        if (game->game_over) {
            Rectangle ov = {0, 0, (float)WINDOW_WIDTH, (float)WINDOW_HEIGHT};
            DrawRectangleRec(ov, (Color){0, 0, 0, 180});

            const char* msg = game->winner == FACTION_PLAYER ?
            "VICTORY! Enemy Castle Destroyed!" :
            "DEFEAT! Your Castle Destroyed!";
            int ox = WINDOW_WIDTH / 2 - 320, oy = WINDOW_HEIGHT / 2 - 60;
            Rectangle box = {(float)ox, (float)oy, 640.0f, 120.0f};
            DrawRectangleRec(box, (Color){50, 50, 60, 255});
            DrawRectangleLinesEx(box, 2, (Color){100, 100, 120, 255});

            Color textColor = game->winner == FACTION_PLAYER ?
            (Color){0, 255, 100, 255} :
            (Color){255, 100, 0, 255};
            DrawText(msg, ox + 20, oy + 20, 24, textColor);
            DrawText("Press ESC to quit", ox + 200, oy + 60, 18, (Color){200, 200, 200, 255});
        }

        if (game->paused) {
            Rectangle pv = {0, 0, (float)WINDOW_WIDTH, (float)WINDOW_HEIGHT};
            DrawRectangleRec(pv, (Color){0, 0, 0, 150});
            DrawText("PAUSED - Press SPACE", WINDOW_WIDTH / 2 - 150, WINDOW_HEIGHT / 2, 24, WHITE);
        }

        /* Instructions */
        DrawText("Click Town Center to train units!", 30, WINDOW_HEIGHT - 20, 12, (Color){200, 200, 150, 255});
}

void render_selection_box(Game* game) {
    if (!game->selecting) return;

    int minx = game->sel_start_x < game->mouse_x ? game->sel_start_x : game->mouse_x;
    int maxx = game->sel_start_x < game->mouse_x ? game->mouse_x : game->sel_start_x;
    int miny = game->sel_start_y < game->mouse_y ? game->sel_start_y : game->mouse_y;
    int maxy = game->sel_start_y < game->mouse_y ? game->mouse_y : game->sel_start_y;

    Rectangle box = {(float)minx, (float)miny, (float)(maxx - minx), (float)(maxy - miny)};
    DrawRectangleLinesEx(box, 2, (Color){100, 200, 255, 200});
    Color semi = (Color){100, 200, 255, 80};
    DrawRectangleRec(box, semi);
}

/* === Input Handling === */
void handle_input(Game* game) {
    if (IsKeyPressed(KEY_ESCAPE)) {
        CloseWindow();
    }
    if (IsKeyPressed(KEY_SPACE)) {
        game->paused = !game->paused;
    }

    /* Camera movement */
    int speed = 20;
    if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP))
        game->camera.y = clamp_int(game->camera.y - speed, -3000, 3000);
    if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN))
        game->camera.y = clamp_int(game->camera.y + speed, -3000, 3000);
    if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT))
        game->camera.x = clamp_int(game->camera.x - speed, -3000, 3000);
    if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT))
        game->camera.x = clamp_int(game->camera.x + speed, -3000, 3000);

    /* Zoom */
    int wheel = GetMouseWheelMove();
    if (wheel != 0) {
        if (wheel > 0)
            game->zoom = clamp_float(game->zoom + 0.1f, 0.5f, 2.0f);
        else
            game->zoom = clamp_float(game->zoom - 0.1f, 0.5f, 2.0f);
    }

    /* Mouse */
    game->mouse_x = GetMousePosition().x;
    game->mouse_y = GetMousePosition().y;

    /* Clear previous selection */
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        game->selecting = 1;
        game->sel_start_x = game->mouse_x;
        game->sel_start_y = game->mouse_y;

        game->selected_building = NULL;
        for (int i = 0; i < game->unit_count; i++) {
            game->units[i].selected = 0;
        }
    }

    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        game->selecting = 0;

        int ex = game->mouse_x;
        int ey = game->mouse_y;

        int minx = game->sel_start_x < ex ? game->sel_start_x : ex;
        int maxx = game->sel_start_x < ex ? ex : game->sel_start_x;
        int miny = game->sel_start_y < ey ? game->sel_start_y : ey;
        int maxy = game->sel_start_y < ey ? ey : game->sel_start_y;

        /* Single click */
        if (maxx - minx < 6 && maxy - miny < 6) {
            /* Check buildings first */
            for (int b = game->building_count - 1; b >= 0; b--) {
                if (game->buildings[b].faction != FACTION_PLAYER || !game->buildings[b].constructed) continue;

                int sx, sy;
                world_to_screen(game->buildings[b].grid_pos.x, game->buildings[b].grid_pos.y,
                                &sx, &sy, game->camera, game->zoom);

                if (abs(sx - minx) < 40 && abs(sy - miny) < 40) {
                    game->selected_building = &game->buildings[b];
                    game->buildings[b].selected = 1;
                    break;
                }
            }

            /* Check units if no building clicked */
            if (!game->selected_building) {
                for (int i = 0; i < game->unit_count; i++) {
                    if (game->units[i].faction != FACTION_PLAYER || game->units[i].state == STATE_DEAD) continue;

                    int sx, sy;
                    world_to_screen((int)game->units[i].target_pos.x, (int)game->units[i].target_pos.y,
                                    &sx, &sy, game->camera, game->zoom);

                    if (abs(sx - minx) < 20 && abs(sy - miny) < 20) {
                        game->units[i].selected = 1;
                        break;
                    }
                }
            }
        } else {
            /* Box select - only units */
            for (int i = 0; i < game->unit_count; i++) {
                if (game->units[i].faction != FACTION_PLAYER || game->units[i].state == STATE_DEAD) continue;

                int ux, uy;
                world_to_screen((int)game->units[i].target_pos.x, (int)game->units[i].target_pos.y,
                                &ux, &uy, game->camera, game->zoom);

                if (ux >= minx && ux <= maxx && uy >= miny && uy <= maxy) {
                    game->units[i].selected = 1;
                }
            }
        }
    }

    /* Right click - move/attack/gather */
    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
        int gx, gy;
        screen_to_world(game->mouse_x, game->mouse_y, &gx, &gy, game->camera, game->zoom);
        Vec2i click_pos = {gx, gy};

        /* Get all selected units */
        int has_selected = 0;
        for (int i = 0; i < game->unit_count; i++) {
            if (game->units[i].selected && game->units[i].state != STATE_DEAD && game->units[i].faction == FACTION_PLAYER) {
                has_selected = 1;
                break;
            }
        }

        if (!has_selected) return;

        for (int i = 0; i < game->unit_count; i++) {
            if (!game->units[i].selected || game->units[i].state == STATE_DEAD || game->units[i].faction != FACTION_PLAYER) continue;

            int attacking = 0;
            for (int j = 0; j < game->unit_count; j++) {
                if (game->units[j].faction != FACTION_PLAYER &&
                    game->units[j].state != STATE_DEAD &&
                    distance_squared(gx, gy, game->units[j].grid_pos.x, game->units[j].grid_pos.y) < 9) {

                    int path_len = find_path(game, game->units[i].grid_pos,
                                             game->units[j].grid_pos,
                                             game->units[i].path, MAX_PATH_LEN);
                    if (path_len > 0) {
                        game->units[i].path_length = path_len;
                        game->units[i].path_index = 0;
                        game->units[i].state = STATE_MOVING;
                        game->units[i].target_pos = (Vec2f){(float)game->units[j].grid_pos.x, (float)game->units[j].grid_pos.y};
                    }
                    attacking = 1;
                    break;
                    }
            }

            if (!attacking) {
                int gather = 0;
                for (int d = 0; d < game->deposit_count; d++) {
                    if (game->deposits[d].active &&
                        distance_squared(gx, gy, game->deposits[d].pos.x, game->deposits[d].pos.y) < 16) {
                        if (game->units[i].unit_type == UNIT_VILLAGER) {
                            game->units[i].gather_target_id = d;
                            game->units[i].state = STATE_GATHERING;
                            gather = 1;
                            break;
                        }
                        }
                }

                if (!gather) {
                    int path_len = find_path(game, game->units[i].grid_pos,
                                             click_pos,
                                             game->units[i].path, MAX_PATH_LEN);
                    if (path_len > 0) {
                        game->units[i].path_length = path_len;
                        game->units[i].path_index = 0;
                        game->units[i].state = STATE_MOVING;
                        game->units[i].target_pos = (Vec2f){(float)gx, (float)gy};
                    }
                }
            }
        }
    }
}

/* === Main Entry Point === */
int main(void) {
    printf("\n========================================\n");
    printf("  Medieval Kingdoms - Isometric RTS\n");
    printf("  FIXED: Unit movement + training\n");
    printf("========================================\n\n");

    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Medieval Kingdoms");
    SetTargetFPS(FPS);

    Font default_font = GetFontDefault();
    (void)default_font;

    printf("CONTROLS:\n");
    printf("  Left Click: Select unit/building\n");
    printf("  Drag: Box-select units\n");
    printf("  Right Click: Move/Attack/Gather\n");
    printf("  WASD/Arrows: Pan camera\n");
    printf("  Scroll: Zoom in/out\n");
    printf("  Space: Pause\n");
    printf("  ESC: Quit\n\n");
    printf("HOW TO PLAY:\n");
    printf("  1. Click your GREEN Town Center (3x3 square)\n");
    printf("  2. Use the TRAIN PANEL to train Villagers/Soldiers\n");
    printf("  3. Select villagers and right-click trees/resources to gather\n");
    printf("  4. Build up your army and destroy the YELLOW Town Center!\n\n");
    printf("========================================\n\n");

    Game game;
    init_game(&game);

    float target_frame_time = 1.0f / FPS;
    float accumulator = 0.0f;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        accumulator += dt;

        while (accumulator >= target_frame_time) {
            if (!game.paused && !game.game_over) {
                for (int i = 0; i < game.unit_count; i++) {
                    update_unit(&game.units[i], &game, target_frame_time);
                }
                update_ai(&game, target_frame_time);
                update_combat(&game);
                check_win_condition(&game);
            }
            handle_input(&game);
            accumulator -= target_frame_time;
        }

        BeginDrawing();
        ClearBackground((Color){20, 20, 30, 255});

        render_map(&game);

        /* Depth-sort buildings */
        static int build_order[MAX_BUILDINGS];
        for (int i = 0; i < game.building_count; i++) build_order[i] = i;
        for (int i = 0; i < game.building_count - 1; i++) {
            for (int j = i + 1; j < game.building_count; j++) {
                if (game.buildings[build_order[i]].grid_pos.y > game.buildings[build_order[j]].grid_pos.y) {
                    int temp = build_order[i];
                    build_order[i] = build_order[j];
                    build_order[j] = temp;
                }
            }
        }
        for (int i = 0; i < game.building_count; i++) {
            render_building(&game.buildings[build_order[i]], &game);
        }

        /* Depth-sort units */
        static int unit_order[MAX_UNITS];
        for (int i = 0; i < game.unit_count; i++) unit_order[i] = i;
        for (int i = 0; i < game.unit_count - 1; i++) {
            for (int j = i + 1; j < game.unit_count; j++) {
                if (game.units[unit_order[i]].grid_pos.y > game.units[unit_order[j]].grid_pos.y) {
                    int temp = unit_order[i];
                    unit_order[i] = unit_order[j];
                    unit_order[j] = temp;
                }
            }
        }
        for (int i = 0; i < game.unit_count; i++) {
            render_unit(&game.units[unit_order[i]], &game);
        }

        render_selection_box(&game);
        render_ui(&game);
        render_minimap(&game);

        EndDrawing();
    }

    CloseWindow();
    printf("Game ended. Thanks for playing!\n");
    return 0;
}
