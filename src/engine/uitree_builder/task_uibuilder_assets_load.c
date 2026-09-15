#include "uitree_builder.h"
#include "uitree_builder_manifest.h"

#include "engine/cache_provider.h"
#include "engine/dat2/dat2_tasks.h"
#include "engine/title_panel.h"
#include "engine/uitree_builder/task_pack_assets_load.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct Task_UIBuilderAssetsLoad
{
    struct ToriRS_Task task;
    struct pt pt;

    struct UITreeBuilder* builder;
    struct UIBuilderManifest const* manifest;

    int i;
    int* unique_objs;
    int unique_obj_count;
    int unique_obj_cap;
};

static int
unique_obj_add(
    struct Task_UIBuilderAssetsLoad* self,
    int obj_id)
{
    assert(self);
    if( obj_id <= 0 )
        return 0;
    for( int i = 0; i < self->unique_obj_count; i++ )
    {
        if( self->unique_objs[i] == obj_id )
            return 0;
    }
    if( self->unique_obj_count >= self->unique_obj_cap )
    {
        int n = self->unique_obj_cap == 0 ? 16 : self->unique_obj_cap * 2;
        int* grown = realloc(self->unique_objs, (size_t)n * sizeof(int));
        assert(grown);
        self->unique_objs = grown;
        self->unique_obj_cap = n;
    }
    self->unique_objs[self->unique_obj_count++] = obj_id;
    return 1;
}

static void
collect_unique_objs(struct Task_UIBuilderAssetsLoad* self)
{
    assert(self && self->manifest);
    for( int i = 0; i < self->manifest->inv_count; i++ )
    {
        struct UIBuilderInvSeed const* seed = &self->manifest->invs[i];
        for( int j = 0; j < seed->item_count; j++ )
            unique_obj_add(self, seed->obj_ids[j]);
    }
}

