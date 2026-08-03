#ifndef UITREE_IFACE_STATS_H
#define UITREE_IFACE_STATS_H

#include "ui/uitree.h"

#include <stdint.h>

/**
 * Per-interface-group open/close ledger (TORIRS_IFACE_STATS=1).
 *
 * Aggregates live in torirs_perf gauges; this names the panel. Records are
 * keyed by group id (component_id >> 16). Enabled only when the env var is
 * set — the record path is a no-op otherwise.
 */

void
UITreeIfaceStats_NoteOpen(int group_id);

void
UITreeIfaceStats_NoteClose(int group_id);

void
UITreeIfaceStats_NoteBake(int group_id);

void
UITreeIfaceStats_NoteBakeReuse(int group_id);

/** Refresh live/hidden/hook-block tallies from the tree and print if due. */
void
UITreeIfaceStats_Tick(
    struct UITree const* tree,
    int tick);

/** Distinct resident groups + hidden/unmounted/freed/hook-block gauges. */
void
UITreeIfaceStats_SampleGauges(struct UITree const* tree);

#endif
