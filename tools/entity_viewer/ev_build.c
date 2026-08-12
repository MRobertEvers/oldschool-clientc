#include "ev_build.h"

#include "engine/toridraw_animation_from_rscache.h"
#include "engine/toridraw_model_from_torirs.h"
#include "engine/torirs_model_from_rscache.h"
#include "engine/torirs_types.h"

#include "toridraw.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- RSCache_Model -> ToriDraw_Model ------------------------------------ */

/*
 * There is no conversion here on purpose.
 *
 * This file used to hand-roll RSCache_Model -> ToriDraw_Model, and it got the
 * details wrong in ways that only showed up as rendering artefacts: face
 * priority is one byte per face in the cache and two 4-bit fields per byte in
 * the renderer, so copying it verbatim gave every face some other face's
 * priority, the painter's sort ran on nonsense and models drew with holes in
 * them. Nothing about that was visible in the code — the copy looked right and
 * even carried a comment saying it was.
 *
 * The client already owns this conversion in two steps that the whole game
 * renders through, so the viewer uses them:
 *
 *   ToriRS_ModelFromRSCache   src/engine/torirs_model_from_rscache.c
 *   ToriDraw_ModelFromToriRS  src/engine/toridraw_model_from_torirs.c
 *
 * They also carry the parts this file never had: the animaya skin, the
 * bounds cylinder, and the PnM texture invariant assert.
 */

/*
 * `ToriDraw_ModelDropNonSdTextures` is the one function in that pair the viewer
 * does not call, and it is the only reason a CacheProvider is named at all.
 * The viewer has no material table or texture pixels; ev_build_npc_model strips
 * texture selectors before lighting so those faces fall back to their source
 * colours instead of disappearing in the raster's missing-texture path.
 */
bool
CacheProvider_TextureIsSd(
    struct CacheProvider* provider,
    int texture_id)
{
    (void)provider;
    (void)texture_id;
    return true;
}

