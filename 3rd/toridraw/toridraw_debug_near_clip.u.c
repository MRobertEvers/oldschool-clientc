#ifndef TORIDRAW_DEBUG_NEAR_CLIP_U_C
#define TORIDRAW_DEBUG_NEAR_CLIP_U_C

/*
 * The near-clip gate's verification harness.
 *
 * Its own file, and included later than the other debug code, because it is
 * the one piece that reaches back into the projection: it runs the OPPOSITE
 * vertex kernel over the same model to compare against, so it has to come
 * after toridraw_project_vertices_{clip,noclip}_* exist. The rest of the debug
 * code only reads results and can sit up with the stats structs.
 *
 * Compiled to nothing without TORIDRAW_NEAR_CLIP_STATS.
 */

/*
 * The near-clip gate's correctness argument, checked on real scene data.
 *
 * Deliberately NOT done by comparing rendered frames between a gated and a
 * forced build: this client's offline boot is not frame-deterministic, so a
 * frame byte-compare answers "did these two runs load the same things", not
 * "do the two kernels agree". Both kernels run over the same model inside one
 * process here instead, and the gated result is restored afterwards so the
 * rest of the frame renders from the path production would have taken.
 *
 * TORIDRAW_NEAR_CLIP_STATS only.
 */
static void
toridraw_dbg_verify_near_clip_gate(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Position* position,
    struct ToriDraw_Camera* camera,
    const struct ProjectedVertex* center_projection_in,
    const struct ToriDraw_BoundsCylinder* proj_bc,
    bool may_clip,
    int model_pitch,
    int model_yaw,
    int model_roll,
    int camera_roll)
{
#ifdef TORIDRAW_NEAR_CLIP_STATS
    struct ProjectedVertex const center_projection = *center_projection_in;
    /*
     * Verification build. Two properties are checked here, on real scene data.
     *
     * Deliberately NOT done by comparing rendered frames between a gated and a
     * forced build: this client's offline boot is not frame-deterministic (the
     * same binary run twice produces different frames — async asset loads
     * settle differently), so a frame byte-compare answers "did these two runs
     * load the same things", not "do the two kernels agree". Everything below
     * runs both kernels over the same model inside one process instead.
     *
     *   1. Conservativeness. If any vertex really did land in front of the near
     *      plane, the gate must have said so. The reverse — gate says "may
     *      clip", nothing actually clips — is merely a missed optimization.
     *      A failure here means the no-clip kernel divided by a z it should
     *      not have, the one way this change can corrupt geometry.
     *   2. Equivalence. Whenever nothing actually clipped, both kernels must
     *      produce identical vertices. Checked regardless of which way the gate
     *      went, so the boundary case (gate says "may clip", reality says no)
     *      is covered too, not just the easy interior.
     *
     * The kernels write screen z as (camera-space z - model_mid_z), so the
     * camera-space z is recoverable exactly and neither check needs the
     * projection to hand anything extra back.
     */
    {
        int const vcount = model_vertex_count(hnd);
        bool actually_clipped = false;
        for( int vi = 0; vi < vcount; vi++ )
        {
            if( scene->screen_vertices_z[vi] + center_projection.z < camera->near_plane_z )
            {
                actually_clipped = true;
                break;
            }
        }

        if( actually_clipped && !may_clip )
        {
            fprintf(
                stderr,
                "near_clip_bound_violation: model clipped but the gate said it could not "
                "(mid_z=%d sphere_r=%d near=%d)\n",
                center_projection.z,
                proj_bc ? proj_bc->min_z_depth_any_rotation : -1,
                camera->near_plane_z);
            assert(0 && "near-clip gate was not conservative");
        }

        if( !actually_clipped )
        {
            static int* verify_x = NULL;
            static int* verify_y = NULL;
            static int* verify_z = NULL;
            static int verify_cap = 0;
            static long compared_models = 0;
            static long nudge_divergences = 0;

            if( vcount > verify_cap )
            {
                verify_cap = vcount;
                verify_x = (int*)realloc(verify_x, (size_t)verify_cap * sizeof(int));
                verify_y = (int*)realloc(verify_y, (size_t)verify_cap * sizeof(int));
                verify_z = (int*)realloc(verify_z, (size_t)verify_cap * sizeof(int));
                assert(verify_x && verify_y && verify_z);
            }
            memcpy(verify_x, scene->screen_vertices_x, (size_t)vcount * sizeof(int));
            memcpy(verify_y, scene->screen_vertices_y, (size_t)vcount * sizeof(int));
            memcpy(verify_z, scene->screen_vertices_z, (size_t)vcount * sizeof(int));

            /* Re-project down the opposite arm and compare. */
            bool const par = toridraw_proj_is_parallel(camera->proj_mode);
            if( may_clip && par )
                toridraw_project_vertices_parallel_noclip(
                    scene, hnd, position, camera,
                    model_pitch, model_yaw, model_roll, camera_roll, center_projection.z);
            else if( par )
                toridraw_project_vertices_parallel_clip(
                    scene, hnd, position, camera,
                    model_pitch, model_yaw, model_roll, camera_roll, center_projection.z);
            else if( may_clip )
                toridraw_project_vertices_noclip_prepared(
                    scene, hnd, position, camera,
                    model_pitch, model_yaw, model_roll, camera_roll, center_projection.z);
            else
                toridraw_project_vertices_clip_prepared(
                    scene, hnd, position, camera,
                    model_pitch, model_yaw, model_roll, camera_roll, center_projection.z);

            compared_models++;
            for( int vi = 0; vi < vcount; vi++ )
            {
                /* The one legitimate divergence: the clipping kernel nudges a
                 * genuinely projected -5000 to -5001 and the no-clip kernel
                 * does not. Counted rather than failed — it is the documented
                 * consequence of dropping the nudge, and the count says how
                 * often it is reachable at all. */
                int const lo = verify_x[vi] < scene->screen_vertices_x[vi]
                                   ? verify_x[vi]
                                   : scene->screen_vertices_x[vi];
                int const hi = verify_x[vi] < scene->screen_vertices_x[vi]
                                   ? scene->screen_vertices_x[vi]
                                   : verify_x[vi];
                if( lo == TORIDRAW_SCREEN_X_NEAR_CLIPPED_NUDGE &&
                    hi == TORIDRAW_SCREEN_X_NEAR_CLIPPED &&
                    verify_y[vi] == scene->screen_vertices_y[vi] &&
                    verify_z[vi] == scene->screen_vertices_z[vi] )
                {
                    nudge_divergences++;
                    continue;
                }
                if( verify_x[vi] != scene->screen_vertices_x[vi] ||
                    verify_y[vi] != scene->screen_vertices_y[vi] ||
                    verify_z[vi] != scene->screen_vertices_z[vi] )
                {
                    fprintf(
                        stderr,
                        "near_clip_mismatch: gate=%d vertex %d/%d "
                        "gated=(%d,%d,%d) other=(%d,%d,%d) "
                        "[mpitch=%d myaw=%d mroll=%d croll=%d tex=%d mid_z=%d near=%d]\n",
                        (int)may_clip,
                        vi,
                        vcount,
                        verify_x[vi],
                        verify_y[vi],
                        verify_z[vi],
                        scene->screen_vertices_x[vi],
                        scene->screen_vertices_y[vi],
                        scene->screen_vertices_z[vi],
                        model_pitch,
                        model_yaw,
                        model_roll,
                        camera_roll,
                        (int)model_has_textures(hnd),
                        center_projection.z,
                        camera->near_plane_z);
                    assert(0 && "the two near-clip kernels disagreed");
                }
            }

            /* Restore the gated result: the rest of the frame must render from
             * the path production would actually have taken. */
            memcpy(scene->screen_vertices_x, verify_x, (size_t)vcount * sizeof(int));
            memcpy(scene->screen_vertices_y, verify_y, (size_t)vcount * sizeof(int));
            memcpy(scene->screen_vertices_z, verify_z, (size_t)vcount * sizeof(int));

            if( (compared_models % 100000) == 0 )
                fprintf(
                    stderr,
                    "near_clip_verify: %ld models compared both kernels, "
                    "%ld nudge-only divergences\n",
                    compared_models,
                    nudge_divergences);
        }
    }
#else
    (void)scene;
    (void)hnd;
    (void)position;
    (void)camera;
    (void)center_projection_in;
    (void)proj_bc;
    (void)may_clip;
    (void)model_pitch;
    (void)model_yaw;
    (void)model_roll;
    (void)camera_roll;
#endif
}

#endif /* TORIDRAW_DEBUG_NEAR_CLIP_U_C */
