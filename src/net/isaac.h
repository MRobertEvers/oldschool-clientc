#ifndef ISAAC_H
#define ISAAC_H

#include <stddef.h>

/**
 * ISAAC is a PRNG.
 * https://en.wikipedia.org/wiki/ISAAC_(cipher)
 */

struct Isaac;

struct Isaac*
isaac_new(
    int* seed,
    int seed_length);

void
isaac_seed(
    struct Isaac* isaac,
    int* seed,
    int seed_length);

void
isaac_free(struct Isaac* isaac);

int
isaac_next(struct Isaac* isaac);

/** Byte size of serialized ISAAC state for dump/restore */
size_t
isaac_state_size(void);

/** Write current ISAAC state into buf (must have isaac_state_size() bytes) */
void
isaac_get_state(
    struct Isaac* isaac,
    void* buf);

/** Restore ISAAC state from buf (must have isaac_state_size() bytes). Returns new Isaac* or NULL.
 */
struct Isaac*
isaac_from_state(const void* buf);

#endif