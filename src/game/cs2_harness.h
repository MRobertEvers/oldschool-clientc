#ifndef CS2_HARNESS_H
#define CS2_HARNESS_H

/*
 * Cross-client CS2 harness: run a shared case list and write one artifact per
 * case, matching Deobfuscator/instr/src/CS2Harness.java byte for byte in shape
 * so tools/perf/cs2_trace_diff.py can align the two instruction streams.
 *
 * Off unless TORIRS_CS2_HARNESS names a case file.
 */

struct RS_CS2Host;
struct TaskRunner;

/** Returns the number of cases run. */
int
CS2Harness_Run(
    struct RS_CS2Host* host,
    struct TaskRunner* runner,
    char const* cases_path,
    char const* out_dir);

#endif /* CS2_HARNESS_H */
