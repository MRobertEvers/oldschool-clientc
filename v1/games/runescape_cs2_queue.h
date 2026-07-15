#ifndef RUNESCAPE_CS2_QUEUE_H
#define RUNESCAPE_CS2_QUEUE_H

#include <stdbool.h>

#define RS_CS2_QUEUE_MAX 256
#define RS_CS2_QUEUE_INT_ARG_MAX 16
#define RS_CS2_QUEUE_STR_ARG_MAX 8

struct GameRunescapeCS2Invoke
{
    int script_id;
    int component_id;
    int int_args[RS_CS2_QUEUE_INT_ARG_MAX];
    int int_arg_count;
    char* str_args[RS_CS2_QUEUE_STR_ARG_MAX]; /* strdup'd */
    int str_arg_count;
};

struct GameRunescapeCS2Queue
{
    struct GameRunescapeCS2Invoke entries[RS_CS2_QUEUE_MAX];
    int head;
    int tail;
    int count;
};

void
GameRunescape_CS2QueueInit(struct GameRunescapeCS2Queue* q);

void
GameRunescape_CS2QueueFree(struct GameRunescapeCS2Queue* q);

/** Returns false if full or invalid args. Copies int_args; strdup's each str. */
bool
GameRunescape_CS2QueueEnqueue(
    struct GameRunescapeCS2Queue* q,
    int script_id,
    int component_id,
    int const* int_args,
    int int_arg_count,
    char const* const* str_args,
    int str_arg_count);

struct GameRunescapeCS2Invoke const*
GameRunescape_CS2QueuePeek(struct GameRunescapeCS2Queue const* q);

/** Removes head; frees strdup'd strings in the popped entry. */
bool
GameRunescape_CS2QueuePop(struct GameRunescapeCS2Queue* q);

bool
GameRunescape_CS2QueueIsEmpty(struct GameRunescapeCS2Queue const* q);

int
GameRunescape_CS2QueueCount(struct GameRunescapeCS2Queue const* q);

#endif /* RUNESCAPE_CS2_QUEUE_H */
