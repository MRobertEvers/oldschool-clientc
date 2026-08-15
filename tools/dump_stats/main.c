/*
 * Dump every NPC and OBJ (item) record in a dat2 cache to CSV.
 *
 * Usage:
 *   dump_stats --rev NAME <cache_dir> [--npc-csv PATH] [--obj-csv PATH]
 *   dump_stats --game G --epoch E --revision N [--quirks LIST] <cache_dir> ...
 *
 * Both config families are walked from their own reference table rather than a
 * 0..max sweep: config groups are sparse, and a sweep decodes a lot of absent
 * records and reports their misses as failures.
 *
 * Each row carries `decoded_bytes` / `record_bytes` / `stop_opcode`. The
 * decoders in 3rd/rscache stop at the first opcode they do not know, because an
 * unknown opcode has an unknown payload length and reading past it fills real
 * fields with payload bytes. A row whose decoded_bytes is short is therefore
 * truthful up to that point and empty after it, and stop_opcode names the
 * opcode that ended it — that is the column to filter on before trusting a
 * late field. -1 means the record terminated cleanly on opcode 0.
 */

#include "rscache.h"

#include "windows_cp1252.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- CSV writing -------------------------------------------------------- */

static void
csv_sep(FILE* f, int* first)
{
    if( *first )
        *first = 0;
    else
        fputc(',', f);
}

/*
 * One character, transcoded.
 *
 * Cache strings are windows-1252 bytes and rscache keeps them byte-transparent,
 * so writing them straight out produces a file that is not valid UTF-8 — real
 * names carry 0x92 (a right single quote) and 0xF9, and a CSV reader stops dead
 * on the first one. Everything below 0x80 is already UTF-8.
 */
static void
csv_putc(FILE* f, unsigned char c)
{
    if( c < 0x80 )
    {
        fputc(c, f);
        return;
    }

    uint16_t u = cp1252_decode_to_utf16(c);
    if( u < 0x800 )
    {
        fputc(0xC0 | (u >> 6), f);
        fputc(0x80 | (u & 0x3F), f);
    }
    else
    {
        fputc(0xE0 | (u >> 12), f);
        fputc(0x80 | ((u >> 6) & 0x3F), f);
        fputc(0x80 | (u & 0x3F), f);
    }
}

/** A field needing no quoting is written verbatim; anything else is quoted with
 *  doubled quotes, which is the whole of RFC 4180 that matters here. */
static void
csv_str(FILE* f, int* first, const char* s)
{
    csv_sep(f, first);
    assert(s);

    int needs_quote = 0;
    for( const char* p = s; *p; p++ )
    {
        if( *p == ',' || *p == '"' || *p == '\n' || *p == '\r' )
        {
            needs_quote = 1;
            break;
        }
    }

    if( needs_quote )
        fputc('"', f);
    for( const char* p = s; *p; p++ )
    {
        if( *p == '\r' )
            continue;
        if( *p == '"' )
            fputc('"', f);
        csv_putc(f, (unsigned char)*p);
    }
    if( needs_quote )
        fputc('"', f);
}

static void
csv_int(FILE* f, int* first, int v)
{
    csv_sep(f, first);
    fprintf(f, "%d", v);
}

static void
csv_bool(FILE* f, int* first, int v)
{
    csv_str(f, first, v ? "true" : "false");
}

/** An int list as a space-separated field. Empty list writes an empty field. */
static void
csv_ints(FILE* f, int* first, const int* v, int count)
{
    csv_sep(f, first);
    assert(v);
    for( int i = 0; i < count; i++ )
        fprintf(f, i ? " %d" : "%d", v[i]);
}

/** Head icons as `archive:sprite archive:sprite` — the sprite index is a short
 *  while the archive id is an int, so this cannot go through csv_pairs. */
static void
csv_head_icons(FILE* f, int* first, const int* archives, const short* sprites, int count)
{
    csv_sep(f, first);
    assert(archives);
    assert(sprites);
    for( int i = 0; i < count; i++ )
        fprintf(f, i ? " %d:%d" : "%d:%d", archives[i], (int)sprites[i]);
}

/** Recolour/retexture pairs as `from>to from>to`. */
static void
csv_pairs(FILE* f, int* first, const int* from, const int* to, int count)
{
    csv_sep(f, first);
    assert(from);
    assert(to);
    for( int i = 0; i < count; i++ )
        fprintf(f, i ? " %d>%d" : "%d>%d", from[i], to[i]);
}

