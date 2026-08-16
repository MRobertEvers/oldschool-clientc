#ifndef TASK_INSTANCE_ON_RC_TYPES_H
#define TASK_INSTANCE_ON_RC_TYPES_H

#include "component_load_types.h"
#include "instance_revconfig_context.h"
#include "revconfig/revconfig.h"

#include <stdbool.h>

bool
revconfig_uicomponent_needs_rs_load(struct RevConfigUIComponentItem const* item);

void
task_instance_on_rc_uicomponent_rs_loaded(
    void* user,
    int component_id,
    int parent_id,
    int rel_x,
    int rel_y);

#endif
