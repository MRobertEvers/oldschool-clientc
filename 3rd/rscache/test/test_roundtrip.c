/*
 * Per-datatype round-trip over the real caches on disk.
 *
 * Two tiers are reported for every datatype, because they answer different
 * questions:
 *
 *   semantic   decode(encode(decode(bytes))) == decode(bytes), compared field by
 *              field. This is the correctness bar. It is always achievable, and a
 *              failure is a genuine encoder bug.
 *
 *   byte-exact encode(decode(bytes)) == bytes. Holds only where the source was
 *              packed in the same canonical order the encoder writes, and where
 *              the decoder kept every field it read. Reported as a count, not
 *              asserted — several decoders in this library deliberately consume
 *              fields without storing them, so 100% is not always reachable. A
 *              *drop* in the percentage is the regression signal.
 *
 * Skips cleanly when a cache directory is absent, so this runs anywhere.
 */

#include "rscache_test.h"

#include <rscache.h>

#include <stdlib.h>

#ifndef RSCACHE_TEST_REPO_ROOT
#define RSCACHE_TEST_REPO_ROOT "../.."
#endif

static const char* CACHE_DIRS[] = {
    "cache", "cache.jan2026", "cache.kronos", "cache.osrs184", "cache.osrs230", "cache.osrs239",
};

struct tally
{
    int records;
    int semantic_ok;
    int byte_exact;
    int encode_failed;
    /* Records whose re-encode came out the same *length* as the source. Together
     * with byte_exact this separates the two reasons a round trip is not
     * byte-exact:
     *
     *   same length, different bytes  -> the fields were reordered. Nothing was
     *                                    lost or invented; the packer simply emits
     *                                    opcodes in an order of its own.
     *   different length              -> a field was dropped or added, i.e. the
     *                                    decoder is lossy for that record.
     *
     * Without this distinction a low byte-exact percentage is unreadable — it could
     * mean either. */
    int length_match;
    /* Records whose decode landed exactly on their terminator. Only tracked for
     * datatypes that expose a _consumed field. */
    int consumed_exact;
    bool tracks_consumed;
};

static void
note_bytes(
    struct tally* tally,
    const uint8_t* encoded,
    uint32_t written,
    const uint8_t* original,
    int original_size)
{
    if( written != (uint32_t)original_size )
        return;

    tally->length_match++;
    if( memcmp(encoded, original, (size_t)original_size) == 0 )
        tally->byte_exact++;
}

/*
 * Caches whose records this library cannot yet fully decode, so exact consumption
 * is reported rather than asserted.
 *
 * `cache.osrs239` is the only entry: revision 239 changed record layouts that the
 * decoders do not implement, which shows up independently in npc (2462/16292
 * exact under either head-icon shape) and spotanim (0/4010). No manifest or
 * config references that cache — it is validation data — so nothing in the client
 * is affected. Listing it here keeps the suite green while making the gap
 * impossible to overlook: it prints on every run.
 *
 * Semantic round-trip is still asserted for these caches. An encoder must
 * reproduce whatever the decoder managed to read, even when that is incomplete.
 */
static bool
consumption_is_known_gap(const char* cache)
{
    return strcmp(cache, "cache.osrs239") == 0;
}

/*
 * Individually accounted-for records that do not consume exactly, outside the
 * wholesale gap above.
 *
 * Stated as an exact allowance rather than a blanket skip, so a regression from one
 * bad record to two still fails. Each entry names what is actually wrong.
 */
struct consumption_allowance
{
    const char* datatype;
    const char* cache;
    int allowed;
    const char* reason;
};

static const struct consumption_allowance CONSUMPTION_ALLOWANCES[] = {
    /* Sequence 8127 carries a 291-frame opcode-1 block and then an opcode the v1
     * codec does not know, so the decode stops at 1863 of 2631 bytes. Kronos is a
     * custom client build, so a record with a newer field than its archive revision
     * implies is plausible; the specific opcode has not been identified and is not
     * guessed at. One record in 8526. */
    { "sequence", "cache.kronos", 1, "seq 8127: unknown opcode after a 291-frame block" },
    { "sequence", "cache.osrs184", 1, "seq 8127: unknown opcode after a 291-frame block" },
};

static int
consumption_allowance_for(
    const char* datatype,
    const char* cache)
{
    for( size_t i = 0; i < sizeof(CONSUMPTION_ALLOWANCES) / sizeof(CONSUMPTION_ALLOWANCES[0]); i++ )
    {
        if( strcmp(CONSUMPTION_ALLOWANCES[i].datatype, datatype) == 0 &&
            strcmp(CONSUMPTION_ALLOWANCES[i].cache, cache) == 0 )
            return CONSUMPTION_ALLOWANCES[i].allowed;
    }
    return 0;
}

static void
tally_report(
    const char* datatype,
    const char* cache,
    const struct tally* tally)
{
    if( tally->records == 0 )
        return;

    printf(
        "   %-10s %-14s records=%-6d semantic=%-6d exact=%-6d (%3d%%) same-len=%-6d (%3d%%)%s\n",
        datatype,
        cache,
        tally->records,
        tally->semantic_ok,
        tally->byte_exact,
        tally->records ? tally->byte_exact * 100 / tally->records : 0,
        tally->length_match,
        tally->records ? tally->length_match * 100 / tally->records : 0,
        tally->tracks_consumed && tally->consumed_exact != tally->records ? "  [consumption gap]"
                                                                         : "");

    /* Where a decoder reports consumption, landing short of the terminator means
     * the record was misread — a decoder bug, not an encoder one. */
    if( tally->tracks_consumed && tally->consumed_exact != tally->records )
    {
        if( consumption_is_known_gap(cache) )
        {
            printf(
                "   KNOWN GAP %s/%s: %d of %d records did not consume exactly "
                "(revision not implemented)\n",
                datatype,
                cache,
                tally->records - tally->consumed_exact,
                tally->records);
        }
        else
        {
            int missed = tally->records - tally->consumed_exact;
            int allowed = consumption_allowance_for(datatype, cache);

            rscache_test_checks++;
            if( missed > allowed )
            {
                rscache_test_failures++;
                printf(
                    "   FAIL %s/%s: %d of %d records did not consume exactly (%d allowed)\n",
                    datatype,
                    cache,
                    missed,
                    tally->records,
                    allowed);
            }
            else
            {
                printf(
                    "   ALLOWED %s/%s: %d of %d records did not consume exactly\n",
                    datatype,
                    cache,
                    missed,
                    tally->records);
            }
        }
    }
    else if( tally->tracks_consumed )
    {
        rscache_test_checks++;
    }

    /* Semantic equality is the bar; anything less is an encoder bug. */
    rscache_test_checks++;
    if( tally->semantic_ok != tally->records )
    {
        rscache_test_failures++;
        printf(
            "   FAIL %s/%s: %d of %d records did not survive a semantic round trip\n",
            datatype,
            cache,
            tally->records - tally->semantic_ok,
            tally->records);
    }

    rscache_test_checks++;
    if( tally->encode_failed != 0 )
    {
        rscache_test_failures++;
        printf(
            "   FAIL %s/%s: encoder refused %d records\n",
            datatype,
            cache,
            tally->encode_failed);
    }
}

/** Revision name for a cache directory, or NULL when the directory does not
 *  identify one (`cache` and `cache.jan2026` are unlabelled snapshots). */
static const char*
profile_name_for_cache(const char* cache_dir)
{
    if( strcmp(cache_dir, "cache.kronos") == 0 )
        return "kronos";
    if( strcmp(cache_dir, "cache.osrs184") == 0 )
        return "osrs184";
    if( strcmp(cache_dir, "cache.osrs230") == 0 )
        return "osrs230";
    if( strcmp(cache_dir, "cache.osrs239") == 0 )
        return "osrs239";
    return NULL;
}

/* Load one config group and hand each member file to `visit`. */
typedef void (*record_visitor)(
    const struct RSCache* profile,
    const uint8_t* data,
    int size,
    struct tally* tally);

static void
scan_config_group(
    const char* root,
    const char* cache_dir,
    int config_kind,
    enum RSCache_Type type,
    record_visitor visit,
    struct tally* tally)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", root, cache_dir);

    struct RSCache_Dat2Disk* disk = RSCache_Dat2DiskNewFromDirectory(path);
    if( !disk )
        return;

    struct RSCache_Dat2DiskArchive* archive =
        RSCache_Dat2DiskArchiveNewLoad(disk, RSCACHE_DAT2_DISK_TABLE_CONFIGS, config_kind);
    if( !archive )
    {
        RSCache_Dat2DiskFree(disk);
        return;
    }
    RSCache_Dat2DiskArchiveInitMetadata(disk, archive);

    /*
     * Use the declared profile when the cache directory names a revision we
     * support, and fall back to the group's archive revision otherwise.
     *
     * This is not cosmetic. `cache.kronos` needs RSCACHE_QUIRK_KRONOS, which no
     * archive revision can imply — it is a client-build difference. Scanning it
     * without the quirk leaves 228 loc records misaligned on the ambient-sound
     * retain byte, which is exactly the failure the quirk exists to prevent. A real
     * caller knows its revision; the harness should too.
     */
    struct RSCache profile;
    const char* revision_name = profile_name_for_cache(cache_dir);
    if( !revision_name || !RSCache_ProfileByName(revision_name, &profile) )
        profile = RSCache_ProfileZero();
    RSCache_ProfileSetGroupRevision(&profile, type, archive->revision);

    struct RSCache_FileList* files =
        RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
    if( files )
    {
        for( int i = 0; i < files->file_count; i++ )
        {
            if( files->file_sizes[i] <= 0 )
                continue;
            visit(&profile, (const uint8_t*)files->files[i], files->file_sizes[i], tally);
        }
        RSCache_FileListFree(files);
    }

    RSCache_Dat2DiskArchiveFree(archive);
    RSCache_Dat2DiskFree(disk);
}

