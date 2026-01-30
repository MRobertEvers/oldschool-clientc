#ifndef CONFIGMAP_H
#define CONFIGMAP_H

#include "rscache/cache.h"
#include "graphics/dash.h"

#include <stdbool.h>

/**
 * Entries in the config table are files with an id.
 *
 * This is a generic container for config table entries.
 *
 * Note: Archives in the config table map to a single container.
 *
 */
struct DashMap*
configmap_new_from_filepack(
    void* data,
    int data_size,
    int* ids_nullable,
    int ids_size);

void
configmap_free(struct DashMap* configmap);

void*
configmap_get(
    struct DashMap* configmap,
    int id);

void*
configmap_iter_next(
    struct DashMapIter* iter,
    int* out_id);

bool
configmap_valid(struct DashMap* configmap);
#endif