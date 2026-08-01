/*
 * find_named — locate cache records by name, and dump what they reference.
 *
 * Everything else in this directory takes an id and works outward from it. That
 * is the wrong end when the only thing known about an asset is what the wiki
 * calls it, so this walks the config groups, decodes every record and matches
 * the name field.
 *
 * Usage:
 *   find_named --rev NAME <cache_dir> --name SUBSTR [--type npc|obj|loc|all]
 *   find_named --rev NAME <cache_dir> --npc ID
 *   find_named --rev NAME <cache_dir> --seq ID
 *   find_named --rev NAME <cache_dir> --spotanim ID
 *   find_named --rev NAME <cache_dir> --scan-spotanim-model ID
 *
 * `--npc`, `--seq` and `--spotanim` dump one record in full. Sequences and
 * spotanims carry no name in most revisions, so they are reached through
 * whatever references them rather than by search: an npc names its own
 * animations, and --scan-spotanim-model finds the spotanims built on a model
 * once a projectile's model id is known.
 */

#include "asset_access.h"
#include "tool_profile.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
usage(const char* argv0)
{
    fprintf(
        stderr,
        "Usage:\n"
        "  %s --rev NAME <cache_dir> --name SUBSTR [--type npc|obj|loc|all]\n"
        "  %s --rev NAME <cache_dir> --npc ID\n"
        "  %s --rev NAME <cache_dir> --obj ID\n"
        "  %s --rev NAME <cache_dir> --seq ID\n"
        "  %s --rev NAME <cache_dir> --spotanim ID\n"
        "  %s --rev NAME <cache_dir> --scan-spotanim-model ID\n",
        argv0,
        argv0,
        argv0,
        argv0,
        argv0,
        argv0);
}

static int
strcasestr_ascii(
    const char* haystack,
    const char* needle)
{
    size_t nlen;
    if( !haystack || !needle )
        return 0;
    nlen = strlen(needle);
    if( nlen == 0 )
        return 1;
    for( const char* p = haystack; *p; p++ )
    {
        size_t i = 0;
        while( i < nlen && p[i] && tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i]) )
            i++;
        if( i == nlen )
            return 1;
    }
    return 0;
}

/*
 * Every id present for `type`.
 *
 * A config group's file ids are sparse — a revision that has removed content
 * leaves holes — so the ids come from the archive's own file_ids table rather
 * than from a 0..max walk, which would decode a lot of absent records and
 * report their misses as failures.
 */
static int
enumerate_ids(
    struct Tool_Dat2Cache* c,
    enum RSCache_Type type,
    int config_kind,
    int** out_ids,
    int* out_count)
{
    struct RSCache_RecordAddress addr = RSCache_RecordAddressFor(&c->profile, type);
    int* ids = NULL;
    int count = 0;
    int cap = 0;
    int table;
    int group_ids[4096];
    int group_count = 0;

    *out_ids = NULL;
    *out_count = 0;

    if( addr.group_shift == 0 )
    {
        /* addr.group is not the config kind — the loaders pass the kind
         * constant separately, and reading addr.group here asks for archive
         * -1. */
        table = RSCache_Dat2DiskTableId(c->disk, RSCACHE_DAT2_TABLE_CONFIGS);
        group_ids[group_count++] = config_kind;
    }
    else
    {
        int table_id = RSCache_Dat2DiskTableId(c->disk, addr.table);
        struct RSCache_ReferenceTable* rt =
            table_id == RSCACHE_DAT2_DISK_TABLE_ABSENT ? NULL : c->disk->tables[table_id];
        if( !rt )
            return 0;
        table = table_id;
        for( int i = 0; i < rt->id_count && group_count < (int)(sizeof(group_ids) / sizeof(int));
             i++ )
            group_ids[group_count++] = rt->ids[i];
    }

    for( int g = 0; g < group_count; g++ )
    {
        struct RSCache_Dat2DiskArchive* archive =
            RSCache_Dat2DiskArchiveNewLoad(c->disk, table, group_ids[g]);
        if( !archive )
            continue;
        if( !RSCache_Dat2DiskArchiveInitMetadata(c->disk, archive) || archive->file_count <= 0 )
        {
            RSCache_Dat2DiskArchiveFree(archive);
            continue;
        }
        for( int i = 0; i < archive->file_count; i++ )
        {
            int file_id = archive->file_ids ? archive->file_ids[i] : i;
            int global = addr.group_shift == 0
                             ? file_id
                             : ((group_ids[g] << addr.group_shift) | (file_id & addr.file_mask));
            if( count == cap )
            {
                int next = cap ? cap * 2 : 1024;
                int* grown = realloc(ids, (size_t)next * sizeof(int));
                if( !grown )
                {
                    free(ids);
                    RSCache_Dat2DiskArchiveFree(archive);
                    return 0;
                }
                ids = grown;
                cap = next;
            }
            ids[count++] = global;
        }
        RSCache_Dat2DiskArchiveFree(archive);
    }

    *out_ids = ids;
    *out_count = count;
    return 1;
}

static void
print_int_list(
    const char* label,
    const int* v,
    int n)
{
    if( !v || n <= 0 )
        return;
    printf("  %-22s", label);
    for( int i = 0; i < n; i++ )
        printf("%s%d", i ? ", " : "", v[i]);
    printf("\n");
}

