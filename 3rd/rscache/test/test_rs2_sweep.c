/*
 * cache.643 (RS2) coverage sweep — a *diagnostic*, not an assertion suite.
 *
 * The round-trip suite (test_roundtrip.c) scans config *groups* in table 2, which is
 * the OldSchool layout. 643 promotes loc/npc/obj/seq/spotanim into their own sharded
 * tables (see RSCache_RecordAddressFor), so those types are invisible to it. This
 * walks the sharded tables directly and reports, per type:
 *
 *   - records seen
 *   - records whose decode landed exactly on their terminator (exact consumption)
 *   - a histogram of the opcode each short decode stopped on
 *
 * The opcode histogram is the useful part. A decoder that stops on opcode 229 has
 * not met a real opcode 229 — it has desynced earlier and is reading a payload byte
 * as an opcode. The *distribution* names the culprit: a single wrong field width
 * shows up as one hot opcode with a long tail of noise behind it.
 *
 *   make -C 3rd/rscache build/test_rs2_sweep && 3rd/rscache/build/test_rs2_sweep
 */
#include "rscache_test.h"

#include "dat2disk.h"
#include "datatypes/dat2_config_loc.h"
#include "datatypes/dat2_config_npc.h"
#include "datatypes/dat2_config_obj.h"
#include "datatypes/dat2_proctexture.h"
#include "datatypes/maps.h"
#include "datatypes/model.h"
#include "filelist.h"
#include "xtea_config.h"
#include "reference_table.h"
#include "revisions/revisions.h"
#include "rscache_profile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef RSCACHE_TEST_REPO_ROOT
#define RSCACHE_TEST_REPO_ROOT "../.."
#endif

static bool
open_cache_with_identity(
    const char* path,
    const struct RSCache* profile,
    struct RSCache_Dat2Disk** out_disk,
    struct RSCache* out_profile)
{
    struct RSCache_Dat2Disk* disk = RSCache_Dat2DiskNewFromDirectory(path);
    if( !disk )
        return false;
    *out_profile = *profile;
    RSCache_Dat2DiskSetProfile(disk, out_profile);
    *out_disk = disk;
    return true;
}

static bool
open_643_cache(
    const char* path,
    struct RSCache_Dat2Disk** out_disk,
    struct RSCache* out_profile)
{
    struct RSCache profile;
    if( !RSCache_ProfileByName("643", &profile) )
        return false;
    return open_cache_with_identity(path, &profile, out_disk, out_profile);
}

struct sweep
{
    int records;
    int consumed_exact;
    int consumed_short;
    /* Opcode the decode stopped on, for records that did not consume exactly. */
    int stop_opcode[256];
    /*
     * False when the datatype has no `_consumed` field, so `consumed_exact` degrades to
     * "the decoder returned something". Reported under a different word, because a number
     * that reads as exact consumption and is not would be worse than no number: the obj
     * decoder returns non-NULL for every 643 record while still logging ~150 buffer
     * overruns, and a "100.0%" column would say the opposite.
     */
    bool measures_consumption;
};

static void
sweep_report(
    const char* label,
    const char* cache,
    const struct sweep* s)
{
    if( s->records == 0 )
    {
        printf("  %-10s %-14s (absent)\n", label, cache);
        return;
    }
    if( !s->measures_consumption )
    {
        printf(
            "  %-10s %-14s %6d records  returned %6d  (no _consumed: cannot check alignment)\n",
            label,
            cache,
            s->records,
            s->consumed_exact);
        return;
    }
    printf(
        "  %-10s %-14s %6d records  exact %6d (%5.1f%%)  short %5d\n",
        label,
        cache,
        s->records,
        s->consumed_exact,
        100.0 * s->consumed_exact / s->records,
        s->consumed_short);

    /* Rank the stop opcodes so the dominant one is first — that is the one worth
     * chasing; the rest are usually its downstream noise. */
    for( int rank = 0; rank < 12; rank++ )
    {
        int best = -1;
        for( int op = 0; op < 256; op++ )
            if( s->stop_opcode[op] > 0 && (best < 0 || s->stop_opcode[op] > s->stop_opcode[best]) )
                best = op;
        if( best < 0 )
            break;
        printf("      stopped on opcode %3d : %d\n", best, s->stop_opcode[best]);
        ((struct sweep*)s)->stop_opcode[best] = -s->stop_opcode[best];
    }
    for( int op = 0; op < 256; op++ )
        if( s->stop_opcode[op] < 0 )
            ((struct sweep*)s)->stop_opcode[op] = -s->stop_opcode[op];
}

