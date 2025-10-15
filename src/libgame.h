#ifndef LIBGAME_H
#define LIBGAME_H

#include <stdbool.h>

struct Game;
struct GameInput;

struct Game* game_new(void);

void game_free(struct Game* game);

bool game_init(struct Game* game);

void game_process_input(struct Game* game, struct GameInput* input);

void game_step_main_loop(struct Game* game);

bool game_is_running(struct Game* game);

#endif