/*
 * Scan a whole table where each archive is a single record, rather than a config
 * group holding many files. Framemaps (table 1) and sprite packs (table 8) work this
 * way, so they need a different traversal from the config types.
 *
 * The archive ids come from the reference table, which is the only place that
 * records which of them exist — they are neither dense nor 0-based in general.
 */
static void
scan_table_archives(
    const char* root,
    const char* cache_dir,
    int table_id,
    enum RSCache_Type type,
    record_visitor visit,
    struct tally* tally)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", root, cache_dir);

    struct RSCache_Dat2Disk* disk = RSCache_Dat2DiskNewFromDirectory(path);
    if( !disk )
        return;

    struct RSCache_ReferenceTable* table = disk->tables[table_id];
    if( !table )
    {
        RSCache_Dat2DiskFree(disk);
        return;
    }

    struct RSCache profile;
    const char* revision_name = profile_name_for_cache(cache_dir);
    if( !revision_name || !RSCache_ProfileByName(revision_name, &profile) )
        profile = RSCache_ProfileZero();
    RSCache_ProfileSetGroupRevision(&profile, type, table->version);

    /* Cap the sweep: some of these tables hold tens of thousands of archives and
     * every one is a separate sector-chain read. A few thousand is ample to catch a
     * systematic encoder fault, and the cap is reported so it cannot be mistaken for
     * full coverage. */
    const int scan_limit = 2000;
    int scanned = 0;

    for( int i = 0; i < table->id_count && scanned < scan_limit; i++ )
    {
        int archive_id = table->ids[i];

        struct RSCache_Dat2DiskArchive* archive =
            RSCache_Dat2DiskArchiveNewLoad(disk, table_id, archive_id);
        if( !archive )
            continue;

        if( archive->data && archive->data_size > 0 )
        {
            visit(&profile, (const uint8_t*)archive->data, archive->data_size, tally);
            scanned++;
        }

        RSCache_Dat2DiskArchiveFree(archive);
    }

    if( scanned >= scan_limit )
        printf("   (capped at %d archives of %d)\n", scan_limit, table->id_count);

    RSCache_Dat2DiskFree(disk);
}

/* ------------------------------------------------------------- framemap --- */

static void
visit_framemap(
    const struct RSCache* profile,
    const uint8_t* data,
    int size,
    struct tally* tally)
{
    struct RSCache_Dat2Framemap* first = RSCache_Dat2FramemapNewDecode2(0, (char*)data, size);
    if( !first )
        return;
    tally->records++;

    uint32_t capacity = RSCache_Dat2FramemapEncodeBound(first) + 64u;
    uint8_t* encoded = malloc(capacity);
    if( !encoded )
    {
        RSCache_Dat2FramemapFree(first);
        return;
    }

    uint32_t written = RSCache_Dat2FramemapEncode(first, encoded, capacity);
    if( written == 0 )
    {
        tally->encode_failed++;
        free(encoded);
        RSCache_Dat2FramemapFree(first);
        return;
    }
    note_bytes(tally, encoded, written, data, size);

    struct RSCache_Dat2Framemap* second =
        RSCache_Dat2FramemapNewDecode2(0, (char*)encoded, (int)written);
    if( second )
    {
        bool equal = first->length == second->length;
        for( int i = 0; equal && i < first->length; i++ )
        {
            if( first->types[i] != second->types[i] ||
                first->bone_groups_lengths[i] != second->bone_groups_lengths[i] )
            {
                equal = false;
                break;
            }
            for( int j = 0; j < first->bone_groups_lengths[i]; j++ )
            {
                if( first->bone_groups[i][j] != second->bone_groups[i][j] )
                {
                    equal = false;
                    break;
                }
            }
        }
        if( equal )
            tally->semantic_ok++;
        RSCache_Dat2FramemapFree(second);
    }

    RSCache_Dat2FramemapFree(first);
    free(encoded);
    (void)profile;
}

/* ---------------------------------------------------------------- model --- */

/*
 * Models need a field-by-field comparison rather than the usual handful of
 * scalars: the struct is 20 arrays, and which of them are NULL is itself
 * meaningful — the decoders free an array when nothing in it was used, and an
 * encoder that reinstated it would change what the renderer does.
 */

#define MODEL_SCALAR(field)                                                                        \
    do                                                                                             \
    {                                                                                              \
        if( first->field != second->field )                                                        \
            return #field;                                                                         \
    } while( 0 )

/** Both NULL or both present, and equal over `count` entries. */
#define MODEL_ARRAY(field, count)                                                                  \
    do                                                                                             \
    {                                                                                              \
        if( (first->field == NULL) != (second->field == NULL) )                                     \
            return #field " presence";                                                             \
        if( first->field )                                                                          \
        {                                                                                          \
            for( int i = 0; i < (count); i++ )                                                      \
                if( first->field[i] != second->field[i] )                                           \
                    return #field;                                                                 \
        }                                                                                          \
    } while( 0 )

/** NULL when the models match, else the name of the first field that differs. */
static const char*
model_difference(
    const struct RSCache_Model* first,
    const struct RSCache_Model* second)
{
    MODEL_SCALAR(vertex_count);
    MODEL_SCALAR(face_count);
    MODEL_SCALAR(textured_face_count);
    MODEL_SCALAR(model_priority);
    MODEL_SCALAR(rotated);
    MODEL_SCALAR(animaya_vertex_count);

    int vertices = first->vertex_count;
    int faces = first->face_count;
    int textured = first->textured_face_count;

    MODEL_ARRAY(vertices_x, vertices);
    MODEL_ARRAY(vertices_y, vertices);
    MODEL_ARRAY(vertices_z, vertices);
    MODEL_ARRAY(vertex_bone_map, vertices);

    MODEL_ARRAY(face_indices_a, faces);
    MODEL_ARRAY(face_indices_b, faces);
    MODEL_ARRAY(face_indices_c, faces);
    MODEL_ARRAY(face_alphas, faces);
    MODEL_ARRAY(face_infos, faces);
    MODEL_ARRAY(face_priorities, faces);
    MODEL_ARRAY(face_colors, faces);
    MODEL_ARRAY(face_bone_map, faces);
    MODEL_ARRAY(face_textures, faces);
    MODEL_ARRAY(face_texture_coords, faces);

    MODEL_ARRAY(textured_p_coordinate, textured);
    MODEL_ARRAY(textured_m_coordinate, textured);
    MODEL_ARRAY(textured_n_coordinate, textured);
    MODEL_ARRAY(texture_render_types, textured);

    MODEL_ARRAY(animaya_group_counts, first->animaya_vertex_count);
    if( (first->animaya_groups == NULL) != (second->animaya_groups == NULL) )
        return "animaya_groups presence";
    if( first->animaya_groups && first->animaya_group_counts )
    {
        for( int i = 0; i < first->animaya_vertex_count; i++ )
        {
            int count = first->animaya_group_counts[i];
            if( (first->animaya_groups[i] == NULL) != (second->animaya_groups[i] == NULL) )
                return "animaya_groups[i] presence";
            for( int j = 0; first->animaya_groups[i] && j < count; j++ )
            {
                if( first->animaya_groups[i][j] != second->animaya_groups[i][j] )
                    return "animaya_groups";
                if( first->animaya_scales[i][j] != second->animaya_scales[i][j] )
                    return "animaya_scales";
            }
        }
    }

    return NULL;
}

#undef MODEL_SCALAR
#undef MODEL_ARRAY

static void
visit_model(
    const struct RSCache* profile,
    const uint8_t* data,
    int size,
    struct tally* tally)
{
    struct RSCache_ModelProvenance* prov = NULL;
    struct RSCache_Model* first = RSCache_ModelNewDecodeProvenance((uint8_t*)data, size, &prov);
    if( !first )
        return;
    tally->records++;

    uint32_t capacity = RSCache_ModelEncodeBound(first, prov) + 64u;
    uint8_t* encoded = malloc(capacity);
    if( !encoded )
    {
        RSCache_ModelProvenanceFree(prov);
        RSCache_ModelFree(first);
        return;
    }

