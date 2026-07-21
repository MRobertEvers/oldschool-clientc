#ifndef CS1VM_SCRIPT_H
#define CS1VM_SCRIPT_H

/*
 * CS1 script containers. Unlike CS2 there is no clientscript archive: IF1
 * value scripts are embedded in the interface component that owns them, so a
 * "script" is just a borrowed view over one opcode stream and the component's
 * script set is the unit the VM is handed.
 */

/** One embedded IF1 value script: a flat opcode/operand int stream. */
struct CS1VM_Script
{
    int const* ops;
    int op_count;
};

/**
 * A component's CS1 data, as borrowed views (owned by UITreeBehavior or a test
 * fixture). scripts[i] is compared against operand[i] using comparator[i];
 * the two arrays are sized independently in the cache format, hence the
 * separate comparator_count.
 */
struct CS1VM_ComponentScripts
{
    int scripts_count;
    int* const* scripts;
    int const* scripts_lengths;

    int comparator_count;
    int const* comparator;
    int const* operand;
};

/** Returns 0 and fills out on success, -1 if index is out of range or unusable. */
int
CS1VM_ComponentScriptGet(
    struct CS1VM_ComponentScripts const* set,
    int index,
    struct CS1VM_Script* out);

#endif /* CS1VM_SCRIPT_H */
