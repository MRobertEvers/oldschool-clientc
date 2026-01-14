#ifndef CONFIGMAP_H
#define CONFIGMAP_H

#include "archive.h"
#include "cache.h"
#include "graphics/dash.h"
#include "tables/configs.h"

#include <stdbool.h>

/**
 * Entries in the config table are files with an id.
 *
 * This is a generic container for config table entries.
 *
 * Note: Archives in the config table map to a single container.
 *
 */
struct ConfigMapPacked
{
    void* data;
    int data_size;
};

struct ConfigMapPacked*
configmap_packed_new(
    struct Cache* cache,
    struct CacheArchive* archive);

void
configmap_packed_free(struct ConfigMapPacked* packed);

struct DashMap*
configmap_new_from_packed(
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
configmap_iter_next(struct DashMapIter* iter);

bool
configmap_valid(struct DashMap* configmap);
#endif