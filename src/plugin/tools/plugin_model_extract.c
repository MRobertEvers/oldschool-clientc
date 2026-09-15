/*
 * Lift one model out of a cache and write it where a plugin can load it.
 *
 * A plugin that wants a piece of geometry has two ways to get one: author it
 * (api.mesh_*) or ship it (api.model_load, which reads the plugin's own asset
 * folder). This tool produces the second kind. It exists as a committed,
 * repeatable step rather than as something somebody did once by hand, because
 * a binary in the tree with no recorded provenance is a binary nobody can
 * refresh, re-point at another revision, or check.
 *
 * The bytes written are the archive payload EXACTLY as the cache holds it --
 * no transcode, no re-encode. That is what makes the shipped file portable:
 * RSCache_ModelNewDecode sniffs the format off the file's own trailer magic
 * (OB2 / OB3 / V2 / V3) and never consults a revision profile, so a model
 * lifted from one cache decodes the same way under a client booted on any
 * other. The file's format is a property of the file, which is the whole
 * reason this is a copy and not a conversion.
 *
 * What it will NOT do quietly is ship geometry whose look depends on the cache
 * it came from. A textured face names a texture id, and texture ids are a
 * revision's own numbering: the same id is a different picture, or nothing, in
 * the next cache. So a textured model is reported and refused unless
 * --allow-textures says the caller means it.
 *
 * A RIGGED model is the case that needs care, and it is the common one for
 * anything that reads as an effect. Its stored vertices are a BIND POSE, which
 * is not a shape the game ever draws -- the shape is a frame of a sequence
 * applied to that rig. Shipping such a model as-is gets whatever the artist
 * left the vertices at, which for both loot beams is a collapsed spike.
 *
 * --pose SEQ [--pose-frame N] is the answer: apply one frame of a sequence and
 * write the POSED geometry, so the file carries the shape the game shows and
 * needs no rig, no frames and no framemap at runtime. It costs the animation
 * -- a static file cannot move -- which is a trade the caller makes knowingly,
 * and the reason the flag is explicit rather than automatic.
 *
 *   plugin_model_extract --cache DIR --rev NAME --model ID --out FILE
 *                        [--pose SEQ [--pose-frame N]] [--allow-textures]
 */

#include "datatypes/dat2_config_sequence.h"
#include "datatypes/dat2_configs.h"
#include "datatypes/dat2_frame.h"
#include "datatypes/dat2_framemap.h"
#include "datatypes/model.h"
#include "dat2disk.h"
#include "filelist.h"
#include "revisions/revisions.h"
#include "rscache_profile.h"

#include "engine/toridraw_animation_from_rscache.h"
#include "engine/toridraw_model_from_torirs.h"
#include "engine/torirs_model_from_rscache.h"
#include "engine/torirs_types.h"

#include "toridraw_animation.h"
#include "toridraw_model.h"
#include "toridraw_types.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
usage(void)
{
    fprintf(
        stderr,
        "usage: plugin_model_extract --cache DIR --rev NAME --model ID --out FILE\n"
        "                            [--pose SEQ [--pose-frame N]] [--allow-textures]\n");
}

/* Frame file ids are neither dense nor 0-based, so a file is found by its id in
 * the archive's own list rather than by position. Same resolution the client's
 * sequence loader does. */
static int
file_pos_for_id(struct RSCache_Dat2DiskArchive const* archive, int file_id)
{
    if( !archive->file_ids )
        return file_id;
    for( int i = 0; i < archive->file_count; i++ )
        if( archive->file_ids[i] == file_id )
            return i;
    return -1;
}

/*
 * One frame of `seq_id`, as a ToriDraw animation carrying just that frame.
 *
 * Deliberately loads ONE frame rather than the whole sequence: the caller is
 * choosing a pose, and decoding thirty frames to throw away twenty-nine is
 * work with nothing to show for it. Returns NULL with a reason printed.
 */
