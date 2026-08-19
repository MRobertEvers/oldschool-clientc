#ifndef RS_VARC_IDS_H
#define RS_VARC_IDS_H

/*
 * Named VarC ids.
 *
 * Most varcs are touched only by CS2 scripts (which carry their own numeric
 * ids), so the C client never needs to name them. A handful, though, are read
 * or written by client C code — and those ids are cache/revision specific.
 * Rather than scatter magic numbers, C code resolves them through this small
 * interface: `varc_ids_for_revision()` returns the id set for a revision, and
 * call sites reference fields by name.
 *
 * To support a new revision, add a `struct VarCIds` instance below and map it in
 * varc_ids_for_revision(); a field of -1 means "not present / unused" for that
 * revision.
 */

/** Logical -> concrete varc id map for one revision. */
struct VarCIds
{
    /* VarC STRING: the chatbox text-input dialog mirrors its current input
     * string here (CS2 reads it back to submit). */
    int chatbox_input_string;
    /*
     * VarC INT: an interface's own text box has taken the keyboard.
     *
     * Set to 1 by `~chatdefault_stopinput` (proc 2157), which is what every
     * panel with a search field calls — settings 134, collection log 621,
     * league tasks — along with disarming the chatbox's onKey. Put back to 0 by
     * `~chatdefault_restoreinput` (proc 4079, wrapper clientscript 2158), which
     * re-arms it. Never written reads -1, so the test is `== 1` and not
     * `!= 0`.
     *
     * The C client reads it for one thing: while a box owns the keyboard no
     * client-side hotkey may fire, or a search term switches tabs and flies the
     * camera as it is typed.
     */
    int interface_input_active;
};

/* OSRS revision 230 (and the near-era caches this client targets). */
static const struct VarCIds VARC_IDS_OSRS230 = {
    .chatbox_input_string = 335,
    .interface_input_active = 11,
};

/** Resolve the varc id set for a revision. Defaults to the OSRS-230 set (the
 *  ids are stable across the near-230 caches this client targets). `rev` is a
 *  GameProtoRevision value; unknown revisions fall back to the default. */
static inline const struct VarCIds*
varc_ids_for_revision(int rev)
{
    (void)rev;
    return &VARC_IDS_OSRS230;
}

#endif /* RS_VARC_IDS_H */