static void
dump_npc(
    struct Tool_Dat2Cache* c,
    int id)
{
    int exact = 0;
    struct RSCache_Dat2ConfigNpc* npc = tool_dat2_npc_load_checked(c, id, &exact);
    if( !npc )
    {
        printf("npc %d: <absent>\n", id);
        return;
    }
    printf("npc %d  \"%s\"%s\n", id, npc->name ? npc->name : "(null)", exact ? "" : "  [INEXACT DECODE]");
    printf("  %-22s%d\n", "size", npc->size);
    printf("  %-22s%d\n", "combat_level", npc->combat_level);
    print_int_list("models", npc->models, npc->models_count);
    print_int_list("chathead_models", npc->chathead_models, npc->chathead_models_count);
    printf("  %-22s%d\n", "standing_anim", npc->standing_animation);
    printf("  %-22s%d\n", "walking_anim", npc->walking_animation);
    printf("  %-22s%d\n", "run_anim", npc->run_animation);
    printf("  %-22s%d\n", "rotate180_anim", npc->rotate180_animation);
    printf("  %-22s%d\n", "rotate_left_anim", npc->rotate_left_animation);
    printf("  %-22s%d\n", "rotate_right_anim", npc->rotate_right_animation);
    printf("  %-22s%d\n", "bas_type_id", npc->bas_type_id);
    printf("  %-22s%d\n", "rotation_speed", npc->rotation_speed);
    printf("  %-22s%d x %d\n", "scale w/h", npc->width_scale, npc->height_scale);
    printf("  %-22s%d\n", "height", npc->height);
    printf("  %-22s%d\n", "category", npc->category);
    printf("  %-22s%d\n", "varbit_id", npc->varbit_id);
    printf("  %-22s%d\n", "varp_index", npc->varp_index);
    print_int_list("configs (transforms)", npc->configs, npc->configs_count);
    printf("  %-22s%s\n", "interactable", npc->is_interactable ? "true" : "false");
    printf("  %-22s%s\n", "minimap_visible", npc->is_minimap_visible ? "true" : "false");
    printf(
        "  %-22s%d %d %d %d %d %d\n",
        "stats(74-79)",
        npc->stats[0],
        npc->stats[1],
        npc->stats[2],
        npc->stats[3],
        npc->stats[4],
        npc->stats[5]);
    for( int i = 0; i < 5; i++ )
        if( npc->actions[i] )
            printf("  %-22s[%d] %s\n", "action", i, npc->actions[i]);
    if( npc->recolor_count > 0 )
    {
        printf("  %-22s", "recolour");
        for( int i = 0; i < npc->recolor_count; i++ )
            printf("%s%d->%d", i ? ", " : "", npc->recolor_to_find[i], npc->recolor_to_replace[i]);
        printf("\n");
    }
    if( npc->retexture_count > 0 )
    {
        printf("  %-22s", "retexture");
        for( int i = 0; i < npc->retexture_count; i++ )
            printf(
                "%s%d->%d", i ? ", " : "", npc->retexture_to_find[i], npc->retexture_to_replace[i]);
        printf("\n");
    }
    if( npc->head_icon_count > 0 )
    {
        printf("  %-22s", "head_icons");
        for( int i = 0; i < npc->head_icon_count; i++ )
            printf(
                "%sarchive %d idx %d",
                i ? ", " : "",
                npc->head_icon_archive_ids[i],
                npc->head_icon_sprite_index[i]);
        printf("\n");
    }
    RSCache_Dat2ConfigNpcFree(npc);
}

static void
dump_seq(
    struct Tool_Dat2Cache* c,
    int id)
{
    struct RSCache_Dat2ConfigSequence* seq = tool_dat2_seq_load(c, id);
    if( !seq )
    {
        printf("seq %d: <absent>\n", id);
        return;
    }
    printf("seq %d%s%s\n", id, seq->debug_name ? "  " : "", seq->debug_name ? seq->debug_name : "");
    printf("  %-22s%d\n", "frame_count", seq->frame_count);
    if( seq->frame_count > 0 )
    {
        int total = 0;
        printf("  %-22s", "frames(len)");
        for( int i = 0; i < seq->frame_count; i++ )
        {
            printf("%s%d(%d)", i ? ", " : "", seq->frame_ids[i], seq->frame_lengths[i]);
            total += seq->frame_lengths[i];
        }
        printf("\n  %-22s%d client cycles (%.2f server ticks)\n", "duration", total, total / 30.0);
    }
    printf("  %-22s%d\n", "frame_step", seq->frame_step);
    printf("  %-22s%s\n", "stretches", seq->stretches ? "true" : "false");
    printf("  %-22s%d\n", "priority", seq->priority);
    printf("  %-22s%d\n", "forced_priority", seq->forced_priority);
    printf("  %-22s%d\n", "max_loops", seq->max_loops);
    printf("  %-22s%d\n", "reply_mode", seq->reply_mode);
    printf("  %-22s%d\n", "precedence_animating", seq->precedence_animating);
    printf("  %-22s%d / %d\n", "hand items l/r", seq->left_hand_item, seq->right_hand_item);
    printf("  %-22s%d\n", "anim_maya_id", seq->anim_maya_id);
    if( seq->frame_sounds.count > 0 )
    {
        printf("  %-22s", "frame_sounds");
        for( int i = 0; i < seq->frame_sounds.count; i++ )
            printf(
                "%sframe %d -> sound %d",
                i ? ", " : "",
                seq->frame_sounds.frames[i],
                seq->frame_sounds.sounds[i].id);
        printf("\n");
    }
    RSCache_Dat2ConfigSequenceFree(seq);
}