static struct ToriDraw_Animation*
pose_frame_load(
    struct RSCache_Dat2Disk* disk,
    struct RSCache const* profile,
    int seq_id,
    int frame_index,
    int* out_frame_count)
{
    struct RSCache_Dat2DiskArchive* config = NULL;
    struct RSCache_Dat2ConfigSequence* seq = NULL;
    struct RSCache_Dat2DiskArchive* frame_archive = NULL;
    struct RSCache_FileList* filelist = NULL;
    struct RSCache_Dat2DiskArchive* fm_archive = NULL;
    struct RSCache_Dat2Framemap* framemap = NULL;
    struct RSCache_Dat2Frame* frame = NULL;
    struct ToriDraw_Animation* anim = NULL;
    int delay = 0;
    int frame_id;
    int file_pos;
    int framemap_id;

    config = RSCache_Dat2DiskArchiveNewLoad(
        disk,
        RSCache_Dat2DiskTableId(disk, RSCACHE_DAT2_TABLE_CONFIGS),
        RSCACHE_DAT2_CONFIG_KIND_SEQUENCE);
    if( !config )
    {
        fprintf(stderr, "plugin_model_extract: cache has no sequence config table\n");
        goto done;
    }
    /* An archive knows how many files it holds only from the table's reference
     * metadata, which the plain load does not read. Without this every
     * multi-file archive answers "0 files" and every lookup in it misses. */
    RSCache_Dat2DiskArchiveInitMetadata(disk, config);
    {
        struct RSCache_FileList* configs = RSCache_FileListNewFromDecode(
            config->data, config->data_size, config->file_count);
        int pos = configs ? file_pos_for_id(config, seq_id) : -1;
        if( !configs || pos < 0 || pos >= configs->file_count )
        {
            fprintf(stderr, "plugin_model_extract: cache has no sequence %d\n", seq_id);
            if( configs )
                RSCache_FileListFree(configs);
            goto done;
        }
        seq = RSCache_Dat2ConfigSequenceNewDecodeProfile(
            profile, configs->files[pos], configs->file_sizes[pos]);
        RSCache_FileListFree(configs);
    }
    if( !seq || seq->frame_count <= 0 || !seq->frame_ids )
    {
        fprintf(stderr, "plugin_model_extract: sequence %d has no frames\n", seq_id);
        goto done;
    }
    if( out_frame_count )
        *out_frame_count = seq->frame_count;
    if( frame_index < 0 || frame_index >= seq->frame_count )
    {
        fprintf(
            stderr,
            "plugin_model_extract: sequence %d has %d frame(s); %d is out of range\n",
            seq_id,
            seq->frame_count,
            frame_index);
        goto done;
    }

    frame_id = seq->frame_ids[frame_index];
    if( frame_id < 0 )
    {
        fprintf(
            stderr,
            "plugin_model_extract: sequence %d frame %d is empty\n",
            seq_id,
            frame_index);
        goto done;
    }
    if( seq->frame_lengths )
        delay = seq->frame_lengths[frame_index];

    frame_archive = RSCache_Dat2DiskArchiveNewLoad(
        disk,
        RSCache_Dat2DiskTableId(disk, RSCACHE_DAT2_TABLE_ANIMATIONS),
        (frame_id >> 16) & 0xFFFF);
    if( !frame_archive )
    {
        fprintf(stderr, "plugin_model_extract: frame archive %d absent\n", frame_id >> 16);
        goto done;
    }
    RSCache_Dat2DiskArchiveInitMetadata(disk, frame_archive);
    filelist = RSCache_FileListNewFromDecode(
        frame_archive->data, frame_archive->data_size, frame_archive->file_count);
    file_pos = filelist ? file_pos_for_id(frame_archive, frame_id & 0xFFFF) : -1;
    if( !filelist || file_pos < 0 || file_pos >= filelist->file_count )
    {
        fprintf(stderr, "plugin_model_extract: frame %d absent from its archive\n", frame_id);
        goto done;
    }

    /* The framemap id rides in the frame file's first two bytes. */
    framemap_id = RSCache_Dat2FrameFramemapIdFromFileProfile(
        profile, filelist->files[file_pos], filelist->file_sizes[file_pos]);
    fm_archive = RSCache_Dat2DiskArchiveNewLoad(
        disk, RSCache_Dat2DiskTableId(disk, RSCACHE_DAT2_TABLE_SKELETONS), framemap_id);
    if( !fm_archive )
    {
        fprintf(stderr, "plugin_model_extract: framemap %d absent\n", framemap_id);
        goto done;
    }
    framemap = RSCache_Dat2FramemapNewFromArchiveProfile(profile, fm_archive, framemap_id);
    if( !framemap )
    {
        fprintf(stderr, "plugin_model_extract: framemap %d will not decode\n", framemap_id);
        goto done;
    }
    frame = RSCache_Dat2FrameNewDecodeProfile(
        profile,
        frame_id,
        framemap,
        filelist->files[file_pos],
        filelist->file_sizes[file_pos]);
    if( !frame )
    {
        fprintf(stderr, "plugin_model_extract: frame %d will not decode\n", frame_id);
        goto done;
    }

    {
        struct RSCache_Dat2Frame const* one[1] = { frame };
        anim = ToriDraw_AnimationFromRSCache(framemap, one, &delay, 1, 0);
    }
    if( !anim )
        fprintf(stderr, "plugin_model_extract: frame %d will not assemble\n", frame_id);

done:
    if( frame )
        RSCache_Dat2FrameFree(frame);
    if( framemap )
        RSCache_Dat2FramemapFree(framemap);
    if( fm_archive )
        RSCache_Dat2DiskArchiveFree(fm_archive);
    if( filelist )
        RSCache_FileListFree(filelist);
    if( frame_archive )
        RSCache_Dat2DiskArchiveFree(frame_archive);
    if( seq )
        RSCache_Dat2ConfigSequenceFree(seq);
    if( config )
        RSCache_Dat2DiskArchiveFree(config);
    return anim;
}

