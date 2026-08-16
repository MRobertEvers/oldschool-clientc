/*
 * Match dat1 interface component indices to dat2 interface archive ids.
 *
 * Usage:
 *   match_dat2_interface <dat2_cache_dir> <dat1_cache_dir> [componentno ...]
 *
 * Prints TSV: dat1_componentno\tdat2_archive_id\tscore
 */

#include "osrs/rscache/dat1a/dat1a_config_component.h"
#include "osrs/rscache/dat1a/dat1a_configs_dat.h"
#include "osrs/rscache/dat2a/dat2a_component.h"
#include "osrs/rscache/dat1disk/dat1disk.h"
#include "osrs/rscache/dat2disk/dat2disk.h"
#include "osrs/rscache/shared/shared_file_list.h"
#include "osrs/rscache/shared/shared_rs_buffer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    int width;
    int height;
    int inv_count;
    int graphic_count;
    int text_count;
    int layer_count;
    int total_count;
} PanelFingerprint;

typedef struct
{
    RSCacheDat2A_Component** components;
    int count;
} DecodedIface;

static void
decoded_iface_free(DecodedIface* iface)
{
    if( !iface )
        return;
    for( int i = 0; i < iface->count; i++ )
        RSCacheDat2A_ComponentFree(iface->components[i]);
    free(iface->components);
    iface->components = NULL;
    iface->count = 0;
}

static RSCacheDat2A_Component*
component_decode_from_bytes(
    int packed_id,
    char* data,
    int size)
{
    if( !data || size <= 0 )
        return NULL;

    RSCacheDat2A_Component* comp = calloc(1, sizeof(RSCacheDat2A_Component));
    if( !comp )
        return NULL;

    struct RSCacheShared_RSBuffer buf;
    RSCacheShared_RSBufferInit(&buf, (uint8_t*)data, size);
    RSCacheDat2A_ComponentInit(comp);
    comp->id = packed_id;
    if( (unsigned char)data[0] == (unsigned char)255 )
        RSCacheDat2A_ComponentDecodeIf3(comp, &buf);
    else
        RSCacheDat2A_ComponentDecodeIf1(comp, &buf);
    return comp;
}

static DecodedIface
decode_iface_archive(
    struct RSCacheDat2Disk_ReferenceTable* reference_table,
    struct RSCacheDat2Disk_Archive* archive,
    int iface_id)
{
    DecodedIface out = { 0 };
    if( !reference_table || !archive )
        return out;

    RSCacheDat2Disk_ArchiveInitMetadataFromTable(reference_table, archive);
    struct RSCacheShared_FileList* fl = RSCacheShared_FileListNewFromCacheArchive(archive);
    if( !fl || fl->file_count <= 0 )
    {
        RSCacheShared_FileListFree(fl);
        return out;
    }

    out.count = fl->file_count;
    out.components = calloc((size_t)fl->file_count, sizeof(RSCacheDat2A_Component*));
    if( !out.components )
    {
        RSCacheShared_FileListFree(fl);
        out.count = 0;
        return out;
    }

    for( int i = 0; i < fl->file_count; i++ )
    {
        int packed_id = (iface_id << 16) | (i & 0xFFFF);
        out.components[i] =
            component_decode_from_bytes(packed_id, fl->files[i], fl->file_sizes[i]);
    }

    RSCacheShared_FileListFree(fl);
    return out;
}

static struct RSCacheDat1A_ConfigComponent*
dat1_get_component(
    struct RSCacheDat1A_ConfigComponentList* list,
    int index)
{
    if( !list || index < 0 || index >= list->components_count )
        return NULL;
    return list->components[index];
}

static int
dat1_resolve_walk_root_id(
    struct RSCacheDat1A_ConfigComponentList* ifaces,
    int componentno)
{
    struct RSCacheDat1A_ConfigComponent* direct = dat1_get_component(ifaces, componentno);
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
            struct RSCacheDat1A_ConfigComponent* layer_comp = dat1_get_component(ifaces, layer_id);
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

    if( root == 0 )
        return -1;
    return root;
}

static void
dat1_count_subtree(
    struct RSCacheDat1A_ConfigComponentList* ifaces,
    int index,
    int depth,
    PanelFingerprint* fp)
{
    struct RSCacheDat1A_ConfigComponent* comp = dat1_get_component(ifaces, index);
    if( !comp || depth > 32 )
        return;

    fp->total_count++;
    switch( comp->type )
    {
    case COMPONENT_TYPE_LAYER:
        fp->layer_count++;
        break;
    case COMPONENT_TYPE_INV:
        fp->inv_count++;
        break;
    case COMPONENT_TYPE_GRAPHIC:
        fp->graphic_count++;
        break;
    case COMPONENT_TYPE_TEXT:
    case COMPONENT_TYPE_INV_TEXT:
        fp->text_count++;
        break;
    default:
        break;
    }

    if( !comp->children || comp->children_count <= 0 )
        return;

    for( int i = 0; i < comp->children_count; i++ )
        dat1_count_subtree(ifaces, comp->children[i], depth + 1, fp);
}

