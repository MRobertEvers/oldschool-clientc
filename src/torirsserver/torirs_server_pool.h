#ifndef SRC_TORIRSSERVER_TORIRS_SERVER_POOL_H
#define SRC_TORIRSSERVER_TORIRS_SERVER_POOL_H
/*
 * The blocking-call pool: the one place a call that can block is allowed.
 *
 * No platform offers portable non-blocking file IO (kqueue and epoll report a
 * regular file as always ready, and macOS has no io_uring), and a password
 * hash is 60,000 rounds of HMAC-SHA256 of plain CPU. So the core loop never
 * makes those calls: a coroutine fills in a job and submits it, a pool thread
 * runs the job's function, and the core resumes the coroutine on the loop
 * thread once it is done.
 *
 * A job's function runs on a pool thread and must touch nothing but its own
 * job -- not the world, not a coroutine, not the session. Everything it needs
 * goes in by copy; everything it produces comes back in the job.
 *
 * ORDER. Each job carries a key, and jobs with the same key run one after
 * another in submission order on the same thread. The key is the account
 * (`ToriRSServer_PoolKey`), so a logout's save always lands before the same
 * account's next login reads the file. Different accounts run in parallel.
 *
 * INLINE. A pool made with zero threads runs each job inside Submit. The
 * client-embedded world, the browser build, scriptrun and the selftests use
 * that: no threads, and an answer before Submit returns, so their tick logs
 * stay deterministic.
 */
#include <stdint.h>

struct ToriRSServerPoll;
struct ToriRSServerPool;
struct ToriRSServerPoolJob;

typedef void (*ToriRSServerPoolFn)(struct ToriRSServerPoolJob* job);

struct ToriRSServerPoolJob
{
    /** Runs on a pool thread (or inline). Required. */
    ToriRSServerPoolFn fn;
    /** Whose turn order it shares; see ToriRSServer_PoolKey. */
    uint32_t key;
    /** Set by the pool once `fn` has returned; read on the loop thread only
     *  after ToriRSServer_PoolCollect handed the job back. */
    int done;
    /** Handed back with the job: the core puts the waiting coroutine here. */
    void* owner;
    struct ToriRSServerPoolJob* next;
};

/** `threads` workers (0 = inline). `wake` is poked from a worker each time a
 *  job finishes, so the loop's wait returns; NULL for an inline pool. */
struct ToriRSServerPool*
ToriRSServer_PoolNew(
    int threads,
    struct ToriRSServerPoll* wake);

/** Finish every submitted job, join the workers, free. NULL accepted. */
void
ToriRSServer_PoolFree(struct ToriRSServerPool* pool);

/** Queue `job`. The job must stay alive until Collect returns it. */
void
ToriRSServer_PoolSubmit(
    struct ToriRSServerPool* pool,
    struct ToriRSServerPoolJob* job);

/** Every job finished since the last call, as a list through `next`, in
 *  completion order. Loop thread only. */
struct ToriRSServerPoolJob*
ToriRSServer_PoolCollect(struct ToriRSServerPool* pool);

/** Jobs submitted and not yet collected. */
int
ToriRSServer_PoolPending(const struct ToriRSServerPool* pool);

/** The ordering key for an account name (case-folded FNV-1a). */
uint32_t
ToriRSServer_PoolKey(const char* name);

#endif