/*
 * The opcode a short decode stopped on.
 *
 * `_consumed` is the position *after* the byte the decoder gave up on — the opcode
 * has already been read when the default arm fires — so the opcode is at
 * `consumed - 1`, not at `consumed`. Reading it at `consumed` reports the first
 * *payload* byte instead and makes the histogram meaningless.
 */
static void
note_stop(
    struct sweep* s,
    const uint8_t* data,
    int size,
    int consumed)
{
    s->consumed_short++;
    if( consumed > 0 && consumed <= size )
        s->stop_opcode[data[consumed - 1]]++;
    else
        s->stop_opcode[0]++;
}

typedef void (*sweep_visitor)(
    const struct RSCache* profile,
    const uint8_t* data,
    int size,
    struct sweep* s);

static void
visit_loc(
    const struct RSCache* profile,
    const uint8_t* data,
    int size,
    struct sweep* s)
{
    struct RSCache_Dat2ConfigLoc loc;
    memset(&loc, 0, sizeof(loc));
    RSCache_Dat2ConfigLocDecodeInplace(
        &loc, (char*)data, size, RSCache_Dat2ConfigLocFlags(profile));
    s->records++;
    if( loc._consumed == size )
        s->consumed_exact++;
    else
        note_stop(s, data, size, loc._consumed);
    RSCache_Dat2ConfigLocFreeInplace(&loc);
}

static void
visit_obj(
    const struct RSCache* profile,
    const uint8_t* data,
    int size,
    struct sweep* s)
{
    /* obj has no profile overload and no _consumed field, so this can only report
     * "the decoder returned something" — enough to tell a hard failure from a
     * silent misread, not enough to prove alignment. See `measures_consumption`. */
    (void)profile;
    struct RSCache_Dat2ConfigObj* obj = RSCache_Dat2ConfigObjNewDecode((char*)data, size);
    s->records++;
    s->measures_consumption = false;
    if( !obj )
        note_stop(s, data, size, -1);
    else
        s->consumed_exact++;
    RSCache_Dat2ConfigObjFree(obj);
}

static void
visit_npc(
    const struct RSCache* profile,
    const uint8_t* data,
    int size,
    struct sweep* s)
{
    struct RSCache_Dat2ConfigNpc* npc =
        RSCache_Dat2ConfigNpcNewDecodeProfile(profile, (char*)data, size);
    s->records++;
    if( !npc )
    {
        note_stop(s, data, size, -1);
        return;
    }
    if( npc->_consumed == size )
        s->consumed_exact++;
    else
        note_stop(s, data, size, npc->_consumed);
    RSCache_Dat2ConfigNpcFree(npc);
}

/*
 * Walk every group of a sharded table.
 *
 * The reference table names which groups exist; loading blind would trip on the
 * sparse ones. Records are decoded straight from the file list — the id itself is
 * not needed here, only the bytes.
 */
static void
sweep_table(
    struct RSCache_Dat2Disk* disk,
    const struct RSCache* profile,
    enum RSCache_Type type,
    sweep_visitor visit,
    struct sweep* s)
{
    struct RSCache_RecordAddress addr = RSCache_RecordAddressFor(profile, type);
    int table_id = RSCache_Dat2DiskTableId(disk, addr.table);
    if( table_id < 0 )
        return;

    struct RSCache_Dat2DiskArchive* ref =
        RSCache_Dat2DiskArchiveNewReferenceTableLoad(disk, table_id);
    if( !ref )
        return;
    struct RSCache_ReferenceTable* table =
        RSCache_ReferenceTableNewDecode(ref->data, ref->data_size);
    RSCache_Dat2DiskArchiveFree(ref);
    if( !table )
        return;

