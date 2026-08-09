#ifndef INTERFACE161_TEST_FIXTURE_H
#define INTERFACE161_TEST_FIXTURE_H

#include "osrs/rscache/dat2disk/dat2disk.h"
#include "toridraw/toridraw.h"

#include <stddef.h>

struct Interface161FixtureSlot
{
    int file_index;
    int obj_id;
};

struct Interface161Fixture
{
    struct Interface161FixtureSlot* slots;
    int slot_count;
};

struct Interface161ObjIconCache
{
    int obj_id;
    int* pixels;
    struct Interface161ObjIconCache* next;
};

void
interface161_fixture_init(struct Interface161Fixture* fx);

void
interface161_fixture_free(struct Interface161Fixture* fx);

int
interface161_fixture_load(
    struct Interface161Fixture* fx,
    const char* path);

int
interface161_fixture_obj_for_file(
    const struct Interface161Fixture* fx,
    int file_index);

void
interface161_obj_icon_cache_free(struct Interface161ObjIconCache* cache);

/** Returns 32×32 ARGB pixels (caller must not free; cached for process lifetime). */
const int*
interface161_obj_icon_get(
    struct RSCacheDat2Disk* cache,
    struct ToriDraw_Scene* scene,
    struct Interface161ObjIconCache** cache_head,
    int obj_id);

#endif
