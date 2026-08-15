#include "dump_interface_common.h"
#include <assert.h>

#include "osrs/rscache/dat1a/dat1a_configs_dat.h"
#include "osrs/rscache/dat1disk/dat1disk.h"
#include "osrs/rscache/shared/shared_file_list.h"
#include "osrs/rscache/shared/shared_rs_buffer.h"
#include "ui/ui_if3_layout.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char const*
dump_iface_component_type_name(RSCacheDat2A_Component const* c)
{
    assert(c);

    if( c->if3 )
    {
        switch( c->type )
        {
        case 0:
            return "layer";
        case 3:
            return "rect";
        case 4:
            return "text";
        case 5:
            return "graphic";
        case 6:
            return "model";
        case 9:
            return "line";
        default:
            return "unknown_if3";
        }
    }

    switch( c->type )
    {
    case COMPONENT_TYPE_LAYER:
        return "layer";
    case COMPONENT_TYPE_UNUSED:
        return "unused";
    case COMPONENT_TYPE_INV:
        return "inv";
    case COMPONENT_TYPE_RECT:
        return "rect";
    case COMPONENT_TYPE_TEXT:
        return "text";
    case COMPONENT_TYPE_GRAPHIC:
        return "graphic";
    case COMPONENT_TYPE_MODEL:
        return "model";
    case COMPONENT_TYPE_INV_TEXT:
        return "inv_text";
    case COMPONENT_TYPE_LINE:
        return "line";
    default:
        return "unknown_if1";
    }
}

int
dump_iface_parent_file_index(
    struct DumpIfaceLoaded const* li,
    int child_index)
{
    assert(li);
    if( child_index < 0 || child_index >= li->count )
        return -1;
    return li->parent_idx ? li->parent_idx[child_index] : -1;
}

int
dump_iface_packed_id_iface(
    int packed_id)
{
    return (packed_id >> 16) & 0xFFFF;
}

int
dump_iface_packed_id_file(
    int packed_id)
{
    return packed_id & 0xFFFF;
}

void
dump_iface_format_packed_id(
    char* buf,
    size_t buf_size,
    int packed_id)
{
    if( buf_size == 0 )
        return;
    assert(buf);
    snprintf(
        buf,
        buf_size,
        "0x%08x (%d<<16|%d)",
        (unsigned)packed_id,
        dump_iface_packed_id_iface(packed_id),
        dump_iface_packed_id_file(packed_id));
}

int
dump_iface_decode_component_from_bytes(
    RSCacheDat2A_Component* out,
    char* data,
    int size,
    int iface_id,
    int file_index)
{
    if( size <= 0 )
        return -1;
    assert(out);
    assert(data);

    struct RSCacheShared_RSBuffer buf;
    RSCacheShared_RSBufferInit(&buf, (uint8_t*)data, size);
    RSCacheDat2A_ComponentInit(out);
    out->id = (iface_id << 16) | (file_index & 0xFFFF);
    if( (unsigned char)data[0] == (unsigned char)255 )
        RSCacheDat2A_ComponentDecodeIf3(out, &buf);
    else
        RSCacheDat2A_ComponentDecodeIf1(out, &buf);
    return 0;
}

