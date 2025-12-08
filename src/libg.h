#ifndef LIBG_H
#define LIBG_H

#include "osrs/ginput.h"
#include "osrs/gio.h"

#include <stdbool.h>
#include <stdint.h>

struct GGame;

struct GGame* libg_game_new(void);
void libg_game_free(struct GGame* game);
void libg_game_step(struct GGame* game, struct GIOQueue* queue, struct GInput* input);

bool libg_game_is_running(struct GGame* game);

#endif