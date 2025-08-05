#include "osrs/cache.h"
#include "osrs/filelist.h"
#include "osrs/tables/config_npctype.h"
#include "osrs/tables/config_sequence.h"
#include "osrs/tables/configs.h"
#include "osrs/tables/frame.h"
#include "osrs/tables/framemap.h"
#include "osrs/tables/model.h"

#include <stdio.h>
#include <string.h>

int g_sin_table[2048];
int g_cos_table[2048];
int g_tan_table[2048];

int
main(int argc, char* argv[])
{
    struct Cache* cache = cache_new_from_directory(CACHE_PATH);
    if( !cache )
    {
        printf("Failed to load cache\n");
        return 1;
    }

    struct CacheArchive* config_archive_npctype =
        cache_archive_new_load(cache, CACHE_CONFIGS, CONFIG_NPC);
    struct FileList* config_npctype_filelist =
        filelist_new_from_cache_archive(config_archive_npctype);

    struct CacheConfigNPCType* npc_tztok_jad = NULL;
    for( int i = 0; i < config_npctype_filelist->file_count; i++ )
    {
        struct CacheConfigNPCType* npc = config_npctype_new_decode(
            config_archive_npctype->revision,
            config_npctype_filelist->files[i],
            config_npctype_filelist->file_sizes[i]);
        if( npc->name && strcmp(npc->name, "TzTok-Jad") == 0 )
        {
            npc_tztok_jad = npc;
            printf("Found TzTok-Jad\n");
            print_npc_type(npc);
        }
        else
            config_npctype_free(npc);
    }

    if( !npc_tztok_jad )
    {
        printf("Failed to find TzTok-Jad\n");
        return 1;
    }

    if( npc_tztok_jad->models_count == 0 )
    {
        printf("TzTok-Jad has no models\n");
        return 1;
    }

    struct CacheModel* model = model_new_from_cache(cache, npc_tztok_jad->models[0]);
    if( !model )
    {
        printf("Failed to load model\n");
        return 1;
    }

    struct CacheModelBones* bones =
        modelbones_new_decode(model->vertex_bone_map, model->vertex_count);
    if( !bones )
    {
        printf("Failed to load bones\n");
        return 1;
    }

    struct CacheArchive* config_archive_sequence =
        cache_archive_new_load(cache, CACHE_CONFIGS, CONFIG_SEQUENCE);
    struct FileList* config_sequence_filelist =
        filelist_new_from_cache_archive(config_archive_sequence);

    int sequence_id = npc_tztok_jad->standing_animation;

    struct CacheConfigSequence* sequence_tztok_jad_standing = config_sequence_new_decode(
        config_archive_sequence->revision,
        config_sequence_filelist->files[sequence_id],
        config_sequence_filelist->file_sizes[sequence_id]);

    int frame_archive_id = 0;
    for( int i = 0; i < sequence_tztok_jad_standing->frame_count; i++ )
    {
        // Get the frame definition ID from the second 2 bytes of the sequence frame ID
        // The first 2 bytes are the sequence ID, the second 2 bytes are the frame archive ID
        int frame_id = sequence_tztok_jad_standing->frame_ids[i];
        frame_archive_id = (frame_id >> 16) & 0xFFFF;
        int index_in_sequence = frame_id & 0xFFFF;
        printf("Frame %d: %d -> %d\n", i, frame_archive_id, index_in_sequence);
    }

    struct CacheArchive* frame_archive =
        cache_archive_new_load(cache, CACHE_ANIMATIONS, frame_archive_id);
    struct FileList* frame_filelist = filelist_new_from_cache_archive(frame_archive);

    for( int i = 0; i < frame_filelist->file_count; i++ )
    {
        char* frame_data = frame_filelist->files[i];
        int frame_data_size = frame_filelist->file_sizes[i];
        int framemap_id = framemap_id_from_frame_archive(frame_data, frame_data_size);

        struct CacheFramemap* framemap =
            framemap_new_decode2(framemap_id, frame_data, frame_data_size);

        struct CacheFrame* frame =
            frame_new_decode2(frame_archive_id, framemap, frame_data, frame_data_size);

        printf("Frame %d: %d + %d\n", i, frame->id, frame->framemap_id);
    }

    modelbones_free(bones);
    config_sequence_free(sequence_tztok_jad_standing);
    config_npctype_free(npc_tztok_jad);
    filelist_free(config_npctype_filelist);
    filelist_free(config_sequence_filelist);
    cache_archive_free(config_archive_npctype);
    cache_archive_free(config_archive_sequence);
    model_free(model);
    cache_free(cache);

    return 0;
}