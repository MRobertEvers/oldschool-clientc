#include "bounds_cylinder.h"

#include <limits.h>
#include <math.h>

struct BoundsCylinder
calculate_bounds_cylinder(int num_vertices, int* vertex_x, int* vertex_y, int* vertex_z)
{
    struct BoundsCylinder bounding_cylinder = { 0 };

    int min_y = INT_MAX;
    int max_y = INT_MIN;
    int radius_squared = 0;

    for( int i = 0; i < num_vertices; i++ )
    {
        int x = vertex_x[i];
        int y = vertex_y[i];
        int z = vertex_z[i];
        if( y < min_y )
            min_y = y;
        if( y > max_y )
            max_y = y;
        int radius_squared_vertex = x * x + z * z;
        if( radius_squared_vertex > radius_squared )
            radius_squared = radius_squared_vertex;
    }

    // Reminder, +y is down on the screen.
    int center_to_bottom_edge = (int)sqrt(radius_squared + min_y * min_y) + 1;
    int center_to_top_edge = (int)sqrt(radius_squared + max_y * max_y) + 1;
    bounding_cylinder.center_to_bottom_edge = (int)sqrt(0 + min_y * min_y) + 1;
    bounding_cylinder.center_to_top_edge = (int)sqrt(0 + max_y * max_y) + 1;

    bounding_cylinder.radius = (int)sqrt(radius_squared);

    // Use max of the two here because OSRS assumes the camera is always above the model,
    // which may not be the case for us.
    bounding_cylinder.min_z_depth_any_rotation =
        center_to_top_edge > center_to_bottom_edge ? center_to_top_edge : center_to_bottom_edge;

    return bounding_cylinder;
}