/*
 * Params (opcode 249) as `key=value;key=value`.
 *
 * This is the only place a cache of this era keeps open-ended per-record data,
 * so it is where anything the struct has no field for ends up. It is written as
 * one column rather than exploded into per-key columns because the key set is
 * not known ahead of the walk and differs between npc and obj.
 */
static void
csv_params(FILE* f, int* first, const struct RSCache_Params* params)
{
    csv_sep(f, first);
    if( params->count <= 0 )
        return;

    /* Built into a scratch buffer so the whole field can go through csv_str's
     * quoting — a string param may contain a comma. */
    size_t cap = 64;
    for( int i = 0; i < params->count; i++ )
    {
        cap += 32;
        if( params->kinds[i] == RSCACHE_PARAM_STRING && params->values[i] )
            cap += strlen((const char*)params->values[i]);
    }

    char* buf = malloc(cap);
    if( !buf )
        return;
    size_t len = 0;
    for( int i = 0; i < params->count; i++ )
    {
        if( i )
            buf[len++] = ';';
        len += (size_t)snprintf(buf + len, cap - len, "%d=", params->keys[i]);
        if( params->kinds[i] == RSCACHE_PARAM_STRING )
            len += (size_t)snprintf(
                buf + len,
                cap - len,
                "%s",
                params->values[i] ? (const char*)params->values[i] : "");
        else if( params->kinds[i] == RSCACHE_PARAM_LONG )
            len += (size_t)snprintf(
                buf + len, cap - len, "%lld", (long long)*(int64_t*)params->values[i]);
        else
            len += (size_t)snprintf(buf + len, cap - len, "%d", *(int*)params->values[i]);
    }
    buf[len] = '\0';

    int inner_first = 1;
    /* csv_sep already wrote the separator, so suppress the second one. */
    csv_str(f, &inner_first, buf);
    free(buf);
}

/* ---- profile / cache open ---------------------------------------------- */

static void
usage(const char* argv0)
{
    fprintf(
        stderr,
        "Usage:\n"
        "  %s --rev NAME <cache_dir> [--npc-csv PATH] [--obj-csv PATH]\n"
        "  %s --game G --epoch E --revision N [--quirks LIST] <cache_dir> [...]\n"
        "\n"
        "Options:\n"
        "  --npc-csv PATH   where to write npc rows (default npc_stats.csv)\n"
        "  --obj-csv PATH   where to write obj rows (default obj_stats.csv)\n"
        "  --npc-only       skip objs\n"
        "  --obj-only       skip npcs\n"
        "  --raw-dir DIR    also write DIR/{npc,obj}.bin + .idx, the undecoded\n"
        "                   record bytes and an (id offset size) index\n",
        argv0,
        argv0);
}

static int
resolve_profile(
    const char* rev_name,
    const char* game_name,
    const char* epoch_name,
    const char* revision_text,
    const char* quirks_list,
    struct RSCache* out)
{
    if( rev_name )
    {
        if( game_name || epoch_name || revision_text )
        {
            fprintf(stderr, "Use either --rev or --game/--epoch/--revision, not both\n");
            return 0;
        }
        if( !RSCache_ProfileByName(rev_name, out) )
        {
            fprintf(stderr, "Unknown revision profile: %s\n", rev_name);
            return 0;
        }
        return 1;
    }

    if( !game_name || !epoch_name || !revision_text )
    {
        fprintf(stderr, "Cache identity required: --rev NAME or --game/--epoch/--revision\n");
        return 0;
    }

    int game = RSCache_GameFromName(game_name);
    int epoch = RSCache_EpochFromName(epoch_name);
    if( game == RSCACHE_GAME_UNSET )
    {
        fprintf(stderr, "Unknown game: %s (expected oldschool or rs2)\n", game_name);
        return 0;
    }
    if( epoch == RSCACHE_EPOCH_UNSET )
    {
        fprintf(stderr, "Unknown epoch: %s (expected dat1 or dat2)\n", epoch_name);
        return 0;
    }

    uint32_t quirks = RSCACHE_QUIRK_NONE;
    if( quirks_list && !RSCache_QuirksFromList(quirks_list, &quirks) )
    {
        fprintf(stderr, "Unknown quirks: %s\n", quirks_list);
        return 0;
    }

    *out = RSCache_ProfileForIdentity(game, epoch, atoi(revision_text), quirks);
    if( !RSCache_ProfileIsIdentified(out) )
    {
        fprintf(stderr, "Resolved profile identity is unset\n");
        return 0;
    }
    return 1;
}

