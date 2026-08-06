/*
 * tool_progress.h -- a progress bar for the long-running cache tools.
 *
 * A full pack writes six figures of archives and prints nothing between the
 * per-type summaries, so the honest reading of the terminal for minutes at a
 * time is "this has hung". It has not; there is simply no unit of work being
 * reported. This is that unit.
 *
 * Two shapes, chosen by whether stderr is a terminal:
 *
 *   interactive  a single line rewritten in place with \r. Cheap and readable,
 *                and it disappears when the phase ends.
 *   redirected   one line per 5% step. A \r bar in a log file is a single
 *                enormous line full of control characters, which is worse than
 *                no bar at all -- and every run in this repo is redirected to a
 *                log, so that is the case that has to stay legible.
 *
 * It writes to stderr on purpose: stdout carries the tool's actual report, and
 * a caller doing `cachepack ... > report.txt` should get the report, not a bar.
 */
#ifndef RSCACHE_TOOLS_PROGRESS_H
#define RSCACHE_TOOLS_PROGRESS_H

#include <stdio.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <io.h>
#define TOOL_PROGRESS_ISATTY() (_isatty(2) != 0)
#else
#include <unistd.h>
#define TOOL_PROGRESS_ISATTY() (isatty(2) != 0)
#endif

struct Tool_Progress
{
    const char* label;
    long total;
    long done;
    int tty;
    int last_step; /* percent/5, so a redirected run prints 21 lines at most */
    time_t started;
};

/* Elapsed and remaining, as m:ss. One second of resolution is right for phases
 * measured in minutes, and time() needs no platform shim. */
static __inline void
tool_progress_clock(long secs, char* out, size_t out_size)
{
    if( secs < 0 )
        secs = 0;
    snprintf(out, out_size, "%ld:%02ld", secs / 60, secs % 60);
}

static __inline void
tool_progress_render(struct Tool_Progress* p, const char* suffix)
{
    int pct = p->total > 0 ? (int)((p->done * 100) / p->total) : 100;
    long elapsed = (long)(time(NULL) - p->started);
    char el[16];
    char eta[16];

    if( pct < 0 )
        pct = 0;
    if( pct > 100 )
        pct = 100;

    tool_progress_clock(elapsed, el, sizeof(el));
    /* Linear extrapolation from work done so far. Crude, and honest about it --
     * asset kinds are wildly different sizes, so this is a scale cue rather than
     * a promise. */
    if( p->done > 0 && p->done < p->total )
        tool_progress_clock(elapsed * (p->total - p->done) / p->done, eta, sizeof(eta));
    else
        snprintf(eta, sizeof(eta), "--:--");

    if( p->tty )
    {
        char bar[41];
        int fill = (pct * 40) / 100;

        memset(bar, '-', sizeof(bar) - 1);
        memset(bar, '#', (size_t)fill);
        bar[sizeof(bar) - 1] = '\0';
        fprintf(stderr, "\r  %-14s [%s] %3d%% (%ld/%ld) %s elapsed, ~%s left%s", p->label, bar,
                pct, p->done, p->total, el, eta, suffix ? suffix : "");
    }
    else
    {
        fprintf(stderr, "  %-14s %3d%% (%ld/%ld) %s elapsed, ~%s left%s\n", p->label, pct,
                p->done, p->total, el, eta, suffix ? suffix : "");
    }
    fflush(stderr);
}

static __inline void
tool_progress_begin(struct Tool_Progress* p, const char* label, long total)
{
    p->label = label ? label : "";
    p->total = total;
    p->done = 0;
    p->tty = TOOL_PROGRESS_ISATTY();
    p->last_step = -1;
    p->started = time(NULL);
    if( total > 0 )
        tool_progress_render(p, NULL);
}

static __inline void
tool_progress_step(struct Tool_Progress* p, long n)
{
    int step;

    p->done += n;
    if( p->total <= 0 )
        return;

    /* Rate-limit by 5% rather than by time: no clock is needed, the output is
     * deterministic (so two runs diff cleanly), and a redirected log gets a
     * bounded number of lines however many archives go by. */
    step = (int)((p->done * 20) / p->total);
    if( step == p->last_step )
        return;
    p->last_step = step;
    tool_progress_render(p, NULL);
}

static __inline void
tool_progress_end(struct Tool_Progress* p)
{
    if( p->total <= 0 )
        return;
    p->done = p->total;
    tool_progress_render(p, p->tty ? "" : NULL);
    if( p->tty )
        fputc('\n', stderr);
}

#endif /* RSCACHE_TOOLS_PROGRESS_H */