/*
 * Pose `target` in place: decode a second copy of the model, animate it, and
 * write the resulting vertex positions back over the first.
 *
 * A second copy because ToriRS_ModelFromRSCache MOVES the arrays out of what it
 * is handed -- the copy that gets animated is hollowed out on the way, and the
 * one being re-encoded has to survive with its provenance intact.
 *
 * Returns 0 with a reason printed.
 */
static int
pose_apply(
    struct RSCache_Model* target,
    uint8_t* data,
    int data_size,
    struct ToriDraw_Animation const* anim)
{
    struct RSCache_Model* scratch_rs = RSCache_ModelNewDecode(data, data_size);
    struct ToriRS_Model* scratch = scratch_rs ? ToriRS_ModelFromRSCache(scratch_rs) : NULL;
    struct ToriDraw_Model* posed = scratch ? ToriDraw_ModelFromToriRS(scratch) : NULL;
    int ok = 0;

    if( !posed )
    {
        fprintf(stderr, "plugin_model_extract: cannot build a posable copy\n");
        goto done;
    }
    if( !posed->vertex_bones )
    {
        fprintf(
            stderr,
            "plugin_model_extract: the model carries no rig, so a sequence has nothing "
            "to drive -- drop --pose\n");
        goto done;
    }
    if( posed->vertex_count != target->vertex_count )
    {
        fprintf(stderr, "plugin_model_extract: posed copy disagrees on vertex count\n");
        goto done;
    }

    ToriDraw_ModelCaptureOriginalVertices(posed);
    ToriDraw_ModelAnimateFrame(posed, anim->base, &anim->frames[0]);

    for( int i = 0; i < target->vertex_count; i++ )
    {
        target->vertices_x[i] = posed->vertices_x[i];
        target->vertices_y[i] = posed->vertices_y[i];
        target->vertices_z[i] = posed->vertices_z[i];
    }
    ok = 1;

done:
    if( posed )
        ToriDraw_ModelFree(posed);
    else if( scratch )
        ToriRS_ModelFree(scratch);
    if( scratch_rs )
        RSCache_ModelFree(scratch_rs);
    return ok;
}

