#include "boot_telemetry.h"

#include "platform/platform_window.h"

#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

static struct ToriRS_BootMark g_marks[TORIRS_BOOT_MARKS];
static int g_mark_count;
static int g_mark_dropped;

void
ToriRS_BootTelemetry_Mark(char const* name)
{
    struct ToriRS_BootMark* mark;

    assert(name);
    if( g_mark_count == TORIRS_BOOT_MARKS )
    {
        g_mark_dropped++;
        return;
    }
    mark = &g_marks[g_mark_count++];
    strncpy(mark->name, name, sizeof(mark->name) - 1);
    mark->name[sizeof(mark->name) - 1] = '\0';
    mark->at_ms = PlatformWindow_Ticks64();
}

void
ToriRS_BootTelemetry_Markf(
    char const* fmt,
    ...)
{
    char name[sizeof(g_marks[0].name)];
    va_list ap;

    assert(fmt);
    va_start(ap, fmt);
    vsnprintf(name, sizeof(name), fmt, ap);
    va_end(ap);
    ToriRS_BootTelemetry_Mark(name);
}

int
ToriRS_BootTelemetry_MarkCount(void)
{
    return g_mark_count;
}

struct ToriRS_BootMark const*
ToriRS_BootTelemetry_MarkAt(int index)
{
    assert(index >= 0);
    assert(index < g_mark_count);
    return &g_marks[index];
}

static void
report_runner(
    FILE* out,
    char const* label,
    struct TaskRunner const* runner)
{
    struct TaskRunnerTelemetry const* t;

    assert(out);
    assert(label);
    if( !runner )
        return;
    t = &runner->telemetry;
    fprintf(
        out,
        "telemetry: %s passes=%ld pump_passes=%ld waiting_passes=%ld tasks_run=%ld "
        "reads=%ld batches=%ld max_batch=%d\n",
        label,
        t->passes,
        t->pump_passes,
        t->passes_waiting,
        t->tasks_run,
        t->reads_issued,
        t->batches,
        t->max_batch);
    for( int i = 0; i < t->by_task_count; i++ )
    {
        struct TaskRunnerTaskTelemetry const* row = &t->by_task[i];
        if( row->reads == 0 )
            continue;
        fprintf(
            out,
            "telemetry:   %-28s runs=%-6ld reads=%-6ld chained=%-6ld max_chain=%d\n",
            row->name,
            row->runs,
            row->reads,
            row->chained_reads,
            row->max_chain);
    }
    if( t->by_task_overflow )
        fprintf(out, "telemetry:   (%d task names past the table)\n", t->by_task_overflow);
}

void
ToriRS_BootTelemetry_Report(
    FILE* out,
    struct TaskRunner const* assets,
    struct TaskRunner const* exec)
{
    uint64_t prev = 0;

    assert(out);
    for( int i = 0; i < g_mark_count; i++ )
    {
        uint64_t at = g_marks[i].at_ms - (g_mark_count ? g_marks[0].at_ms : 0);
        fprintf(
            out,
            "telemetry: %8llu ms  +%-6llu %s\n",
            (unsigned long long)at,
            (unsigned long long)(i ? at - prev : 0),
            g_marks[i].name);
        prev = at;
    }
    if( g_mark_dropped )
        fprintf(out, "telemetry: (%d marks dropped)\n", g_mark_dropped);
    report_runner(out, "assets", assets);
    report_runner(out, "exec", exec);
}

/* A growable string, because the JSON's size depends on how many task names
 * the runners saw. */
struct JsonOut
{
    char* buf;
    size_t len;
    size_t cap;
};

static void
json_put(
    struct JsonOut* o,
    char const* fmt,
    ...)
{
    va_list ap;
    int n;

    assert(o);
    for( ;; )
    {
        va_start(ap, fmt);
        n = vsnprintf(o->buf + o->len, o->cap - o->len, fmt, ap);
        va_end(ap);
        assert(n >= 0);
        if( (size_t)n < o->cap - o->len )
        {
            o->len += (size_t)n;
            return;
        }
        o->cap = o->cap * 2 + (size_t)n + 1;
        o->buf = realloc(o->buf, o->cap);
        assert(o->buf);
    }
}

static void
json_runner(
    struct JsonOut* o,
    char const* label,
    struct TaskRunner const* runner)
{
    struct TaskRunnerTelemetry const* t;
    int first = 1;

    assert(o);
    assert(label);
    if( !runner )
    {
        json_put(o, "\"%s\":null", label);
        return;
    }
    t = &runner->telemetry;
    json_put(
        o,
        "\"%s\":{\"passes\":%ld,\"pump_passes\":%ld,\"waiting_passes\":%ld,"
        "\"tasks_run\":%ld,\"reads\":%ld,\"batches\":%ld,\"max_batch\":%d,\"tasks\":[",
        label,
        t->passes,
        t->pump_passes,
        t->passes_waiting,
        t->tasks_run,
        t->reads_issued,
        t->batches,
        t->max_batch);
    for( int i = 0; i < t->by_task_count; i++ )
    {
        struct TaskRunnerTaskTelemetry const* row = &t->by_task[i];
        json_put(
            o,
            "%s{\"name\":\"%s\",\"runs\":%ld,\"reads\":%ld,\"chained\":%ld,\"max_chain\":%d}",
            first ? "" : ",",
            row->name,
            row->runs,
            row->reads,
            row->chained_reads,
            row->max_chain);
        first = 0;
    }
    json_put(o, "]}");
}

char*
ToriRS_BootTelemetry_Json(
    struct TaskRunner const* assets,
    struct TaskRunner const* exec)
{
    struct JsonOut o = { NULL, 0, 0 };

    o.cap = 4096;
    o.buf = malloc(o.cap);
    assert(o.buf);
    o.buf[0] = '\0';

    json_put(&o, "{\"marks\":[");
    for( int i = 0; i < g_mark_count; i++ )
    {
        json_put(
            &o,
            "%s{\"name\":\"%s\",\"ms\":%llu}",
            i ? "," : "",
            g_marks[i].name,
            (unsigned long long)(g_marks[i].at_ms - g_marks[0].at_ms));
    }
    json_put(&o, "],");
    json_runner(&o, "assets", assets);
    json_put(&o, ",");
    json_runner(&o, "exec", exec);
    json_put(&o, "}");
    return o.buf;
}