void
dump_iface_resolve_layout(
    RSCacheDat2A_Component* comps,
    int n,
    int root_x,
    int root_y,
    int root_w,
    int root_h,
    int* parent_idx,
    int* out_x,
    int* out_y,
    int* out_w,
    int* out_h)
{
    int* depth = calloc((size_t)n, sizeof(int));
    int* order = calloc((size_t)n, sizeof(int));
    assert(depth);
    assert(order);

    for( int i = 0; i < n; i++ )
    {
        parent_idx[i] = -1;
        if( comps[i].layer < 0 )
            continue;
        for( int j = 0; j < n; j++ )
        {
            if( comps[j].id == comps[i].layer )
            {
                parent_idx[i] = j;
                break;
            }
        }
    }

    for( int i = 0; i < n; i++ )
    {
        int d = 0;
        int cur = i;
        while( parent_idx[cur] >= 0 )
        {
            cur = parent_idx[cur];
            d++;
            if( d > n )
                break;
        }
        depth[i] = d;
    }

    for( int i = 0; i < n; i++ )
        order[i] = i;
    for( int a = 0; a < n; a++ )
    {
        for( int b = a + 1; b < n; b++ )
        {
            if( depth[order[b]] < depth[order[a]] )
            {
                int t = order[a];
                order[a] = order[b];
                order[b] = t;
            }
        }
    }

    for( int k = 0; k < n; k++ )
    {
        int i = order[k];
        int px = root_x;
        int py = root_y;
        int pw = root_w;
        int ph = root_h;
        if( parent_idx[i] >= 0 )
        {
            int p = parent_idx[i];
            px = out_x[p];
            py = out_y[p];
            pw = out_w[p];
            ph = out_h[p];
        }

        if( !comps[i].if3 )
        {
            out_x[i] = px + comps[i].baseX;
            out_y[i] = py + comps[i].baseY;
            out_w[i] = comps[i].baseWidth;
            out_h[i] = comps[i].baseHeight;
            continue;
        }

        int rel_x = 0;
        int rel_y = 0;
        int w = 0;
        int h = 0;
        UITree_If3ComponentParentRelativeLayout(
            1,
            comps[i].widthMode,
            comps[i].heightMode,
            comps[i].xMode,
            comps[i].yMode,
            comps[i].baseX,
            comps[i].baseY,
            comps[i].baseWidth,
            comps[i].baseHeight,
            comps[i].aspect_ratio_w,
            comps[i].aspect_ratio_h,
            pw,
            ph,
            &rel_x,
            &rel_y,
            &w,
            &h);
        out_w[i] = w;
        out_h[i] = h;
        out_x[i] = px + rel_x;
        out_y[i] = py + rel_y;
    }

    free(depth);
    free(order);
}

int
dump_iface_detect_cache_mode(char const* cache_dir)
{
    assert(cache_dir);

    char path[1024];
    snprintf(path, sizeof(path), "%s/main_file_cache.dat2", cache_dir);
    FILE* fp = fopen(path, "rb");
    if( fp )
    {
        fclose(fp);
        return DUMP_IFACE_CACHE_DAT2;
    }

    snprintf(path, sizeof(path), "%s/main_file_cache.dat", cache_dir);
    fp = fopen(path, "rb");
    if( fp )
    {
        fclose(fp);
        return DUMP_IFACE_CACHE_DAT1;
    }

    return -1;
}

static struct RSCacheDat1A_ConfigComponent*
dat1_ifaces_get_component(
    struct RSCacheDat1A_ConfigComponentList* list,
    int component_id)
{
    if( component_id < 0 )
        return NULL;
    assert(list);

    if( component_id < list->components_count && list->components[component_id] )
    {
        struct RSCacheDat1A_ConfigComponent* at_index = list->components[component_id];
        if( at_index->id == component_id )
            return at_index;
    }

    for( int i = 0; i < list->components_count; i++ )
    {
        struct RSCacheDat1A_ConfigComponent* c = list->components[i];
        if( c && c->id == component_id )
            return c;
    }
    return NULL;
}

static int
dat1_resolve_walk_root_id(
    struct RSCacheDat1A_ConfigComponentList* ifaces,
    int componentno)
{
    struct RSCacheDat1A_ConfigComponent* direct = dat1_ifaces_get_component(ifaces, componentno);
    if( !direct )
        return componentno;

    int root = componentno;

    if( direct->type == COMPONENT_TYPE_LAYER )
        root = direct->id;
    else
    {
        int layer_id = direct->layer;
        for( int depth = 0; layer_id >= 0 && depth < 32; depth++ )
        {
            struct RSCacheDat1A_ConfigComponent* layer_comp =
                dat1_ifaces_get_component(ifaces, layer_id);
            if( !layer_comp )
                break;
            if( layer_comp->type == COMPONENT_TYPE_LAYER )
            {
                root = layer_comp->id;
                break;
            }
            layer_id = layer_comp->layer;
        }

        if( root == componentno )
        {
            if( direct->layer >= 0 )
                root = direct->layer;
            else
                root = direct->id;
        }
    }

    return root;
}

