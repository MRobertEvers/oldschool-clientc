/* Oracle dump for NPC skeletal pipeline (3draster runtime path). */
#include "osrs/rscache/dat2a/dat2a_animaya.h"
#include "osrs/rscache/dat2a/dat2a_config_npctype.h"
#include "osrs/rscache/dat2a/dat2a_config_sequence.h"
#include "osrs/rscache/dat2a/dat2a_configs.h"
#include "osrs/rscache/dat2a/dat2a_model.h"
#include "osrs/rscache/dat2a/dat2a_skeletalbase.h"
#include "osrs/rscache/dat2disk/dat2disk.h"
#include "osrs/rscache/shared/shared_file_list.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    int* vertices_x;
    int* vertices_y;
    int* vertices_z;
    int vertex_count;

    int animaya_vertex_count;
    uint8_t* animaya_group_counts;
    uint8_t** animaya_groups;
    uint8_t** animaya_scales;
} DumpModel;

static struct RSCacheDat2Disk_ArchiveReference*
archive_reference(
    struct RSCacheDat2Disk* cache,
    struct RSCacheDat2Disk_Archive* archive)
{
    struct RSCacheDat2Disk_ReferenceTable* table;

    if( !cache || !archive )
        return NULL;

    RSCacheDat2Disk_ArchiveInitMetadata(cache, archive);
    table = cache->tables[archive->table_id];
    if( !table || archive->archive_id < 0 || archive->archive_id >= table->archive_count )
        return NULL;

    return &table->archives[archive->archive_id];
}

static char*
load_config_file(
    struct RSCacheDat2Disk* cache,
    int config_kind,
    int file_id,
    int* out_size,
    int* out_revision)
{
    struct RSCacheDat2Disk_Archive* archive = RSCacheDat2Disk_ArchiveNewLoad(
        cache, RSCacheDat2Disk_Table_Configs, config_kind);
    struct RSCacheDat2Disk_ArchiveReference* archive_ref;
    struct RSCacheShared_FileList* filelist;
    char* data = NULL;

    if( !archive )
        return NULL;

    archive_ref = archive_reference(cache, archive);
    filelist = RSCacheShared_FileListNewFromCacheArchive(archive);
    if( !archive_ref || !filelist )
        goto cleanup;

    if( out_revision )
        *out_revision = archive->revision;

    for( int i = 0; i < filelist->file_count; i++ )
    {
        if( archive_ref->children.files[i].id != file_id )
            continue;
        *out_size = filelist->file_sizes[i];
        data = malloc((size_t)filelist->file_sizes[i]);
        if( data )
            memcpy(data, filelist->files[i], (size_t)filelist->file_sizes[i]);
        break;
    }

    RSCacheShared_FileListFree(filelist);

cleanup:
    RSCacheDat2Disk_ArchiveFree(archive);
    return data;
}

static int
skinned_vertex_count(const struct RSCacheDat2A_Model* model)
{
    if( !model || !model->animaya_group_counts )
        return 0;

    int count = 0;
    int vc = model->animaya_vertex_count > 0 ? model->animaya_vertex_count : model->vertex_count;
    for( int i = 0; i < vc; i++ )
    {
        if( model->animaya_group_counts[i] > 0 )
            count++;
    }
    return count;
}

static void
dump_model_free(DumpModel* m)
{
    if( !m )
        return;
    free(m->vertices_x);
    free(m->vertices_y);
    free(m->vertices_z);
    if( m->animaya_group_counts )
    {
        for( int i = 0; i < m->animaya_vertex_count; i++ )
        {
            free(m->animaya_groups ? m->animaya_groups[i] : NULL);
            free(m->animaya_scales ? m->animaya_scales[i] : NULL);
        }
    }
    free(m->animaya_group_counts);
    free(m->animaya_groups);
    free(m->animaya_scales);
    memset(m, 0, sizeof(*m));
}

