#include "osrs/cache.h"
#include "osrs/tables/model.h"

#include <stdio.h>

int
main(int argc, char* argv[])
{
    struct Cache* cache = cache_new_from_directory("cache");
    if( !cache )
    {
        printf("Failed to load cache\n");
        return 1;
    }

    struct Model* model = model_new_from_cache(cache, 0);
    if( !model )
    {
        printf("Failed to load model\n");
        return 1;
    }

    model_free(model);

    cache_free(cache);

    return 0;
}