/* Load and decode one spotanim record. Returns NULL when absent. */
static struct RSCache_Dat2ConfigSpotanim*
load_spotanim(
    struct Tool_Dat2Cache* c,
    int id)
{
    struct RSCache_RecordAddress addr =
        RSCache_RecordAddressFor(&c->profile, RSCACHE_TYPE_SPOTANIM);
    int table;
    int archive_id;
    struct RSCache_Dat2DiskArchive* archive;
    struct RSCache_FileList* files;
    struct RSCache_Dat2ConfigSpotanim* out = NULL;

    if( addr.group_shift == 0 )
    {
        table = RSCache_Dat2DiskTableId(c->disk, RSCACHE_DAT2_TABLE_CONFIGS);
        archive_id = RSCACHE_DAT2_CONFIG_KIND_SPOTANIM;
    }
    else
    {
        table = RSCache_Dat2DiskTableId(c->disk, addr.table);
        archive_id = id >> addr.group_shift;
    }

    archive = RSCache_Dat2DiskArchiveNewLoad(c->disk, table, archive_id);
    if( !archive )
        return NULL;
    if( !RSCache_Dat2DiskArchiveInitMetadata(c->disk, archive) || archive->file_count <= 0 )
    {
        RSCache_Dat2DiskArchiveFree(archive);
        return NULL;
    }
    files = RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
    if( !files )
    {
        RSCache_Dat2DiskArchiveFree(archive);
        return NULL;
    }
    for( int i = 0; i < files->file_count; i++ )
    {
        int file_id = archive->file_ids ? archive->file_ids[i] : i;
        int global = addr.group_shift == 0
                         ? file_id
                         : ((archive_id << addr.group_shift) | (file_id & addr.file_mask));
        if( global != id || files->file_sizes[i] <= 0 )
            continue;
        out = RSCache_Dat2ConfigSpotanimNewDecode(
            c->profile.revision, files->files[i], files->file_sizes[i]);
        break;
    }
    RSCache_FileListFree(files);
    RSCache_Dat2DiskArchiveFree(archive);
    return out;
}

static void
print_spotanim(
    int id,
    const struct RSCache_Dat2ConfigSpotanim* sa)
{
    printf("spotanim %d%s%s\n", id, sa->name ? "  " : "", sa->name ? sa->name : "");
    printf("  %-22s%d\n", "model", sa->model);
    printf("  %-22s%d\n", "anim (seq)", sa->anim);
    printf("  %-22s%d / %d\n", "resize h/v", sa->resizeh, sa->resizev);
    printf("  %-22s%d\n", "angle", sa->angle);
    printf("  %-22s%d / %d\n", "ambient/contrast", sa->ambient, sa->contrast);
    if( sa->recol_count > 0 )
    {
        printf("  %-22s", "recolour");
        for( int i = 0; i < sa->recol_count; i++ )
            printf("%s%d->%d", i ? ", " : "", sa->recol_s[i], sa->recol_d[i]);
        printf("\n");
    }
    if( sa->retex_count > 0 )
    {
        printf("  %-22s", "retexture");
        for( int i = 0; i < sa->retex_count; i++ )
            printf("%s%d->%d", i ? ", " : "", sa->retex_s[i], sa->retex_d[i]);
        printf("\n");
    }
}


/* Load and decode one obj record. Returns NULL when absent. */
static struct RSCache_Dat2ConfigObj*
load_obj(
    struct Tool_Dat2Cache* c,
    int id)
{
    struct RSCache_RecordAddress addr = RSCache_RecordAddressFor(&c->profile, RSCACHE_TYPE_OBJ);
    int table;
    int archive_id;
    struct RSCache_Dat2DiskArchive* archive;
    struct RSCache_FileList* files;
    struct RSCache_Dat2ConfigObj* out = NULL;

    if( addr.group_shift == 0 )
    {
        table = RSCache_Dat2DiskTableId(c->disk, RSCACHE_DAT2_TABLE_CONFIGS);
        archive_id = RSCACHE_DAT2_CONFIG_KIND_OBJECT;
    }
    else
    {
        table = RSCache_Dat2DiskTableId(c->disk, addr.table);
        archive_id = id >> addr.group_shift;
    }

    archive = RSCache_Dat2DiskArchiveNewLoad(c->disk, table, archive_id);
    if( !archive )
        return NULL;
    if( !RSCache_Dat2DiskArchiveInitMetadata(c->disk, archive) || archive->file_count <= 0 )
    {
        RSCache_Dat2DiskArchiveFree(archive);
        return NULL;
    }
    files = RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
    if( !files )
    {
        RSCache_Dat2DiskArchiveFree(archive);
        return NULL;
    }
    for( int i = 0; i < files->file_count; i++ )
    {
        int file_id = archive->file_ids ? archive->file_ids[i] : i;
        int global = addr.group_shift == 0
                         ? file_id
                         : ((archive_id << addr.group_shift) | (file_id & addr.file_mask));
        if( global != id || files->file_sizes[i] <= 0 )
            continue;
        out = RSCache_Dat2ConfigObjNewDecodeProfile(
            &c->profile, files->files[i], files->file_sizes[i]);
        break;
    }
    RSCache_FileListFree(files);
    RSCache_Dat2DiskArchiveFree(archive);
    return out;
}