    for( int i = 0; i < table->id_count; i++ )
    {
        int group = table->ids[i];
        struct RSCache_Dat2DiskArchive* archive =
            RSCache_Dat2DiskArchiveNewLoad(disk, table_id, group);
        if( !archive )
            continue;
        RSCache_Dat2DiskArchiveInitMetadata(disk, archive);

        struct RSCache_FileList* files =
            RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
        if( files )
        {
            for( int f = 0; f < files->file_count; f++ )
            {
                if( files->file_sizes[f] <= 0 )
                    continue;
                visit(profile, (const uint8_t*)files->files[f], files->file_sizes[f], s);
            }
            RSCache_FileListFree(files);
        }
        RSCache_Dat2DiskArchiveFree(archive);
    }

    RSCache_ReferenceTableFree(table);
}

/*
 * Dump one loc record: raw bytes plus what the decoder made of them. For hand-checking a
 * single id against LocType.decodeOpcode when a *rendered* loc looks wrong even though the
 * whole corpus consumes exactly — exact consumption proves alignment, not interpretation.
 *
 *   test_rs2_sweep <root> loc <id>
 */
static int
dump_loc(const char* root, int loc_id)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/cache.643", root);
    struct RSCache_Dat2Disk* disk;
    struct RSCache profile;
    if( !open_643_cache(path, &disk, &profile) )
    {
        printf("no cache at %s\n", path);
        return 1;
    }

    struct RSCache_RecordAddress addr = RSCache_RecordAddressFor(&profile, RSCACHE_TYPE_LOC);
    int table_id = RSCache_Dat2DiskTableId(disk, addr.table);
    struct RSCache_Dat2DiskArchive* archive =
        RSCache_Dat2DiskArchiveNewLoad(disk, table_id, loc_id >> addr.group_shift);
    if( !archive )
    {
        printf("loc %d: group %d absent\n", loc_id, loc_id >> addr.group_shift);
        RSCache_Dat2DiskFree(disk);
        return 1;
    }
    RSCache_Dat2DiskArchiveInitMetadata(disk, archive);

    struct RSCache_FileList* files =
        RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
    if( !files )
    {
        RSCache_Dat2DiskArchiveFree(archive);
        RSCache_Dat2DiskFree(disk);
        return 1;
    }

    /* Sharded file ids are group-relative; the file list is positional and the archive's
     * metadata carries the real ids, so match through archive->file_ids. */
    int want_file = loc_id & addr.file_mask;
    int found = 0;
    for( int i = 0; i < files->file_count; i++ )
    {
        int file_id = archive->file_ids ? archive->file_ids[i] : i;
        if( file_id != want_file || files->file_sizes[i] <= 0 )
            continue;
        found = 1;
        const uint8_t* data = (const uint8_t*)files->files[i];
        int size = files->file_sizes[i];

        printf("loc %d: %d bytes\n", loc_id, size);
        for( int off = 0; off < size; off += 16 )
        {
            printf("  %04x:", off);
            for( int b = 0; b < 16 && off + b < size; b++ )
                printf(" %02x", data[off + b]);
            printf("\n");
        }

        struct RSCache_Dat2ConfigLoc loc;
        memset(&loc, 0, sizeof(loc));
        RSCache_Dat2ConfigLocDecodeInplace(
            &loc, (char*)data, size, RSCache_Dat2ConfigLocFlags(&profile));
        printf("  consumed %d/%d\n", loc._consumed, size);
        printf("  name=%s size=%dx%d\n", loc.name ? loc.name : "(null)", loc.size_x, loc.size_z);
        printf("  entries=%d (shapes %s)\n", loc.shapes_and_model_count, loc.shapes ? "yes" : "no");
        for( int e = 0; e < loc.shapes_and_model_count; e++ )
        {
            printf("    shape %3d:", loc.shapes ? loc.shapes[e] : -1);
            for( int m = 0; m < loc.lengths[e]; m++ )
                printf(" %d", loc.models[e][m]);
            printf("\n");
        }
        RSCache_Dat2ConfigLocFreeInplace(&loc);
    }
    if( !found )
        printf("loc %d: file %d absent from group\n", loc_id, want_file);

    RSCache_FileListFree(files);
    RSCache_Dat2DiskArchiveFree(archive);
    RSCache_Dat2DiskFree(disk);
    return 0;
}

