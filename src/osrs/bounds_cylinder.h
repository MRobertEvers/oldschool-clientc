#ifndef BOUNDS_CYLINDER_H
#define BOUNDS_CYLINDER_H

/**
 * A top and bottom cylinder that bounds the model.
 */
struct BoundsCylinder
{
    int center_to_top_edge;
    int center_to_bottom_edge;
    int min_y;
    int max_y;
    int radius;

    // TODO: Name?
    // - Max extent from model origin.
    // - Distance to farthest vertex?
    int min_z_depth_any_rotation;
};

struct BoundsCylinder
calculate_bounds_cylinder(int num_vertices, int* vertex_x, int* vertex_y, int* vertex_z);

#endif