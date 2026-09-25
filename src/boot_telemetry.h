#ifndef SRC_BOOT_TELEMETRY_H
#define SRC_BOOT_TELEMETRY_H

#include "task_runner.h"

#include <stdint.h>
#include <stdio.h>

/*
 * The startup, as a list of named moments and two runners' counters.
 *
 * A boot is a sequence -- runtime up, manifest read, indices opened, tables
 * filled, title shown, login sent, login answered, world loaded, ready -- and
 * the question "why did it take seven seconds" is answered by which of those
 * gaps was wide, not by the total. Every stage that matters drops a mark here
 * with the wall clock, and the report prints them with the gap to the one
 * before.
 *
 * The runner half (TaskRunnerTelemetry, task_runner.h) says what the queues
 * were doing in those gaps: how many passes ended with every answer still on
 * the wire, and which tasks asked for their reads one at a time.
 *
 * On the desktop TORIRS_BOOT_STATS=1 prints the report when the client is
 * ready. In the browser the page pulls the same thing as JSON
 * (torirs_telemetry_json, main.c) and tools/web/browser_probe.py prints it.
 */

#define TORIRS_BOOT_MARKS 96

struct ToriRS_BootMark
{
    char name[48];
    uint64_t at_ms;
};

/** Record that `name` happened now. Cheap, unconditional, bounded. */
void
ToriRS_BootTelemetry_Mark(char const* name);

/** Like Mark, with printf formatting for a name that carries a number. */
void
ToriRS_BootTelemetry_Markf(
    char const* fmt,
    ...);

int
ToriRS_BootTelemetry_MarkCount(void);

struct ToriRS_BootMark const*
ToriRS_BootTelemetry_MarkAt(int index);

/** The human report: marks with gaps, then both runners' counters. */
void
ToriRS_BootTelemetry_Report(
    FILE* out,
    struct TaskRunner const* assets,
    struct TaskRunner const* exec);

/** The same as JSON, malloc'd; the caller frees it. */
char*
ToriRS_BootTelemetry_Json(
    struct TaskRunner const* assets,
    struct TaskRunner const* exec);

#endif
