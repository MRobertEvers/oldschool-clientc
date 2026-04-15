static void
bucket_fill_distances(
    struct PainterBucketCtx* w,
    struct Painter* painter,
    int camera_sx,
    int camera_sz,
    int min_draw_x,
    int max_draw_x,
    int min_draw_z,
    int max_draw_z,
    int min_level,
    int max_level)
{
    uint8_t draw_mask = painter->level_mask ? painter->level_mask : 0xFu;
    for( int s = min_level; s < max_level && s < painter->levels; s++ )
    {
        if( (draw_mask & (1u << s)) == 0 )
            continue;
        for( int z = min_draw_z; z < max_draw_z; z++ )
        {
            for( int x = min_draw_x; x < max_draw_x; x++ )
            {
                int ti = painter_coord_idx(painter, x, z, s);
                w->dist[ti] = abs(x - camera_sx) + abs(z - camera_sz);
            }
        }
    }
}