/*
 * Dump one model's interpreted geometry: vertices, faces, colours, textures, alpha. The
 * companion to dump_loc — a byte-exact model round-trip still permits a wrong reading, and
 * this is what a wrong reading looks like from the renderer's side.
 *
 *   test_rs2_sweep <root> model <id>
 */
static int
dump_model(const char* root, int model_id)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/cache.643", root);
    struct RSCache_Dat2Disk* disk;
    struct RSCache profile;
    if( !open_643_cache(path, &disk, &profile) )
    {
        printf("no cache at %s\n", path);
        return 1;
    }

    struct RSCache_Model* model = RSCache_ModelNewFromCache(disk, model_id);
    if( !model )
    {
        printf("model %d: decode failed\n", model_id);
        RSCache_Dat2DiskFree(disk);
        return 1;
    }

    printf(
        "model %d: v=%d f=%d textured_faces=%d\n",
        model_id,
        model->vertex_count,
        model->face_count,
        model->textured_face_count);
    for( int i = 0; i < model->vertex_count && i < 32; i++ )
        printf(
            "  v%-3d (%d, %d, %d)\n",
            i,
            model->vertices_x[i],
            model->vertices_y[i],
            model->vertices_z[i]);
    for( int i = 0; i < model->face_count && i < 32; i++ )
        printf(
            "  f%-3d (%d,%d,%d) colour=%d info=%d alpha=%d tex=%d texcoord=%d\n",
            i,
            model->face_indices_a[i],
            model->face_indices_b[i],
            model->face_indices_c[i],
            model->face_colors ? model->face_colors[i] : -1,
            model->face_infos ? model->face_infos[i] : -1,
            model->face_alphas ? model->face_alphas[i] : -1,
            model->face_textures ? model->face_textures[i] : -1,
            model->face_texture_coords ? model->face_texture_coords[i] : -1);

    RSCache_ModelFree(model);
    RSCache_Dat2DiskFree(disk);
    return 0;
}

/*
 * Tally one square's loc spawns by id, worst offenders first. When a render is dominated by
 * one repeated wrong-looking model, this is what names the loc ids responsible — the client's
 * scenery debug prints extents for the first sighting only, which hides who is prolific.
 *
 *   test_rs2_sweep <root> spawns <map_x> <map_z>
 */
static int
dump_spawns(const char* root, int map_x, int map_z)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/cache.643", root);
    struct RSCache_Dat2Disk* disk;
    struct RSCache profile;
    if( !open_643_cache(path, &disk, &profile) )
    {
        printf("no cache at %s\n", path);
        return 1;
    }
    if( RSCache_MapLocsEncrypted(&profile) )
    {
        snprintf(path, sizeof(path), "%s/cache.643/xteas.json", root);
        RSCache_XteaConfigLoadKeys(path);
    }

    struct RSCache_MapLocs* locs = RSCache_MapLocsNewFromCache(disk, map_x, map_z);
    if( !locs )
    {
        RSCache_Dat2DiskFree(disk);
        return 1;
    }

    /* id -> (count, shape mask) over this square's spawns. */
    struct
    {
        int loc_id;
        int count;
        int shapes[8];
        int shape_count;
    } tally[512];
    int tally_count = 0;

    for( int i = 0; i < locs->locs_count; i++ )
    {
        const struct RSCache_MapLoc* spawn = &locs->locs[i];
        int t;
        for( t = 0; t < tally_count; t++ )
            if( tally[t].loc_id == spawn->loc_id )
                break;
        if( t == tally_count )
        {
            if( tally_count == (int)(sizeof(tally) / sizeof(tally[0])) )
                continue;
            memset(&tally[t], 0, sizeof(tally[t]));
            tally[t].loc_id = spawn->loc_id;
            tally_count++;
        }
        tally[t].count++;
        {
            int seen = 0;
            for( int u = 0; u < tally[t].shape_count; u++ )
                if( tally[t].shapes[u] == spawn->shape_select )
                    seen = 1;
            if( !seen && tally[t].shape_count < 8 )
                tally[t].shapes[tally[t].shape_count++] = spawn->shape_select;
        }
    }

    printf("map %d,%d: %d spawns, %d unique ids\n", map_x, map_z, locs->locs_count, tally_count);
    for( int rank = 0; rank < 24; rank++ )
    {
        int best = -1;
        for( int t = 0; t < tally_count; t++ )
            if( tally[t].count > 0 && (best < 0 || tally[t].count > tally[best].count) )
                best = t;
        if( best < 0 )
            break;
        printf("  loc %5d x%4d shapes[", tally[best].loc_id, tally[best].count);
        for( int u = 0; u < tally[best].shape_count; u++ )
            printf("%s%d", u ? "," : "", tally[best].shapes[u]);
        printf("]\n");
        tally[best].count = -tally[best].count;
    }

    RSCache_MapLocsFree(locs);
    RSCache_Dat2DiskFree(disk);
    return 0;
}

