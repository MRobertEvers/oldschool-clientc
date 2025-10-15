#include "libinput.h"

#include <stdlib.h>
#include <string.h>

struct GameInput*
GameInput_New(void)
{
    struct GameInput* input = (struct GameInput*)malloc(sizeof(struct GameInput));
    memset(input, 0, sizeof(struct GameInput));
    return input;
}

void
GameInput_Free(struct GameInput* input)
{
    free(input);
}