static char*
dump_iface_strdup(char const* s)
{
    assert(s);
    size_t n = strlen(s) + 1;
    char* out = malloc(n);
    if( out )
        memcpy(out, s, n);
    return out;
}

static void
dump_iface_dat1_convert(
    RSCacheDat2A_Component* out,
    struct RSCacheDat1A_ConfigComponent const* src)
{
    RSCacheDat2A_ComponentInit(out);
    out->id = src->id;
    out->type = src->type;
    out->layer = src->layer;
    out->baseX = src->x;
    out->baseY = src->y;
    out->baseWidth = src->width;
    out->baseHeight = src->height;
    out->x = src->x;
    out->y = src->y;
    out->width = src->width;
    out->height = src->height;
    out->hidden = src->hide;
    out->transparency = src->alpha;
    out->color = src->colour;
    out->fill = src->fill;
    out->buttonType = src->buttonType;
    out->clientCode = src->clientCode;
    out->linkedComponentId = src->overlayer;
    out->activeColour = src->activeColour;
    out->overColour = src->overColour;
    out->activeOverColour = src->activeOverColour;
    out->modelType = src->modelType;
    out->modelId = src->model;
    out->modelZoom = src->zoom;
    out->modelXAngle = src->xan;
    out->modelYAngle = src->yan;
    out->marginX = src->marginX;
    out->marginY = src->marginY;
    out->textFont = src->font;
    out->textShadow = src->shadowed;
    out->scrollHeight = src->scroll;
    out->if3 = false;

    if( src->text )
        out->text = dump_iface_strdup(src->text);
    if( src->activeText )
        out->activeText = dump_iface_strdup(src->activeText);
    if( src->option )
        out->option = dump_iface_strdup(src->option);
    if( src->targetVerb )
        out->targetVerb = dump_iface_strdup(src->targetVerb);
    if( src->targetText )
        out->targetText = dump_iface_strdup(src->targetText);

    if( src->type == COMPONENT_TYPE_GRAPHIC )
    {
        if( src->graphic )
            out->name = dump_iface_strdup(src->graphic);
        if( src->activeGraphic )
            out->activeGraphic = 0;
    }

    if( src->iop )
    {
        out->opsLen = 5;
        out->ops = calloc(5, sizeof(char*));
        if( out->ops )
        {
            for( int i = 0; i < 5; i++ )
                out->ops[i] = dump_iface_strdup(src->iop[i]);
        }
    }
}

static void
dat1_interfaces_list_free(struct RSCacheDat1A_ConfigComponentList* interfaces)
{
    if( !interfaces )
        return;

    if( interfaces->components )
    {
        for( int i = 0; i < interfaces->components_count; i++ )
        {
            if( interfaces->components[i] )
                RSCacheDat1A_ConfigComponentFree(interfaces->components[i]);
        }
        free(interfaces->components);
    }
    free(interfaces);
}

static struct RSCacheDat1A_ConfigComponentList*
dat1_load_interfaces(char const* dat1_dir)
{
    struct RSCacheDat1Disk* disk = RSCacheDat1Disk_NewFromDirectory(dat1_dir);
    if( !disk )
        return NULL;

    struct RSCacheDat1Disk_Archive* arc =
        RSCacheDat1Disk_ArchiveNewLoad(disk, 0, RSCacheDat1A_ConfigKind_Interfaces);
    if( !arc )
    {
        RSCacheDat1Disk_Free(disk);
        return NULL;
    }

    struct RSCacheShared_FileListDat* fl = RSCacheShared_FileListDatNewFromCacheDatArchive(arc);
    RSCacheDat1Disk_ArchiveFree(arc);
    if( !fl )
    {
        RSCacheDat1Disk_Free(disk);
        return NULL;
    }

    int data_idx = RSCacheShared_FileListDatFindFileByName(fl, "data");
    if( data_idx < 0 )
    {
        RSCacheShared_FileListDatFree(fl);
        RSCacheDat1Disk_Free(disk);
        return NULL;
    }

    struct RSCacheDat1A_ConfigComponentList* list = RSCacheDat1A_ConfigComponentListNewDecode(
        fl->files[data_idx], fl->file_sizes[data_idx]);
    RSCacheShared_FileListDatFree(fl);
    RSCacheDat1Disk_Free(disk);
    return list;
}

