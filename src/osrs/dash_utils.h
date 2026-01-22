#ifndef DASH_UTILS_H
#define DASH_UTILS_H

#include "graphics/dash.h"
#include "osrs/rscache/tables/frame.h"
#include "osrs/rscache/tables/framemap.h"
#include "osrs/rscache/tables_dat/animframe.h"

struct CacheFrame;
struct CacheFramemap;

struct DashFramemap*
dashframemap_new_from_cache_framemap(struct CacheFramemap* framemap);

struct DashFrame*
dashframe_new_from_cache_frame(struct CacheFrame* frame);

struct DashFrame*
dashframe_new_from_animframe(struct CacheAnimframe* animframe);

struct DashFramemap*
dashframemap_new_from_animframe(struct CacheAnimframe* animframe);

#endif