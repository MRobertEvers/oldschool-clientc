#include "framemap.h"

#include <stdlib.h>
#include <string.h>

void
decode_framemap(struct FramemapDefinition* def, int id, struct Buffer* buffer)
{
    // Initialize the framemap definition
    def->id = id;
    def->length = read_8(buffer);
    def->types = malloc(def->length * sizeof(int));
    def->frameMaps = malloc(def->length * sizeof(int*));
    def->frameMapLengths = malloc(def->length * sizeof(int));
    memset(def->types, 0, def->length * sizeof(int));
    memset(def->frameMaps, 0, def->length * sizeof(int*));
    memset(def->frameMapLengths, 0, def->length * sizeof(int));

    // Read the types array
    for( int i = 0; i < def->length; i++ )
    {
        def->types[i] = read_8(buffer);
    }

    // Read the frame maps lengths
    for( int i = 0; i < def->length; i++ )
    {
        def->frameMapLengths[i] = read_8(buffer);
        def->frameMaps[i] = malloc(def->frameMapLengths[i] * sizeof(int));
        memset(def->frameMaps[i], 0, def->frameMapLengths[i] * sizeof(int));
    }

    // Read the frame maps data
    for( int i = 0; i < def->length; i++ )
    {
        for( int j = 0; j < def->frameMapLengths[i]; j++ )
        {
            def->frameMaps[i][j] = read_8(buffer);
        }
    }
}

void
free_framemap(struct FramemapDefinition* def)
{
    if( def->types )
    {
        free(def->types);
    }
    if( def->frameMaps )
    {
        for( int i = 0; i < def->length; i++ )
        {
            if( def->frameMaps[i] )
            {
                free(def->frameMaps[i]);
            }
        }
        free(def->frameMaps);
    }
}