typedef struct
{
    int comp_id;
    int parent_comp_id;
    int rel_x;
    int rel_y;
    int w;
    int h;
    struct RSCacheDat1A_ConfigComponent* src;
} DumpIfaceDat1Node;

static int
dat1_push_child_nodes(
    struct RSCacheDat1A_ConfigComponentList* ifaces,
    struct RSCacheDat1A_ConfigComponent* layer,
    int parent_rel_x,
    int parent_rel_y,
    DumpIfaceDat1Node* nodes,
    int* node_count,
    int node_cap,
    int* visited,
    int visited_cap)
{
    if( !layer || layer->type != COMPONENT_TYPE_LAYER || !layer->children || layer->children_count <= 0 )
        return 0;

    if( !layer->childX || !layer->childY )
        return -1;

    for( int i = layer->children_count - 1; i >= 0; i-- )
    {
        int child_index = layer->children[i];
        if( child_index < 0 || child_index >= ifaces->components_count )
            continue;

        struct RSCacheDat1A_ConfigComponent* child = ifaces->components[child_index];
        if( !child )
            continue;

        if( visited && child->id >= 0 && child->id < visited_cap && visited[child->id] )
            continue;
        if( visited && child->id >= 0 && child->id < visited_cap )
            visited[child->id] = 1;

        if( *node_count >= node_cap )
            return -1;

        int rel_x = layer->childX[i] + child->x;
        int rel_y = layer->childY[i] + child->y;
        nodes[*node_count] = (DumpIfaceDat1Node){
            .comp_id = child->id,
            .parent_comp_id = layer->id,
            .rel_x = parent_rel_x + rel_x,
            .rel_y = parent_rel_y + rel_y,
            .w = child->width,
            .h = child->height,
            .src = child,
        };
        (*node_count)++;

        if( child->type == COMPONENT_TYPE_LAYER )
        {
            if( dat1_push_child_nodes(
                    ifaces,
                    child,
                    parent_rel_x + rel_x,
                    parent_rel_y + rel_y,
                    nodes,
                    node_count,
                    node_cap,
                    visited,
                    visited_cap) != 0 )
                return -1;
        }
    }

    return 0;
}

int
dump_iface_load_dat1(
    char const* cache_dir,
    int componentno,
    int root_w,
    int root_h,
    struct DumpIfaceLoaded* out)
{
    memset(out, 0, sizeof(*out));
    out->cache_mode = DUMP_IFACE_CACHE_DAT1;

    struct RSCacheDat1A_ConfigComponentList* list = dat1_load_interfaces(cache_dir);
    if( !list )
        return -1;

    int walk_root = dat1_resolve_walk_root_id(list, componentno);
    struct RSCacheDat1A_ConfigComponent* root = dat1_ifaces_get_component(list, walk_root);
    if( !root )
    {
        dat1_interfaces_list_free(list);
        return -1;
    }

    int visited_cap = list->components_count + 1;
    int* visited = calloc((size_t)visited_cap, sizeof(int));
    DumpIfaceDat1Node* nodes = calloc((size_t)list->components_count, sizeof(DumpIfaceDat1Node));
    assert(visited);
    assert(nodes);

