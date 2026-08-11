/*
 * Dumps decoded per-bone transform values for every frame of a QBD sequence,
 * straight out of the shipped destination cache (the same cache.osrs239.rs2012
 * dir and profile the live client reads) — no rendering, no camera, no pose
 * math that could hide or fake a decode bug. One line per (frame, bone).
 *
 * Usage: rs2012_wake_dump --cache DIR --seq NAME
 *   NAME is a [rs2012_seq_NNNNN] section in
 *   OSRS-Content/osrs239-content/ported/rs2012_qbd_td/configs/rs2012.seq
 */
#include "datatypes/dat2_frame.h"
#include "datatypes/dat2_framemap.h"
#include "dat2disk.h"
#include "filelist.h"
#include "revisions/revisions.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int
seq_file_pos_for_id(struct RSCache_Dat2DiskArchive* archive, int file_id)
{
    for( int k = 0; k < archive->file_count; k++ )
        if( archive->file_ids[k] == file_id )
            return k;
    return -1;
}

int
main(int argc, char** argv)
{
    const char* cache_dir = NULL;
    const char* seq_cfg =
        "OSRS-Content/osrs239-content/ported/rs2012_qbd_td/configs/rs2012.seq";
    const char* seq_name = NULL;

    for( int i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--cache") == 0 && i + 1 < argc )
            cache_dir = argv[++i];
        else if( strcmp(argv[i], "--seq") == 0 && i + 1 < argc )
            seq_name = argv[++i];
        else if( strcmp(argv[i], "--seqcfg") == 0 && i + 1 < argc )
            seq_cfg = argv[++i];
    }
    if( !cache_dir || !seq_name )
    {
        fprintf(stderr, "usage: %s --cache DIR --seq rs2012_seq_16714\n", argv[0]);
        return 2;
    }

    /* Parse the frame id list for this seq straight out of the ported text
     * config — same source tools/rs2012_qbd_wake_frames.sh uses. */
    FILE* f = fopen(seq_cfg, "rb");
    if( !f )
    {
        fprintf(stderr, "cannot open %s\n", seq_cfg);
        return 2;
    }
    char header[256];
    snprintf(header, sizeof(header), "[%s]", seq_name);
    char line[512];
    int in_section = 0;
    int frame_ids[512];
    int frame_count = 0;
    while( fgets(line, sizeof(line), f) )
    {
        size_t n = strlen(line);
        while( n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r') )
            line[--n] = '\0';
        if( line[0] == '[' )
        {
            in_section = (strcmp(line, header) == 0);
            continue;
        }
        if( !in_section )
            continue;
        if( strncmp(line, "frame=", 6) == 0 )
        {
            int fid = 0, len = 0;
            if( sscanf(line + 6, "%d,%d", &fid, &len) == 2 && frame_count < 512 )
                frame_ids[frame_count++] = fid;
        }
    }
    fclose(f);
    if( frame_count == 0 )
    {
        fprintf(stderr, "seq %s: no frames found in %s\n", seq_name, seq_cfg);
        return 2;
    }
    fprintf(stderr, "%s: %d frames\n", seq_name, frame_count);

    struct RSCache_Dat2Disk* disk = RSCache_Dat2DiskNewFromDirectory(cache_dir);
    if( !disk )
    {
        fprintf(stderr, "cannot open cache %s\n", cache_dir);
        return 2;
    }
    struct RSCache profile =
        RSCache_ProfileForIdentity(RSCACHE_GAME_OLDSCHOOL, RSCACHE_EPOCH_DAT2, 239, 0u);
    RSCache_Dat2DiskSetProfile(disk, &profile);

    struct RSCache_Dat2Framemap* framemap = NULL;

    for( int fi = 0; fi < frame_count; fi++ )
    {
        int packed = frame_ids[fi];
        int group = packed >> 16;
        int file_id = packed & 0xFFFF;

        struct RSCache_Dat2DiskArchive* archive =
            RSCache_Dat2DiskArchiveNewLoad(disk, RSCACHE_DAT2_TABLE_ANIMATIONS, group);
        if( !archive )
        {
            printf("frame %d (%d): no animation group %d\n", fi, packed, group);
            continue;
        }
        RSCache_Dat2DiskArchiveInitMetadata(disk, archive);
        struct RSCache_FileList* files =
            RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
        int pos = files ? seq_file_pos_for_id(archive, file_id) : -1;
        if( !files || pos < 0 )
        {
            printf("frame %d (%d): file %d not in group %d\n", fi, packed, file_id, group);
            RSCache_FileListFree(files);
            RSCache_Dat2DiskArchiveFree(archive);
            continue;
        }

        if( !framemap )
        {
            int fm_id = RSCache_Dat2FramemapIdFromFrameArchive(files->files[pos], files->file_sizes[pos]);
            if( fm_id >= 0 )
                framemap = RSCache_Dat2FramemapNewFromCache(disk, fm_id);
            if( framemap )
            {
                fprintf(stderr, "framemap id=%d bones=%d\n", framemap->id, framemap->length);
                for( int b = 0; b < framemap->length; b++ )
                {
                    int glen = framemap->bone_groups_lengths[b];
                    int gmin = glen > 0 ? framemap->bone_groups[b][0] : -1;
                    int gmax = gmin;
                    for( int g = 1; g < glen; g++ )
                    {
                        int v = framemap->bone_groups[b][g];
                        if( v < gmin ) gmin = v;
                        if( v > gmax ) gmax = v;
                    }
                    fprintf(stderr, "  bone=%3d type=%d group_len=%d idx=[%d..%d]\n", b,
                            framemap->types[b], glen, gmin, gmax);
                }
            }
            else
                fprintf(stderr, "framemap: failed to resolve/load (fm_id=%d)\n", fm_id);
        }

        struct RSCache_Dat2Frame* frame = framemap ? RSCache_Dat2FrameNewDecodeProfile(
            &profile, packed, framemap, files->files[pos], files->file_sizes[pos]) : NULL;

        if( !frame )
        {
            printf("frame %d (%d): DECODE FAILED\n", fi, packed);
        }
        else
        {
            int maxabs = 0;
            for( int t = 0; t < frame->translator_count; t++ )
            {
                int ax = frame->translator_arg_x[t];
                int ay = frame->translator_arg_y[t];
                int az = frame->translator_arg_z[t];
                int a = abs(ax); if( abs(ay) > a ) a = abs(ay); if( abs(az) > a ) a = abs(az);
                if( a > maxabs ) maxabs = a;
            }
            printf("frame %d (%d): translators=%d flag_count=%d maxabs=%d\n",
                   fi, packed, frame->translator_count, frame->flag_count, maxabs);
            for( int t = 0; t < frame->translator_count; t++ )
            {
                int bone = frame->index_frame_ids[t];
                int type = (bone >= 0 && bone < framemap->length) ? framemap->types[bone] : -1;
                printf("  bone=%3d type=%d synth=%d  x=%6d y=%6d z=%6d\n",
                       bone, type, frame->synthesized[t],
                       frame->translator_arg_x[t], frame->translator_arg_y[t],
                       frame->translator_arg_z[t]);
            }
            RSCache_Dat2FrameFree(frame);
        }

        RSCache_FileListFree(files);
        RSCache_Dat2DiskArchiveFree(archive);
    }

    RSCache_Dat2FramemapFree(framemap);
    return 0;
}