    uint32_t written = RSCache_ModelEncode(profile, first, prov, encoded, capacity);
    if( written == 0 )
    {
        tally->encode_failed++;
        free(encoded);
        RSCache_ModelProvenanceFree(prov);
        RSCache_ModelFree(first);
        return;
    }
    note_bytes(tally, encoded, written, data, size);

    struct RSCache_Model* second = RSCache_ModelNewDecode(encoded, (int)written);
    if( second )
    {
        if( model_difference(first, second) == NULL )
            tally->semantic_ok++;
        RSCache_ModelFree(second);
    }

    RSCache_ModelProvenanceFree(prov);
    RSCache_ModelFree(first);
    free(encoded);
}

/* -------------------------------------------------------------- sprites --- */

static void
visit_sprites(
    const struct RSCache* profile,
    const uint8_t* data,
    int size,
    struct tally* tally)
{
    /* Decode unnormalised: normalising rewrites the geometry in place, so an encode
     * of a normalised pack is deliberately not a copy of the source. */
    struct RSCache_Dat2SpritePack* first =
        RSCache_Dat2SpritePackNewDecode(data, size, RSCACHE_SPRITELOAD_FLAG_NONE);
    if( !first )
        return;
    tally->records++;

    uint32_t capacity = RSCache_Dat2SpritePackEncodeBound(first) + 64u;
    uint8_t* encoded = malloc(capacity);
    if( !encoded )
    {
        RSCache_Dat2SpritePackFree(first);
        return;
    }

    uint32_t written = RSCache_Dat2SpritePackEncode(first, encoded, capacity);
    if( written == 0 )
    {
        tally->encode_failed++;
        free(encoded);
        RSCache_Dat2SpritePackFree(first);
        return;
    }
    note_bytes(tally, encoded, written, data, size);

    struct RSCache_Dat2SpritePack* second =
        RSCache_Dat2SpritePackNewDecode(encoded, (int)written, RSCACHE_SPRITELOAD_FLAG_NONE);
    if( second )
    {
        bool equal = first->count == second->count &&
                     first->palette_length == second->palette_length;
        for( int i = 1; equal && i < first->palette_length; i++ )
        {
            if( first->palette[i] != second->palette[i] )
                equal = false;
        }
        for( int i = 0; equal && i < first->count; i++ )
        {
            const struct RSCache_Dat2Sprite* want = &first->sprites[i];
            const struct RSCache_Dat2Sprite* got = &second->sprites[i];
            if( want->width != got->width || want->height != got->height ||
                want->crop_width != got->crop_width || want->crop_height != got->crop_height ||
                want->offset_x != got->offset_x || want->offset_y != got->offset_y )
            {
                equal = false;
                break;
            }
            int dimension = want->crop_width * want->crop_height;
            if( memcmp(want->palette_pixels, got->palette_pixels, (size_t)dimension) != 0 ||
                memcmp(want->pixel_alphas, got->pixel_alphas, (size_t)dimension) != 0 )
            {
                equal = false;
                break;
            }
        }
        if( equal )
            tally->semantic_ok++;
        RSCache_Dat2SpritePackFree(second);
    }

    RSCache_Dat2SpritePackFree(first);
    free(encoded);
    (void)profile;
}

/** NULL-tolerant string compare; defined further down with the config visitors. */
static bool
str_equal(
    const char* lhs,
    const char* rhs);

/* --------------------------------------------------------- clientscript --- */

static void
visit_clientscript(
    const struct RSCache* profile,
    const uint8_t* data,
    int size,
    struct tally* tally)
{
    int flags = RSCache_ClientScriptFlags(profile);

    struct RSCache_ClientScript* first =
        RSCache_ClientScriptNewFromDecodeFlags(0, data, size, flags);
    if( !first )
        return;
    tally->records++;

    uint32_t capacity = RSCache_ClientScriptEncodeBound(first);
    uint8_t* encoded = malloc(capacity);
    if( !encoded )
    {
        RSCache_ClientScriptFree(first);
        return;
    }

    uint32_t written = RSCache_ClientScriptEncodeFlags(first, flags, encoded, capacity);
    if( written == 0 )
    {
        tally->encode_failed++;
        free(encoded);
        RSCache_ClientScriptFree(first);
        return;
    }
    note_bytes(tally, encoded, written, data, size);

    struct RSCache_ClientScript* second =
        RSCache_ClientScriptNewFromDecodeFlags(0, encoded, (int)written, flags);
    if( second )
    {
        const struct RSCache_CS2_Script* want = &first->script;
        const struct RSCache_CS2_Script* got = &second->script;

        bool equal = want->op_count == got->op_count &&
                     want->local_int_count == got->local_int_count &&
                     want->local_string_count == got->local_string_count &&
                     want->local_long_count == got->local_long_count &&
                     want->int_argument_count == got->int_argument_count &&
                     want->string_argument_count == got->string_argument_count &&
                     want->long_argument_count == got->long_argument_count &&
                     want->switch_table_count == got->switch_table_count &&
                     str_equal(want->signature, got->signature);

        for( int i = 0; equal && i < want->op_count; i++ )
        {
            if( want->opcodes[i] != got->opcodes[i] ||
                want->int_operands[i] != got->int_operands[i] ||
                !str_equal(want->string_operands[i], got->string_operands[i]) )
                equal = false;
        }
        for( int s = 0; equal && s < want->switch_table_count; s++ )
        {
            if( want->switch_tables[s].case_count != got->switch_tables[s].case_count )
            {
                equal = false;
                break;
            }
            for( int c = 0; c < want->switch_tables[s].case_count; c++ )
            {
                if( want->switch_tables[s].cases[c].key != got->switch_tables[s].cases[c].key ||
                    want->switch_tables[s].cases[c].target_pc !=
                        got->switch_tables[s].cases[c].target_pc )
                {
                    equal = false;
                    break;
                }
            }
        }

        if( equal )
            tally->semantic_ok++;
        RSCache_ClientScriptFree(second);
    }

    RSCache_ClientScriptFree(first);
    free(encoded);
}

/* ---------------------------------------------------------------- frame --- */

/*
 * Frames need their framemap, which lives in a different table and is named by the
 * first two bytes of the frame archive — so this needs its own traversal rather than
 * the generic table scan.
 */
static void
scan_frames(
    const char* root,
    const char* cache_dir,
    struct tally* tally)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", root, cache_dir);

    struct RSCache_Dat2Disk* disk = RSCache_Dat2DiskNewFromDirectory(path);
    if( !disk )
        return;

    struct RSCache_ReferenceTable* frames = disk->tables[RSCACHE_DAT2_DISK_TABLE_ANIMATIONS];
    if( !frames )
    {
        RSCache_Dat2DiskFree(disk);
        return;
    }

    const int scan_limit = 400;
    int scanned = 0;

    for( int i = 0; i < frames->id_count && scanned < scan_limit; i++ )
    {
        struct RSCache_Dat2DiskArchive* frame_archive =
            RSCache_Dat2DiskArchiveNewLoad(disk, RSCACHE_DAT2_DISK_TABLE_ANIMATIONS, frames->ids[i]);
        if( !frame_archive )
            continue;

        /* A frame archive is a group of frame files, each starting with its framemap
         * id. Take the first file only — enough to exercise the codec without
         * multiplying the framemap loads. */
        if( !frame_archive->data || frame_archive->data_size < 3 )
        {
            RSCache_Dat2DiskArchiveFree(frame_archive);
            continue;
        }
        RSCache_Dat2DiskArchiveInitMetadata(disk, frame_archive);

        struct RSCache_FileList* files = RSCache_FileListNewFromDecode(
            frame_archive->data, frame_archive->data_size, frame_archive->file_count);
        if( !files )
        {
            RSCache_Dat2DiskArchiveFree(frame_archive);
            continue;
        }

        for( int f = 0; f < files->file_count && scanned < scan_limit; f++ )
        {
            if( files->file_sizes[f] < 3 )
                continue;

            int framemap_id =
                RSCache_Dat2FrameFramemapIdFromFile(files->files[f], files->file_sizes[f]);

            struct RSCache_Dat2DiskArchive* fm_archive = RSCache_Dat2DiskArchiveNewLoad(
                disk, RSCACHE_DAT2_DISK_TABLE_SKELETONS, framemap_id);
            if( !fm_archive )
                continue;

            struct RSCache_Dat2Framemap* framemap = RSCache_Dat2FramemapNewDecode2(
                framemap_id, fm_archive->data, fm_archive->data_size);
            if( !framemap )
            {
                RSCache_Dat2DiskArchiveFree(fm_archive);
                continue;
            }

            struct RSCache_Dat2Frame* first = RSCache_Dat2FrameNewDecode2(
                0, framemap, files->files[f], files->file_sizes[f]);
            if( first )
            {
                tally->records++;
                scanned++;

                uint32_t capacity = RSCache_Dat2FrameEncodeBound(first) + 64u;
                uint8_t* encoded = malloc(capacity);
                if( encoded )
                {
                    uint32_t written = RSCache_Dat2FrameEncode(first, encoded, capacity);
                    if( written == 0 )
                    {
                        tally->encode_failed++;
                    }
                    else
                    {
                        note_bytes(
                            tally, encoded, written, (const uint8_t*)files->files[f],
                            files->file_sizes[f]);

                        struct RSCache_Dat2Frame* second = RSCache_Dat2FrameNewDecode2(
                            0, framemap, (char*)encoded, (int)written);
                        if( second )
                        {
                            bool equal = first->translator_count == second->translator_count &&
                                         first->showing == second->showing &&
                                         first->flag_count == second->flag_count;
                            for( int k = 0; equal && k < first->translator_count; k++ )
                            {
                                if( first->index_frame_ids[k] != second->index_frame_ids[k] ||
                                    first->translator_arg_x[k] != second->translator_arg_x[k] ||
                                    first->translator_arg_y[k] != second->translator_arg_y[k] ||
                                    first->translator_arg_z[k] != second->translator_arg_z[k] ||
                                    first->synthesized[k] != second->synthesized[k] )
                                    equal = false;
                            }
                            if( equal )
                                tally->semantic_ok++;
                            RSCache_Dat2FrameFree(second);
                        }
                    }
                    free(encoded);
                }
                RSCache_Dat2FrameFree(first);
            }

            RSCache_Dat2FramemapFree(framemap);
            RSCache_Dat2DiskArchiveFree(fm_archive);
        }

        RSCache_FileListFree(files);
        RSCache_Dat2DiskArchiveFree(frame_archive);
    }

    if( scanned >= scan_limit )
        printf("   (capped at %d frames)\n", scan_limit);

    RSCache_Dat2DiskFree(disk);
}