    int node_count = 0;
    if( root->id >= 0 && root->id < visited_cap )
        visited[root->id] = 1;
    nodes[node_count++] = (DumpIfaceDat1Node){
        .comp_id = root->id,
        .parent_comp_id = -1,
        .rel_x = 0,
        .rel_y = 0,
        .w = root->width > 0 ? root->width : root_w,
        .h = root->height > 0 ? root->height : root_h,
        .src = root,
    };

    if( root->type == COMPONENT_TYPE_LAYER )
    {
        if( dat1_push_child_nodes(
                list, root, 0, 0, nodes, &node_count, list->components_count, visited, visited_cap) !=
            0 )
        {
            free(visited);
            free(nodes);
            dat1_interfaces_list_free(list);
            return -1;
        }
    }

    if( componentno != walk_root )
    {
        struct RSCacheDat1A_ConfigComponent* direct = dat1_ifaces_get_component(list, componentno);
        if( direct && direct->id != walk_root )
        {
            if( direct->id >= 0 && direct->id < visited_cap && !visited[direct->id] )
            {
                if( direct->id >= 0 && direct->id < visited_cap )
                    visited[direct->id] = 1;
                nodes[node_count++] = (DumpIfaceDat1Node){
                    .comp_id = direct->id,
                    .parent_comp_id = direct->layer,
                    .rel_x = direct->x,
                    .rel_y = direct->y,
                    .w = direct->width,
                    .h = direct->height,
                    .src = direct,
                };
            }
        }
    }

    free(visited);

    int n = node_count;
    RSCacheDat2A_Component* comps = calloc((size_t)n, sizeof(RSCacheDat2A_Component));
    int* parent_idx = calloc((size_t)n, sizeof(int));
    int* lay_x = calloc((size_t)n, sizeof(int));
    int* lay_y = calloc((size_t)n, sizeof(int));
    int* lay_w = calloc((size_t)n, sizeof(int));
    int* lay_h = calloc((size_t)n, sizeof(int));
    struct RSCacheDat1A_ConfigComponent** dat1_src =
        calloc((size_t)n, sizeof(struct RSCacheDat1A_ConfigComponent*));
    if( !comps || !parent_idx || !lay_x || !lay_y || !lay_w || !lay_h || !dat1_src )
    {
        free(comps);
        free(parent_idx);
        free(lay_x);
        free(lay_y);
        free(lay_w);
        free(lay_h);
        free(dat1_src);
        free(nodes);
        dat1_interfaces_list_free(list);
        return -1;
    }

    for( int i = 0; i < n; i++ )
    {
        dump_iface_dat1_convert(&comps[i], nodes[i].src);
        dat1_src[i] = nodes[i].src;
        lay_x[i] = nodes[i].rel_x;
        lay_y[i] = nodes[i].rel_y;
        lay_w[i] = nodes[i].w;
        lay_h[i] = nodes[i].h;
        parent_idx[i] = -1;
        for( int j = 0; j < n; j++ )
        {
            if( nodes[j].comp_id == nodes[i].parent_comp_id )
            {
                parent_idx[i] = j;
                break;
            }
        }
    }

    free(nodes);

    out->comps = comps;
    out->parent_idx = parent_idx;
    out->lay_x = lay_x;
    out->lay_y = lay_y;
    out->lay_w = lay_w;
    out->lay_h = lay_h;
    out->count = n;
    out->dat1_src = dat1_src;
    out->dat1_list = list;
    return 0;
}

int
dump_iface_load_dat2(
    struct RSCacheDat2Disk* cache,
    int iface_id,
    int root_w,
    int root_h,
    struct DumpIfaceLoaded* out)
{
    memset(out, 0, sizeof(*out));

    struct RSCacheDat2Disk_Archive* arch =
        RSCacheDat2Disk_ArchiveNewLoad(cache, RSCacheDat2Disk_Table_Interfaces, iface_id);
    if( !arch )
        return -1;

    RSCacheDat2Disk_ArchiveInitMetadata(cache, arch);
    struct RSCacheShared_FileList* fl = RSCacheShared_FileListNewFromCacheArchive(arch);
    if( !fl )
    {
        RSCacheDat2Disk_ArchiveFree(arch);
        return -1;
    }

