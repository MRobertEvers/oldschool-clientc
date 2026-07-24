/*
 * Helper for test_bzip_interop.sh.
 *
 * Reads a file, compresses it with our bzip2 encoder, and writes a complete
 * .bz2 stream (magic included) so the reference `bzip2` binary can verify it.
 *
 * This is the independent check on the encoder: our own bunzip decoder and our
 * encoder could in principle share a misreading of the format, whereas
 * bzip2 1.0.8 cannot.
 */

#include <bzip.h>

#include <stdio.h>
#include <stdlib.h>

int
main(int argc, char** argv)
{
    if( argc != 3 )
    {
        fprintf(stderr, "usage: %s <in> <out.bz2>\n", argv[0]);
        return 2;
    }

    FILE* in = fopen(argv[1], "rb");
    if( !in )
    {
        fprintf(stderr, "cannot open %s\n", argv[1]);
        return 2;
    }

    fseek(in, 0, SEEK_END);
    long size = ftell(in);
    fseek(in, 0, SEEK_SET);
    if( size < 0 )
    {
        fclose(in);
        return 2;
    }

    unsigned char* data = malloc((size_t)size ? (size_t)size : 1);
    if( !data )
    {
        fclose(in);
        return 2;
    }
    if( size > 0 && fread(data, 1, (size_t)size, in) != (size_t)size )
    {
        fclose(in);
        free(data);
        return 2;
    }
    fclose(in);

    unsigned int bound = bzip_compress_bound((unsigned int)size);
    unsigned char* packed = malloc(bound);
    if( !packed )
    {
        free(data);
        return 2;
    }

    /* Level 1 — the block size every RuneScape cache uses. */
    unsigned int written = bzip_compress(packed, bound, data, (unsigned int)size, 1);
    if( written == 0 )
    {
        fprintf(stderr, "bzip_compress failed\n");
        free(data);
        free(packed);
        return 1;
    }

    FILE* out = fopen(argv[2], "wb");
    if( !out )
    {
        free(data);
        free(packed);
        return 2;
    }
    fwrite(packed, 1, written, out);
    fclose(out);

    free(data);
    free(packed);
    return 0;
}
