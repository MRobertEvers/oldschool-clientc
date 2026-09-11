#ifndef TORIDRAW_TEXTURE_MAPPING_H
#define TORIDRAW_TEXTURE_MAPPING_H

/*
 * Prepared model-space mapping for texture render types 1-3 (cylinder, cube,
 * and sphere).  Raster-kernel callbacks receive a borrowed, read-only pointer
 * to one of these records when mapping_payload is TORIDRAW_RASTER_MAPPING_HD.
 */
struct ToriDraw_TexMapping
{
    /* Midpoint of the bounding box of every vertex in the face group. */
    int centre_x;
    int centre_y;
    int centre_z;

    /* Model space -> mapping space.  Rotation and per-axis scales have
     * already been folded into this row-major 3x3 matrix. */
    float matrix[9];

    /* 0-3 scroll direction, followed by the projection-specific offsets. */
    int direction;
    float speed;
    float u_offset;
    float v_offset;

    /* Cylinder u-wrap scale. */
    float scale_z;

    /* Cube face-selection scales (raw stored scale divided by 64). */
    float axis_scale_x;
    float axis_scale_y;
    float axis_scale_z;
};

#endif