    int n = fl->file_count;
    RSCacheDat2A_Component* comps = calloc((size_t)n, sizeof(RSCacheDat2A_Component));
    int* parent_idx = calloc((size_t)n, sizeof(int));
    int* lay_x = calloc((size_t)n, sizeof(int));
    int* lay_y = calloc((size_t)n, sizeof(int));
    int* lay_w = calloc((size_t)n, sizeof(int));
    int* lay_h = calloc((size_t)n, sizeof(int));
    assert(comps);
    assert(parent_idx);
    assert(lay_x);
    assert(lay_y);
    assert(lay_w);
    assert(lay_h);

    for( int fi = 0; fi < n; fi++ )
    {
        if( dump_iface_decode_component_from_bytes(
                &comps[fi], fl->files[fi], fl->file_sizes[fi], iface_id, fi) != 0 )
            RSCacheDat2A_ComponentInit(&comps[fi]);
    }

    dump_iface_resolve_layout(
        comps, n, 0, 0, root_w, root_h, parent_idx, lay_x, lay_y, lay_w, lay_h);

    out->comps = comps;
    out->parent_idx = parent_idx;
    out->lay_x = lay_x;
    out->lay_y = lay_y;
    out->lay_w = lay_w;
    out->lay_h = lay_h;
    out->count = n;
    out->cache_mode = DUMP_IFACE_CACHE_DAT2;

    RSCacheShared_FileListFree(fl);
    RSCacheDat2Disk_ArchiveFree(arch);
    return 0;
}

void
dump_iface_free(struct DumpIfaceLoaded* li)
{
    if( !li )
        return;
    for( int i = 0; i < li->count; i++ )
        RSCacheDat2A_ComponentFree(&li->comps[i]);
    free(li->comps);
    free(li->parent_idx);
    free(li->lay_x);
    free(li->lay_y);
    free(li->lay_w);
    free(li->lay_h);
    free(li->dat1_src);
    if( li->dat1_list )
        dat1_interfaces_list_free(li->dat1_list);
    memset(li, 0, sizeof(*li));
}

static bool
hook_refs_iface(
    ComponentScriptVar const* hook,
    int hook_len,
    int target_iface)
{
    if( hook_len <= 0 )
        return false;
    assert(hook);
    for( int i = 0; i < hook_len; i++ )
    {
        if( hook[i].type != SCRIPT_VAR_INT )
            continue;
        int v = hook[i].value.i;
        if( v == target_iface || (v >> 16) == target_iface )
            return true;
    }
    return false;
}

static bool
component_refs_iface(
    RSCacheDat2A_Component const* c,
    int target_iface)
{
    assert(c);

    int packed = target_iface << 16;
    if( c->id == packed || (c->id >> 16) == target_iface )
        return false;
    if( c->linkedComponentId == target_iface || c->linkedComponentId == packed ||
        (c->linkedComponentId >> 16) == target_iface )
        return true;

    if( hook_refs_iface(c->onLoad, c->onLoadLen, target_iface) )
        return true;
    if( hook_refs_iface(c->onInvTransmit, c->onInvTransmitLen, target_iface) )
        return true;
    if( hook_refs_iface(c->onVarpTransmit, c->onVarpTransmitLen, target_iface) )
        return true;
    if( hook_refs_iface(c->onClick, c->onClickLen, target_iface) )
        return true;
    if( hook_refs_iface(c->onOp, c->onOpLen, target_iface) )
        return true;
    return false;
}