/* ---- archive walk ------------------------------------------------------- */

/*
 * Optional raw-record sink.
 *
 * Deriving an opcode table for a revision the shared codecs do not cover needs
 * the bytes, not the decode: a blob of every record plus an index of
 * (id, offset, size) is enough to run the analysis outside this tool, and it is
 * written straight from the same walk so the ids line up with the CSV rows.
 */
struct RawSink
{
    FILE* blob;
    FILE* index;
    long offset;
};

static int
raw_open(struct RawSink* raw, const char* dir, const char* what)
{
    assert(dir);

    char path[1024];
    snprintf(path, sizeof(path), "%s/%s.bin", dir, what);
    raw->blob = fopen(path, "wb");
    snprintf(path, sizeof(path), "%s/%s.idx", dir, what);
    raw->index = fopen(path, "wb");
    if( !raw->blob || !raw->index )
    {
        fprintf(stderr, "Cannot write raw %s records under %s\n", what, dir);
        if( raw->blob )
            fclose(raw->blob);
        if( raw->index )
            fclose(raw->index);
        raw->blob = NULL;
        raw->index = NULL;
        return 0;
    }
    raw->offset = 0;
    return 1;
}

static void
raw_close(struct RawSink* raw)
{
    if( raw->blob )
        fclose(raw->blob);
    if( raw->index )
        fclose(raw->index);
    raw->blob = NULL;
    raw->index = NULL;
}

static void
raw_write(struct RawSink* raw, int id, const char* data, int size)
{
    assert(raw);
    if( !raw->blob )
        return;
    fwrite(data, 1, (size_t)size, raw->blob);
    fprintf(raw->index, "%d %ld %d\n", id, raw->offset, size);
    raw->offset += size;
}

struct WalkStats
{
    int records;
    int exact;
    /* Histogram over the opcode each short record stopped on. 256 is "ran off
     * the end of the buffer without a terminator". */
    int stop_counts[257];
};

static int
file_global_id(
    const struct RSCache_RecordAddress* addr,
    int archive_id,
    int file_id)
{
    if( addr->group_shift == 0 )
        return file_id;
    return (archive_id << addr->group_shift) | (file_id & addr->file_mask);
}

/**
 * Every group id holding records of `type`, in ascending order.
 *
 * Returns a malloc'd array; the caller frees. `*out_table` is the resolved disk
 * table id to load those groups from.
 */
static int
group_ids_for(
    struct RSCache_Dat2Disk* disk,
    const struct RSCache* profile,
    enum RSCache_Type type,
    int config_kind,
    int* out_table,
    int** out_groups,
    int* out_count)
{
    struct RSCache_RecordAddress addr = RSCache_RecordAddressFor(profile, type);

    if( addr.group_shift == 0 )
    {
        int* groups = malloc(sizeof(int));
        if( !groups )
            return 0;
        groups[0] = addr.group >= 0 ? addr.group : config_kind;
        *out_table = RSCache_Dat2DiskTableId(disk, RSCACHE_DAT2_TABLE_CONFIGS);
        *out_groups = groups;
        *out_count = 1;
        return 1;
    }

    int table_id = RSCache_Dat2DiskTableId(disk, addr.table);
    if( table_id == RSCACHE_DAT2_DISK_TABLE_ABSENT )
    {
        fprintf(stderr, "No disk table for logical table %d\n", (int)addr.table);
        return 0;
    }

    /*
     * Read the reference table directly rather than off `disk->tables[]`.
     *
     * The disk loads reference tables lazily, on the first archive load that
     * needs one, so the slot is still NULL here — and the group ids are exactly
     * what is needed to make that first archive load. Decoding a private copy
     * breaks the circle; the disk still loads and caches its own when the walk
     * below starts pulling archives.
     */
    struct RSCache_Dat2DiskArchive* table_archive =
        RSCache_Dat2DiskArchiveNewReferenceTableLoad(disk, table_id);
    if( !table_archive )
    {
        fprintf(stderr, "Failed to load reference table %d\n", table_id);
        return 0;
    }

    struct RSCache_ReferenceTable* rt =
        RSCache_ReferenceTableNewDecode(table_archive->data, table_archive->data_size);
    RSCache_Dat2DiskArchiveFree(table_archive);
    if( !rt || rt->id_count <= 0 )
    {
        fprintf(stderr, "Reference table %d decoded empty\n", table_id);
        if( rt )
            RSCache_ReferenceTableFree(rt);
        return 0;
    }

    int* groups = malloc((size_t)rt->id_count * sizeof(int));
    if( !groups )
    {
        RSCache_ReferenceTableFree(rt);
        return 0;
    }
    for( int i = 0; i < rt->id_count; i++ )
        groups[i] = rt->ids[i];
    int count = rt->id_count;
    RSCache_ReferenceTableFree(rt);

    *out_table = table_id;
    *out_groups = groups;
    *out_count = count;
    return 1;
}

