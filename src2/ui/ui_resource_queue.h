#ifndef UI_RESOURCE_QUEUE_H
#define UI_RESOURCE_QUEUE_H

#include "toridraw/toridraw_sprite.h"

#include <stdbool.h>
#include <stdint.h>

#define UI_RESOURCE_QUEUE_MAX_SIZE 256

enum UIResourceQueueItem_Status
{
    UIRES_PENDING = 0,
    UIRES_RESOLVED,
    UIRES_ERROR,
};

enum UIResourceQueueItem_Kind
{
    UIRES_KIND_SPRITE = 1,
};

struct UIResourceQueueItem
{
    enum UIResourceQueueItem_Kind kind;
    char name[64];
    char table[64];
    char archive[64];
    char container[64];
    char index_filename[64];
    char data_filename[64];
    char format[16];
    int atlas_index;
    int atlas_count;
    bool atlas_use_count;
    /** Index into UILoaderState::pending_archives; -1 if unresolved. */
    int source_archive_index;

    enum UIResourceQueueItem_Status status;
    int error_code;

    struct ToriDraw_Sprite** result_sprites;
    int result_count;
};

struct UIResourceQueue
{
    struct UIResourceQueueItem items[UI_RESOURCE_QUEUE_MAX_SIZE];
    int count;
};

struct UIResourceQueue*
ui_resource_queue_new(void);

void
ui_resource_queue_free(struct UIResourceQueue* queue);

void
ui_resource_queue_clear(struct UIResourceQueue* queue);

bool
ui_resource_queue_push_sprite(
    struct UIResourceQueue* queue,
    const struct UIResourceQueueItem* item);

struct UIResourceQueueItem*
ui_resource_queue_find_by_name(
    struct UIResourceQueue* queue,
    const char* name);

#endif