static void
dump_obj(
    struct Tool_Dat2Cache* c,
    int id)
{
    struct RSCache_Dat2ConfigObj* obj = load_obj(c, id);
    if( !obj )
    {
        printf("obj %d: <absent>\n", id);
        return;
    }
    printf("obj %d  \"%s\"\n", id, obj->name ? obj->name : "(null)");
    if( obj->examine )
        printf("  %-22s%s\n", "examine", obj->examine);
    printf("  %-22s%d\n", "inventory_model", obj->inventory_model_id);
    printf("  %-22s%d\n", "cost", obj->cost);
    printf("  %-22s%s\n", "members", obj->is_members ? "yes" : "no");
    printf("  %-22s%d\n", "stacking_behaviour", obj->stacking_behaviour);
    printf("  %-22s%d / %d / %d\n", "wearpos 1/2/3", obj->wearpos_1, obj->wearpos_2, obj->wearpos_3);
    printf("  %-22s%d\n", "2dzoom", obj->zoom2d);
    printf("  %-22s%d / %d / %d\n", "2dxan/yan/zan", obj->xan2d, obj->yan2d, obj->zan2d);
    printf("  %-22s%d / %d\n", "2dxof/yof", obj->offset_x2d, obj->offset_y2d);
    printf("  %-22s%d / %d\n", "ambient/contrast", obj->ambient, obj->contrast);
    printf("  %-22s%d / %d / %d  off %d\n", "male models", obj->male_model_0,
           obj->male_model_1, obj->male_model_2, obj->male_offset);
    printf("  %-22s%d / %d / %d  off %d\n", "female models", obj->female_model_0,
           obj->female_model_1, obj->female_model_2, obj->female_offset);
    printf("  %-22s%d / %d\n", "male head", obj->male_head_model, obj->male_head_model_2);
    printf("  %-22s%d / %d\n", "female head", obj->female_head_model, obj->female_head_model_2);
    printf("  %-22s%d\n", "category", obj->category);
    printf("  %-22s%d\n", "weight", obj->weight);
    printf("  %-22s%d / %d\n", "noted id/template", obj->noted_id, obj->noted_template);
    printf("  %-22s%d / %d / %d\n", "resize x/y/z", obj->resize_x, obj->resize_y, obj->resize_z);
    for( int i = 0; i < 5; i++ )
        if( obj->actions[i] )
            printf("  %-22s[%d] %s\n", "ground op", i, obj->actions[i]);
    for( int i = 0; i < 5; i++ )
        if( obj->if_actions[i] )
            printf("  %-22s[%d] %s\n", "inv op", i, obj->if_actions[i]);
    if( obj->recolor_count > 0 )
    {
        printf("  %-22s", "recolour");
        for( int i = 0; i < obj->recolor_count; i++ )
            printf("%s%d->%d", i ? ", " : "", obj->recolors_from[i], obj->recolors_to[i]);
        printf("\n");
    }
    if( obj->retexture_count > 0 )
    {
        printf("  %-22s", "retexture");
        for( int i = 0; i < obj->retexture_count; i++ )
            printf("%s%d->%d", i ? ", " : "", obj->retextures_from[i], obj->retextures_to[i]);
        printf("\n");
    }
    RSCache_Dat2ConfigObjFree(obj);
}


/*
 * Dump a framemap: the rig an animation is authored against.
 *
 * A frame does not move vertices, it moves *label groups* — each transform in
 * the framemap names a set of labels, and the client applies it to whichever of
 * the model's vertices carry them. So a model and an animation only agree if
 * they were built against the same numbering, and this is what shows whether
 * two eras did.
 */
static void
dump_framemap(
    struct Tool_Dat2Cache* c,
    int id)
{
    static const char* kind[] = { "origin", "translate", "rotate", "scale", "?4", "alpha" };
    struct RSCache_Dat2Framemap* fm = tool_dat2_framemap_load(c, id);
    int max_label = -1;
    if( !fm )
    {
        printf("framemap %d: <absent>\n", id);
        return;
    }
    printf("framemap %d  transforms=%d\n", id, fm->length);
    for( int i = 0; i < fm->length; i++ )
    {
        int t = fm->types[i];
        printf(
            "  [%3d] %-9s labels:",
            i,
            (t >= 0 && t < 6) ? kind[t] : "?");
        for( int j = 0; j < fm->bone_groups_lengths[i]; j++ )
        {
            printf(" %d", fm->bone_groups[i][j]);
            if( fm->bone_groups[i][j] > max_label )
                max_label = fm->bone_groups[i][j];
        }
        printf("\n");
    }
    printf("  highest label referenced: %d\n", max_label);
    RSCache_Dat2FramemapFree(fm);
}


/* Load and decode one loc record. Returns NULL when absent. */
static struct RSCache_Dat2ConfigLoc*
load_loc(
    struct Tool_Dat2Cache* c,
    int id)
{
    struct RSCache_RecordAddress addr = RSCache_RecordAddressFor(&c->profile, RSCACHE_TYPE_LOC);
    int table;
    int archive_id;
    struct RSCache_Dat2DiskArchive* archive;
    struct RSCache_FileList* files;
    struct RSCache_Dat2ConfigLoc* out = NULL;

    if( addr.group_shift == 0 )
    {
        table = RSCache_Dat2DiskTableId(c->disk, RSCACHE_DAT2_TABLE_CONFIGS);
        archive_id = RSCACHE_DAT2_CONFIG_KIND_LOCS;
    }
    else
    {
        table = RSCache_Dat2DiskTableId(c->disk, addr.table);
        archive_id = id >> addr.group_shift;
    }

    archive = RSCache_Dat2DiskArchiveNewLoad(c->disk, table, archive_id);
    if( !archive )
        return NULL;
    if( !RSCache_Dat2DiskArchiveInitMetadata(c->disk, archive) || archive->file_count <= 0 )
    {
        RSCache_Dat2DiskArchiveFree(archive);
        return NULL;
    }
    files = RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
    if( !files )
    {
        RSCache_Dat2DiskArchiveFree(archive);
        return NULL;
    }
    for( int i = 0; i < files->file_count; i++ )
    {
        int file_id = archive->file_ids ? archive->file_ids[i] : i;
        int global = addr.group_shift == 0
                         ? file_id
                         : ((archive_id << addr.group_shift) | (file_id & addr.file_mask));
        if( global != id || files->file_sizes[i] <= 0 )
            continue;
        out = RSCache_Dat2ConfigLocNewDecodeProfile(
            &c->profile, files->files[i], files->file_sizes[i]);
        break;
    }
    RSCache_FileListFree(files);
    RSCache_Dat2DiskArchiveFree(archive);
    return out;
}

