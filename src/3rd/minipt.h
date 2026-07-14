#ifndef MINIPT_H
#define MINIPT_H

/**
 * minipt.h - A standalone, single-header protothread implementation.
 * * Based on the concepts by Adam Dunkels and Simon Tatham.
 */

#ifndef PT_ENABLE_USER_PTR
#define PT_ENABLE_USER_PTR 1
#endif

// The protothread state structure. Only requires 2 bytes.
struct pt
{
    unsigned short lc;
#if PT_ENABLE_USER_PTR == 1
    void* user;
#endif
};

// Return states for the protothread functions
#define PT_WAITING 0
#define PT_YIELDED 1
#define PT_EXITED 2
#define PT_ENDED 3

// clang-format off
/**
 * Initializes the protothread state. Must be called before the thread runs.
 */
#define PT_INIT(s) \
    do { (s)->lc = 0; } while(0)

/**
 * Declares the start of the protothread C logic. 
 * Opens the hidden switch statement.
 */
#define PT_BEGIN(s) \
    switch((s)->lc) { case 0:

/**
 * Blocks the thread until the condition is true.
 * Yields PT_WAITING to the caller while blocked.
 */
#define PT_WAIT_UNTIL(s, cond) \
    do { \
        (s)->lc = __LINE__; \
        case __LINE__: \
        if (!(cond)) return PT_WAITING; \
    } while(0)

/**
 * Blocks the thread while the condition is true.
 */
#define PT_WAIT_WHILE(s, cond) \
    PT_WAIT_UNTIL((s), !(cond))

/**
 * Voluntarily yields control back to the caller.
 * The next time the thread is called, it resumes immediately after this.
 */
#define PT_YIELD(s) \
    do { \
        (s)->lc = __LINE__; \
        return PT_YIELDED; \
        case __LINE__:; \
    } while(0)

/**
 * Restarts the protothread from the beginning.
 */
#define PT_RESTART(s) \
    do { \
        (s)->lc = 0; \
        return PT_WAITING; \
    } while(0)

/**
 * Exits the protothread early.
 */
#define PT_EXIT(s) \
    do { \
        (s)->lc = 0; \
        return PT_EXITED; \
    } while(0)

/**
 * Declares the end of the protothread.
 * Closes the hidden switch statement and resets the state.
 */
#define PT_END(s) \
    } \
    (s)->lc = 0; \
    return PT_ENDED

// clang-format on

#endif // MINIPT_H