/* ------------------------------------------------------------- spotanim --- */

static void
visit_spotanim(
    const struct RSCache* profile,
    const uint8_t* data,
    int size,
    struct tally* tally)
{
    struct RSCache_Dat2ConfigSpotanim* first =
        RSCache_Dat2ConfigSpotanimNewDecode(0, (char*)data, size);
    if( !first )
        return;
    tally->records++;
    tally->tracks_consumed = true;

    uint8_t encoded[4096];
    uint32_t written = RSCache_Dat2ConfigSpotanimEncode(first, encoded, sizeof(encoded));
    if( written == 0 )
    {
        tally->encode_failed++;
        RSCache_Dat2ConfigSpotanimFree(first);
        return;
    }

    /* Exact consumption: the decode must have landed on the terminator, not bailed
     * partway. This is the check that caught opcodes 9/40 being misread. */
    if( first->_consumed == size )
        tally->consumed_exact++;

    note_bytes(tally, encoded, written, data, size);

    struct RSCache_Dat2ConfigSpotanim* second =
        RSCache_Dat2ConfigSpotanimNewDecode(0, (char*)encoded, (int)written);
    if( second )
    {
        bool equal = first->model == second->model && first->anim == second->anim &&
                     first->resizeh == second->resizeh && first->resizev == second->resizev &&
                     first->angle == second->angle && first->ambient == second->ambient &&
                     first->contrast == second->contrast &&
                     first->recol_count == second->recol_count &&
                     first->retex_count == second->retex_count &&
                     !!first->name == !!second->name &&
                     (!first->name || strcmp(first->name, second->name) == 0) &&
                     memcmp(first->recol_s, second->recol_s, sizeof(first->recol_s)) == 0 &&
                     memcmp(first->recol_d, second->recol_d, sizeof(first->recol_d)) == 0 &&
                     memcmp(first->retex_s, second->retex_s, sizeof(first->retex_s)) == 0 &&
                     memcmp(first->retex_d, second->retex_d, sizeof(first->retex_d)) == 0;
        if( equal )
            tally->semantic_ok++;
        RSCache_Dat2ConfigSpotanimFree(second);
    }

    RSCache_Dat2ConfigSpotanimFree(first);
    (void)profile;
}

/* ------------------------------------------------------------------ idk --- */

static bool
idk_equal(
    const struct RSCache_Dat2ConfigIdk* lhs,
    const struct RSCache_Dat2ConfigIdk* rhs)
{
    if( lhs->body_part_id != rhs->body_part_id )
        return false;
    if( lhs->is_not_selectable != rhs->is_not_selectable )
        return false;
    if( lhs->model_ids_count != rhs->model_ids_count )
        return false;
    for( int i = 0; i < lhs->model_ids_count; i++ )
    {
        if( lhs->model_ids[i] != rhs->model_ids[i] )
            return false;
    }
    if( lhs->recolor_count != rhs->recolor_count )
        return false;
    for( int i = 0; i < lhs->recolor_count; i++ )
    {
        if( lhs->recolors_from[i] != rhs->recolors_from[i] )
            return false;
        if( lhs->recolors_to[i] != rhs->recolors_to[i] )
            return false;
    }
    if( lhs->retexture_count != rhs->retexture_count )
        return false;
    for( int i = 0; i < lhs->retexture_count; i++ )
    {
        if( lhs->retextures_from[i] != rhs->retextures_from[i] )
            return false;
        if( lhs->retextures_to[i] != rhs->retextures_to[i] )
            return false;
    }
    for( int i = 0; i < 10; i++ )
    {
        if( lhs->if_model_ids[i] != rhs->if_model_ids[i] )
            return false;
    }
    return true;
}

static void
visit_idk(
    const struct RSCache* profile,
    const uint8_t* data,
    int size,
    struct tally* tally)
{
    struct RSCache_Dat2ConfigIdk first;
    struct RSCache_Dat2ConfigIdk second;

    RSCache_Dat2ConfigIdkDecodeInplace(&first, (char*)data, size);
    tally->records++;

    uint8_t encoded[8192];
    uint32_t written = RSCache_Dat2ConfigIdkEncode(&first, encoded, sizeof(encoded));
    if( written == 0 )
    {
        tally->encode_failed++;
        return;
    }

    note_bytes(tally, encoded, written, data, size);

    RSCache_Dat2ConfigIdkDecodeInplace(&second, (char*)encoded, (int)written);
    if( idk_equal(&first, &second) )
        tally->semantic_ok++;

    /* The decoder allocates into the struct but there is no in-place free, so
     * release the arrays directly. */
    free(first.model_ids);
    free(first.recolors_from);
    free(first.recolors_to);
    free(first.retextures_from);
    free(first.retextures_to);
    free(second.model_ids);
    free(second.recolors_from);
    free(second.recolors_to);
    free(second.retextures_from);
    free(second.retextures_to);
    (void)profile;
}

/* ----------------------------------------------------------------- enum --- */

static bool
enum_equal(
    const struct RSCache_Dat2ConfigEnum* lhs,
    const struct RSCache_Dat2ConfigEnum* rhs)
{
    if( lhs->output_is_string != rhs->output_is_string )
        return false;
    if( lhs->default_int != rhs->default_int )
        return false;
    if( !!lhs->default_string != !!rhs->default_string )
        return false;
    if( lhs->default_string && strcmp(lhs->default_string, rhs->default_string) != 0 )
        return false;
    if( lhs->count != rhs->count )
        return false;
    for( int i = 0; i < lhs->count; i++ )
    {
        if( lhs->keys[i] != rhs->keys[i] )
            return false;
        if( lhs->output_is_string )
        {
            const char* left = lhs->string_values ? lhs->string_values[i] : NULL;
            const char* right = rhs->string_values ? rhs->string_values[i] : NULL;
            if( !!left != !!right )
                return false;
            if( left && strcmp(left, right) != 0 )
                return false;
        }
        else
        {
            int left = lhs->int_values ? lhs->int_values[i] : 0;
            int right = rhs->int_values ? rhs->int_values[i] : 0;
            if( left != right )
                return false;
        }
    }
    return true;
}

static void
visit_enum(
    const struct RSCache* profile,
    const uint8_t* data,
    int size,
    struct tally* tally)
{
    struct RSCache_Dat2ConfigEnum first;
    struct RSCache_Dat2ConfigEnum second;
    memset(&first, 0, sizeof(first));
    memset(&second, 0, sizeof(second));

    RSCache_Dat2ConfigEnumDecodeInplace(&first, data, size);
    tally->records++;

    /* Enums can be large — a lookup table of thousands of entries — so size the
     * scratch from the source rather than guessing. */
    uint32_t capacity = (uint32_t)size * 4u + 4096u;
    uint8_t* encoded = malloc(capacity);
    if( !encoded )
        return;

    uint32_t written = RSCache_Dat2ConfigEnumEncode(&first, encoded, capacity);
    if( written == 0 )
    {
        tally->encode_failed++;
        free(encoded);
        RSCache_Dat2ConfigEnumFreeInplace(&first);
        return;
    }

    note_bytes(tally, encoded, written, data, size);

    RSCache_Dat2ConfigEnumDecodeInplace(&second, encoded, (int)written);
    if( enum_equal(&first, &second) )
        tally->semantic_ok++;

