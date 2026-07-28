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
        "  %s --rev NAME <cache_dir> --seq ID\n"
        "  %s --rev NAME <cache_dir> --spotanim ID\n"
        "  %s --rev NAME <cache_dir> --scan-spotanim-model ID\n",
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

int
main(int argc, char** argv)
{
    const char* cache_dir = NULL;
    const char* rev = NULL;
    const char* name = NULL;
    const char* type_filter = "all";
    int dump_npc_id = -1;
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
