#ifndef REVCONFIG_LOAD_H
#define REVCONFIG_LOAD_H

#include "revconfig.h"

#include <stdint.h>

void
revconfig_load_fields_from_ini(
    const char* filename,
    struct RevConfigBuffer* revconfig_buffer);

#endif