struct ToriDraw_Model*
ev_build_npc_model(
    struct Tool_Dat2Cache* c,
    int npc_id)
{
    struct RSCache_Dat2ConfigNpc* npc = tool_dat2_npc_load(c, npc_id);
    if( !npc )
        return NULL;
    if( npc->models_count <= 0 )
    {
        RSCache_Dat2ConfigNpcFree(npc);
        return NULL;
    }

    struct ToriDraw_Model** parts = calloc((size_t)npc->models_count, sizeof(*parts));
    if( !parts )
    {
        RSCache_Dat2ConfigNpcFree(npc);
        return NULL;
    }

    int part_count = 0;
    for( int i = 0; i < npc->models_count; i++ )
    {
        struct RSCache_Model* rs = tool_dat2_model_load(c, npc->models[i]);
        if( !rs )
            continue;

        /* ToriRS_ModelFromRSCache *moves* the arrays out and leaves `rs`
         * hollow, so the free below releases a shell, not the geometry. */
        struct ToriRS_Model* mid = ToriRS_ModelFromRSCache(rs);
        RSCache_ModelFree(rs);
        if( !mid )
            continue;

        struct ToriDraw_Model* part = ToriDraw_ModelFromToriRS(mid);
        ToriRS_ModelFree(mid);
        if( part )
        {
            /* What each part brings to the merge's priority fold. A part with
             * neither a per-face array nor a model_priority contributes faces
             * at priority 0, which draws them behind everything. */
            if( getenv("EV_PART_DEBUG") )
                fprintf(
                    stderr,
                    "    part model=%d faces=%d face_priorities=%s model_priority=%d\n",
                    npc->models[i],
                    part->face_count,
                    part->face_priorities ? "yes" : "no",
                    part->model_priority);
            /* EV_ONLY_PART=<index> keeps a single part of a merged npc, so a
             * shape that looks wrong can be attributed to the model that
             * actually contains it rather than guessed at from the composite. */
            const char* only = getenv("EV_ONLY_PART");
            if( only && atoi(only) != i )
                ToriDraw_ModelFree(part);
            else
                parts[part_count++] = part;
        }
    }

    struct ToriDraw_Model* merged = NULL;
    if( part_count == 1 )
        merged = parts[0];
    else if( part_count > 1 )
    {
        merged = ToriDraw_ModelMerge(parts, part_count);

        /*
         * Check the merge's index arithmetic against a plain concatenation.
         *
         * A merged model's face must point at vertices belonging to the part it
         * came from, shifted by that part's vertex offset. Getting it wrong
         * produces geometry that is *somewhere*, so it renders — a ghost copy of
         * a part at the wrong place, which is what a shield appearing twice
         * looks like.
         */
        if( merged && getenv("EV_CHECK_MERGE") )
        {
            int voff = 0;
            int foff = 0;
            int bad = 0;
            for( int pi = 0; pi < part_count; pi++ )
            {
                for( int f = 0; f < parts[pi]->face_count; f++ )
                {
                    int d = foff + f;
                    if( merged->face_indices_a[d] != parts[pi]->face_indices_a[f] + voff ||
                        merged->face_indices_b[d] != parts[pi]->face_indices_b[f] + voff ||
                        merged->face_indices_c[d] != parts[pi]->face_indices_c[f] + voff )
                        bad++;
                }
                for( int v = 0; v < parts[pi]->vertex_count; v++ )
                    if( merged->vertices_x[voff + v] != parts[pi]->vertices_x[v] ||
                        merged->vertices_y[voff + v] != parts[pi]->vertices_y[v] ||
                        merged->vertices_z[voff + v] != parts[pi]->vertices_z[v] )
                        bad++;
                voff += parts[pi]->vertex_count;
                foff += parts[pi]->face_count;
            }
            fprintf(
                stderr,
                "  merge check: %d parts, %d vertices, %d faces, %d mismatch(es)\n",
                part_count, voff, foff, bad);
        }
        for( int i = 0; i < part_count; i++ )
            ToriDraw_ModelFree(parts[i]);
    }
    free(parts);

    if( !merged )
    {
        RSCache_Dat2ConfigNpcFree(npc);
        return NULL;
    }

    /*
     * From here the order is the client's, step for step — app.c's npc model
     * build. It is an order, not a list: recolour has to precede lighting
     * because lighting bakes face colours into per-vertex shaded colours and a
     * swap afterwards is a no-op, and the scale has to precede the bounds
     * cylinder because the cylinder is measured off the vertices.
     */
    for( int i = 0; i < npc->recolor_count; i++ )
        ToriDraw_ModelRecolor(merged, npc->recolor_to_find[i], npc->recolor_to_replace[i]);
    for( int i = 0; i < npc->retexture_count; i++ )
        ToriDraw_ModelRetexture(merged, npc->retexture_to_find[i], npc->retexture_to_replace[i]);

    /* The browser renderer receives geometry but no material table or texture
     * pixels. Its missing-texture rule skips a textured face entirely, which
     * made texture-driven backports such as the Summoning models bake
     * successfully and then render as a blank canvas. The reference client
     * also falls back to the face colour when a texture is unavailable, so do
     * that here before lighting bakes the per-corner colours. */
    if( merged->face_textures )
        for( int face = 0; face < merged->face_count; face++ )
            merged->face_textures[face] = (faceint_t)-1;

    /* Npc opcodes 97/98. Missing here until now, so every npc with a scale of
     * its own — a giant, a small pet — rendered at the model's raw size. */
    if( npc->width_scale != 128 || npc->height_scale != 128 )
        ToriDraw_ModelScale(merged, npc->width_scale, npc->width_scale, npc->height_scale);

    struct ToriDraw_ModelHandle hnd;
    memset(&hnd, 0, sizeof(hnd));
    hnd.kind = TORIDRAWMK_MODEL;
    hnd.u.model.model = merged;
    ToriDraw_LightModelActor(hnd, npc->contrast, npc->ambient);

    ToriDraw_ModelSetBoundsCylinder(merged);
    ToriDraw_ModelCaptureOriginalVertices(merged);

    RSCache_Dat2ConfigNpcFree(npc);
    return merged;
}

/* ---- sequence -> animation ---------------------------------------------- */

/*
 * Assemble a sequence's animation.
 *
 * The framemap is the rig, the frames are the poses in sequence order, and
 * `ToriDraw_AnimationFromRSCache` is the client's own assembler for exactly
 * that. This used to build the ToriDraw_Animation field by field here, which
 * meant a second implementation of the frame/base copy — the same duplication
 * that got face priority wrong on the model side.
 */
