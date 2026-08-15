#ifndef TASK_CS1_RUN_H
#define TASK_CS1_RUN_H

struct ToriRS_Task;
struct RS_CS1Host;

/**
 * Evaluates every CS1-bearing component in the host's tree, caching the active
 * state and %N values onto each node. Services the VM's asset yields through
 * the cache-load task pipeline, then re-runs the component.
 *
 * Sets host->eval_dirty when a cached result changed, so the caller knows a
 * redraw is needed. Returns NULL when there is nothing to evaluate — callers
 * use PT_TASK_AWAITSELF_IF.
 */
struct ToriRS_Task*
CreateTask_CS1Eval(struct RS_CS1Host* host);

#endif /* TASK_CS1_RUN_H */
