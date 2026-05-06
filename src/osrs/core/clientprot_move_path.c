#include "clientprot_move_path.h"

#include "osrs/rscache/rsbuf.h"

/* BFS path len is capped at 25; corner count is bounded by ~26. */
#define MOVE_PATH_CORNER_MAX 32

/** Corner-compress a forward BFS path (Client.ts route from first step toward dest). */
static int
clientprot_move_path_compress_corners(
    const int* px,
    const int* pz,
    int n,
    int* cx,
    int* cz,
    int cap)
{
    if( n < 1 || cap < 1 )
        return 0;

    int m = 0;
    cx[m] = px[0];
    cz[m] = pz[0];
    m++;

    for( int i = 0; i < n - 1; i++ )
    {
        int dx = px[i + 1] - px[i];
        int dz = pz[i + 1] - pz[i];
        if( i > 0 )
        {
            int pdx = px[i] - px[i - 1];
            int pdz = pz[i] - pz[i - 1];
            if( dx != pdx || dz != pdz )
            {
                if( m >= cap - 1 )
                    goto finish_dest;
                cx[m] = px[i];
                cz[m] = pz[i];
                m++;
            }
        }
    }

finish_dest:
    if( px[n - 1] != cx[m - 1] || pz[n - 1] != cz[m - 1] )
    {
        if( m < cap )
        {
            cx[m] = px[n - 1];
            cz[m] = pz[n - 1];
            m++;
        }
        else
        {
            cx[m - 1] = px[n - 1];
            cz[m - 1] = pz[n - 1];
        }
    }
    return m;
}

void
clientprot_move_path_write_subpacket(
    struct RSBuffer* b,
    int run,
    int map_build_base_x,
    int map_build_base_z,
    const int* path_x,
    const int* path_z,
    int path_len)
{
    if( !b || path_len < 1 || !path_x || !path_z )
        return;

    int corners_x[MOVE_PATH_CORNER_MAX];
    int corners_z[MOVE_PATH_CORNER_MAX];
    int m = clientprot_move_path_compress_corners(
        path_x, path_z, path_len, corners_x, corners_z, MOVE_PATH_CORNER_MAX);
    if( m < 1 )
        return;

    if( m > 25 )
    {
        corners_x[24] = path_x[path_len - 1];
        corners_z[24] = path_z[path_len - 1];
        m = 25;
    }

    int buffer_size = m;
    int start_x     = corners_x[0];
    int start_z     = corners_z[0];

    p1(b, buffer_size + buffer_size + 3);
    p1(b, run ? 1 : 0);
    p2(b, start_x + map_build_base_x);
    p2(b, start_z + map_build_base_z);

    for( int i = 1; i < buffer_size; i++ )
    {
        p1(b, corners_x[i] - start_x);
        p1(b, corners_z[i] - start_z);
    }
}
