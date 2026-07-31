#ifndef TASK_CS2_RUN_H
#define TASK_CS2_RUN_H

#include "asyncio.h"

#include <stdint.h>

struct RS_CS2Host;
struct CS2VM2_Script;

/**
 * Cooperative CS2 script runner: binds RS_CS2Host_Exec, runs CS2VM2_ThreadRun,
 * and on YIELD awaits the matching CreateTask_*Load then re-enters ThreadRun
 * (CS2VM2 restores the opcode site so the host succeeds without PushCallScript).
 */
struct ToriRS_Task*
CreateTask_CS2Run(
    struct RS_CS2Host* host,
    int script_id,
    int active_component_id,
    int dot_component_id,
    int const* int_args,
    int int_arg_count);

/**
 * CS2Run with mixed positional hook args. args/arg_count is the full positional
 * list; str_mask bit i marks position i as a string arg whose value comes from
 * str_args[] (position order, k-th set bit -> str_args[k]). Ints fill the
 * script's int locals in order, strings its string locals in order.
 */
struct ToriRS_Task*
CreateTask_CS2RunMixed(
    struct RS_CS2Host* host,
    int script_id,
    int active_component_id,
    int dot_component_id,
    int const* args,
    int arg_count,
    uint64_t str_mask,
    char const* const* str_args,
    int str_arg_count);

/** Same as CreateTask_CS2Run but starts from an already-loaded script pointer. */
struct ToriRS_Task*
CreateTask_CS2RunScript(
    struct RS_CS2Host* host,
    struct CS2VM2_Script* script,
    int active_component_id,
    int dot_component_id,
    int const* int_args,
    int int_arg_count);

/**
 * Run registered inv-transmit hooks whose triggers match container_id
 * (or all hooks when container_id < 0).
 */
struct ToriRS_Task*
CreateTask_CS2InvTransmitDispatch(
    struct RS_CS2Host* host,
    int container_id);

/**
 * Run registered var-transmit hooks whose triggers match var_id
 * (or all hooks when var_id < 0).
 */
struct ToriRS_Task*
CreateTask_CS2VarTransmitDispatch(
    struct RS_CS2Host* host,
    int var_id);

/**
 * Same, for a whole set of changed var ids (RS_CS2Host::var_changed_ids): a hook
 * runs when any of them is one of its triggers. An empty set means every hook,
 * which is what an unhide needs (the trigger lists say nothing about it).
 */
struct ToriRS_Task*
CreateTask_CS2VarTransmitDispatchSet(
    struct RS_CS2Host* host,
    int const* var_ids,
    int var_count);

/**
 * Run every `on_sub_change` hook in the tree.
 *
 * These are the "a sub-interface came or went" scripts, and the gameframe's are
 * what decide whether the sidebar shows the tab strip or whatever replaced it.
 * The mount path already runs them (task_interface_open step 8); the *unmount*
 * path did not, so closing a side panel left the tabs hidden and the sidebar
 * blank. Hooks are snapshotted before the first one runs, because a hook may
 * mutate the tree it was found in.
 */
struct ToriRS_Task*
CreateTask_CS2SubChangeDispatch(struct RS_CS2Host* host);

/*
 * Re-run every IF_SETONMISCTRANSMIT hook: the run-energy and run-weight orbs.
 *
 * Driven once per logic tick from RS_CS2_DispatchTransmits when
 * misc_transmit_dirty is set. Unlike the var and inv dispatches there is no
 * trigger set to filter on — a misc hook registers no ids — so this fires all
 * of them, and the caller gates on a real value change instead. Hooks are
 * snapshotted before the first one runs, because a hook may mutate the tree it
 * was found in.
 */
struct ToriRS_Task*
CreateTask_CS2MiscTransmitDispatch(struct RS_CS2Host* host);

#endif /* TASK_CS2_RUN_H */