/* Everything the caller needs told about what it just lifted. A model is being
 * frozen into the tree here, so the counts go on the record: a later reader
 * comparing the file against the cache has something to compare. */
static void
report(char const* out, int model_id, struct RSCache_Model const* model, int size)
{
    int min_x = 0, max_x = 0, min_y = 0, max_y = 0, min_z = 0, max_z = 0;

    for( int i = 0; i < model->vertex_count; i++ )
    {
        if( i == 0 || model->vertices_x[i] < min_x ) min_x = model->vertices_x[i];
        if( i == 0 || model->vertices_x[i] > max_x ) max_x = model->vertices_x[i];
        if( i == 0 || model->vertices_y[i] < min_y ) min_y = model->vertices_y[i];
        if( i == 0 || model->vertices_y[i] > max_y ) max_y = model->vertices_y[i];
        if( i == 0 || model->vertices_z[i] < min_z ) min_z = model->vertices_z[i];
        if( i == 0 || model->vertices_z[i] > max_z ) max_z = model->vertices_z[i];
    }
    printf(
        "%s <- model %d: %d bytes, %d vertices, %d faces, %d textured, %s\n",
        out,
        model_id,
        size,
        model->vertex_count,
        model->face_count,
        model->textured_face_count,
        /* Whether the file is RIGGED is the thing a plugin author has to check
         * before shipping it. What gets written is the BIND POSE, and for a
         * rigged model that may or may not be the shape the game shows: both
         * loot beams turn out to be authored at full size and animated only
         * within that envelope, while an effect that GROWS is stored collapsed
         * and ships as a spike. The extents below are how the two are told
         * apart -- compare them against a posed extract. */
        model->vertex_bone_map ? "rigged (bind pose written; --pose bakes a frame instead)"
                               : "unrigged");
    /* The extents are how a caller choosing a POSE tells the frames apart: an
     * effect's frames differ mostly in size, and 128 units is one tile. */
    printf(
        "    extent x %d..%d, y %d..%d, z %d..%d (%d wide, %d tall; 128 = one tile)\n",
        min_x,
        max_x,
        min_y,
        max_y,
        min_z,
        max_z,
        max_x - min_x,
        max_y - min_y);
}

