#ifndef CONFIGMAP_H
#define CONFIGMAP_H

#include "archive.h"
#include "cache.h"
#include "tables/configs.h"

/**
 * Entries in the config table are files with an id.
 *
 * This is a generic container for config table entries.
 *
 * Note: Archives in the config table map to a single container.
 *
 */

struct ConfigMap;

struct ConfigMapIter
{
    struct HMapIter* hmap_iter;
};

struct ConfigMap* configmap_new_from_archive(struct Cache* cache, struct CacheArchive* archive);
void configmap_free(struct ConfigMap* configmap);

enum ConfigKind configmap_kind(struct ConfigMap* configmap);
void* configmap_get(struct ConfigMap* configmap, int id);

struct ConfigMapIter* configmap_iter_new(struct ConfigMap* configmap);
void configmap_iter_free(struct ConfigMapIter* iter);
void* configmap_iter_next(struct ConfigMapIter* iter);

#endif