static int
Task_UIBuilderAssetsLoad_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IO* io)
{
    struct Task_UIBuilderAssetsLoad* self = (struct Task_UIBuilderAssetsLoad*)base;
    assert(self->builder);
    assert(self->manifest);
    assert(self->builder->provider);

    PT_BEGIN(&self->pt);

    /* Sprites — register names first so bake can resolve even if a leaf load fails. */
    for( self->i = 0; self->i < self->manifest->sprite_count; self->i++ )
    {
        struct UIBuilderSpriteReq const* req = &self->manifest->sprites[self->i];
        UITreeBuilder_RegisterSprite(
            self->builder,
            req->name,
            req->archive_id,
            req->atlas_index,
            req->atlas_count);
    }
    for( self->i = 0; self->i < self->manifest->sprite_count; self->i++ )
    {
        struct UIBuilderSpriteReq const* req = &self->manifest->sprites[self->i];
        if( req->archive_id >= 0 )
        {
            PT_TASK_AWAITSELF_IF(CreateTask_SpriteLoad(self->builder->provider, req->archive_id));
        }
        else if( req->defaults_slot >= 0 && strcmp(req->table, "defaults") == 0 )
        {
            /*
             * `table=defaults slot=<n>`: read the id out of the defaults record,
             * which is what the client does. Index 17 group 3 stores eleven
             * sprite ids positionally and the engine loads each by id — it never
             * looks a sprite up by name — so a slot is an address rather than a
             * label, and this path does not depend on index 8 still shipping
             * name hashes.
             *
             * Checked before the name path so a section may carry both: a
             * profile can name `archive=` as documentation of what the slot
             * resolved to at the revision it was written for, without that name
             * being what the client acts on.
             */
            PT_TASK_AWAITSELF_IF(CreateTask_DefaultsSpriteLoad(
                self->builder->provider, req->defaults_slot, req->name));
        }
        else if(
            strcmp(req->table, "binary") == 0 && req->archive[0] != '\0' &&
            strcmp(req->format, TORIRS_TITLE_PANEL_FORMAT) == 0 )
        {
            /* The title backdrop, which OldSchool keeps in the BINARY table
             * rather than among the sprites -- so it is addressed by table and
             * name, and assembled by the same composite the dat1 lane uses. */
            PT_TASK_AWAITSELF_IF(CreateTask_Dat2TitlePanelLoad(
                self->builder->provider, req->archive, req->name));
        }
        else if( req->archive[0] != '\0' && req->data_filename[0] == '\0' )
        {
            /* Dat2 name-keyed sprite: `table=sprites archive=<name>`. The
             * sprites table is addressed by archive NAME on this era, and the
             * id it lands on differs between caches — which is the whole reason
             * the name belongs in RevConfig and not in a C table. The dat1
             * spelling is distinguished by carrying filename=, since a dat1
             * section names both a jagfile archive and a file inside it. */
            PT_TASK_AWAITSELF_IF(
                CreateTask_SpriteLoadByName(self->builder->provider, req->archive));
        }
        else if( req->format[0] != '\0' && req->data_filename[0] != '\0' )
        {
            /* Dat1 name-keyed sprite: the local descriptor is fully consumed by
             * the creator before any yield; only self->i persists. */
            struct CacheProviderSpriteSource src = {
                .name = req->name,
                .format = req->format,
                .data_filename = req->data_filename,
                .index_filename = req->index_filename,
                /* Which jagfile: "title" for the login screen's art, the media
                 * archive for everything else. Distinguished from the dat2
                 * reading of `archive=` above by this branch carrying
                 * filename=, which a dat2 section never does. */
                .archive = req->archive,
                .atlas_index = req->atlas_index,
                .atlas_count = req->atlas_count,
                .crop_x = req->crop_x,
                .crop_y = req->crop_y,
                .crop_width = req->crop_width,
                .crop_height = req->crop_height,
                .transform = (char const(*)[64])req->transform,
                .transform_count = req->transform_count,
            };
            PT_TASK_AWAITSELF_IF(CreateTask_SpriteLoadFromSource(self->builder->provider, &src));
        }
    }
    /* Re-register name-keyed sprites with their assigned provider ids so bake's
     * UITreeBuilder_ResolveSpriteRef returns a loadable id. */
    for( self->i = 0; self->i < self->manifest->sprite_count; self->i++ )
    {
        struct UIBuilderSpriteReq const* req = &self->manifest->sprites[self->i];
        if( req->archive_id < 0 )
        {
            int assigned = CacheProvider_SpriteIdByName(self->builder->provider, req->name);
            /* A dat2 load registers under the ARCHIVE name, which need not be
             * the section name — rev-239 ships the hitsplat pack as `hitmark`
             * while the client asks for `hitmarks`. Alias the section name onto
             * the same id so every later lookup, including the host's static
             * slots, can use the one spelling C knows. */
            if( assigned < 0 && req->archive[0] != '\0' )
            {
                assigned = CacheProvider_SpriteIdByName(self->builder->provider, req->archive);
                if( assigned >= 0 )
                    CacheProvider_SpriteNameMapPut(self->builder->provider, req->name, assigned);
            }
            if( assigned >= 0 )
                UITreeBuilder_RegisterSprite(
                    self->builder, req->name, assigned, req->atlas_index, req->atlas_count);
        }
    }

    /* Fonts */
    for( self->i = 0; self->i < self->manifest->font_count; self->i++ )
    {
        struct UIBuilderFontReq const* req = &self->manifest->fonts[self->i];
        UITreeBuilder_RegisterFont(
            self->builder, req->name, req->archive_id, req->cache_font_id);
    }
    for( self->i = 0; self->i < self->manifest->font_count; self->i++ )
    {
        struct UIBuilderFontReq const* req = &self->manifest->fonts[self->i];
        if( req->archive_id >= 0 )
            PT_TASK_AWAITSELF_IF(CreateTask_FontLoad(self->builder->provider, req->archive_id));
        else if( req->cache_font_id >= 0 && req->font_name[0] != '\0' )
            PT_TASK_AWAITSELF_IF(CreateTask_FontLoadByName(
                self->builder->provider, req->font_name, req->cache_font_id));
    }

    /* Unique inv objs */
    collect_unique_objs(self);
    for( self->i = 0; self->i < self->unique_obj_count; self->i++ )
    {
        PT_TASK_AWAITSELF_IF(
            CreateTask_ObjLoad(self->builder->provider, self->unique_objs[self->i]));
    }

    /* Components / packs. Locals do not survive the yields between the two
     * awaits — index through self each time. */
    for( self->i = 0; self->i < self->manifest->component_count; self->i++ )
    {
        PT_TASK_AWAITSELF_IF(CreateTask_ComponentLoad(
            self->builder->provider, self->manifest->components[self->i].packed_id));
        /* Prefetch pack-referenced assets (MODEL widgets especially; dat1 sprite
         * refs resolve during the pack load itself, so those awaits no-op). */
        PT_TASK_AWAITSELF_IF(CreateTask_PackAssetsLoad(
            self->builder->provider, self->manifest->components[self->i].iface_id));
    }

    PT_END(&self->pt);
}

static void
Task_UIBuilderAssetsLoad_Free(struct ToriRS_Task* base)
{
    struct Task_UIBuilderAssetsLoad* self = (struct Task_UIBuilderAssetsLoad*)base;
    free(self->unique_objs);
    free(self);
}

static struct ToriRS_TaskVTable Task_UIBuilderAssetsLoad_VTable = {
    .run = Task_UIBuilderAssetsLoad_Run,
    .free = Task_UIBuilderAssetsLoad_Free,
};

struct ToriRS_Task*
CreateTask_UIBuilderAssetsLoad(
    struct UITreeBuilder* builder,
    struct UIBuilderManifest const* manifest)
{
    assert(builder);
    assert(manifest);

    struct Task_UIBuilderAssetsLoad* task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_UIBuilderAssetsLoad_VTable;
    strncpy(task->task.name, "UIBuilderAssetsLoad", sizeof(task->task.name) - 1);
    task->builder = builder;
    task->manifest = manifest;
    PT_INIT(&task->pt);
    return &task->task;
}
