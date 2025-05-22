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
    def->vertex_groups = malloc(def->length * sizeof(int*));
    def->vertex_group_lengths = malloc(def->length * sizeof(int));
    memset(def->types, 0, def->length * sizeof(int));
    memset(def->vertex_groups, 0, def->length * sizeof(int*));
    memset(def->vertex_group_lengths, 0, def->length * sizeof(int));

    // Read the types array
    for( int i = 0; i < def->length; i++ )
    {
        def->types[i] = read_8(buffer);
    }

    // Read the frame maps lengths
    for( int i = 0; i < def->length; i++ )
    {
        def->vertex_group_lengths[i] = read_8(buffer);
        def->vertex_groups[i] = malloc(def->vertex_group_lengths[i] * sizeof(int));
        memset(def->vertex_groups[i], 0, def->vertex_group_lengths[i] * sizeof(int));
    }

    // Read the frame maps data
    for( int i = 0; i < def->length; i++ )
    {
        for( int j = 0; j < def->vertex_group_lengths[i]; j++ )
        {
            def->vertex_groups[i][j] = read_8(buffer);
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
    if( def->vertex_groups )
    {
        for( int i = 0; i < def->length; i++ )
        {
            if( def->vertex_groups[i] )
            {
                free(def->vertex_groups[i]);
            }
        }
        free(def->vertex_groups);
    }
}
