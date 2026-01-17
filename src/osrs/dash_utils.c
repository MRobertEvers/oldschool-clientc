#ifndef DASH_UTILS_C
#define DASH_UTILS_C

#include "dash_utils.h"

struct DashFramemap*
dashframemap_new_from_cache_framemap(struct CacheFramemap* framemap)
{
    struct DashFramemap* dashframemap = malloc(sizeof(struct DashFramemap));
    memset(dashframemap, 0, sizeof(struct DashFramemap));
    dashframemap->id = framemap->id;
    dashframemap->length = framemap->length;

    dashframemap->bone_groups = malloc(framemap->length * sizeof(int*));
    memcpy(dashframemap->bone_groups, framemap->bone_groups, framemap->length * sizeof(int*));

    dashframemap->bone_groups_lengths = malloc(framemap->length * sizeof(int));
    memcpy(
        dashframemap->bone_groups_lengths,
        framemap->bone_groups_lengths,
        framemap->length * sizeof(int));

    dashframemap->types = malloc(framemap->length * sizeof(int));
    memcpy(dashframemap->types, framemap->types, framemap->length * sizeof(int));
    return dashframemap;
}

struct DashFrame*
dashframe_new_from_cache_frame(struct CacheFrame* frame)
{
    struct DashFrame* dashframe = malloc(sizeof(struct DashFrame));
    memset(dashframe, 0, sizeof(struct DashFrame));
    dashframe->_id = frame->_id;
    dashframe->framemap_id = frame->framemap_id;
    dashframe->translator_count = frame->translator_count;

    dashframe->index_frame_ids = malloc(frame->translator_count * sizeof(int));
    memcpy(
        dashframe->index_frame_ids, frame->index_frame_ids, frame->translator_count * sizeof(int));

    dashframe->translator_arg_x = malloc(frame->translator_count * sizeof(int));
    memcpy(
        dashframe->translator_arg_x,
        frame->translator_arg_x,
        frame->translator_count * sizeof(int));

    dashframe->translator_arg_y = malloc(frame->translator_count * sizeof(int));
    memcpy(
        dashframe->translator_arg_y,
        frame->translator_arg_y,
        frame->translator_count * sizeof(int));

    dashframe->translator_arg_z = malloc(frame->translator_count * sizeof(int));
    memcpy(
        dashframe->translator_arg_z,
        frame->translator_arg_z,
        frame->translator_count * sizeof(int));

    dashframe->showing = frame->showing;
    return dashframe;
}

#endif