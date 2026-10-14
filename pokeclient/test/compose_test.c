/*
 * Composite a player from stored LAYER sheets and write the cell as raw
 * 0xAARRGGBB, for tools/pokesprite/layers.py composite() to compare against.
 *
 *   compose_test <out.raw> <facing> <frame> <hsl_from:hsl_to,...|-> <layer.pss>...
 */
#include "ps_compose.h"
#include "ps_sheet.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t*
slurp(const char* path, int* size)
{
    FILE* f = fopen(path, "rb");
    assert(f);
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* b = malloc((size_t)n);
    assert(b);
    size_t got = fread(b, 1, (size_t)n, f);
    assert(got == (size_t)n);
    fclose(f);
    *size = (int)n;
    return b;
}

int
main(int argc, char** argv)
{
    assert(argc >= 6);
    struct PS_ComposeInput in;
    memset(&in, 0, sizeof(in));
    in.facing = atoi(argv[2]);
    in.frame = atoi(argv[3]);
    if( strcmp(argv[4], "-") != 0 )
    {
        char* spec = strdup(argv[4]);
        assert(spec);
        for( char* t = strtok(spec, ","); t; t = strtok(NULL, ",") )
        {
            unsigned a, b;
            assert(sscanf(t, "%u:%u", &a, &b) == 2);
            in.recolours[in.recolour_count].from_hsl16 = (uint16_t)a;
            in.recolours[in.recolour_count++].to_hsl16 = (uint16_t)b;
        }
        free(spec);
    }
    for( int i = 5; i < argc; i++ )
    {
        int size;
        uint8_t* bytes = slurp(argv[i], &size);
        struct PS_Sheet* s = PS_SheetDecode(bytes, size);
        assert(s);
        free(bytes);
        in.layers[in.layer_count++] = s;
    }
    int w = in.layers[0]->head.cell_w, h = in.layers[0]->head.cell_h;
    uint32_t* out = malloc((size_t)w * (size_t)h * 4);
    assert(out);
    PS_Compose(&in, out, w, h);
    FILE* f = fopen(argv[1], "wb");
    assert(f);
    fwrite(&w, 4, 1, f);
    fwrite(&h, 4, 1, f);
    fwrite(out, 4, (size_t)w * (size_t)h, f);
    fclose(f);
    printf("composited %d layers, %dx%d\n", in.layer_count, w, h);
    return 0;
}