static void
print_hook_ref(
    FILE* fp,
    int parent_iface,
    int file_index,
    char const* hook_name,
    ComponentScriptVar const* hook,
    int hook_len,
    int target_iface)
{
    if( !hook_refs_iface(hook, hook_len, target_iface) )
        return;
    fprintf(
        fp,
        "  iface %d file[%02d] hook %s:",
        parent_iface,
        file_index,
        hook_name);
    for( int i = 0; i < hook_len; i++ )
    {
        if( hook[i].type == SCRIPT_VAR_INT )
            fprintf(fp, " %d", hook[i].value.i);
        else if( hook[i].type == SCRIPT_VAR_STRING && hook[i].value.s )
            fprintf(fp, " '%s'", hook[i].value.s);
    }
    fputc('\n', fp);
}

int
dump_iface_scan_parents(
    struct RSCacheDat2Disk* cache,
    int target_iface,
    FILE* fp)
{
    if( target_iface < 0 )
        return -1;
    assert(cache);
    assert(fp);

    struct RSCacheDat2Disk_ReferenceTable* table =
        cache->tables[RSCacheDat2Disk_Table_Interfaces];
    if( !table )
        return -1;

    fprintf(fp, "Scanning interface archives for references to iface %d...\n\n", target_iface);

    struct DumpIfaceLoaded target;
    if( dump_iface_load_dat2(cache, target_iface, DUMP_IFACE_FIXED_ROOT_W, DUMP_IFACE_FIXED_ROOT_H, &target) ==
        0 )
    {
        fprintf(fp, "Internal component parents (iface %d):\n", target_iface);
        for( int i = 0; i < target.count; i++ )
        {
            if( target.comps[i].type < 0 )
                continue;
            int parent = dump_iface_parent_file_index(&target, i);
            char id_buf[48];
            char layer_buf[48];
            dump_iface_format_packed_id(id_buf, sizeof(id_buf), target.comps[i].id);
            if( target.comps[i].layer >= 0 )
                dump_iface_format_packed_id(layer_buf, sizeof(layer_buf), target.comps[i].layer);
            else
                snprintf(layer_buf, sizeof(layer_buf), "-1");
            fprintf(
                fp,
                "  [%02d] parent=%02d id=%s layer=%s type=%s inv_triggers=",
                i,
                parent,
                id_buf,
                layer_buf,
                dump_iface_component_type_name(&target.comps[i]));
            if( target.comps[i].inventoryTriggers && target.comps[i].inventoryTriggersLen > 0 )
            {
                for( int t = 0; t < target.comps[i].inventoryTriggersLen; t++ )
                    fprintf(fp, "%s%d", t ? "," : "", target.comps[i].inventoryTriggers[t]);
            }
            else
                fputs("none", fp);
            if( target.comps[i].onInvTransmitLen > 0 && target.comps[i].onInvTransmit &&
                target.comps[i].onInvTransmit[0].type == SCRIPT_VAR_INT )
                fprintf(fp, " onInvTransmit=%d", target.comps[i].onInvTransmit[0].value.i);
            else if( target.comps[i].onInvTransmitLen > 0 )
                fprintf(fp, " onInvTransmitLen=%d", target.comps[i].onInvTransmitLen);
            fputc('\n', fp);
        }
        fputc('\n', fp);
        dump_iface_free(&target);
    }

    struct DumpIfaceLoaded gameframe;
    if( dump_iface_load_dat2(cache, 165, DUMP_IFACE_FIXED_ROOT_W, DUMP_IFACE_FIXED_ROOT_H, &gameframe) == 0 )
    {
        fprintf(fp, "Gameframe 165 mount slots (runtime sidebar parents):\n");
        for( int i = 0; i < gameframe.count; i++ )
        {
            if( gameframe.comps[i].type != 0 )
                continue;
            int parent = dump_iface_parent_file_index(&gameframe, i);
            if( parent != 1 )
                continue;
            char id_buf[48];
            dump_iface_format_packed_id(id_buf, sizeof(id_buf), gameframe.comps[i].id);
            fprintf(
                fp,
                "  child[%02d] id=%s  %dx%d mount layer\n",
                i,
                id_buf,
                gameframe.lay_w[i],
                gameframe.lay_h[i]);
        }
        fprintf(
            fp,
            "  (sidebar panels mount on gameframe 165 children 8-21 via DisplayHandler; "
            "see rev_kronos_ui.ini tab table for iface %d)\n\n",
            target_iface);
        dump_iface_free(&gameframe);
    }

    int ref_count = 0;
    int worn_watch_count = 0;
    fprintf(fp, "Interfaces watching worn container 94 (onInvTransmit):\n");
    for( int ti = 0; ti < table->id_count; ti++ )
    {
        int iface_id = table->ids[ti];
        struct DumpIfaceLoaded li;
        if( dump_iface_load_dat2(
                cache, iface_id, DUMP_IFACE_FIXED_ROOT_W, DUMP_IFACE_FIXED_ROOT_H, &li) != 0 )
            continue;

        for( int fi = 0; fi < li.count; fi++ )
        {
            RSCacheDat2A_Component const* c = &li.comps[fi];
            if( c->type < 0 || c->onInvTransmitLen <= 0 )
                continue;
            bool watches_94 = c->inventoryTriggersLen <= 0;
            if( c->inventoryTriggers )
            {
                watches_94 = false;
                for( int t = 0; t < c->inventoryTriggersLen; t++ )
                {
                    if( c->inventoryTriggers[t] == 94 )
                        watches_94 = true;
                }
            }
            if( !watches_94 )
                continue;
            fprintf(
                fp,
                "  iface %d file[%02d] onInvTransmit=%d inv_triggers=",
                iface_id,
                fi,
                c->onInvTransmit[0].type == SCRIPT_VAR_INT ? c->onInvTransmit[0].value.i : -1);
            if( c->inventoryTriggersLen > 0 )
            {
                for( int t = 0; t < c->inventoryTriggersLen; t++ )
                    fprintf(fp, "%s%d", t ? "," : "", c->inventoryTriggers[t]);
            }
            else
                fputs("all", fp);
            fputc('\n', fp);
            worn_watch_count++;
        }
        dump_iface_free(&li);
    }
    if( worn_watch_count == 0 )
        fprintf(fp, "  (none)\n");
    fputc('\n', fp);

    fprintf(fp, "Cross-interface component references:\n");
    for( int ti = 0; ti < table->id_count; ti++ )
    {
        int parent_iface = table->ids[ti];
        if( parent_iface == target_iface )
            continue;

        struct DumpIfaceLoaded li;
        if( dump_iface_load_dat2(
                cache, parent_iface, DUMP_IFACE_FIXED_ROOT_W, DUMP_IFACE_FIXED_ROOT_H, &li) != 0 )
            continue;

        bool any = false;
        for( int fi = 0; fi < li.count; fi++ )
        {
            RSCacheDat2A_Component const* c = &li.comps[fi];
            if( c->type < 0 )
                continue;
            if( !component_refs_iface(c, target_iface) )
                continue;
            if( !any )
            {
                fprintf(fp, "iface %d:\n", parent_iface);
                any = true;
                ref_count++;
            }
            char id_buf[48];
            dump_iface_format_packed_id(id_buf, sizeof(id_buf), c->id);
            fprintf(
                fp,
                "  file[%02d] id=%s linked=%d clientCode=%d\n",
                fi,
                id_buf,
                c->linkedComponentId,
                c->clientCode);
            print_hook_ref(fp, parent_iface, fi, "onLoad", c->onLoad, c->onLoadLen, target_iface);
            print_hook_ref(
                fp, parent_iface, fi, "onInvTransmit", c->onInvTransmit, c->onInvTransmitLen, target_iface);
            print_hook_ref(
                fp, parent_iface, fi, "onClick", c->onClick, c->onClickLen, target_iface);
        }
        dump_iface_free(&li);
    }

    if( ref_count == 0 )
        fprintf(fp, "  (none found in component hooks/linked ids)\n");
    fprintf(
        fp,
        "\nNote: sidebar mounting of iface %d is driven by client DisplayHandler onto gameframe 165 "
        "child 12, not embedded in iface 165 component data.\n",
        target_iface);
    return 0;
}