/*
 * Print one material's flags, or with id -1 a summary of the whole table. `valid` is the one
 * that matters for rendering: rs-map-viewer's isSd() is exactly this flag, and ModelData.light
 * drops the texture from any face whose material is not valid — those are HD-only materials
 * the SD renderer draws as plain coloured faces.
 *
 *   test_rs2_sweep <root> materials [id]
 */
static int
dump_materials(const char* root, int want_id)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/cache.643", root);
    struct RSCache_Dat2Disk* disk;
    struct RSCache profile;
    if( !open_643_cache(path, &disk, &profile) )
    {
        printf("no cache at %s\n", path);
        return 1;
    }

    int table_id = RSCache_Dat2DiskTableId(disk, RSCACHE_DAT2_TABLE_MATERIALS);
    struct RSCache_Dat2DiskArchive* archive = RSCache_Dat2DiskArchiveNewLoad(disk, table_id, 0);
    if( !archive )
    {
        printf("materials table absent\n");
        RSCache_Dat2DiskFree(disk);
        return 1;
    }
    struct RSCache_Dat2MaterialTable* table = RSCache_Dat2MaterialTableNewDecode(
        archive->data, archive->data_size, RSCache_Dat2ProcTextureFlags(&profile));
    RSCache_Dat2DiskArchiveFree(archive);
    if( !table )
    {
        RSCache_Dat2DiskFree(disk);
        return 1;
    }

    if( want_id >= 0 && want_id < table->count )
    {
        const struct RSCache_Dat2Material* mat = &table->materials[want_id];
        printf(
            "material %d: exists=%d valid=%d alpha=%d small=%d disabled=%d brightness=%d "
            "blanch=%d average_hsl=%d\n",
            want_id,
            mat->exists,
            mat->valid,
            mat->alpha,
            mat->small,
            mat->disabled,
            mat->brightness,
            mat->blanch,
            mat->average_hsl);
    }
    else
    {
        int exists = 0;
        int valid = 0;
        int invalid_ids = 0;
        for( int i = 0; i < table->count; i++ )
        {
            if( !table->materials[i].exists )
                continue;
            exists++;
            if( table->materials[i].valid )
                valid++;
            else if( invalid_ids < 40 )
            {
                if( invalid_ids == 0 )
                    printf("  not-SD ids:");
                printf(" %d", i);
                invalid_ids++;
            }
        }
        if( invalid_ids )
            printf("%s\n", invalid_ids == 40 ? " ..." : "");
        printf("materials: %d, exists=%d, valid(SD)=%d, not-SD=%d\n",
               table->count, exists, valid, exists - valid);
        /* The engine's texture maps are 256-slot (scene map, texture_failed, the raster's
         * id guard), so an SD id past 255 would silently never draw. Report the split. */
        {
            int sd_above_255 = 0;
            int max_sd = -1;
            for( int i = 0; i < table->count; i++ )
                if( table->materials[i].exists && table->materials[i].valid )
                {
                    if( i > 255 )
                        sd_above_255++;
                    max_sd = i;
                }
            printf("  SD ids above 255: %d (highest SD id %d)\n", sd_above_255, max_sd);
        }
    }

    RSCache_Dat2MaterialTableFree(table);
    RSCache_Dat2DiskFree(disk);
    return 0;
}