struct ToriDraw_Animation*
ev_build_seq_anim(
    struct Tool_Dat2Cache* c,
    int seq_id,
    int* out_framemap_id)
{
    if( out_framemap_id )
        *out_framemap_id = -1;

    struct RSCache_Dat2ConfigSequence* seq = tool_dat2_seq_load(c, seq_id);
    if( !seq )
        return NULL;

    /*
     * Skeletal (Animaya) first: these sequences carry no frame list at all, so
     * the classic path below would see an empty sequence and give up. Every
     * modern OldSchool npc animates this way.
     *
     * The result is still a ToriDraw_Animation — `skeletal` set, `base`/`frames`
     * NULL — so frame stepping is identical and only the pose call branches.
     * `frame_count` is the config's play range when it states one, which is how
     * one baked palette serves several sequences that slice it differently.
     */
    if( seq->frame_count <= 0 && seq->anim_maya_id > 0 )
    {
        struct RSCache_Dat2AnimMaya* maya = tool_dat2_animaya_load(c, seq->anim_maya_id);
        struct ToriDraw_Animation* anim = NULL;

        if( maya && maya->base_id >= 0 )
        {
            if( out_framemap_id )
                *out_framemap_id = maya->base_id;

            struct RSCache_Dat2SkeletalBase* base =
                tool_dat2_skeletal_base_load(c, maya->base_id);
            struct ToriDraw_SkeletalAnim* skeletal =
                ToriDraw_SkeletalAnimFromRSCache(seq_id, maya, base);

            if( skeletal )
            {
                anim = calloc(1, sizeof(*anim));
                if( anim )
                {
                    int play = skeletal->frame_count;
                    if( seq->anim_maya_end > seq->anim_maya_start )
                    {
                        play = seq->anim_maya_end - seq->anim_maya_start;
                        if( play > skeletal->frame_count )
                            play = skeletal->frame_count;
                    }
                    anim->skeletal = skeletal;
                    anim->frame_count = play > 0 ? play : 1;
                    anim->replaceheldleft = -1;
                    anim->replaceheldright = -1;
                }
                else
                    ToriDraw_SkeletalAnimFree(skeletal);
            }
            RSCache_Dat2SkeletalBaseFree(base);
        }

        RSCache_Dat2AnimMayaFree(maya);
        RSCache_Dat2ConfigSequenceFree(seq);
        return anim;
    }

    if( !seq->frame_ids || seq->frame_count <= 0 )
    {
        RSCache_Dat2ConfigSequenceFree(seq);
        return NULL;
    }

    int framemap_id = tool_dat2_seq_framemap_id(c, seq, 0);
    struct RSCache_Dat2Framemap* fm =
        framemap_id >= 0 ? tool_dat2_framemap_load(c, framemap_id) : NULL;
    if( !fm )
    {
        RSCache_Dat2ConfigSequenceFree(seq);
        return NULL;
    }
    if( out_framemap_id )
        *out_framemap_id = framemap_id;

    struct RSCache_Dat2Frame** frames =
        calloc((size_t)seq->frame_count, sizeof(*frames));
    int* delays = calloc((size_t)seq->frame_count, sizeof(*delays));
    struct ToriDraw_Animation* anim = NULL;

    if( frames && delays )
    {
        /*
         * Every frame slot is filled, including the ones that fail to decode.
         * Dropping a frame would shorten the sequence, which moves every later
         * frame's timing and shifts what `frame_step` loops back to — a
         * sequence that plays slightly wrong is harder to notice than one
         * frame that holds.
         */
        int decoded = 0;
        for( int i = 0; i < seq->frame_count; i++ )
        {
            frames[i] = tool_dat2_frame_load(c, fm, seq->frame_ids[i]);
            delays[i] = seq->frame_lengths ? seq->frame_lengths[i] : 1;
            if( frames[i] )
                decoded++;
        }

        if( decoded > 0 )
            anim = ToriDraw_AnimationFromRSCache(
                fm,
                (struct RSCache_Dat2Frame const* const*)frames,
                delays,
                seq->frame_count,
                seq->frame_step);

        for( int i = 0; i < seq->frame_count; i++ )
            RSCache_Dat2FrameFree(frames[i]);
    }

    free(frames);
    free(delays);
    RSCache_Dat2FramemapFree(fm);
    RSCache_Dat2ConfigSequenceFree(seq);
    return anim;
}
