#include "game/rs_save_debounce.h"

#include <assert.h>

void
RS_SaveDebounce_Mark(
    struct RS_SaveDebounce* debounce,
    uint64_t cycle)
{
    assert(debounce);
    debounce->dirty_cycle = cycle;
}

bool
RS_SaveDebounce_Take(
    struct RS_SaveDebounce* debounce,
    bool changed)
{
    bool wanted;

    assert(debounce);
    wanted = changed || debounce->dirty_cycle != 0;
    debounce->dirty_cycle = 0;
    return wanted;
}

bool
RS_SaveDebounce_Due(
    struct RS_SaveDebounce const* debounce,
    uint64_t cycle,
    uint64_t settle)
{
    assert(debounce);
    return debounce->dirty_cycle && cycle - debounce->dirty_cycle >= settle;
}

void
RS_SaveDebounce_Clear(struct RS_SaveDebounce* debounce)
{
    assert(debounce);
    debounce->dirty_cycle = 0;
}