/*
 * Census of ob3 `format_version` across a cache's models.
 *
 * The field decides whether the reference's `scaleDown(2)` applies (version >= 13 stores
 * vertices at 4x), so "which models carry it" is the difference between correct geometry and
 * everything rendering 4x too large. It is a *census* rather than an assertion because the
 * answer is a property of the cache, not of this library — and because the claim it backs
 * ("most of 643 is version 13+, no OldSchool model is") deserves a number rather than a
 * recollection.
 *
 *   test_rs2_sweep <root> modelvers [cache-dir]
 */
static int
dump_model_versions(const char* root, const char* cache_dir)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", root, cache_dir);
    struct RSCache_Dat2Disk* disk = RSCache_Dat2DiskNewFromDirectory(path);
    if( !disk )
    {
        printf("  %-14s (absent)\n", cache_dir);
        return 0;
    }

    int table_id = RSCache_Dat2DiskTableId(disk, RSCACHE_DAT2_TABLE_MODELS);
    struct RSCache_Dat2DiskArchive* ref =
        RSCache_Dat2DiskArchiveNewReferenceTableLoad(disk, table_id);
    if( !ref )
    {
        RSCache_Dat2DiskFree(disk);
        return 0;
    }
    struct RSCache_ReferenceTable* table =
        RSCache_ReferenceTableNewDecode(ref->data, ref->data_size);
    RSCache_Dat2DiskArchiveFree(ref);
    if( !table )
    {
        RSCache_Dat2DiskFree(disk);
        return 0;
    }

    int versions[256];
    memset(versions, 0, sizeof(versions));
    int total = 0;
    int scaled = 0;

    for( int i = 0; i < table->id_count; i++ )
    {
        struct RSCache_Model* model = RSCache_ModelNewFromCache(disk, table->ids[i]);
        if( !model )
            continue;
        total++;
        if( model->format_version >= 0 && model->format_version < 256 )
            versions[model->format_version]++;
        if( model->format_version >= 13 )
            scaled++;
        RSCache_ModelFree(model);
    }

    printf(
        "  %-14s %6d models, version >= 13 (scaleDown applies): %d (%.1f%%)\n",
        cache_dir,
        total,
        scaled,
        total ? 100.0 * scaled / total : 0.0);
    printf("      versions:");
    for( int v = 0; v < 256; v++ )
        if( versions[v] )
            printf(" %d x%d", v, versions[v]);
    printf("\n");

    RSCache_ReferenceTableFree(table);
    RSCache_Dat2DiskFree(disk);
    return 0;
}

