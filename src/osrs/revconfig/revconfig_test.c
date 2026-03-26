#include "revconfig.h"
#include "revconfig_load.h"

#include <stdio.h>

int
main()
{
    const char* filename = "../src/osrs/revconfig/"
                           "configs/rev_254_2/rev_245_2_cache.ini";

    struct RevConfigBuffer* buffer = revconfig_buffer_new(16);
    uint32_t field_count = 0;

    revconfig_load_fields_from_ini(filename, buffer);

    for( uint32_t i = 0; i < buffer->field_count; i++ )
    {
        struct RevConfigField* field = &buffer->fields[i];
        printf(
            "Field %u: kind=%s, value=%s\n",
            i,
            revconfig_field_kind_str(field->kind),
            field->value);
    }
    return 0;
}