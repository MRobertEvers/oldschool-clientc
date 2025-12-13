#include "chunks.u.c"

#include <assert.h>
#include <stdio.h>

static void
step()
{
    for( int i = 0; i < 16; i++ )
    {
        int chunk_side = 2;
        int stride = 2 * chunk_side;

        int y = i / stride;
        int chunk_y = y / chunk_side;

        int x = i % stride;
        int chunk_x = x / chunk_side;

        int elem_idx = (y % chunk_side) * chunk_side + (x % chunk_side);
        int chunk_idx = chunk_y * 2 + chunk_x;

        printf("i %d: Chunk %d, Element %d\n", i, chunk_idx, elem_idx);
    }
}

int
main(int argc, char* argv[])
{
    struct Chunk chunks[100] = { 0 };

    chunks_inview(chunks, 64 * 50, 64 * 50, 127);

    for( int i = 0; i < 9; i++ )
    {
        printf("Chunk %d: %d, %d\n", i, chunks[i].x, chunks[i].z);
    }

    step();

    return 0;
}