static PanelFingerprint
dat1_fingerprint(
    struct RSCacheDat1A_ConfigComponentList* ifaces,
    int componentno)
{
    PanelFingerprint fp = { 0 };
    int root = dat1_resolve_walk_root_id(ifaces, componentno);
    struct RSCacheDat1A_ConfigComponent* direct = dat1_get_component(ifaces, componentno);

    if( root < 0 || !direct )
        return fp;

    fp.width = direct->width;
    fp.height = direct->height;
    if( fp.width <= 0 || fp.height <= 0 )
    {
        struct RSCacheDat1A_ConfigComponent* root_comp = dat1_get_component(ifaces, root);
        if( root_comp )
        {
            fp.width = root_comp->width;
            fp.height = root_comp->height;
        }
    }

    dat1_count_subtree(ifaces, root, 0, &fp);
    if( fp.total_count <= 0 )
        dat1_count_subtree(ifaces, componentno, 0, &fp);
    return fp;
}

static void
dat2_count_component_tree(
    RSCacheDat2A_Component* comp,
    int depth,
    PanelFingerprint* fp)
{
    if( !comp || depth > 32 )
        return;

    fp->total_count++;
    switch( comp->type )
    {
    case COMPONENT_TYPE_LAYER:
        fp->layer_count++;
        break;
    case COMPONENT_TYPE_INV:
        fp->inv_count++;
        break;
    case COMPONENT_TYPE_GRAPHIC:
        fp->graphic_count++;
        break;
    case COMPONENT_TYPE_TEXT:
    case COMPONENT_TYPE_INV_TEXT:
        fp->text_count++;
        break;
    default:
        break;
    }

    if( !comp->createdComponents || comp->createdComponentsLen <= 0 )
        return;

    for( int i = 0; i < comp->createdComponentsLen; i++ )
        dat2_count_component_tree(comp->createdComponents[i], depth + 1, fp);
}

static void
dat2_count_archive(
    DecodedIface const* iface,
    PanelFingerprint* fp)
{
    for( int i = 0; i < iface->count; i++ )
        dat2_count_component_tree(iface->components[i], 0, fp);
}

static void
dat2_root_dimensions(
    DecodedIface const* iface,
    int* out_w,
    int* out_h)
{
    int best_area = 0;
    int best_w = 0;
    int best_h = 0;

    if( !iface || iface->count <= 0 )
        return;

    for( int i = 0; i < iface->count; i++ )
    {
        RSCacheDat2A_Component* comp = iface->components[i];
        if( !comp )
            continue;
        int w = comp->width > 0 ? comp->width : comp->baseWidth;
        int h = comp->height > 0 ? comp->height : comp->baseHeight;
        int area = w * h;
        if( area > best_area )
        {
            best_area = area;
            best_w = w;
            best_h = h;
        }
    }

    if( iface->components[0] )
    {
        RSCacheDat2A_Component* root = iface->components[0];
        int w = root->width > 0 ? root->width : root->baseWidth;
        int h = root->height > 0 ? root->height : root->baseHeight;
        if( w > 0 && h > 0 && w <= 260 && h <= 280 )
        {
            *out_w = w;
            *out_h = h;
            return;
        }
    }

    *out_w = best_w;
    *out_h = best_h;
}

static int
fingerprint_score(
    const PanelFingerprint* target,
    const PanelFingerprint* candidate)
{
    int score = 0;
    if( target->width < 500 && target->width > 0 && target->width == candidate->width )
        score += 8;
    if( target->height < 500 && target->height > 0 && target->height == candidate->height )
        score += 8;
    if( candidate->width == 190 && candidate->height == 261 && target->total_count <= 120 )
        score += 25;
    if( target->inv_count == candidate->inv_count )
        score += 20;
    if( target->graphic_count == candidate->graphic_count )
        score += 6;
    if( target->text_count == candidate->text_count )
        score += 4;
    if( target->layer_count == candidate->layer_count )
        score += 3;
    if( target->total_count == candidate->total_count )
        score += 30;
    else
    {
        int delta = target->total_count - candidate->total_count;
        if( delta < 0 )
            delta = -delta;
        if( delta <= 2 )
            score += 12;
        else if( delta <= 5 )
            score += 6;
    }
    return score;
}

static bool
dat2_archive_references_dat1_id(
    DecodedIface const* iface,
    int dat1_id)
{
    int packed = dat1_id << 16;

    for( int i = 0; i < iface->count; i++ )
    {
        RSCacheDat2A_Component* stack[256];
        int stack_count = 0;
        if( iface->components[i] )
            stack[stack_count++] = iface->components[i];

        while( stack_count > 0 )
        {
            RSCacheDat2A_Component* comp = stack[--stack_count];
            if( !comp )
                continue;
            if( comp->id == packed || comp->id == dat1_id || comp->linkedComponentId == dat1_id )
                return true;
            if( comp->createdComponents && comp->createdComponentsLen > 0 )
            {
                for( int c = 0; c < comp->createdComponentsLen && stack_count < 256; c++ )
                    stack[stack_count++] = comp->createdComponents[c];
            }
        }
    }

    return false;
}