int
main(int argc, char** argv)
{
    char const* cache_dir = NULL;
    char const* rev = NULL;
    char const* out_path = NULL;
    int model_id = -1;
    int allow_textures = 0;
    int pose_seq = -1;
    int pose_frame = 0;
    int pose_frame_count = 0;
    struct ToriDraw_Animation* pose = NULL;
    uint8_t* out_bytes = NULL;
    uint32_t out_size = 0;
    struct RSCache profile;
    struct RSCache_Dat2Disk* disk = NULL;
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct RSCache_Model* model = NULL;
    FILE* out = NULL;
    int table;
    int rc = 1;

    for( int i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--cache") == 0 && i + 1 < argc )
            cache_dir = argv[++i];
        else if( strcmp(argv[i], "--rev") == 0 && i + 1 < argc )
            rev = argv[++i];
        else if( strcmp(argv[i], "--out") == 0 && i + 1 < argc )
            out_path = argv[++i];
        else if( strcmp(argv[i], "--model") == 0 && i + 1 < argc )
            model_id = atoi(argv[++i]);
        else if( strcmp(argv[i], "--pose") == 0 && i + 1 < argc )
            pose_seq = atoi(argv[++i]);
        else if( strcmp(argv[i], "--pose-frame") == 0 && i + 1 < argc )
            pose_frame = atoi(argv[++i]);
        else if( strcmp(argv[i], "--allow-textures") == 0 )
            allow_textures = 1;
        else
        {
            fprintf(stderr, "plugin_model_extract: unknown argument '%s'\n", argv[i]);
            usage();
            return 1;
        }
    }
    if( !cache_dir || !rev || !out_path || model_id < 0 )
    {
        usage();
        return 1;
    }

    if( !RSCache_ProfileByName(rev, &profile) )
    {
        fprintf(stderr, "plugin_model_extract: unknown revision '%s'\n", rev);
        return 1;
    }

    disk = RSCache_Dat2DiskNewReadOnlyFromDirectory(cache_dir);
    if( !disk )
    {
        fprintf(stderr, "plugin_model_extract: cannot open cache '%s'\n", cache_dir);
        return 1;
    }
    RSCache_Dat2DiskSetProfile(disk, &profile);

    table = RSCache_Dat2DiskTableId(disk, RSCACHE_DAT2_TABLE_MODELS);
    archive = RSCache_Dat2DiskArchiveNewLoad(disk, table, model_id);
    if( !archive || archive->data_size <= 0 )
    {
        fprintf(
            stderr,
            "plugin_model_extract: %s has no model %d\n",
            cache_dir,
            model_id);
        goto done;
    }

    /* Decoded only to be checked and described. The bytes that get written are
     * the archive's, untouched. */
    model = RSCache_ModelNewDecode((uint8_t*)archive->data, archive->data_size);
    if( !model )
    {
        fprintf(
            stderr,
            "plugin_model_extract: model %d does not decode (%d bytes)\n",
            model_id,
            archive->data_size);
        goto done;
    }
    if( model->textured_face_count > 0 && !allow_textures )
    {
        fprintf(
            stderr,
            "plugin_model_extract: model %d has %d textured face(s), and a texture id "
            "means a different picture in every cache -- pass --allow-textures if the "
            "plugin shipping this is meant to look different per revision\n",
            model_id,
            model->textured_face_count);
        goto done;
    }

    /*
     * Unposed, the archive's own bytes go out untouched -- a copy, not a
     * conversion. Posed, the geometry has changed and has to be written back
     * out as a model; RSCache_ModelEncodeFormat states that it produces a
     * valid model that decodes back to an equal struct, which is exactly the
     * bar a shipped asset has to clear.
     */
    if( pose_seq >= 0 )
    {
        pose = pose_frame_load(
            disk, &profile, pose_seq, pose_frame, &pose_frame_count);
        if( !pose )
            goto done;
        if( !pose_apply(model, (uint8_t*)archive->data, archive->data_size, pose) )
            goto done;

        out_size = RSCache_ModelEncodeBound(model, NULL);
        out_bytes = malloc(out_size);
        assert(out_bytes);
        out_size = RSCache_ModelEncodeFormat(
            model, NULL, RSCACHE_MODEL_FORMAT_OB2, out_bytes, out_size);
        if( out_size == 0 )
        {
            fprintf(
                stderr,
                "plugin_model_extract: the posed model %d will not re-encode\n",
                model_id);
            goto done;
        }
    }

    out = fopen(out_path, "wb");
    if( !out )
    {
        fprintf(stderr, "plugin_model_extract: cannot write '%s'\n", out_path);
        goto done;
    }
    {
        void const* bytes = out_bytes ? (void const*)out_bytes : (void const*)archive->data;
        size_t const size = out_bytes ? (size_t)out_size : (size_t)archive->data_size;
        if( fwrite(bytes, 1, size, out) != size )
        {
            fprintf(stderr, "plugin_model_extract: short write to '%s'\n", out_path);
            goto done;
        }
        report(out_path, model_id, model, (int)size);
    }
    if( pose_seq >= 0 )
        printf(
            "    posed by sequence %d frame %d of %d; the rig is baked away and the "
            "file is static\n",
            pose_seq,
            pose_frame,
            pose_frame_count);
    rc = 0;

done:
    if( out )
        fclose(out);
    free(out_bytes);
    if( pose )
        ToriDraw_AnimationFree(pose);
    if( model )
        RSCache_ModelFree(model);
    if( archive )
        RSCache_Dat2DiskArchiveFree(archive);
    RSCache_Dat2DiskFree(disk);
    return rc;
}