/* ---- npc ---------------------------------------------------------------- */

static const char* const NPC_HEADER =
    "id,name,combat_level,size,footprint_size,height,category,"
    "is_interactable,is_minimap_visible,is_pet,has_render_priority,render_priority,"
    "rotation_flag,low_priority_follower_ops,rotation_speed,"
    "width_scale,height_scale,ambient,contrast,"
    "stat_74,stat_75,stat_76,stat_77,stat_78,stat_79,"
    "varbit_id,varp_index,configs,"
    "op1,op2,op3,op4,op5,"
    "models,chathead_models,recolors,retextures,"
    "bas_type_id,anim_stand,anim_walk,anim_run,"
    "anim_idle_rot_left,anim_idle_rot_right,anim_rot180,anim_rot_left,anim_rot_right,"
    "anim_run_rot180,anim_run_rot_left,anim_run_rot_right,"
    "anim_crawl,anim_crawl_rot180,anim_crawl_rot_left,anim_crawl_rot_right,"
    "sound_idle,sound_crawl,sound_walk,sound_run,sound_radius,ambient_sound_volume,"
    "head_icons,params,record_bytes,decoded_bytes,stop_opcode\n";

static void
npc_row(
    FILE* f,
    int id,
    const struct RSCache_Dat2ConfigNpc* n,
    int record_bytes,
    int stop_opcode)
{
    int c = 1;

    csv_int(f, &c, id);
    csv_str(f, &c, n->name);
    csv_int(f, &c, n->combat_level);
    csv_int(f, &c, n->size);
    csv_int(f, &c, n->footprint_size);
    csv_int(f, &c, n->height);
    csv_int(f, &c, n->category);

    csv_bool(f, &c, n->is_interactable);
    csv_bool(f, &c, n->is_minimap_visible);
    csv_bool(f, &c, n->is_pet);
    csv_bool(f, &c, n->has_render_priority);
    csv_int(f, &c, n->render_priority);
    csv_bool(f, &c, n->rotation_flag);
    csv_bool(f, &c, n->low_priority_follower_ops);
    csv_int(f, &c, n->rotation_speed);

    csv_int(f, &c, n->width_scale);
    csv_int(f, &c, n->height_scale);
    csv_int(f, &c, n->ambient);
    csv_int(f, &c, n->contrast);

    for( int i = 0; i < 6; i++ )
        csv_int(f, &c, n->stats[i]);

    csv_int(f, &c, n->varbit_id);
    csv_int(f, &c, n->varp_index);
    csv_ints(f, &c, n->configs, n->configs_count);

    for( int i = 0; i < 5; i++ )
        csv_str(f, &c, n->actions[i]);

    csv_ints(f, &c, n->models, n->models_count);
    csv_ints(f, &c, n->chathead_models, n->chathead_models_count);
    csv_pairs(f, &c, n->recolor_to_find, n->recolor_to_replace, n->recolor_count);
    csv_pairs(f, &c, n->retexture_to_find, n->retexture_to_replace, n->retexture_count);

    csv_int(f, &c, n->bas_type_id);
    csv_int(f, &c, n->standing_animation);
    csv_int(f, &c, n->walking_animation);
    csv_int(f, &c, n->run_animation);
    csv_int(f, &c, n->idle_rotate_left_animation);
    csv_int(f, &c, n->idle_rotate_right_animation);
    csv_int(f, &c, n->rotate180_animation);
    csv_int(f, &c, n->rotate_left_animation);
    csv_int(f, &c, n->rotate_right_animation);
    csv_int(f, &c, n->run_rotate180_animation);
    csv_int(f, &c, n->run_rotate_left_animation);
    csv_int(f, &c, n->run_rotate_right_animation);
    csv_int(f, &c, n->crawl_animation);
    csv_int(f, &c, n->crawl_rotate180_animation);
    csv_int(f, &c, n->crawl_rotate_left_animation);
    csv_int(f, &c, n->crawl_rotate_right_animation);

    csv_int(f, &c, n->sound_idle);
    csv_int(f, &c, n->sound_crawl);
    csv_int(f, &c, n->sound_walk);
    csv_int(f, &c, n->sound_run);
    csv_int(f, &c, n->sound_radius);
    csv_int(f, &c, n->ambient_sound_volume);

    csv_head_icons(
        f, &c, n->head_icon_archive_ids, n->head_icon_sprite_index, n->head_icon_count);
    csv_params(f, &c, &n->params);
    csv_int(f, &c, record_bytes);
    csv_int(f, &c, n->_consumed);
    csv_int(f, &c, stop_opcode);
    fputc('\n', f);
}

