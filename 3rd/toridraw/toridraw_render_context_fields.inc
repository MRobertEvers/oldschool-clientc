/*
 * Intentionally no include guard: ToriDraw_RenderContext owns these fields and
 * ToriDraw_Scene includes them again in an anonymous overlay for source
 * compatibility with legacy scene->field users. Keep this list declarative.
 */
    uint32_t flags;
    int max_vertices;
    int max_faces;
    int depth_levels;
    int depth_stride;
    int priority_stride;
    /** Entries in each flexible-priority (10/11) array. The two are allocated
     *  the same size; the sorter merges 11 into 10, so the merged run must fit
     *  this many entries. */
    int flex_prio_capacity;

    struct ToriDraw_ModelHandle active_hnd;

    struct ProjectedVertex projected_vertex;
    struct ToriDraw_AABB aabb;
    struct ToriDraw_AABB cylinder_fast_aabb;

    /*
     * Whether the model this context last projected could reach behind the
     * near plane, and so whether screen_vertices_x may hold
     * TORIDRAW_SCREEN_X_NEAR_CLIPPED. False for the overwhelming majority of
     * models: the camera has to be inside the model's bounding sphere for a
     * vertex to clip. Consumers must test this first because the no-clip
     * kernel does not nudge a genuine -5000 out of the way.
     */
    bool near_clipped;

    /*
     * Near plane the projection actually used, including the safety increase
     * needed to keep coordinates inside the rasterizer's 16.16 domain. Every
     * consumer of these projection arrays must clip against this value rather
     * than the camera's requested plane.
     */
    int projection_near_plane_z;

    struct ToriDraw_TextureState* tex_state;

    /*
     * Viewport-sized depth scratch retained independently by each context.
     * It is lazy because SceneNew has no viewport dimensions. The stride is in
     * elements and matches the framebuffer stride, so the same pixel offset
     * walks both buffers. A depth-rendered model resets its own region before
     * drawing, which confines depth ordering to that model.
     */
    torizdepth_t* zbuffer;
    int zbuffer_stride;
    int zbuffer_rows;

    int* screen_vertices_x;
    int* screen_vertices_y;
    int* screen_vertices_z;
    int* orthographic_vertices_x;
    int* orthographic_vertices_y;
    int* orthographic_vertices_z;

    /*
     * Optional camera-only constants for the projection hot path. Pointer
     * identity makes the normal render stream one cheap comparison. Keep the
     * prepared vectors immediately after the six pointers for the Apple
     * AArch64 projection ABI.
     */
    struct ToriDraw_ProjectionPreparedCamera projection_prepared_camera;
    const struct ToriDraw_Camera* projection_prepared_camera_source;

    /* Sorting intermediates are shared: they are dead before callbacks run. */
    faceint_t* tmp_depth_face_count;
    faceint_t* tmp_depth_faces;
    faceint_t* tmp_priority_face_count;
    /* An int because this is a sum of depths; a face index-sized value wraps. */
    int* tmp_priority_depth_sum;
    faceint_t* tmp_priority_faces;
    int* tmp_flex_prio11_face_to_depth;
    int* tmp_flex_prio12_face_to_depth;

    faceint_t* sm_face_depth;
    int* sm_depth_offset;
    int* sm_depth_cursor;
    faceint_t* sm_faces_by_depth;
    int sm_prio_count[13];
    int* sm_prio_offset;
    faceint_t* sm_prio_faces;
    int* sm_flex_prio11_face_to_depth;
    int* sm_flex_prio12_face_to_depth;

    /* Final order remains live across a face callback and is context-owned. */
    int* tmp_face_order;
    int tmp_face_order_count;
