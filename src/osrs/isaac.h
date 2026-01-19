#ifndef ISAAC_H
#define ISAAC_H

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
isaac_free(struct Isaac* isaac);

int
isaac_next(struct Isaac* isaac);

#endif