/**
 * Decode one npc record, reporting the opcode that ended it.
 *
 * This repeats the library's decode loop rather than calling
 * RSCache_Dat2ConfigNpcNewDecodeProfile because the library keeps only *how
 * far* it got (`_consumed`), not *what stopped it* — and the opcode is the
 * thing that tells a reader of the CSV whether a short row matters.
 */
static void
npc_decode_tracked(
    const struct RSCache* profile,
    char* data,
    int size,
    struct RSCache_Dat2ConfigNpc* out,
    int* out_stop_opcode)
{
    int flags = RSCache_Dat2ConfigNpcFlags(profile);
    struct RSCache_Buffer buffer;

    /* The library's own entry point callocs before Init, so Init does not zero
     * every field. A stack record must be zeroed first or the row writer reads
     * a stale pointer/count pair. */
    memset(out, 0, sizeof(*out));
    RSCache_Dat2ConfigNpcInit(out);
    RSCache_BufferInit(&buffer, (uint8_t*)data, (uint32_t)size);
    *out_stop_opcode = 256;

    while( 1 )
    {
        if( buffer.position >= buffer.size )
            break;

        int opcode = g1(&buffer);
        if( opcode == 0 )
        {
            out->_consumed = (int)buffer.position;
            *out_stop_opcode = -1;
            break;
        }
        if( !RSCache_Dat2ConfigNpcDecodeOp(out, opcode, &buffer, (unsigned)flags) )
        {
            *out_stop_opcode = opcode;
            break;
        }
    }

    RSCache_Dat2ConfigNpcFinish(out, (unsigned)flags);
}

static int
walk_npcs(
    struct RSCache_Dat2Disk* disk,
    const struct RSCache* profile,
    FILE* out,
    struct RawSink* raw,
    struct WalkStats* stats)
{
    struct RSCache_RecordAddress addr = RSCache_RecordAddressFor(profile, RSCACHE_TYPE_NPC);
    int table = 0;
    int* groups = NULL;
    int group_count = 0;

    if( !group_ids_for(
            disk,
            profile,
            RSCACHE_TYPE_NPC,
            RSCACHE_DAT2_CONFIG_KIND_NPC,
            &table,
            &groups,
            &group_count) )
        return 0;

    fputs(NPC_HEADER, out);

    for( int g = 0; g < group_count; g++ )
    {
        struct RSCache_Dat2DiskArchive* archive =
            RSCache_Dat2DiskArchiveNewLoad(disk, table, groups[g]);
        if( !archive )
            continue;
        if( !RSCache_Dat2DiskArchiveInitMetadata(disk, archive) || archive->file_count <= 0 )
        {
            RSCache_Dat2DiskArchiveFree(archive);
            continue;
        }

        /* The group's own revision selects the codec variant for records inside
         * it — a cache can hold groups written at different times. */
        struct RSCache local = *profile;
        RSCache_ProfileSetGroupRevision(&local, RSCACHE_TYPE_NPC, archive->revision);

        struct RSCache_FileList* files =
            RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
        if( !files )
        {
            RSCache_Dat2DiskArchiveFree(archive);
            continue;
        }

        for( int i = 0; i < files->file_count; i++ )
        {
            if( files->file_sizes[i] <= 0 )
                continue;

            int file_id = archive->file_ids ? archive->file_ids[i] : i;
            int id = file_global_id(&addr, archive->archive_id, file_id);

            struct RSCache_Dat2ConfigNpc npc;
            int stop_opcode = 256;
            npc_decode_tracked(&local, files->files[i], files->file_sizes[i], &npc, &stop_opcode);

            npc_row(out, id, &npc, files->file_sizes[i], stop_opcode);
            raw_write(raw, id, files->files[i], files->file_sizes[i]);

            stats->records++;
            if( stop_opcode == -1 && npc._consumed == files->file_sizes[i] )
                stats->exact++;
            else
                stats->stop_counts[stop_opcode < 0 ? 256 : stop_opcode]++;

            RSCache_Dat2ConfigNpcFreeInplace(&npc);
        }

        RSCache_FileListFree(files);
        RSCache_Dat2DiskArchiveFree(archive);
    }

