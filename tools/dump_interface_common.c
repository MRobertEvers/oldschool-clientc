#include "dump_interface_common.h"

#include "osrs/rscache/dat1a/dat1a_configs_dat.h"
#include "osrs/rscache/dat1disk/dat1disk.h"
#include "osrs/rscache/shared/shared_file_list.h"
#include "osrs/rscache/shared/shared_rs_buffer.h"
#include "ui/ui_if3_layout.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char const*
dump_iface_component_type_name(Component const* c)
{
    if( !c )
        return "unknown";

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
    if( !li || child_index < 0 || child_index >= li->count )
        return -1;
    return li->parent_idx ? li->parent_idx[child_index] : -1;
}

int
dump_iface_decode_component_from_bytes(
    Component* out,
    char* data,
    int size,
    int iface_id,
    int file_index)
{
    if( !out || !data || size <= 0 )
        return -1;

    struct RSCacheShared_RSBuffer buf;
    RSCacheShared_RSBufferInit(&buf, (uint8_t*)data, size);
    Component_init(out);
    out->id = (iface_id << 16) | (file_index & 0xFFFF);
    if( (unsigned char)data[0] == (unsigned char)255 )
        Component_decodeIf3(out, &buf);
    else
        Component_decodeIf1(out, &buf);
    return 0;
}

void
dump_iface_resolve_layout(
    Component* comps,
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
    if( !depth || !order )
    {
        free(depth);
        free(order);
        for( int i = 0; i < n; i++ )
        {
            if( parent_idx )
                parent_idx[i] = -1;
            out_x[i] = root_x + comps[i].baseX;
            out_y[i] = root_y + comps[i].baseY;
            out_w[i] = comps[i].baseWidth;
            out_h[i] = comps[i].baseHeight;
        }
        return;
    }

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
        ui_if3_component_parent_relative_layout(
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
    if( !cache_dir )
        return -1;

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
    if( !list || component_id < 0 )
        return NULL;

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
    if( !s )
        return NULL;
    size_t n = strlen(s) + 1;
    char* out = malloc(n);
    if( out )
        memcpy(out, s, n);
    return out;
}

static void
dump_iface_dat1_convert(
    Component* out,
    struct RSCacheDat1A_ConfigComponent const* src)
{
    Component_init(out);
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
    if( !visited || !nodes )
    {
        free(visited);
        free(nodes);
        dat1_interfaces_list_free(list);
        return -1;
    }

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
    Component* comps = calloc((size_t)n, sizeof(Component));
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
    Component* comps = calloc((size_t)n, sizeof(Component));
    int* parent_idx = calloc((size_t)n, sizeof(int));
    int* lay_x = calloc((size_t)n, sizeof(int));
    int* lay_y = calloc((size_t)n, sizeof(int));
    int* lay_w = calloc((size_t)n, sizeof(int));
    int* lay_h = calloc((size_t)n, sizeof(int));
    if( !comps || !parent_idx || !lay_x || !lay_y || !lay_w || !lay_h )
    {
        free(comps);
        free(parent_idx);
        free(lay_x);
        free(lay_y);
        free(lay_w);
        free(lay_h);
        RSCacheShared_FileListFree(fl);
        RSCacheDat2Disk_ArchiveFree(arch);
        return -1;
    }

    for( int fi = 0; fi < n; fi++ )
    {
        if( dump_iface_decode_component_from_bytes(
                &comps[fi], fl->files[fi], fl->file_sizes[fi], iface_id, fi) != 0 )
            Component_init(&comps[fi]);
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
        Component_free(&li->comps[i]);
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