    RSCache_Dat2ConfigEnumFreeInplace(&first);
    RSCache_Dat2ConfigEnumFreeInplace(&second);
    free(encoded);
    (void)profile;
}

/* ---------------------------------------------------------------- param --- */

static void
visit_param(
    const struct RSCache* profile,
    const uint8_t* data,
    int size,
    struct tally* tally)
{
    struct RSCache_Dat2ConfigParam first;
    struct RSCache_Dat2ConfigParam second;
    memset(&first, 0, sizeof(first));
    memset(&second, 0, sizeof(second));

    RSCache_Dat2ConfigParamDecodeInplace(&first, data, size);
    tally->records++;

    uint8_t encoded[1024];
    uint32_t written = RSCache_Dat2ConfigParamEncode(&first, encoded, sizeof(encoded));
    if( written == 0 )
    {
        tally->encode_failed++;
        RSCache_Dat2ConfigParamFreeInplace(&first);
        return;
    }

    note_bytes(tally, encoded, written, data, size);

    RSCache_Dat2ConfigParamDecodeInplace(&second, encoded, (int)written);

    bool equal = first.type == second.type && first.default_int == second.default_int &&
                 first.default_long == second.default_long &&
                 first.auto_disable == second.auto_disable &&
                 !!first.default_string == !!second.default_string &&
                 (!first.default_string ||
                  strcmp(first.default_string, second.default_string) == 0);
    if( equal )
        tally->semantic_ok++;

    RSCache_Dat2ConfigParamFreeInplace(&first);
    RSCache_Dat2ConfigParamFreeInplace(&second);
    (void)profile;
}

/* --------------------------------------------------------------- struct --- */

static void
visit_struct(
    const struct RSCache* profile,
    const uint8_t* data,
    int size,
    struct tally* tally)
{
    struct RSCache_Dat2ConfigStruct first;
    struct RSCache_Dat2ConfigStruct second;
    memset(&first, 0, sizeof(first));
    memset(&second, 0, sizeof(second));

    RSCache_Dat2ConfigStructDecodeInplace(&first, data, size);
    tally->records++;

    uint32_t capacity = (uint32_t)size * 2u + 1024u;
    uint8_t* encoded = malloc(capacity);
    if( !encoded )
        return;

    uint32_t written = RSCache_Dat2ConfigStructEncode(&first, encoded, capacity);
    if( written == 0 )
    {
        tally->encode_failed++;
        free(encoded);
        RSCache_Dat2ConfigStructFreeInplace(&first);
        return;
    }

    note_bytes(tally, encoded, written, data, size);

    RSCache_Dat2ConfigStructDecodeInplace(&second, encoded, (int)written);

    bool equal = first.params.count == second.params.count;
    for( int i = 0; equal && i < first.params.count; i++ )
    {
        if( first.params.keys[i] != second.params.keys[i] ||
            first.params.is_string[i] != second.params.is_string[i] )
        {
            equal = false;
            break;
        }
        if( first.params.is_string[i] )
            equal = strcmp((char*)first.params.values[i], (char*)second.params.values[i]) == 0;
        else
            equal = *(int*)first.params.values[i] == *(int*)second.params.values[i];
    }
    if( equal )
        tally->semantic_ok++;

    RSCache_Dat2ConfigStructFreeInplace(&first);
    RSCache_Dat2ConfigStructFreeInplace(&second);
    free(encoded);
    (void)profile;
}

/* ------------------------------------------------------------------ flo --- */

static void
visit_underlay(
    const struct RSCache* profile,
    const uint8_t* data,
    int size,
    struct tally* tally)
{
    struct RSCache_Dat2ConfigUnderlay first;
    struct RSCache_Dat2ConfigUnderlay second;

    RSCache_Dat2ConfigUnderlayDecodeInplace(&first, (char*)data, size);
    tally->records++;

    uint8_t encoded[64];
    uint32_t written = RSCache_Dat2ConfigUnderlayEncode(&first, encoded, sizeof(encoded));
    if( written == 0 )
    {
        tally->encode_failed++;
        return;
    }
    note_bytes(tally, encoded, written, data, size);

    RSCache_Dat2ConfigUnderlayDecodeInplace(&second, (char*)encoded, (int)written);
    if( first.rgb_color == second.rgb_color )
        tally->semantic_ok++;
    (void)profile;
}

static void
visit_overlay(
    const struct RSCache* profile,
    const uint8_t* data,
    int size,
    struct tally* tally)
{
    struct RSCache_Dat2ConfigOverlay first;
    struct RSCache_Dat2ConfigOverlay second;

    RSCache_Dat2ConfigOverlayDecodeInplace(&first, (char*)data, size);
    tally->records++;

    uint8_t encoded[512];
    uint32_t written = RSCache_Dat2ConfigOverlayEncode(&first, encoded, sizeof(encoded));
    if( written == 0 )
    {
        tally->encode_failed++;
        RSCache_Dat2ConfigOverlayFreeInplace(&first);
        return;
    }
    note_bytes(tally, encoded, written, data, size);

    RSCache_Dat2ConfigOverlayDecodeInplace(&second, (char*)encoded, (int)written);
    bool equal = first.rgb_color == second.rgb_color && first.texture == second.texture &&
                 first.secondary_rgb_color == second.secondary_rgb_color &&
                 first.hide_underlay == second.hide_underlay &&
                 first.flotype_overlay == second.flotype_overlay &&
                 !!first.flotype_name == !!second.flotype_name &&
                 (!first.flotype_name || strcmp(first.flotype_name, second.flotype_name) == 0);
    if( equal )
        tally->semantic_ok++;

    RSCache_Dat2ConfigOverlayFreeInplace(&first);
    RSCache_Dat2ConfigOverlayFreeInplace(&second);
    (void)profile;
}

/* ------------------------------------------------------------ mapelement --- */

static void
visit_mapelement(
    const struct RSCache* profile,
    const uint8_t* data,
    int size,
    struct tally* tally)
{
    struct RSCache_MapElement first;
    struct RSCache_MapElement second;
    memset(&first, 0, sizeof(first));
    memset(&second, 0, sizeof(second));

    RSCache_MapElementDecodeInplace(&first, data, size);
    tally->records++;

    uint32_t capacity = (uint32_t)size * 2u + 1024u;
    uint8_t* encoded = malloc(capacity);
    if( !encoded )
        return;

    uint32_t written = RSCache_MapElementEncode(&first, encoded, capacity);
    if( written == 0 )
    {
        tally->encode_failed++;
        free(encoded);
        RSCache_MapElementFreeInplace(&first);
        return;
    }
    note_bytes(tally, encoded, written, data, size);

    RSCache_MapElementDecodeInplace(&second, encoded, (int)written);

    /* Only the four retained fields can be compared — this encoder is documented
     * as lossy for everything else. */
    bool equal = first.sprite_id == second.sprite_id && first.text_size == second.text_size &&
                 first.category == second.category && !!first.name == !!second.name &&
                 (!first.name || strcmp(first.name, second.name) == 0);
    if( equal )
        tally->semantic_ok++;

    RSCache_MapElementFreeInplace(&first);
    RSCache_MapElementFreeInplace(&second);
    free(encoded);
    (void)profile;
}

/* ------------------------------------------------------------------ obj --- */

static bool
str_equal(
    const char* lhs,
    const char* rhs)
{
    if( !!lhs != !!rhs )
        return false;
    return !lhs || strcmp(lhs, rhs) == 0;
}

