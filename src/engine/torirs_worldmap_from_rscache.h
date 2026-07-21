#ifndef TORIRS_WORLDMAP_FROM_RSCACHE_H
#define TORIRS_WORLDMAP_FROM_RSCACHE_H

#include <stdbool.h>

struct RSCache_WorldMapArea;
struct RSCache_MapElement;
struct ToriRS_WorldMapArea;
struct ToriRS_WorldMapAreas;
struct ToriRS_MapElement;

/**
 * Convert decoded cache areas into the engine's world map. Takes ownership of
 * nothing: `entries` stays the caller's to free. Returns NULL when `count` is
 * zero.
 */
struct ToriRS_WorldMapAreas*
ToriRS_WorldMapAreasFromRSCacheDat2(
    struct RSCache_WorldMapArea const* entries,
    int count);

void
ToriRS_WorldMapAreasFree(struct ToriRS_WorldMapAreas* areas);

struct ToriRS_WorldMapArea*
ToriRS_WorldMapAreasGet(
    struct ToriRS_WorldMapAreas* areas,
    int id);

/* Packed world map coord: plane<<28 | x<<14 | y. */
int
ToriRS_WorldMapPackCoord(int plane, int x, int y);
void
ToriRS_WorldMapUnpackCoord(int packed, int* plane, int* x, int* y);

/** Is (x, y) inside the area's map surface. */
bool
ToriRS_WorldMapArea_ContainsPosition(
    struct ToriRS_WorldMapArea const* area,
    int x,
    int y);

/** Does the area cover the real world coord (plane, x, y). */
bool
ToriRS_WorldMapArea_ContainsCoord(
    struct ToriRS_WorldMapArea const* area,
    int plane,
    int x,
    int y);

/** Map surface position -> world coord. False when off the surface. */
bool
ToriRS_WorldMapArea_Coord(
    struct ToriRS_WorldMapArea const* area,
    int x,
    int y,
    int* out_plane,
    int* out_x,
    int* out_y);

/** World coord -> map surface position. False when the area does not cover it. */
bool
ToriRS_WorldMapArea_Position(
    struct ToriRS_WorldMapArea const* area,
    int plane,
    int x,
    int y,
    int* out_x,
    int* out_y);

int
ToriRS_WorldMapArea_WidthTiles(struct ToriRS_WorldMapArea const* area);
int
ToriRS_WorldMapArea_HeightTiles(struct ToriRS_WorldMapArea const* area);
void
ToriRS_WorldMapArea_Bounds(
    struct ToriRS_WorldMapArea const* area,
    int* min_x,
    int* min_y,
    int* max_x,
    int* max_y);

/** Convert one decoded map element config. */
struct ToriRS_MapElement*
ToriRS_MapElementFromRSCacheDat2(
    int element_id,
    struct RSCache_MapElement* entry);

void
ToriRS_MapElementFree(struct ToriRS_MapElement* element);

#endif /* TORIRS_WORLDMAP_FROM_RSCACHE_H */