int
main(int argc, char** argv)
{
    const char* root = RSCACHE_TEST_REPO_ROOT;
    const char* rev_name = NULL;
    const char* game_name = NULL;
    const char* epoch_name = NULL;
    const char* revision_text = NULL;
    const char* quirks_list = NULL;
    int positional = 0;
    const char* pos[4] = { NULL, NULL, NULL, NULL };

    for( int i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--rev") == 0 && i + 1 < argc )
        {
            rev_name = argv[++i];
            continue;
        }
        if( strcmp(argv[i], "--game") == 0 && i + 1 < argc )
        {
            game_name = argv[++i];
            continue;
        }
        if( strcmp(argv[i], "--epoch") == 0 && i + 1 < argc )
        {
            epoch_name = argv[++i];
            continue;
        }
        if( strcmp(argv[i], "--revision") == 0 && i + 1 < argc )
        {
            revision_text = argv[++i];
            continue;
        }
        if( strcmp(argv[i], "--quirks") == 0 && i + 1 < argc )
        {
            quirks_list = argv[++i];
            continue;
        }
        if( positional < 4 )
            pos[positional++] = argv[i];
    }

    if( positional > 0 )
        root = pos[0];

    struct RSCache default_profile;
    if( rev_name )
    {
        if( game_name || epoch_name || revision_text )
        {
            fprintf(stderr, "test_rs2_sweep: use either --rev or --game/--epoch/--revision\n");
            return 1;
        }
        if( !RSCache_ProfileByName(rev_name, &default_profile) )
        {
            fprintf(stderr, "test_rs2_sweep: unknown profile %s\n", rev_name);
            return 1;
        }
    }
    else if( game_name || epoch_name || revision_text )
    {
        if( !game_name || !epoch_name || !revision_text )
        {
            fprintf(stderr, "test_rs2_sweep: --game/--epoch/--revision are all required together\n");
            return 1;
        }
        int game = RSCache_GameFromName(game_name);
        int epoch = RSCache_EpochFromName(epoch_name);
        if( game == RSCACHE_GAME_UNSET || epoch == RSCACHE_EPOCH_UNSET )
        {
            fprintf(stderr, "test_rs2_sweep: bad game/epoch name\n");
            return 1;
        }
        uint32_t quirks = RSCACHE_QUIRK_NONE;
        if( quirks_list && !RSCache_QuirksFromList(quirks_list, &quirks) )
        {
            fprintf(stderr, "test_rs2_sweep: bad quirks list\n");
            return 1;
        }
        default_profile = RSCache_ProfileForIdentity(game, epoch, atoi(revision_text), quirks);
    }
    else if( !RSCache_ProfileByName("643", &default_profile) )
    {
        fprintf(stderr, "test_rs2_sweep: unknown profile 643\n");
        return 1;
    }

    if( positional > 1 && strcmp(pos[1], "modelvers") == 0 )
    {
        if( positional > 2 )
            return dump_model_versions(root, pos[2]);
        static const char* DIRS[] = { "cache.643", "cache.osrs230", "cache" };
        printf("ob3 format_version census\n");
        for( size_t i = 0; i < sizeof(DIRS) / sizeof(DIRS[0]); i++ )
            dump_model_versions(root, DIRS[i]);
        return 0;
    }

    if( positional > 2 && strcmp(pos[1], "loc") == 0 )
        return dump_loc(root, atoi(pos[2]));
    if( positional > 2 && strcmp(pos[1], "model") == 0 )
        return dump_model(root, atoi(pos[2]));
    if( positional > 3 && strcmp(pos[1], "spawns") == 0 )
        return dump_spawns(root, atoi(pos[2]), atoi(pos[3]));
    if( positional > 1 && strcmp(pos[1], "materials") == 0 )
        return dump_materials(root, positional > 2 ? atoi(pos[2]) : -1);

    static const char* CACHES[] = { "cache.643", "cache.rs643" };

    struct
    {
        const char* label;
        enum RSCache_Type type;
        sweep_visitor visit;
    } CASES[] = {
        { "loc", RSCACHE_TYPE_LOC, visit_loc },
        { "obj", RSCACHE_TYPE_OBJ, visit_obj },
        { "npc", RSCACHE_TYPE_NPC, visit_npc },
    };

    printf("rs2 sweep (root %s)\n", root);

    for( size_t c = 0; c < sizeof(CACHES) / sizeof(CACHES[0]); c++ )
    {
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", root, CACHES[c]);
        struct RSCache_Dat2Disk* disk;
        struct RSCache profile;
        if( !open_cache_with_identity(path, &default_profile, &disk, &profile) )
        {
            printf("  (no cache at %s)\n", path);
            continue;
        }

        for( size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++ )
        {
            struct sweep s;
            memset(&s, 0, sizeof(s));
            /* Visitors for datatypes without a `_consumed` field clear this. */
            s.measures_consumption = true;
            sweep_table(disk, &profile, CASES[i].type, CASES[i].visit, &s);
            sweep_report(CASES[i].label, CACHES[c], &s);
        }

        RSCache_Dat2DiskFree(disk);
    }

    return 0;
}