    free(groups);
    return 1;
}

/* ---- obj ---------------------------------------------------------------- */

static const char* const OBJ_HEADER =
    "id,name,examine,cost,is_members,tradeable,ge_tradeable,stacking_behaviour,"
    "team,weight,category,shift_click_drop_index,"
    "noted_id,noted_template,placeholder_id,placeholder_template,"
    "bought_id,bought_template,"
    "inv_model,wearpos_1,wearpos_2,wearpos_3,"
    "male_model_0,male_model_1,male_model_2,male_head_model,male_head_model_2,male_offset,"
    "female_model_0,female_model_1,female_model_2,female_head_model,female_head_model_2,"
    "female_offset,"
    "zoom2d,xan2d,yan2d,zan2d,offset_x2d,offset_y2d,resize_x,resize_y,resize_z,"
    "ambient,contrast,recolors,retextures,"
    "ground_op1,ground_op2,ground_op3,ground_op4,ground_op5,"
    "inv_op1,inv_op2,inv_op3,inv_op4,inv_op5,"
    "stack_ids,stack_counts,params,record_bytes,decoded_bytes,stop_opcode\n";

static void
obj_row(
    FILE* f,
    int id,
    const struct RSCache_Dat2ConfigObj* o,
    int record_bytes,
    int decoded_bytes,
    int stop_opcode)
{
    int c = 1;

    csv_int(f, &c, id);
    csv_str(f, &c, o->name);
    csv_str(f, &c, o->examine);
    csv_int(f, &c, o->cost);
    csv_bool(f, &c, o->is_members);
    csv_bool(f, &c, o->tradeable);
    csv_bool(f, &c, o->ge_tradeable);
    csv_int(f, &c, o->stacking_behaviour);
    csv_int(f, &c, o->team);
    csv_int(f, &c, o->weight);
    csv_int(f, &c, o->category);
    csv_int(f, &c, o->shift_click_drop_index);

    csv_int(f, &c, o->noted_id);
    csv_int(f, &c, o->noted_template);
    csv_int(f, &c, o->placeholder_id);
    csv_int(f, &c, o->placeholder_template_id);
    csv_int(f, &c, o->bought_id);
    csv_int(f, &c, o->bought_template_id);

    csv_int(f, &c, o->inventory_model_id);
    csv_int(f, &c, o->wearpos_1);
    csv_int(f, &c, o->wearpos_2);
    csv_int(f, &c, o->wearpos_3);

    csv_int(f, &c, o->male_model_0);
    csv_int(f, &c, o->male_model_1);
    csv_int(f, &c, o->male_model_2);
    csv_int(f, &c, o->male_head_model);
    csv_int(f, &c, o->male_head_model_2);
    csv_int(f, &c, o->male_offset);

    csv_int(f, &c, o->female_model_0);
    csv_int(f, &c, o->female_model_1);
    csv_int(f, &c, o->female_model_2);
    csv_int(f, &c, o->female_head_model);
    csv_int(f, &c, o->female_head_model_2);
    csv_int(f, &c, o->female_offset);

    csv_int(f, &c, o->zoom2d);
    csv_int(f, &c, o->xan2d);
    csv_int(f, &c, o->yan2d);
    csv_int(f, &c, o->zan2d);
    csv_int(f, &c, o->offset_x2d);
    csv_int(f, &c, o->offset_y2d);
    csv_int(f, &c, o->resize_x);
    csv_int(f, &c, o->resize_y);
    csv_int(f, &c, o->resize_z);
    csv_int(f, &c, o->ambient);
    csv_int(f, &c, o->contrast);

    csv_pairs(f, &c, o->recolors_from, o->recolors_to, o->recolor_count);
    csv_pairs(f, &c, o->retextures_from, o->retextures_to, o->retexture_count);

    for( int i = 0; i < 5; i++ )
        csv_str(f, &c, o->actions[i]);
    for( int i = 0; i < 5; i++ )
        csv_str(f, &c, o->if_actions[i]);

    csv_ints(f, &c, o->count_obj, 10);
    csv_ints(f, &c, o->count_co, 10);

    csv_params(f, &c, &o->params);
    csv_int(f, &c, record_bytes);
    csv_int(f, &c, decoded_bytes);
    csv_int(f, &c, stop_opcode);
    fputc('\n', f);
}