static int
dump_model_merge_concat(DumpModel* out, struct RSCacheDat2A_Model** pieces, int piece_count)
{
    int total_vertices = 0;
    int has_animaya = 0;

    memset(out, 0, sizeof(*out));

    for( int i = 0; i < piece_count; i++ )
    {
        if( !pieces[i] )
            continue;
        total_vertices += pieces[i]->vertex_count;
        if( pieces[i]->animaya_group_counts && pieces[i]->animaya_groups && pieces[i]->animaya_scales )
            has_animaya = 1;
    }

    if( total_vertices <= 0 )
        return 0;

    out->vertex_count = total_vertices;
    out->vertices_x = malloc((size_t)total_vertices * sizeof(int));
    out->vertices_y = malloc((size_t)total_vertices * sizeof(int));
    out->vertices_z = malloc((size_t)total_vertices * sizeof(int));
    if( !out->vertices_x || !out->vertices_y || !out->vertices_z )
        return -1;

    if( has_animaya )
    {
        out->animaya_vertex_count = total_vertices;
        out->animaya_group_counts = calloc((size_t)total_vertices, sizeof(uint8_t));
        out->animaya_groups = calloc((size_t)total_vertices, sizeof(uint8_t*));
        out->animaya_scales = calloc((size_t)total_vertices, sizeof(uint8_t*));
        if( !out->animaya_group_counts || !out->animaya_groups || !out->animaya_scales )
            return -1;
    }

    int vtx_off = 0;
    for( int i = 0; i < piece_count; i++ )
    {
        struct RSCacheDat2A_Model* m = pieces[i];
        if( !m )
            continue;

        for( int v = 0; v < m->vertex_count; v++ )
        {
            int dst = vtx_off + v;
            out->vertices_x[dst] = m->vertices_x[v];
            out->vertices_y[dst] = m->vertices_y[v];
            out->vertices_z[dst] = m->vertices_z[v];

            if( out->animaya_group_counts && m->animaya_group_counts )
            {
                int cnt = (int)m->animaya_group_counts[v];
                out->animaya_group_counts[dst] = (uint8_t)cnt;
                if( cnt > 0 && m->animaya_groups && m->animaya_scales )
                {
                    out->animaya_groups[dst] = malloc((size_t)cnt);
                    out->animaya_scales[dst] = malloc((size_t)cnt);
                    if( !out->animaya_groups[dst] || !out->animaya_scales[dst] )
                        return -1;
                    memcpy(out->animaya_groups[dst], m->animaya_groups[v], (size_t)cnt);
                    memcpy(out->animaya_scales[dst], m->animaya_scales[v], (size_t)cnt);
                }
            }
        }
        vtx_off += m->vertex_count;
    }

    return 0;
}

static void
apply_skeletal_vertex(
    int* out_x,
    int* out_y,
    int* out_z,
    int ox,
    int oy,
    int oz,
    const DumpModel* model,
    int vi,
    const float* frame_matrices,
    int bone_count)
{
    int cnt = model->animaya_group_counts ? (int)model->animaya_group_counts[vi] : 0;
    if( cnt <= 0 )
    {
        *out_x = ox;
        *out_y = oy;
        *out_z = oz;
        return;
    }

    float fx = (float)ox;
    float fy = -(float)oy;
    float fz = -(float)oz;
    float bx = 0.0f, by = 0.0f, bz = 0.0f;
    int contributed = 0;

    for( int j = 0; j < cnt; j++ )
    {
        int bone = model->animaya_groups[vi] ? (int)model->animaya_groups[vi][j] : 0;
        int scale = model->animaya_scales[vi] ? (int)model->animaya_scales[vi][j] : 0;
        if( bone < 0 || bone >= bone_count || scale == 0 )
            continue;

        const float* M = &frame_matrices[bone * 16];
        int finite = 1;
        for( int k = 0; k < 16; k++ )
        {
            if( !isfinite(M[k]) )
            {
                finite = 0;
                break;
            }
        }
        if( !finite )
            continue;

        float w = (float)scale / 255.0f;
        bx += w * (M[0] * fx + M[4] * fy + M[8] * fz + M[12]);
        by += w * (M[1] * fx + M[5] * fy + M[9] * fz + M[13]);
        bz += w * (M[2] * fx + M[6] * fy + M[10] * fz + M[14]);
        contributed = 1;
    }

    if( contributed )
    {
        *out_x = (int)lroundf(bx);
        *out_y = (int)-lroundf(by);
        *out_z = (int)-lroundf(bz);
    }
    else
    {
        *out_x = ox;
        *out_y = oy;
        *out_z = oz;
    }
}