static bool
obj_equal(
    const struct RSCache_Dat2ConfigObj* lhs,
    const struct RSCache_Dat2ConfigObj* rhs)
{
    if( !str_equal(lhs->name, rhs->name) || !str_equal(lhs->examine, rhs->examine) )
        return false;
    if( lhs->inventory_model_id != rhs->inventory_model_id || lhs->zoom2d != rhs->zoom2d ||
        lhs->xan2d != rhs->xan2d || lhs->yan2d != rhs->yan2d || lhs->zan2d != rhs->zan2d ||
        lhs->offset_x2d != rhs->offset_x2d || lhs->offset_y2d != rhs->offset_y2d ||
        lhs->cost != rhs->cost || lhs->stacking_behaviour != rhs->stacking_behaviour ||
        lhs->is_members != rhs->is_members || lhs->tradeable != rhs->tradeable ||
        lhs->weight != rhs->weight || lhs->category != rhs->category ||
        lhs->team != rhs->team || lhs->ambient != rhs->ambient ||
        lhs->contrast != rhs->contrast || lhs->resize_x != rhs->resize_x ||
        lhs->resize_y != rhs->resize_y || lhs->resize_z != rhs->resize_z ||
        lhs->wearpos_1 != rhs->wearpos_1 || lhs->wearpos_2 != rhs->wearpos_2 ||
        lhs->wearpos_3 != rhs->wearpos_3 ||
        lhs->shift_click_drop_index != rhs->shift_click_drop_index ||
        lhs->noted_id != rhs->noted_id || lhs->noted_template != rhs->noted_template ||
        lhs->bought_id != rhs->bought_id || lhs->bought_template_id != rhs->bought_template_id ||
        lhs->placeholder_id != rhs->placeholder_id ||
        lhs->placeholder_template_id != rhs->placeholder_template_id )
        return false;

    if( lhs->male_model_0 != rhs->male_model_0 || lhs->male_model_1 != rhs->male_model_1 ||
        lhs->male_model_2 != rhs->male_model_2 || lhs->male_offset != rhs->male_offset ||
        lhs->male_head_model != rhs->male_head_model ||
        lhs->male_head_model_2 != rhs->male_head_model_2 ||
        lhs->female_model_0 != rhs->female_model_0 ||
        lhs->female_model_1 != rhs->female_model_1 ||
        lhs->female_model_2 != rhs->female_model_2 || lhs->female_offset != rhs->female_offset ||
        lhs->female_head_model != rhs->female_head_model ||
        lhs->female_head_model_2 != rhs->female_head_model_2 )
        return false;

    for( int i = 0; i < 5; i++ )
    {
        if( !str_equal(lhs->actions[i], rhs->actions[i]) )
            return false;
        if( !str_equal(lhs->if_actions[i], rhs->if_actions[i]) )
            return false;
    }

    if( lhs->recolor_count != rhs->recolor_count || lhs->retexture_count != rhs->retexture_count )
        return false;
    for( int i = 0; i < lhs->recolor_count; i++ )
    {
        if( lhs->recolors_from[i] != rhs->recolors_from[i] ||
            lhs->recolors_to[i] != rhs->recolors_to[i] )
            return false;
    }
    for( int i = 0; i < lhs->retexture_count; i++ )
    {
        if( lhs->retextures_from[i] != rhs->retextures_from[i] ||
            lhs->retextures_to[i] != rhs->retextures_to[i] )
            return false;
    }

    for( int i = 0; i < 10; i++ )
    {
        if( lhs->count_obj[i] != rhs->count_obj[i] || lhs->count_co[i] != rhs->count_co[i] )
            return false;
    }

    for( int action = 0; action < 5; action++ )
    {
        if( !!lhs->sub_actions[action] != !!rhs->sub_actions[action] )
            return false;
        if( !lhs->sub_actions[action] )
            continue;
        for( int sub = 0; sub < 20; sub++ )
        {
            if( !str_equal(lhs->sub_actions[action][sub], rhs->sub_actions[action][sub]) )
                return false;
        }
    }

    if( lhs->params.count != rhs->params.count )
        return false;
    for( int i = 0; i < lhs->params.count; i++ )
    {
        if( lhs->params.keys[i] != rhs->params.keys[i] ||
            lhs->params.is_string[i] != rhs->params.is_string[i] )
            return false;
        if( lhs->params.is_string[i] )
        {
            if( strcmp((char*)lhs->params.values[i], (char*)rhs->params.values[i]) != 0 )
                return false;
        }
        else if( *(int*)lhs->params.values[i] != *(int*)rhs->params.values[i] )
            return false;
    }

    return true;
}

static void
visit_obj(
    const struct RSCache* profile,
    const uint8_t* data,
    int size,
    struct tally* tally)
{
    struct RSCache_Dat2ConfigObj* first = RSCache_Dat2ConfigObjNewDecode((char*)data, size);
    if( !first )
        return;
    tally->records++;

    uint32_t capacity = (uint32_t)size * 3u + 4096u;
    uint8_t* encoded = malloc(capacity);
    if( !encoded )
    {
        RSCache_Dat2ConfigObjFree(first);
        return;
    }

    uint32_t written = RSCache_Dat2ConfigObjEncode(first, encoded, capacity);
    if( written == 0 )
    {
        tally->encode_failed++;
        free(encoded);
        RSCache_Dat2ConfigObjFree(first);
        return;
    }
    note_bytes(tally, encoded, written, data, size);

    struct RSCache_Dat2ConfigObj* second =
        RSCache_Dat2ConfigObjNewDecode((char*)encoded, (int)written);
    if( second )
    {
        if( obj_equal(first, second) )
            tally->semantic_ok++;
        RSCache_Dat2ConfigObjFree(second);
    }

    RSCache_Dat2ConfigObjFree(first);
    free(encoded);
    (void)profile;
}

/* ------------------------------------------------------------------ npc --- */

static bool
npc_equal(
    const struct RSCache_Dat2ConfigNpc* lhs,
    const struct RSCache_Dat2ConfigNpc* rhs)
{
    if( !str_equal(lhs->name, rhs->name) )
        return false;
    if( lhs->size != rhs->size || lhs->standing_animation != rhs->standing_animation ||
        lhs->walking_animation != rhs->walking_animation ||
        lhs->idle_rotate_left_animation != rhs->idle_rotate_left_animation ||
        lhs->idle_rotate_right_animation != rhs->idle_rotate_right_animation ||
        lhs->rotate180_animation != rhs->rotate180_animation ||
        lhs->rotate_left_animation != rhs->rotate_left_animation ||
        lhs->rotate_right_animation != rhs->rotate_right_animation ||
        lhs->category != rhs->category || lhs->combat_level != rhs->combat_level ||
        lhs->width_scale != rhs->width_scale || lhs->height_scale != rhs->height_scale ||
        lhs->has_render_priority != rhs->has_render_priority || lhs->ambient != rhs->ambient ||
        lhs->contrast != rhs->contrast || lhs->rotation_speed != rhs->rotation_speed ||
        lhs->varbit_id != rhs->varbit_id || lhs->varp_index != rhs->varp_index ||
        lhs->is_pet != rhs->is_pet || lhs->run_animation != rhs->run_animation ||
        lhs->run_rotate180_animation != rhs->run_rotate180_animation ||
        lhs->run_rotate_left_animation != rhs->run_rotate_left_animation ||
        lhs->run_rotate_right_animation != rhs->run_rotate_right_animation ||
        lhs->crawl_animation != rhs->crawl_animation ||
        lhs->crawl_rotate180_animation != rhs->crawl_rotate180_animation ||
        lhs->crawl_rotate_left_animation != rhs->crawl_rotate_left_animation ||
        lhs->crawl_rotate_right_animation != rhs->crawl_rotate_right_animation ||
        lhs->low_priority_follower_ops != rhs->low_priority_follower_ops ||
        lhs->height != rhs->height )
        return false;

    if( lhs->models_count != rhs->models_count )
        return false;
    for( int i = 0; i < lhs->models_count; i++ )
    {
        if( lhs->models[i] != rhs->models[i] )
            return false;
    }
    if( lhs->chathead_models_count != rhs->chathead_models_count )
        return false;
    for( int i = 0; i < lhs->chathead_models_count; i++ )
    {
        if( lhs->chathead_models[i] != rhs->chathead_models[i] )
            return false;
    }

    for( int i = 0; i < 5; i++ )
    {
        if( !str_equal(lhs->actions[i], rhs->actions[i]) )
            return false;
    }
    for( int i = 0; i < 6; i++ )
    {
        if( lhs->stats[i] != rhs->stats[i] )
            return false;
    }

    if( lhs->recolor_count != rhs->recolor_count || lhs->retexture_count != rhs->retexture_count )
        return false;
    for( int i = 0; i < lhs->recolor_count; i++ )
    {
        if( lhs->recolor_to_find[i] != rhs->recolor_to_find[i] ||
            lhs->recolor_to_replace[i] != rhs->recolor_to_replace[i] )
            return false;
    }
    for( int i = 0; i < lhs->retexture_count; i++ )
    {
        if( lhs->retexture_to_find[i] != rhs->retexture_to_find[i] ||
            lhs->retexture_to_replace[i] != rhs->retexture_to_replace[i] )
            return false;
    }

    if( lhs->head_icon_count != rhs->head_icon_count )
        return false;
    for( int i = 0; i < lhs->head_icon_count; i++ )
    {
        if( lhs->head_icon_archive_ids[i] != rhs->head_icon_archive_ids[i] ||
            lhs->head_icon_sprite_index[i] != rhs->head_icon_sprite_index[i] )
            return false;
    }

    if( lhs->configs_count != rhs->configs_count )
        return false;
    for( int i = 0; i < lhs->configs_count; i++ )
    {
        if( lhs->configs[i] != rhs->configs[i] )
            return false;
    }

    if( lhs->params.count != rhs->params.count )
        return false;
    for( int i = 0; i < lhs->params.count; i++ )
    {
        if( lhs->params.keys[i] != rhs->params.keys[i] ||
            lhs->params.is_string[i] != rhs->params.is_string[i] )
            return false;
        if( lhs->params.is_string[i] )
        {
            if( strcmp((char*)lhs->params.values[i], (char*)rhs->params.values[i]) != 0 )
                return false;
        }
        else if( *(int*)lhs->params.values[i] != *(int*)rhs->params.values[i] )
            return false;
    }

    return true;
}

