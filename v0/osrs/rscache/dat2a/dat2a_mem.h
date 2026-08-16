#ifndef RSCACHE_DAT2A_MEM_H
#define RSCACHE_DAT2A_MEM_H

#include <stddef.h>

struct RSCacheDat2A_Model;
struct RSCacheDat2A_MapTerrain;
struct RSCacheDat2A_MapLocs;
struct RSCacheDat2A_ConfigSequence;
struct RSCacheDat2A_ConfigOverlay;
struct RSCacheDat2A_ConfigUnderlay;
struct RSCacheDat2A_ConfigLocation;
struct RSCacheDat2A_AnimMaya;
struct RSCacheDat2A_ConfigIdk;
struct RSCacheDat2A_ConfigObject;
struct RSCacheDat2A_ConfigNpctype;
struct RSCacheDat2A_Frame;
struct RSCacheDat2A_Framemap;
struct Dat2BuildCache_FramesArchive;

size_t
RSCacheDat2A_ModelSizeOf(const struct RSCacheDat2A_Model* model);

size_t
RSCacheDat2A_MapTerrainSizeOf(const struct RSCacheDat2A_MapTerrain* map_terrain);

size_t
RSCacheDat2A_MapLocsSizeOf(const struct RSCacheDat2A_MapLocs* map_locs);

size_t
RSCacheDat2A_ConfigSequenceSizeOf(const struct RSCacheDat2A_ConfigSequence* sequence);

size_t
RSCacheDat2A_ConfigOverlaySizeOf(const struct RSCacheDat2A_ConfigOverlay* overlay);

size_t
RSCacheDat2A_ConfigUnderlaySizeOf(const struct RSCacheDat2A_ConfigUnderlay* underlay);

size_t
RSCacheDat2A_ConfigLocationSizeOf(const struct RSCacheDat2A_ConfigLocation* loc);

size_t
RSCacheDat2A_AnimMayaSizeOf(const struct RSCacheDat2A_AnimMaya* maya);

size_t
RSCacheDat2A_ConfigIdkSizeOf(const struct RSCacheDat2A_ConfigIdk* idk);

size_t
RSCacheDat2A_ConfigObjectSizeOf(const struct RSCacheDat2A_ConfigObject* object);

size_t
RSCacheDat2A_ConfigNpctypeSizeOf(const struct RSCacheDat2A_ConfigNpctype* npc);

size_t
RSCacheDat2A_FrameSizeOf(const struct RSCacheDat2A_Frame* frame);

size_t
RSCacheDat2A_FramemapSizeOf(const struct RSCacheDat2A_Framemap* framemap);

#endif
