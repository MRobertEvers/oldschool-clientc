#ifndef LIBGAME_H
#define LIBGAME_H

#include <stdbool.h>

struct GameGfxOpList;
struct GameGfxOp;
struct Game;
struct GameIO;

struct GameGfxOpList* game_gfx_op_list_new(int capacity_hint);
void game_gfx_op_list_free(struct GameGfxOpList* list);
void game_gfx_op_list_reset(struct GameGfxOpList* list);
int game_gfx_op_list_push(struct GameGfxOpList* list, struct GameGfxOp* op);

struct Game* game_new(int flags, struct GameGfxOpList* gfx_op_list);
void game_init(struct Game* game, struct GameIO* input, struct GameGfxOpList* gfx_op_list);
void game_free(struct Game* game);

void
game_step_main_loop(struct Game* game, struct GameIO* input, struct GameGfxOpList* out_commands);

bool game_is_running(struct Game* game);

#endif