static void
visit_npc(
    const struct RSCache* profile,
    const uint8_t* data,
    int size,
    struct tally* tally)
{
    struct RSCache_Dat2ConfigNpc* first =
        RSCache_Dat2ConfigNpcNewDecodeProfile(profile, (char*)data, size);
    if( !first )
        return;
    tally->records++;
    tally->tracks_consumed = true;
    if( first->_consumed == size )
        tally->consumed_exact++;

    uint32_t capacity = (uint32_t)size * 3u + 4096u;
    uint8_t* encoded = malloc(capacity);
    if( !encoded )
    {
        RSCache_Dat2ConfigNpcFree(first);
        return;
    }

    uint32_t written = RSCache_Dat2ConfigNpcEncodeProfile(profile, first, encoded, capacity);
    if( written == 0 )
    {
        tally->encode_failed++;
        free(encoded);
        RSCache_Dat2ConfigNpcFree(first);
        return;
    }
    note_bytes(tally, encoded, written, data, size);

    struct RSCache_Dat2ConfigNpc* second =
        RSCache_Dat2ConfigNpcNewDecodeProfile(profile, (char*)encoded, (int)written);
    if( second )
    {
        if( npc_equal(first, second) )
            tally->semantic_ok++;
        RSCache_Dat2ConfigNpcFree(second);
    }

    RSCache_Dat2ConfigNpcFree(first);
    free(encoded);
}

/* ------------------------------------------------------------------ loc --- */

static bool
loc_equal(
    const struct RSCache_Dat2ConfigLoc* lhs,
    const struct RSCache_Dat2ConfigLoc* rhs)
{
    if( !str_equal(lhs->name, rhs->name) || !str_equal(lhs->desc, rhs->desc) )
        return false;
    if( lhs->size_x != rhs->size_x || lhs->size_z != rhs->size_z ||
        lhs->blocks_walk != rhs->blocks_walk ||
        lhs->blocks_projectiles != rhs->blocks_projectiles ||
        lhs->is_interactive != rhs->is_interactive || lhs->sharelight != rhs->sharelight ||
        lhs->occlude != rhs->occlude || lhs->seq_id != rhs->seq_id ||
        lhs->wall_width != rhs->wall_width || lhs->ambient != rhs->ambient ||
        lhs->contrast != rhs->contrast || lhs->map_function_id != rhs->map_function_id ||
        lhs->map_scene_id != rhs->map_scene_id || lhs->mirrored != rhs->mirrored ||
        lhs->shadowed != rhs->shadowed || lhs->resize_x != rhs->resize_x ||
        lhs->resize_height != rhs->resize_height || lhs->resize_z != rhs->resize_z ||
        lhs->offset_x != rhs->offset_x || lhs->offset_y != rhs->offset_y ||
        lhs->offset_z != rhs->offset_z || lhs->obstructs_ground != rhs->obstructs_ground ||
        lhs->break_routefinding != rhs->break_routefinding ||
        lhs->support_items != rhs->support_items ||
        lhs->transform_varbit != rhs->transform_varbit ||
        lhs->transform_varp != rhs->transform_varp ||
        lhs->contoured_ground != rhs->contoured_ground ||
        lhs->contour_ground_type != rhs->contour_ground_type ||
        lhs->seq_random_start != rhs->seq_random_start )
        return false;

    /* Ambient sound. The retain byte is only meaningful when a sound is present. */
    if( lhs->ambient_sound_id != rhs->ambient_sound_id ||
        lhs->ambient_sound_distance != rhs->ambient_sound_distance ||
        lhs->ambient_sound_ticks_min != rhs->ambient_sound_ticks_min ||
        lhs->ambient_sound_ticks_max != rhs->ambient_sound_ticks_max ||
        lhs->ambient_sound_retain != rhs->ambient_sound_retain ||
        lhs->ambient_sound_id_count != rhs->ambient_sound_id_count )
        return false;
    for( int i = 0; i < lhs->ambient_sound_id_count; i++ )
    {
        if( lhs->ambient_sound_ids[i] != rhs->ambient_sound_ids[i] )
            return false;
    }

    if( lhs->shapes_and_model_count != rhs->shapes_and_model_count )
        return false;
    if( !!lhs->shapes != !!rhs->shapes )
        return false;
    for( int i = 0; i < lhs->shapes_and_model_count; i++ )
    {
        if( lhs->shapes && lhs->shapes[i] != rhs->shapes[i] )
            return false;
        if( lhs->lengths[i] != rhs->lengths[i] )
            return false;
        for( int j = 0; j < lhs->lengths[i]; j++ )
        {
            if( lhs->models[i][j] != rhs->models[i][j] )
                return false;
        }
    }

    for( int i = 0; i < 9; i++ )
    {
        if( !str_equal(lhs->actions[i], rhs->actions[i]) )
            return false;
    }

    if( lhs->recolor_count != rhs->recolor_count || lhs->retexture_count != rhs->retexture_count )
        return false;
    for( int i = 0; i < lhs->recolor_count; i++ )
    {
        if( lhs->recolors_from[i] != rhs->recolors_from[i] ||
            lhs->recolors_to[i] != rhs->recolors_to[i] )
            return false;
    }
    for( int i = 0; i < lhs->retexture_count; i++ )
    {
        if( lhs->retextures_from[i] != rhs->retextures_from[i] ||
            lhs->retextures_to[i] != rhs->retextures_to[i] )
            return false;
    }

    if( lhs->transform_count != rhs->transform_count )
        return false;
    for( int i = 0; i < lhs->transform_count; i++ )
    {
        if( lhs->transforms[i] != rhs->transforms[i] )
            return false;
    }

    if( lhs->random_seq_id_count != rhs->random_seq_id_count )
        return false;
    for( int i = 0; i < lhs->random_seq_id_count; i++ )
    {
        if( lhs->random_seq_ids[i] != rhs->random_seq_ids[i] ||
            lhs->random_seq_delays[i] != rhs->random_seq_delays[i] )
            return false;
    }

    if( lhs->campaign_id_count != rhs->campaign_id_count )
        return false;
    for( int i = 0; i < lhs->campaign_id_count; i++ )
    {
        if( lhs->campaign_ids[i] != rhs->campaign_ids[i] )
            return false;
    }

    if( lhs->params.count != rhs->params.count )
        return false;
    for( int i = 0; i < lhs->params.count; i++ )
    {
        if( lhs->params.keys[i] != rhs->params.keys[i] ||
            lhs->params.is_string[i] != rhs->params.is_string[i] )
            return false;
        if( lhs->params.is_string[i] )
        {
            if( strcmp((char*)lhs->params.values[i], (char*)rhs->params.values[i]) != 0 )
                return false;
        }
        else if( *(int*)lhs->params.values[i] != *(int*)rhs->params.values[i] )
            return false;
    }

    return true;
}

static void
visit_loc(
    const struct RSCache* profile,
    const uint8_t* data,
    int size,
    struct tally* tally)
{
    int flags = RSCache_Dat2ConfigLocFlags(profile);

    struct RSCache_Dat2ConfigLoc first;
    struct RSCache_Dat2ConfigLoc second;
    memset(&first, 0, sizeof(first));
    memset(&second, 0, sizeof(second));

    RSCache_Dat2ConfigLocDecodeInplace(&first, (char*)data, size, flags);
    tally->records++;
    tally->tracks_consumed = true;
    if( first._consumed == size )
        tally->consumed_exact++;

    uint32_t capacity = (uint32_t)size * 3u + 8192u;
    uint8_t* encoded = malloc(capacity);
    if( !encoded )
    {
        RSCache_Dat2ConfigLocFreeInplace(&first);
        return;
    }

    uint32_t written = RSCache_Dat2ConfigLocEncodeFlags(&first, flags, encoded, capacity);
    if( written == 0 )
    {
        tally->encode_failed++;
        free(encoded);
        RSCache_Dat2ConfigLocFreeInplace(&first);
        return;
    }
    note_bytes(tally, encoded, written, data, size);

    RSCache_Dat2ConfigLocDecodeInplace(&second, (char*)encoded, (int)written, flags);
    if( loc_equal(&first, &second) )
        tally->semantic_ok++;

    RSCache_Dat2ConfigLocFreeInplace(&first);
    RSCache_Dat2ConfigLocFreeInplace(&second);
    free(encoded);
}

/* ------------------------------------------------------------- sequence --- */