static void
fprintf_json_string_array(
    FILE* fp,
    const uint8_t* arr,
    int count)
{
    fputc('[', fp);
    for( int i = 0; i < count; i++ )
    {
        if( i > 0 )
            fputc(',', fp);
        fprintf(fp, "%d", (int)arr[i]);
    }
    fputc(']', fp);
}

int
main(int argc, char** argv)
{
    const char* cache_dir = argc > 1 ? argv[1] : "cache";
    int npc_id = argc > 2 ? atoi(argv[2]) : 12204;
    const char* out_path = argc > 3 ? argv[3] : "/tmp/ours_49222.json";

    struct RSCacheDat2Disk* cache = RSCacheDat2Disk_NewFromDirectory(cache_dir);
    if( !cache )
    {
        fprintf(stderr, "Failed to open cache: %s\n", cache_dir);
        return 1;
    }

    int npc_size = 0, npc_revision = 0;
    char* npc_data = load_config_file(
        cache, RSCacheDat2A_ConfigKind_Npc, npc_id, &npc_size, &npc_revision);
    if( !npc_data )
    {
        fprintf(stderr, "Failed to load NPC %d\n", npc_id);
        RSCacheDat2Disk_Free(cache);
        return 1;
    }

    struct RSCacheDat2A_ConfigNpctype* npc =
        RSCacheDat2A_ConfigNpctypeNewDecode(npc_revision, npc_data, npc_size);
    free(npc_data);
    if( !npc )
    {
        fprintf(stderr, "Failed to decode NPC %d\n", npc_id);
        RSCacheDat2Disk_Free(cache);
        return 1;
    }

    int seq_size = 0, seq_revision = 0;
    char* seq_data = load_config_file(
        cache,
        RSCacheDat2A_ConfigKind_Sequence,
        npc->standing_animation,
        &seq_size,
        &seq_revision);
    struct RSCacheDat2A_ConfigSequence* seq = NULL;
    if( seq_data )
    {
        seq = RSCacheDat2A_ConfigSequenceNewDecode(seq_revision, seq_data, seq_size);
        free(seq_data);
    }

    struct RSCacheDat2A_Model** pieces = calloc((size_t)npc->models_count, sizeof(*pieces));
    if( !pieces )
        return 1;

    FILE* fp = fopen(out_path, "w");
    if( !fp )
    {
        fprintf(stderr, "Failed to open %s\n", out_path);
        return 1;
    }

    fprintf(fp, "{\n");
    fprintf(fp, "  \"source\": \"3draster\",\n");
    fprintf(fp, "  \"npcId\": %d,\n", npc_id);
    fprintf(fp, "  \"revision\": %d,\n", npc_revision);
    fprintf(fp, "  \"modelIds\": [");
    for( int i = 0; i < npc->models_count; i++ )
    {
        if( i > 0 )
            fputc(',', fp);
        fprintf(fp, "%d", npc->models[i]);
    }
    fprintf(fp, "],\n");

    fprintf(fp, "  \"pieces\": [\n");
    for( int i = 0; i < npc->models_count; i++ )
    {
        int model_id = npc->models[i];
        pieces[i] = RSCacheDat2A_ModelNewFromCache(cache, model_id);
        int vc = pieces[i] ? pieces[i]->vertex_count : 0;
        int skinned = skinned_vertex_count(pieces[i]);
        int has_maya = pieces[i] && pieces[i]->animaya_group_counts ? 1 : 0;
        fprintf(
            fp,
            "    {\"modelId\":%d,\"verticesCount\":%d,\"hasMayaGroups\":%s,\"skinnedVertexCount\":%d}%s\n",
            model_id,
            vc,
            has_maya ? "true" : "false",
            skinned,
            i + 1 < npc->models_count ? "," : "");
    }
    fprintf(fp, "  ],\n");

    DumpModel merged = { 0 };
    if( dump_model_merge_concat(&merged, pieces, npc->models_count) != 0 )
    {
        fprintf(stderr, "Merge failed\n");
        return 1;
    }

    int merged_skinned = 0;
    for( int i = 0; i < merged.animaya_vertex_count; i++ )
    {
        if( merged.animaya_group_counts && merged.animaya_group_counts[i] > 0 )
            merged_skinned++;
    }

    fprintf(
        fp,
        "  \"merged\": {\"verticesCount\":%d,\"skinnedVertexCount\":%d},\n",
        merged.vertex_count,
        merged_skinned);

    int anim_maya_id = seq ? seq->anim_maya_id : -1;
  int anim_maya_start = seq ? seq->anim_maya_start : 0;
  int anim_maya_end = seq ? seq->anim_maya_end : 0;
    fprintf(
        fp,
        "  \"seq\": {\"seqId\":%d,\"skeletalId\":%d,\"skeletalStart\":%d,\"skeletalEnd\":%d},\n",
        npc->standing_animation,
        anim_maya_id,
        anim_maya_start,
        anim_maya_end);

    float* palette = NULL;
    int frame_count = 0, bone_count = 0;
    if( anim_maya_id >= 0 )
    {
        struct RSCacheDat2A_AnimMaya* maya =
            RSCacheDat2A_AnimMayaNewFromCache(cache, anim_maya_id);
        struct RSCacheDat2A_SkeletalBase* base = maya
            ? RSCacheDat2A_SkeletalBaseNewFromCache(cache, maya->base_id)
            : NULL;
        if( maya && base )
            palette = RSCacheDat2A_SkeletalBaseBakePalette(maya, base, &frame_count, &bone_count);

        fprintf(fp, "  \"boneFinalMatrices\": {\n");
        int bone_first = 1;
        if( palette && bone_count > 0 )
        {
            for( int bone = 0; bone < bone_count; bone++ )
            {
                if( !bone_first )
                    fprintf(fp, ",\n");
                bone_first = 0;
                const float* M = &palette[bone * 16];
                fprintf(fp, "    \"%d\": [", bone);
                for( int k = 0; k < 16; k++ )
                {
                    if( k > 0 )
                        fputc(',', fp);
                    fprintf(fp, "%g", (double)M[k]);
                }
                fprintf(fp, "]");
            }
        }
        fprintf(fp, "\n  },\n");

        if( base )
            RSCacheDat2A_SkeletalBaseFree(base);
        if( maya )
            RSCacheDat2A_AnimMayaFree(maya);
    }
    else
    {
        fprintf(fp, "  \"boneFinalMatrices\": {},\n");
    }

    fprintf(fp, "  \"vertices\": [\n");
    const float* frame_matrices =
        palette && frame_count > 0 ? palette : NULL;
    for( int vi = 0; vi < merged.vertex_count; vi++ )
    {
        int pre_x = merged.vertices_x[vi];
        int pre_y = merged.vertices_y[vi];
        int pre_z = merged.vertices_z[vi];
        int post_x = pre_x, post_y = pre_y, post_z = pre_z;
        if( frame_matrices )
        {
            apply_skeletal_vertex(
                &post_x,
                &post_y,
                &post_z,
                pre_x,
                pre_y,
                pre_z,
                &merged,
                vi,
                frame_matrices,
                bone_count);
        }

        int cnt = merged.animaya_group_counts
            ? (int)merged.animaya_group_counts[vi]
            : 0;

        fprintf(fp, "    {\"pre\":[%d,%d,%d],\"post\":[%d,%d,%d],\"group\":", pre_x, pre_y, pre_z, post_x, post_y, post_z);
        if( cnt > 0 && merged.animaya_groups[vi] )
            fprintf_json_string_array(fp, merged.animaya_groups[vi], cnt);
        else
            fprintf(fp, "[]");
        fprintf(fp, ",\"scale\":");
        if( cnt > 0 && merged.animaya_scales[vi] )
            fprintf_json_string_array(fp, merged.animaya_scales[vi], cnt);
        else
            fprintf(fp, "[]");
        fprintf(fp, "}%s\n", vi + 1 < merged.vertex_count ? "," : "");
    }
    fprintf(fp, "  ]\n");
    fprintf(fp, "}\n");

    fclose(fp);
    printf(
        "Wrote %s\nNPC %d revision=%d models=%d merged_vc=%d skinned=%d\n",
        out_path,
        npc_id,
        npc_revision,
        npc->models_count,
        merged.vertex_count,
        merged_skinned);

    free(palette);
    dump_model_free(&merged);
    for( int i = 0; i < npc->models_count; i++ )
    {
        if( pieces[i] )
            RSCacheDat2A_ModelFree(pieces[i]);
    }
    free(pieces);
    if( seq )
        RSCacheDat2A_ConfigSequenceFree(seq);
    RSCacheDat2A_ConfigNpctypeFree(npc);
    RSCacheDat2Disk_Free(cache);
    return 0;
}
