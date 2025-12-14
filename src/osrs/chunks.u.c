#ifndef CHUNKS_U_C
#define CHUNKS_U_C

#include <assert.h>

#define CHUNK_TILE_SIZE 64
struct Chunk
{
    int x;
    int z;
};

struct ChunksInView
{
    int count;
    int width;
};

int
chunks_inview(
    struct Chunk* chunks,
    int world_x,
    int world_z,
    int scene_size,
    struct ChunksInView* out_nullable)
{
    assert(scene_size < 128);

    int radius = (scene_size >> 1);
    int min_chunk_x = (world_x - radius) / CHUNK_TILE_SIZE;
    int max_chunk_x = (world_x + radius) / CHUNK_TILE_SIZE;
    int min_chunk_z = (world_z - radius) / CHUNK_TILE_SIZE;
    int max_chunk_z = (world_z + radius) / CHUNK_TILE_SIZE;

    int i = 0;
    for( int x = min_chunk_x; x <= max_chunk_x; x++ )
    {
        for( int z = min_chunk_z; z <= max_chunk_z; z++ )
        {
            chunks[i].x = x;
            chunks[i].z = z;
            i++;
        }
    }

    if( out_nullable )
    {
        out_nullable->count = i;
        out_nullable->width = max_chunk_x - min_chunk_x + 1;
    }

    return i;
};

#endif