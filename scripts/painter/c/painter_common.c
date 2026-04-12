#include "painter_common.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static double
rad(double deg)
{
    return deg * (M_PI / 180.0);
}

void painter_compute_frustum_mask(
    uint64_t *mask_words,
    int grid_size,
    double eye_x,
    double eye_z,
    double yaw_deg,
    double fov_deg)
{
    int nbits = grid_size * grid_size;
    int nwords = (nbits + 63) / 64;
    memset(mask_words, 0, (size_t)nwords * sizeof(uint64_t));

    double half_fov = fov_deg * 0.5;
    double face_x = sin(rad(yaw_deg));
    double face_z = -cos(rad(yaw_deg));

    for( int z = 0; z < grid_size; z++ )
    {
        for( int x = 0; x < grid_size; x++ )
        {
            double dx = (double)x - eye_x;
            double dz = (double)z - eye_z;
            int bit = painter_frustum_bit_index(x, z, grid_size);
            int wi = bit / 64;
            int sh = bit % 64;

            if( dx == 0.0 && dz == 0.0 )
            {
                mask_words[wi] |= (uint64_t)1u << sh;
                continue;
            }

            double dist = sqrt(dx * dx + dz * dz);
            if( dist <= 0.0 )
                continue;

            double dot = (dx / dist) * face_x + (dz / dist) * face_z;
            if( dot > 1.0 )
                dot = 1.0;
            if( dot < -1.0 )
                dot = -1.0;
            double angle = acos(dot) * (180.0 / M_PI);
            if( angle <= half_fov )
                mask_words[wi] |= (uint64_t)1u << sh;
        }
    }
}

bool painter_tile_visible_in_mask(
    const uint64_t *mask_words,
    int x,
    int z,
    int grid_size)
{
    int bit = painter_frustum_bit_index(x, z, grid_size);
    int wi = bit / 64;
    int sh = bit % 64;
    return (mask_words[wi] >> sh) & 1u;
}