/** The obj twin of npc_decode_tracked. The obj struct has no `_consumed`, so
 *  consumption is reported out-of-band here. */
static void
obj_decode_tracked(
    const struct RSCache* profile,
    char* data,
    int size,
    struct RSCache_Dat2ConfigObj* out,
    int* out_consumed,
    int* out_stop_opcode)
{
    int flags = RSCache_Dat2ConfigObjFlags(profile);
    struct RSCache_Buffer buffer;

    memset(out, 0, sizeof(*out));
    RSCache_Dat2ConfigObjInit(out);
    RSCache_BufferInit(&buffer, (uint8_t*)data, (uint32_t)size);
    *out_stop_opcode = 256;
    *out_consumed = 0;

    while( 1 )
    {
        if( buffer.position >= buffer.size )
        {
            *out_consumed = (int)buffer.position;
            break;
        }

        int opcode = g1(&buffer);
        if( opcode == 0 )
        {
            *out_consumed = (int)buffer.position;
            *out_stop_opcode = -1;
            break;
        }
        if( !RSCache_Dat2ConfigObjDecodeOp(out, opcode, &buffer, (unsigned)flags) )
        {
            /* Back up over the opcode byte: nothing after it was consumed. */
            *out_consumed = (int)buffer.position - 1;
            *out_stop_opcode = opcode;
            break;
        }
    }
}

static int
walk_objs(
    struct RSCache_Dat2Disk* disk,
    const struct RSCache* profile,
    FILE* out,
    struct RawSink* raw,
    struct WalkStats* stats)
{
    struct RSCache_RecordAddress addr = RSCache_RecordAddressFor(profile, RSCACHE_TYPE_OBJ);
    int table = 0;
    int* groups = NULL;
    int group_count = 0;

    if( !group_ids_for(
            disk,
            profile,
            RSCACHE_TYPE_OBJ,
            RSCACHE_DAT2_CONFIG_KIND_OBJECT,
            &table,
            &groups,
            &group_count) )
        return 0;

    fputs(OBJ_HEADER, out);

    for( int g = 0; g < group_count; g++ )
    {
        struct RSCache_Dat2DiskArchive* archive =
            RSCache_Dat2DiskArchiveNewLoad(disk, table, groups[g]);
        if( !archive )
            continue;
        if( !RSCache_Dat2DiskArchiveInitMetadata(disk, archive) || archive->file_count <= 0 )
        {
            RSCache_Dat2DiskArchiveFree(archive);
            continue;
        }

        struct RSCache local = *profile;
        RSCache_ProfileSetGroupRevision(&local, RSCACHE_TYPE_OBJ, archive->revision);

        struct RSCache_FileList* files =
            RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
        if( !files )
        {
            RSCache_Dat2DiskArchiveFree(archive);
            continue;
        }

        for( int i = 0; i < files->file_count; i++ )
        {
            if( files->file_sizes[i] <= 0 )
                continue;

            int file_id = archive->file_ids ? archive->file_ids[i] : i;
            int id = file_global_id(&addr, archive->archive_id, file_id);

            struct RSCache_Dat2ConfigObj obj;
            int consumed = 0;
            int stop_opcode = 256;
            obj_decode_tracked(
                &local, files->files[i], files->file_sizes[i], &obj, &consumed, &stop_opcode);

            obj_row(out, id, &obj, files->file_sizes[i], consumed, stop_opcode);
            raw_write(raw, id, files->files[i], files->file_sizes[i]);

            stats->records++;
            if( stop_opcode == -1 && consumed == files->file_sizes[i] )
                stats->exact++;
            else
                stats->stop_counts[stop_opcode < 0 ? 256 : stop_opcode]++;

            RSCache_Dat2ConfigObjFreeInplace(&obj);
        }

        RSCache_FileListFree(files);
        RSCache_Dat2DiskArchiveFree(archive);
    }

    free(groups);
    return 1;
}

/* ---- main --------------------------------------------------------------- */

static void
report(const char* what, const struct WalkStats* s, const char* path)
{
    fprintf(
        stderr,
        "%s: %d records -> %s (%d decoded to the terminator, %d short)\n",
        what,
        s->records,
        path,
        s->exact,
        s->records - s->exact);

    for( int op = 0; op <= 256; op++ )
    {
        if( !s->stop_counts[op] )
            continue;
        if( op == 256 )
            fprintf(stderr, "  %6d stopped at end-of-buffer (no opcode 0)\n", s->stop_counts[op]);
        else
            fprintf(stderr, "  %6d stopped at unimplemented opcode %d\n", s->stop_counts[op], op);
    }
}