static int
match_dat2_archive(
    struct RSCacheDat2Disk* dat2,
    struct RSCacheDat2Disk_ReferenceTable* iface_table,
    const PanelFingerprint* target,
    int dat1_id)
{
    int best_aid = -1;
    int best_score = -1;

    for( int i = 0; i < iface_table->id_count; i++ )
    {
        int aid = iface_table->ids[i];
        struct RSCacheDat2Disk_Archive* archive =
            RSCacheDat2Disk_ArchiveNewLoad(dat2, RSCacheDat2Disk_Table_Interfaces, aid);
        if( !archive )
            continue;

        DecodedIface iface = decode_iface_archive(iface_table, archive, aid);
        RSCacheDat2Disk_ArchiveFree(archive);
        if( iface.count <= 0 )
        {
            decoded_iface_free(&iface);
            continue;
        }

        if( dat2_archive_references_dat1_id(&iface, dat1_id) )
        {
            decoded_iface_free(&iface);
            return aid;
        }

        PanelFingerprint fp = { 0 };
        dat2_root_dimensions(&iface, &fp.width, &fp.height);
        dat2_count_archive(&iface, &fp);

        if( target->inv_count > 0 && (fp.width != 190 || fp.height != 261) )
        {
            decoded_iface_free(&iface);
            continue;
        }
        if( target->inv_count == 0 && target->total_count <= 120 &&
            (fp.width != 190 || fp.height != 261) )
        {
            decoded_iface_free(&iface);
            continue;
        }

        int score = fingerprint_score(target, &fp);
        if( score > best_score )
        {
            best_score = score;
            best_aid = aid;
        }

        decoded_iface_free(&iface);
    }

    return best_score >= 15 ? best_aid : -1;
}

static struct RSCacheDat1A_ConfigComponentList*
load_dat1_interfaces(char const* dat1_dir)
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

int
main(int argc, char** argv)
{
    if( argc < 3 )
    {
        fprintf(stderr, "Usage: %s <dat2_cache_dir> <dat1_cache_dir> [componentno ...]\n", argv[0]);
        return 1;
    }

    struct RSCacheDat1A_ConfigComponentList* dat1_ifaces = load_dat1_interfaces(argv[2]);
    if( !dat1_ifaces )
    {
        fprintf(stderr, "Failed to load dat1 interfaces from %s\n", argv[2]);
        return 1;
    }

    struct RSCacheDat2Disk* dat2 = RSCacheDat2Disk_NewFromDirectory(argv[1]);
    if( !dat2 )
    {
        fprintf(stderr, "Failed to open dat2 cache %s\n", argv[1]);
        return 1;
    }

    struct RSCacheDat2Disk_ReferenceTable* iface_table = dat2->tables[RSCacheDat2Disk_Table_Interfaces];
    if( !iface_table )
    {
        fprintf(stderr, "Interfaces reference table not loaded\n");
        RSCacheDat2Disk_Free(dat2);
        return 1;
    }

    int default_ids[] = { 5855, 3917, 638, 3213, 1644, 5608, 1151, 5065, 5715, 2449, 904, 147, 962, 0 };
    int* ids = default_ids;
    int id_count = (int)(sizeof(default_ids) / sizeof(default_ids[0])) - 1;
    bool ids_owned = false;
    if( argc > 3 )
    {
        id_count = argc - 3;
        ids = malloc((size_t)id_count * sizeof(int));
        ids_owned = true;
        for( int i = 0; i < id_count; i++ )
            ids[i] = atoi(argv[3 + i]);
    }

    for( int i = 0; i < id_count; i++ )
    {
        int componentno = ids[i];
        int walk_root = dat1_resolve_walk_root_id(dat1_ifaces, componentno);
        PanelFingerprint target = dat1_fingerprint(dat1_ifaces, componentno);
        struct RSCacheDat1A_ConfigComponent* root_comp = dat1_get_component(dat1_ifaces, walk_root);
        if( root_comp && (target.width <= 0 || target.width >= 500) )
        {
            target.width = root_comp->width;
            target.height = root_comp->height;
        }
        fprintf(
            stderr,
            "dat1 %d (root=%d): w=%d h=%d inv=%d graphic=%d text=%d total=%d\n",
            componentno,
            walk_root,
            target.width,
            target.height,
            target.inv_count,
            target.graphic_count,
            target.text_count,
            target.total_count);
        int aid = match_dat2_archive(dat2, iface_table, &target, componentno);
        int score = -1;
        if( aid >= 0 )
        {
            struct RSCacheDat2Disk_Archive* archive =
                RSCacheDat2Disk_ArchiveNewLoad(dat2, RSCacheDat2Disk_Table_Interfaces, aid);
            DecodedIface iface = decode_iface_archive(iface_table, archive, aid);
            RSCacheDat2Disk_ArchiveFree(archive);
            PanelFingerprint fp = { 0 };
            dat2_count_archive(&iface, &fp);
            score = fingerprint_score(&target, &fp);
            decoded_iface_free(&iface);
        }
        printf("%d\t%d\t%d\n", componentno, aid, score);
    }

    if( ids_owned )
        free(ids);

    RSCacheDat2Disk_Free(dat2);
    return 0;
}
