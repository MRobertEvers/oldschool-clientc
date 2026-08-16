#include "engine/torirs_enum_from_rscache.h"

#include "engine/torirs_types.h"

#include <assert.h>
#include <rscache.h>
#include <stdlib.h>
#include <string.h>

struct ToriRS_Enum*
ToriRS_EnumFromRSCacheDat2(
    int enum_id,
    struct RSCache_Dat2ConfigEnum* entry)
{
    struct ToriRS_Enum* e;

    assert(entry);

    e = calloc(1, sizeof(*e));
    assert(e);
    e->id = enum_id;
    e->output_is_string = entry->output_is_string;
    e->default_int = entry->default_int;
    e->default_string = entry->default_string;
    e->keys = entry->keys;
    e->int_values = entry->int_values;
    e->string_values = entry->string_values;
    e->count = entry->count;

    entry->default_string = NULL;
    entry->keys = NULL;
    entry->int_values = NULL;
    entry->string_values = NULL;
    entry->count = 0;
    return e;
}