static void
dump_loc(
    struct Tool_Dat2Cache* c,
    int id)
{
    struct RSCache_Dat2ConfigLoc* loc = load_loc(c, id);
    if( !loc )
    {
        printf("loc %d: <absent>\n", id);
        return;
    }
    printf("loc %d  \"%s\"\n", id, loc->name ? loc->name : "(null)");
    printf("  %-22s%d x %d\n", "width x length", loc->size_x, loc->size_z);
    for( int i = 0; i < loc->shapes_and_model_count; i++ )
    {
        printf("  %-22sshape %d:", "models", loc->shapes ? loc->shapes[i] : -1);
        for( int j = 0; j < (loc->lengths ? loc->lengths[i] : 0); j++ )
            printf(" %d", loc->models[i][j]);
        printf("\n");
    }
    printf("  %-22s%d\n", "transform_varbit", loc->transform_varbit);
    printf("  %-22s%d\n", "transform_varp", loc->transform_varp);
    if( loc->transform_count > 0 )
    {
        printf("  %-22s", "transforms");
        for( int i = 0; i < loc->transform_count; i++ )
            printf("%s%d", i ? ", " : "", loc->transforms[i]);
        printf("\n");
    }
    RSCache_Dat2ConfigLocFree(loc);
}


/*
 * Dump the AnimBase inside a LostCity `.anim` archive — the destination rig,
 * for comparing against a source framemap.
 */