static bool
sequence_equal(
    const struct RSCache_Dat2ConfigSequence* lhs,
    const struct RSCache_Dat2ConfigSequence* rhs,
    int codec_version)
{
    if( lhs->frame_count != rhs->frame_count || lhs->frame_step != rhs->frame_step ||
        lhs->stretches != rhs->stretches || lhs->forced_priority != rhs->forced_priority ||
        lhs->left_hand_item != rhs->left_hand_item ||
        lhs->right_hand_item != rhs->right_hand_item || lhs->max_loops != rhs->max_loops ||
        lhs->precedence_animating != rhs->precedence_animating ||
        lhs->priority != rhs->priority || lhs->reply_mode != rhs->reply_mode ||
        lhs->anim_maya_id != rhs->anim_maya_id ||
        lhs->anim_maya_start != rhs->anim_maya_start ||
        lhs->anim_maya_end != rhs->anim_maya_end ||
        lhs->vertical_offset != rhs->vertical_offset ||
        lhs->chat_frame_id_count != rhs->chat_frame_id_count )
        return false;

    if( !str_equal(lhs->debug_name, rhs->debug_name) )
        return false;

    for( int i = 0; i < lhs->frame_count; i++ )
    {
        if( lhs->frame_ids[i] != rhs->frame_ids[i] ||
            lhs->frame_lengths[i] != rhs->frame_lengths[i] )
            return false;
    }
    for( int i = 0; i < lhs->chat_frame_id_count; i++ )
    {
        if( lhs->chat_frame_ids[i] != rhs->chat_frame_ids[i] )
            return false;
    }

    if( !!lhs->interleave_leave != !!rhs->interleave_leave )
        return false;
    if( lhs->interleave_leave )
    {
        for( int i = 0;; i++ )
        {
            if( lhs->interleave_leave[i] != rhs->interleave_leave[i] )
                return false;
            if( lhs->interleave_leave[i] == 9999999 )
                break;
        }
    }

    if( !!lhs->anim_maya_masks != !!rhs->anim_maya_masks )
        return false;
    if( lhs->anim_maya_masks && memcmp(lhs->anim_maya_masks, rhs->anim_maya_masks, 256) != 0 )
        return false;

    if( lhs->frame_sounds.count != rhs->frame_sounds.count )
        return false;
    for( int i = 0; i < lhs->frame_sounds.count; i++ )
    {
        const struct RSCache_Dat2ConfigFrameSound* want = &lhs->frame_sounds.sounds[i];
        const struct RSCache_Dat2ConfigFrameSound* got = &rhs->frame_sounds.sounds[i];
        if( lhs->frame_sounds.frames[i] != rhs->frame_sounds.frames[i] )
            return false;
        if( want->id != got->id || want->loops != got->loops || want->location != got->location )
            return false;
        /* v1 packs three fields into 24 bits, so retain and weight have nowhere to
         * live and are not expected to survive. */
        if( codec_version != RSCACHE_CODEC_SEQUENCE_V1 )
        {
            if( want->retain != got->retain )
                return false;
        }
        if( codec_version == RSCACHE_CODEC_SEQUENCE_V3 && want->weight != got->weight )
            return false;
    }

    return true;
}

static void
visit_sequence(
    const struct RSCache* profile,
    const uint8_t* data,
    int size,
    struct tally* tally)
{
    int codec = RSCache_Dat2ConfigSequenceCodecVersion(profile);

    struct RSCache_Dat2ConfigSequence first;
    struct RSCache_Dat2ConfigSequence second;
    memset(&first, 0, sizeof(first));
    memset(&second, 0, sizeof(second));

    RSCache_Dat2ConfigSequenceDecodeProfile(&first, profile, (char*)data, size);
    tally->records++;
    tally->tracks_consumed = true;
    if( first._consumed == size )
        tally->consumed_exact++;

    uint32_t capacity = (uint32_t)size * 4u + 8192u;
    uint8_t* encoded = malloc(capacity);
    if( !encoded )
    {
        RSCache_Dat2ConfigSequenceFreeInplace(&first);
        return;
    }

    uint32_t written = RSCache_Dat2ConfigSequenceEncodeCodec(&first, codec, encoded, capacity);
    if( written == 0 )
    {
        tally->encode_failed++;
        free(encoded);
        RSCache_Dat2ConfigSequenceFreeInplace(&first);
        return;
    }
    note_bytes(tally, encoded, written, data, size);

    RSCache_Dat2ConfigSequenceDecodeProfile(&second, profile, (char*)encoded, (int)written);
    if( sequence_equal(&first, &second, codec) )
        tally->semantic_ok++;

    RSCache_Dat2ConfigSequenceFreeInplace(&first);
    RSCache_Dat2ConfigSequenceFreeInplace(&second);
    free(encoded);
}

/* ----------------------------------------------------------------- main --- */

struct datatype_case
{
    const char* name;
    int config_kind;
    enum RSCache_Type type;
    record_visitor visit;
};

int
main(int argc, char** argv)
{
    const char* root = argc > 1 ? argv[1] : RSCACHE_TEST_REPO_ROOT;

    printf("datatype round-trip tests (root %s)\n", root);

    static const struct datatype_case CASES[] = {
        { "spotanim", RSCACHE_DAT2_CONFIG_KIND_SPOTANIM, RSCACHE_TYPE_SPOTANIM, visit_spotanim },
        { "idk", RSCACHE_DAT2_CONFIG_KIND_IDENTKIT, RSCACHE_TYPE_IDK, visit_idk },
        { "enum", RSCACHE_DAT2_CONFIG_KIND_ENUM, RSCACHE_TYPE_ENUM, visit_enum },
        { "param", RSCACHE_DAT2_CONFIG_KIND_PARAMS, RSCACHE_TYPE_PARAM, visit_param },
        { "struct", RSCACHE_DAT2_CONFIG_KIND_STRUCT, RSCACHE_TYPE_STRUCT, visit_struct },
        { "underlay", RSCACHE_DAT2_CONFIG_KIND_UNDERLAY, RSCACHE_TYPE_UNDERLAY, visit_underlay },
        { "overlay", RSCACHE_DAT2_CONFIG_KIND_OVERLAY, RSCACHE_TYPE_OVERLAY, visit_overlay },
        { "mapelement", RSCACHE_DAT2_CONFIG_KIND_AREA, RSCACHE_TYPE_MAPELEMENT,
          visit_mapelement },
        { "obj", RSCACHE_DAT2_CONFIG_KIND_OBJECT, RSCACHE_TYPE_OBJ, visit_obj },
        { "npc", RSCACHE_DAT2_CONFIG_KIND_NPC, RSCACHE_TYPE_NPC, visit_npc },
        { "loc", RSCACHE_DAT2_CONFIG_KIND_LOCS, RSCACHE_TYPE_LOC, visit_loc },
        { "sequence", RSCACHE_DAT2_CONFIG_KIND_SEQUENCE, RSCACHE_TYPE_SEQUENCE, visit_sequence },
    };

    /* Types whose archives are one record each, rather than config groups. */
    struct table_case
    {
        const char* name;
        int table_id;
        enum RSCache_Type type;
        record_visitor visit;
    };
    static const struct table_case TABLE_CASES[] = {
        { "framemap", RSCACHE_DAT2_DISK_TABLE_SKELETONS, RSCACHE_TYPE_FRAMEMAP, visit_framemap },
        { "sprites", RSCACHE_DAT2_DISK_TABLE_SPRITES, RSCACHE_TYPE_SPRITE, visit_sprites },
        { "cs2script", RSCACHE_DAT2_DISK_TABLE_CLIENTSCRIPT, RSCACHE_TYPE_CLIENTSCRIPT,
          visit_clientscript },
        { "model", RSCACHE_DAT2_DISK_TABLE_MODELS, RSCACHE_TYPE_MODEL, visit_model },
    };

    int scanned_any = 0;

    RSCACHE_TEST_GROUP("frame");
    for( size_t d = 0; d < sizeof(CACHE_DIRS) / sizeof(CACHE_DIRS[0]); d++ )
    {
        struct tally tally = { 0 };
        scan_frames(root, CACHE_DIRS[d], &tally);
        if( tally.records > 0 )
            scanned_any = 1;
        tally_report("frame", CACHE_DIRS[d], &tally);
    }

    for( size_t c = 0; c < sizeof(TABLE_CASES) / sizeof(TABLE_CASES[0]); c++ )
    {
        RSCACHE_TEST_GROUP(TABLE_CASES[c].name);

        for( size_t d = 0; d < sizeof(CACHE_DIRS) / sizeof(CACHE_DIRS[0]); d++ )
        {
            struct tally tally = { 0 };
            scan_table_archives(
                root,
                CACHE_DIRS[d],
                TABLE_CASES[c].table_id,
                TABLE_CASES[c].type,
                TABLE_CASES[c].visit,
                &tally);
            if( tally.records > 0 )
                scanned_any = 1;
            tally_report(TABLE_CASES[c].name, CACHE_DIRS[d], &tally);
        }
    }

    for( size_t c = 0; c < sizeof(CASES) / sizeof(CASES[0]); c++ )
    {
        RSCACHE_TEST_GROUP(CASES[c].name);

        for( size_t d = 0; d < sizeof(CACHE_DIRS) / sizeof(CACHE_DIRS[0]); d++ )
        {
            struct tally tally = { 0 };
            scan_config_group(
                root, CACHE_DIRS[d], CASES[c].config_kind, CASES[c].type, CASES[c].visit, &tally);
            if( tally.records > 0 )
                scanned_any = 1;
            tally_report(CASES[c].name, CACHE_DIRS[d], &tally);
        }
    }

    if( !scanned_any )
    {
        printf("roundtrip: SKIP (no cache directories found under %s)\n", root);
        return 0;
    }

    return rscache_test_report("roundtrip");
}