int
main(int argc, char** argv)
{
    const char* rev_name = NULL;
    const char* game_name = NULL;
    const char* epoch_name = NULL;
    const char* revision_text = NULL;
    const char* quirks_list = NULL;
    const char* cache_dir = NULL;
    const char* npc_path = "npc_stats.csv";
    const char* obj_path = "obj_stats.csv";
    const char* raw_dir = NULL;
    int do_npc = 1;
    int do_obj = 1;

    for( int i = 1; i < argc; i++ )
    {
        const char* a = argv[i];
        const char** slot = NULL;

        if( strcmp(a, "--rev") == 0 )
            slot = &rev_name;
        else if( strcmp(a, "--game") == 0 )
            slot = &game_name;
        else if( strcmp(a, "--epoch") == 0 )
            slot = &epoch_name;
        else if( strcmp(a, "--revision") == 0 )
            slot = &revision_text;
        else if( strcmp(a, "--quirks") == 0 )
            slot = &quirks_list;
        else if( strcmp(a, "--npc-csv") == 0 )
            slot = &npc_path;
        else if( strcmp(a, "--obj-csv") == 0 )
            slot = &obj_path;
        else if( strcmp(a, "--raw-dir") == 0 )
            slot = &raw_dir;
        else if( strcmp(a, "--npc-only") == 0 )
        {
            do_obj = 0;
            continue;
        }
        else if( strcmp(a, "--obj-only") == 0 )
        {
            do_npc = 0;
            continue;
        }
        else if( strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0 )
        {
            usage(argv[0]);
            return 0;
        }
        else if( a[0] == '-' )
        {
            fprintf(stderr, "Unknown option: %s\n", a);
            usage(argv[0]);
            return 1;
        }
        else
        {
            if( cache_dir )
            {
                fprintf(stderr, "Only one cache directory may be given\n");
                return 1;
            }
            cache_dir = a;
            continue;
        }

        if( i + 1 >= argc )
        {
            usage(argv[0]);
            return 1;
        }
        *slot = argv[++i];
    }

    if( !cache_dir )
    {
        usage(argv[0]);
        return 1;
    }

    struct RSCache profile;
    if( !resolve_profile(
            rev_name, game_name, epoch_name, revision_text, quirks_list, &profile) )
        return 1;

    struct RSCache_Dat2Disk* disk = RSCache_Dat2DiskNewFromDirectory(cache_dir);
    if( !disk )
    {
        fprintf(stderr, "Failed to open dat2 cache at %s\n", cache_dir);
        return 1;
    }

    /* Without this the disk has no game, and RSCache_Dat2DiskTableId cannot map
     * the RS2 per-type tables (npc 18, obj 19) onto disk indices. */
    RSCache_Dat2DiskSetProfile(disk, &profile);

    int rc = 0;

    if( do_npc )
    {
        FILE* f = fopen(npc_path, "wb");
        if( !f )
        {
            fprintf(stderr, "Cannot write %s\n", npc_path);
            rc = 1;
        }
        else
        {
            struct RawSink raw = { 0 };
            if( !raw_open(&raw, raw_dir, "npc") )
                rc = 1;

            struct WalkStats s = { 0 };
            if( !walk_npcs(disk, &profile, f, raw.blob ? &raw : NULL, &s) )
            {
                fprintf(stderr, "NPC walk failed\n");
                rc = 1;
            }
            fclose(f);
            raw_close(&raw);
            report("npc", &s, npc_path);
        }
    }

    if( do_obj )
    {
        FILE* f = fopen(obj_path, "wb");
        if( !f )
        {
            fprintf(stderr, "Cannot write %s\n", obj_path);
            rc = 1;
        }
        else
        {
            struct RawSink raw = { 0 };
            if( !raw_open(&raw, raw_dir, "obj") )
                rc = 1;

            struct WalkStats s = { 0 };
            if( !walk_objs(disk, &profile, f, raw.blob ? &raw : NULL, &s) )
            {
                fprintf(stderr, "OBJ walk failed\n");
                rc = 1;
            }
            fclose(f);
            raw_close(&raw);
            report("obj", &s, obj_path);
        }
    }

    RSCache_Dat2DiskFree(disk);
    return rc;
}
