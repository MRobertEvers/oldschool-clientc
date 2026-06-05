#ifndef CORE_TASKS_REVCONFIG_H
#define CORE_TASKS_REVCONFIG_H

#include "../../core/core_io.h"
#include "core_task.h"
#include "osrs/revconfig/revconfig.h"
#include "osrs/revconfig/revconfig_load.h"

#include <assert.h>

enum InstanceLoader_WorkKind
{
    INSTANCELOADER_WORK_PROCESS_REVCONFIG = 0,

};

struct InstanceLoader_WorkItem_RevConfig
{
    struct RevConfigBuffer* revconfig_buffer;
};

struct InstanceLoader_WorkItem
{
    enum InstanceLoader_WorkKind kind;
    union
    {
        struct InstanceLoader_WorkItem_RevConfig revconfig;
    } u;
};

struct InstanceLoader
{
    struct InstanceLoader_WorkItem work_items[64];
    int work_head;
    int work_count;
};

static void
instance_loader_push_workitem_revconfig(
    struct InstanceLoader* loader,
    struct RevConfigBuffer* revconfig_buffer)
{
    struct InstanceLoader_WorkItem work_item;
    work_item.kind = INSTANCELOADER_WORK_PROCESS_REVCONFIG;
    work_item.u.revconfig.revconfig_buffer = revconfig_buffer;
    loader->work_items[loader->work_count++] = work_item;
}

static void
instance_loader_process_revconfig(
    struct InstanceLoader* loader,
    struct RevConfigBuffer* revconfig_buffer)
{
    for( int i = 0; i < revconfig_buffer->field_count; i++ )
    {
        struct RevConfigField* field = &revconfig_buffer->fields[i];
        switch( field->kind )
        {
        case RCFIELD_ITEMTYPE:
            break;
        case RCFIELD_ITEMNAME:
            break;
        case RCFIELD_ITEMDONE:
            break;
        case RCFIELD_CACHE_TABLE:
            break;
        case RCFIELD_CACHE_ARCHIVE:
            break;
        case RCFIELD_CACHE_CONTAINER:
            break;
        case RCFIELD_CACHE_INDEX_FILENAME:
            break;
        case RCFIELD_CACHE_DATA_FILENAME:
            break;
        case RCFIELD_CACHE_FORMAT:
            break;
        case RCFIELD_CACHE_ATLAS_INDEX:
            break;
        case RCFIELD_CACHE_ATLAS_COUNT:
            break;
        case RCFIELD_CACHE_TRANSFORM:
            break;
        case RCFIELD_CACHE_CROP_X:
            break;
        case RCFIELD_CACHE_CROP_Y:
            break;
        case RCFIELD_CACHE_CROP_WIDTH:
            break;
        case RCFIELD_CACHE_CROP_HEIGHT:
            break;
        case RCFIELD_UICOMPONENT_TYPE:
            break;
        case RCFIELD_UICOMPONENT_SPRITE:
            break;
        case RCFIELD_UICOMPONENT_WIDTH:
            break;
        case RCFIELD_UICOMPONENT_HEIGHT:
            break;
        case RCFIELD_UICOMPONENT_ANCHOR_X:
            break;
        case RCFIELD_UICOMPONENT_ANCHOR_Y:
            break;
        case RCFIELD_UICOMPONENT_TABNO:
            break;
        case RCFIELD_UICOMPONENT_SPRITE_ACTIVE:
            break;
        case RCFIELD_UICOMPONENT_COMPONENTNO:
            break;
        case RCFIELD_UICOMPONENT_INV:
            break;
        case RCFIELD_UICOMPONENT_PAINT_LEVELS:
            break;
        case RCFIELD_INV_ITEM:
            break;
        case RCFIELD_UILAYOUT_COMPONENT:
            break;
        case RCFIELD_UILAYOUT_X:
            break;
        case RCFIELD_UILAYOUT_Y:
            break;
        case RCFIELD_UILAYOUT_WIDTH:
            break;
        case RCFIELD_UILAYOUT_HEIGHT:
            break;
        }
    }
}

PT_STATE_BEGIN(instance_loader_task)
struct InstanceLoader loader;
PT_STATE_END(instance_loader_task)
PT_THREAD(instance_loader_task)
{
    PT_BEGIN();

    struct InstanceLoader_WorkItem* work_item = &state->loader.work_items[state->loader.work_head];
    switch( work_item->kind )
    {
    case INSTANCELOADER_WORK_PROCESS_REVCONFIG:
        instance_loader_process_revconfig(&state->loader, work_item->u.revconfig.revconfig_buffer);
        break;
    }

    PT_END();
}

PT_STATE_BEGIN(revconfig_task)
char revconfig_filenames[REVCONFIG_MAX_FILES][256];
struct RevConfigBuffer* revconfig_buffer;

PT_STATE_CHILD(
    instance_loader_task,
    loader);
PT_STATE_END(revconfig_task)

PT_THREAD(revconfig_task)
{
    PT_BEGIN();

    state->revconfig_buffer = revconfig_buffer_new(64);
    assert(state->revconfig_buffer);

    for( int i = 0; i < REVCONFIG_MAX_FILES; i++ )
    {
        if( state->revconfig_filenames[i][0] == '\0' )
            break;
        configio_fetch_revconfig(ctx, state->revconfig_filenames[i]);
    }

    PT_YIELD();

    for( int i = 0; i < REVCONFIG_MAX_FILES; i++ )
    {
        if( state->revconfig_filenames[i][0] == '\0' )
            break;
        void* data = NULL;
        int data_size = configio_decode_revconfig(ctx, &data);
        if( !data || data_size == 0 )
            continue;
        revconfig_load_fields_from_ini_bytes(data, data_size, state->revconfig_buffer);
    }

    instance_loader_push_workitem_revconfig(&state->loader.loader, state->revconfig_buffer);

    PT_AWAIT(instance_loader_task(&state->loader, ctx));

    revconfig_buffer_free(state->revconfig_buffer);
    PT_END();
}

#endif