static void
dump_dat1_animbase(const char* path)
{
    FILE* f = fopen(path, "rb");
    long size;
    char* data;
    struct RSCache_Dat1AnimBaseFrames* abf;
    int max_label = -1;

    if( !f )
    {
        printf("%s: cannot open\n", path);
        return;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = malloc((size_t)size);
    if( !data || fread(data, 1, (size_t)size, f) != (size_t)size )
    {
        fclose(f);
        free(data);
        printf("%s: read failed\n", path);
        return;
    }
    fclose(f);

    abf = RSCache_Dat1AnimBaseFramesNewDecode(data, (int)size);
    if( !abf || !abf->base )
    {
        printf("%s: decode failed\n", path);
        free(data);
        return;
    }
    printf("%s\n  frames=%d  transforms=%d\n", path, abf->frame_count, abf->base->length);
    for( int i = 0; i < abf->base->length; i++ )
        for( int j = 0; j < abf->base->label_counts[i]; j++ )
            if( abf->base->labels[i][j] > max_label )
                max_label = abf->base->labels[i][j];
    printf("  highest label referenced: %d\n", max_label);
    for( int i = 0; i < abf->base->length; i++ )
    {
        printf("  [%3d] type %d labels:", i, abf->base->types[i]);
        for( int j = 0; j < abf->base->label_counts[i]; j++ )
            printf(" %d", abf->base->labels[i][j]);
        printf("\n");
    }
    RSCache_Dat1AnimBaseFramesFree(abf);
    free(data);
}



/* Load one config record's bytes. Caller frees out->data. */
static int
load_record(
    struct Tool_Dat2Cache* c,
    enum RSCache_Type type,
    int config_kind,
    int id,
    struct Tool_Bytes* out)
{
    struct RSCache_RecordAddress addr = RSCache_RecordAddressFor(&c->profile, type);
    int table, archive_id, found = 0;
    struct RSCache_Dat2DiskArchive* archive;
    struct RSCache_FileList* files;

    memset(out, 0, sizeof(*out));
    if( addr.group_shift == 0 )
    {
        table = RSCache_Dat2DiskTableId(c->disk, RSCACHE_DAT2_TABLE_CONFIGS);
        archive_id = config_kind;
    }
    else
    {
        table = RSCache_Dat2DiskTableId(c->disk, addr.table);
        archive_id = id >> addr.group_shift;
    }
    archive = RSCache_Dat2DiskArchiveNewLoad(c->disk, table, archive_id);
    if( !archive )
        return 0;
    if( !RSCache_Dat2DiskArchiveInitMetadata(c->disk, archive) || archive->file_count <= 0 )
    {
        RSCache_Dat2DiskArchiveFree(archive);
        return 0;
    }
    files = RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
    if( !files )
    {
        RSCache_Dat2DiskArchiveFree(archive);
        return 0;
    }
    for( int i = 0; i < files->file_count; i++ )
    {
        int file_id = archive->file_ids ? archive->file_ids[i] : i;
        int global = addr.group_shift == 0
                         ? file_id
                         : ((archive_id << addr.group_shift) | (file_id & addr.file_mask));
        if( global != id || files->file_sizes[i] <= 0 )
            continue;
        out->size = files->file_sizes[i];
        out->data = malloc((size_t)out->size);
        if( out->data )
        {
            memcpy(out->data, files->files[i], (size_t)out->size);
            found = 1;
        }
        break;
    }
    RSCache_FileListFree(files);
    RSCache_Dat2DiskArchiveFree(archive);
    return found;
}

/*
 * Per-label vertex centroids of the source rig's own body models.
 *
 * A vertex label is a joint index local to the rig that authored it, so the
 * only way to line two eras' rigs up is to ask where each label physically
 * sits on the body. Identikit models are the right sample: they are the player
 * body itself, in the rest pose, and between them they cover every joint an
 * animation can address.
 *
 * body_part_id is printed alongside so the anatomy can be checked rather than
 * assumed — an arm label landing among the legs means the match is wrong.
 */
static void
dump_idk_centroids(struct Tool_Dat2Cache* c)
{
    long long sx[256] = { 0 }, sy[256] = { 0 }, sz[256] = { 0 };
    long long n[256] = { 0 };
    int part_seen[256][16];
    int* ids = NULL;
    int count = 0;

    memset(part_seen, 0, sizeof(part_seen));
    if( !enumerate_ids(c, RSCACHE_TYPE_IDK, RSCACHE_DAT2_CONFIG_KIND_IDENTKIT, &ids, &count) )
    {
        printf("idk: enumerate failed\n");
        return;
    }
    for( int i = 0; i < count; i++ )
    {
        struct Tool_Bytes rec;
        struct RSCache_Dat2ConfigIdk* idk;
        if( !load_record(c, RSCACHE_TYPE_IDK, RSCACHE_DAT2_CONFIG_KIND_IDENTKIT, ids[i], &rec) )
            continue;
        idk = RSCache_Dat2ConfigIdkNewDecode(rec.data, rec.size);
        tool_bytes_free(&rec);
        if( !idk )
            continue;
        for( int m = 0; m < idk->model_ids_count; m++ )
        {
            struct RSCache_Model* model = tool_dat2_model_load(c, idk->model_ids[m]);
            if( !model )
                continue;
            if( model->vertex_bone_map )
                for( int v = 0; v < model->vertex_count; v++ )
                {
                    int lab = model->vertex_bone_map[v];
                    if( lab == 255 )
                        continue;
                    sx[lab] += model->vertices_x[v];
                    sy[lab] += model->vertices_y[v];
                    sz[lab] += model->vertices_z[v];
                    n[lab]++;
                    if( idk->body_part_id >= 0 && idk->body_part_id < 16 )
                        part_seen[lab][idk->body_part_id] = 1;
                }
            RSCache_ModelFree(model);
        }
        RSCache_Dat2ConfigIdkFree(idk);
    }
    free(ids);

    printf("# label n x y z parts\n");
    for( int l = 0; l < 256; l++ )
    {
        if( !n[l] )
            continue;
        printf("%d %lld %lld %lld %lld", l, n[l], sx[l] / n[l], sy[l] / n[l], sz[l] / n[l]);
        for( int p = 0; p < 16; p++ )
            if( part_seen[l][p] )
                printf(" p%d", p);
        printf("\n");
    }
}


/* Face-level statistics for a decoded model: what a renderer actually eats. */
static void
dump_model_stats(
    const char* title,
    struct RSCache_Model* model)
{
    int flat = 0;
    int textured = 0;
    int hidden = 0;
    int alpha_faces = 0;
    int dark = 0;
    int light_hist[8] = { 0 };

    printf("%s\n", title);
    if( !model )
    {
        printf("  <decode failed>\n");
        return;
    }
    printf("  %-22s%d\n", "vertices", model->vertex_count);
    printf("  %-22s%d\n", "faces", model->face_count);
    printf("  %-22s%d\n", "textured triangles", model->textured_face_count);
    printf(
        "  %-22s%s / %s / %s / %s\n",
        "arrays i/p/a/t",
        model->face_infos ? "infos" : "-",
        model->face_priorities ? "prios" : "-",
        model->face_alphas ? "alphas" : "-",
        model->face_textures ? "textures" : "-");
    for( int i = 0; i < model->face_count; i++ )
    {
        int info = model->face_infos ? model->face_infos[i] : 0;
        int color = model->face_colors ? model->face_colors[i] : 0;
        if( (info & 3) == 2 )
            textured++;
        else if( (info & 3) == 3 )
            hidden++;
        else if( (info & 3) == 1 )
            flat++;
        if( model->face_alphas && model->face_alphas[i] != 0 )
            alpha_faces++;
        {
            int light = color & 0x7f;
            light_hist[light >> 4]++;
            if( light < 8 )
                dark++;
        }
    }
    printf("  %-22s%d\n", "flat-shaded (info&3=1)", flat);
    printf("  %-22s%d\n", "textured (info&3=2)", textured);
    printf("  %-22s%d\n", "info&3=3", hidden);
    printf("  %-22s%d\n", "alpha!=0 faces", alpha_faces);
    {
        int prio_hist[16] = { 0 };
        int prio_over = 0;
        for( int i = 0; i < model->face_count; i++ )
            if( model->face_priorities )
            {
                int prio = model->face_priorities[i];
                if( prio < 16 )
                    prio_hist[prio]++;
                else
                    prio_over++;
            }
        printf("  %-22s", "priority histogram");
        for( int i = 0; i < 16; i++ )
            if( prio_hist[i] )
                printf("%d:%d  ", i, prio_hist[i]);
        printf(">15:%d\n", prio_over);
    }
    {
        int alpha_hist[8] = { 0 };
        for( int i = 0; i < model->face_count; i++ )
            if( model->face_alphas && model->face_alphas[i] != 0 )
                alpha_hist[model->face_alphas[i] >> 5]++;
        printf("  %-22s", "alpha histogram");
        for( int i = 0; i < 8; i++ )
            printf("%s%d-%d:%d", i ? "  " : "", i * 32, i * 32 + 31, alpha_hist[i]);
        printf("\n");
    }
    printf("  %-22s%d\n", "lightness<8 faces", dark);
    printf("  %-22s", "lightness histogram");
    for( int i = 0; i < 8; i++ )
        printf("%s%d-%d:%d", i ? "  " : "", i * 16, i * 16 + 15, light_hist[i]);
    printf("\n");

    /*
     * Vertex labels — the thing RIGGING_OSRS_RS2.md's whole method exists to
     * retarget, and the one field the rest of this dump does not report.
     * `vertex_bone_map` is NULL for a model with no per-vertex labels at all
     * (a single rigid piece with nothing to retarget); printing "none" rather
     * than staying silent is what tells a caller that apart from a model that
     * genuinely carries labels 0..N and was not asked about.
     */
    if( !model->vertex_bone_map )
    {
        printf("  %-22snone (rigid, no per-vertex labels)\n", "vertex labels");
    }
    else
    {
        int seen[256] = { 0 };
        int distinct = 0;

        for( int i = 0; i < model->vertex_count; i++ )
            seen[model->vertex_bone_map[i]]++;
        for( int i = 0; i < 256; i++ )
            if( seen[i] )
                distinct++;
        /* No sentinel assumed here — 0 is a real label on the rev-254 side
         * (joint 0, "ground anchor" in RIGGING_OSRS_RS2.md's table) and 255 is
         * that side's no-group sentinel, but this prints whichever cache the
         * model came from, so every value it saw is listed rather than one
         * being silently treated as "none". */
        printf("  %-22s%d distinct label(s) over %d vertices:",
               "vertex labels", distinct, model->vertex_count);
        for( int i = 0; i < 256; i++ )
            if( seen[i] )
                printf(" %d(x%d)", i, seen[i]);
        printf("\n");

        /* Per-label centroid, the same geometry-not-guesswork check
         * RIGGING_OSRS_RS2.md uses to settle which physical hand a label is. */
        for( int i = 0; i < 256; i++ )
        {
            long long sx = 0, sy = 0, sz = 0;

            if( !seen[i] )
                continue;
            for( int v = 0; v < model->vertex_count; v++ )
                if( model->vertex_bone_map[v] == i )
                {
                    sx += model->vertices_x[v];
                    sy += model->vertices_y[v];
                    sz += model->vertices_z[v];
                }
            printf("    label %3d centroid (x,y,z) = (%lld, %lld, %lld)\n", i,
                   sx / seen[i], sy / seen[i], sz / seen[i]);
        }
    }
}

int
main(int argc, char** argv)
{
    const char* cache_dir = NULL;
    const char* rev = NULL;
    const char* name = NULL;
    const char* type_filter = "all";
    int dump_npc_id = -1;
    int dump_obj_id = -1;
    int dump_loc_id = -1;
    int dump_model_id = -1;
    const char* dump_ob2_path = NULL;
    int dump_framemap_id = -1;
    const char* dat1_anim = NULL;
    int idk_centroids = 0;
    int dump_seq_id = -1;
    int dump_spotanim_id = -1;
    int scan_spotanim_model = -1;
    struct RSCache profile;
    struct Tool_Dat2Cache cache;

    for( int i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--rev") == 0 && i + 1 < argc )
            rev = argv[++i];
        else if( strcmp(argv[i], "--name") == 0 && i + 1 < argc )
            name = argv[++i];
        else if( strcmp(argv[i], "--type") == 0 && i + 1 < argc )
            type_filter = argv[++i];
        else if( strcmp(argv[i], "--npc") == 0 && i + 1 < argc )
            dump_npc_id = atoi(argv[++i]);
        else if( strcmp(argv[i], "--obj") == 0 && i + 1 < argc )
            dump_obj_id = atoi(argv[++i]);
        else if( strcmp(argv[i], "--loc") == 0 && i + 1 < argc )
            dump_loc_id = atoi(argv[++i]);
        else if( strcmp(argv[i], "--model") == 0 && i + 1 < argc )
            dump_model_id = atoi(argv[++i]);
        else if( strcmp(argv[i], "--ob2") == 0 && i + 1 < argc )
            dump_ob2_path = argv[++i];
        else if( strcmp(argv[i], "--framemap") == 0 && i + 1 < argc )
            dump_framemap_id = atoi(argv[++i]);
        else if( strcmp(argv[i], "--dat1-anim") == 0 && i + 1 < argc )
            dat1_anim = argv[++i];
        else if( strcmp(argv[i], "--idk-centroids") == 0 )
            idk_centroids = 1;
        else if( strcmp(argv[i], "--seq") == 0 && i + 1 < argc )
            dump_seq_id = atoi(argv[++i]);
        else if( strcmp(argv[i], "--spotanim") == 0 && i + 1 < argc )
            dump_spotanim_id = atoi(argv[++i]);
        else if( strcmp(argv[i], "--scan-spotanim-model") == 0 && i + 1 < argc )
            scan_spotanim_model = atoi(argv[++i]);
        else if( argv[i][0] != '-' )
            cache_dir = argv[i];
        else
        {
            usage(argv[0]);
            return 2;
        }
    }

    if( dat1_anim )
    {
        dump_dat1_animbase(dat1_anim);
        return 0;
    }
    if( !cache_dir || !rev )
    {
        usage(argv[0]);
        return 2;
    }
    if( !tool_resolve_profile(rev, NULL, NULL, NULL, NULL, &profile) )
        return 2;
    if( !tool_dat2_open(cache_dir, &profile, &cache) )
    {
        fprintf(stderr, "could not open dat2 cache at %s\n", cache_dir);
        return 1;
    }

    if( dump_npc_id >= 0 )
        dump_npc(&cache, dump_npc_id);
    if( dump_obj_id >= 0 )
        dump_obj(&cache, dump_obj_id);
    if( dump_loc_id >= 0 )
        dump_loc(&cache, dump_loc_id);
    if( dump_model_id >= 0 )
    {
        char title[64];
        struct RSCache_Model* model = tool_dat2_model_load(&cache, dump_model_id);
        snprintf(title, sizeof(title), "source model %d", dump_model_id);
        dump_model_stats(title, model);
        if( model )
            RSCache_ModelFree(model);
    }
    if( dump_ob2_path )
    {
        FILE* fh = fopen(dump_ob2_path, "rb");
        if( !fh )
            printf("ob2 %s: <unreadable>\n", dump_ob2_path);
        else
        {
            char* buf = malloc(4 << 20);
            size_t got = fread(buf, 1, 4 << 20, fh);
            fclose(fh);
            struct RSCache_Model* model = RSCache_ModelNewDecode(buf, (int)got);
            dump_model_stats(dump_ob2_path, model);
            if( model )
                RSCache_ModelFree(model);
            free(buf);
        }
    }
    if( dump_framemap_id >= 0 )
        dump_framemap(&cache, dump_framemap_id);
    if( idk_centroids )
        dump_idk_centroids(&cache);
    if( dump_seq_id >= 0 )
        dump_seq(&cache, dump_seq_id);
    if( dump_spotanim_id >= 0 )
    {
        struct RSCache_Dat2ConfigSpotanim* sa = load_spotanim(&cache, dump_spotanim_id);
        if( !sa )
            printf("spotanim %d: <absent>\n", dump_spotanim_id);
        else
        {
            print_spotanim(dump_spotanim_id, sa);
            RSCache_Dat2ConfigSpotanimFree(sa);
        }
    }

    if( scan_spotanim_model >= 0 )
    {
        int* ids = NULL;
        int count = 0;
        if( enumerate_ids(&cache, RSCACHE_TYPE_SPOTANIM, RSCACHE_DAT2_CONFIG_KIND_SPOTANIM, &ids, &count) )
        {
            for( int i = 0; i < count; i++ )
            {
                struct RSCache_Dat2ConfigSpotanim* sa = load_spotanim(&cache, ids[i]);
                if( !sa )
                    continue;
                if( sa->model == scan_spotanim_model )
                    print_spotanim(ids[i], sa);
                RSCache_Dat2ConfigSpotanimFree(sa);
            }
            free(ids);
        }
    }

    if( name )
    {
        int all = strcmp(type_filter, "all") == 0;
        int want_npc = all || strcmp(type_filter, "npc") == 0;
        int want_seq = all || strcmp(type_filter, "seq") == 0;
        int want_spotanim = all || strcmp(type_filter, "spotanim") == 0;
        int want_obj = all || strcmp(type_filter, "obj") == 0;

        if( want_obj )
        {
            int* ids = NULL;
            int count = 0;
            if( enumerate_ids(
                    &cache, RSCACHE_TYPE_OBJ, RSCACHE_DAT2_CONFIG_KIND_OBJECT, &ids, &count) )
            {
                for( int i = 0; i < count; i++ )
                {
                    struct RSCache_Dat2ConfigObj* obj = load_obj(&cache, ids[i]);
                    if( !obj )
                        continue;
                    if( obj->name && strcasestr_ascii(obj->name, name) )
                        printf(
                            "obj %6d  \"%s\"  model %d\n",
                            ids[i],
                            obj->name,
                            obj->inventory_model_id);
                    RSCache_Dat2ConfigObjFree(obj);
                }
                free(ids);
            }
        }

        /* Sequences and spotanims carry the content team's own debug names in
         * this era, which is the only handle on assets no config points at by
         * name — projectile spotanims especially. */
        if( want_seq )
        {
            struct RSCache_Dat2ConfigSequence** seqs = NULL;
            int* ids = NULL;
            int count = 0;
            if( tool_dat2_seq_load_all(&cache, &seqs, &ids, &count) )
            {
                for( int i = 0; i < count; i++ )
                {
                    if( seqs[i] && seqs[i]->debug_name &&
                        strcasestr_ascii(seqs[i]->debug_name, name) )
                        printf(
                            "seq %6d  \"%s\"  frames %d\n",
                            ids[i],
                            seqs[i]->debug_name,
                            seqs[i]->frame_count);
                    RSCache_Dat2ConfigSequenceFree(seqs[i]);
                }
                free(seqs);
                free(ids);
            }
        }

        if( want_spotanim )
        {
            int* ids = NULL;
            int count = 0;
            if( enumerate_ids(
                    &cache, RSCACHE_TYPE_SPOTANIM, RSCACHE_DAT2_CONFIG_KIND_SPOTANIM, &ids, &count) )
            {
                for( int i = 0; i < count; i++ )
                {
                    struct RSCache_Dat2ConfigSpotanim* sa = load_spotanim(&cache, ids[i]);
                    if( !sa )
                        continue;
                    if( sa->name && strcasestr_ascii(sa->name, name) )
                        printf(
                            "spotanim %6d  \"%s\"  model %d  seq %d\n",
                            ids[i],
                            sa->name,
                            sa->model,
                            sa->anim);
                    RSCache_Dat2ConfigSpotanimFree(sa);
                }
                free(ids);
            }
        }

        if( want_npc )
        {
            int* ids = NULL;
            int count = 0;
            if( enumerate_ids(&cache, RSCACHE_TYPE_NPC, RSCACHE_DAT2_CONFIG_KIND_NPC, &ids, &count) )
            {
                for( int i = 0; i < count; i++ )
                {
                    struct RSCache_Dat2ConfigNpc* npc = tool_dat2_npc_load(&cache, ids[i]);
                    if( !npc )
                        continue;
                    if( npc->name && strcasestr_ascii(npc->name, name) )
                        printf(
                            "npc %6d  \"%s\"  level %d  size %d\n",
                            ids[i],
                            npc->name,
                            npc->combat_level,
                            npc->size);
                    RSCache_Dat2ConfigNpcFree(npc);
                }
                free(ids);
            }
        }
    }

    tool_dat2_close(&cache);
    return 0;
}
