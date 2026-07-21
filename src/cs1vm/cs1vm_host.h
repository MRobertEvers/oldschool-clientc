#ifndef CS1VM_HOST_H
#define CS1VM_HOST_H

/*
 * The CS1VM host boundary. The VM core never reads game state or the cache
 * directly: every state-reading opcode builds a CS1VM_HostRequest and calls
 * vm->host_exec. Mirrors cs2vm2_host.h, with one difference — CS1 has no
 * operand stack, so the host writes its answer into request->out_value
 * instead of pushing onto the thread.
 */

enum CS1VM_HostRequestKind
{
    CS1VM_HOST_REQUEST_STAT_LEVEL,        /* op 1  */
    CS1VM_HOST_REQUEST_STAT_BASE_LEVEL,   /* op 2  */
    CS1VM_HOST_REQUEST_STAT_XP,           /* op 3  */
    CS1VM_HOST_REQUEST_STAT_XP_REMAINING, /* op 6  */
    CS1VM_HOST_REQUEST_INV_COUNT,         /* op 4  */
    CS1VM_HOST_REQUEST_INV_CONTAINS,      /* op 10 */
    CS1VM_HOST_REQUEST_VARP,              /* ops 5, 7, 13 — VM applies the scale/bit test */
    CS1VM_HOST_REQUEST_VARBIT,            /* op 14 */
    CS1VM_HOST_REQUEST_COMBAT_LEVEL,      /* op 8  */
    CS1VM_HOST_REQUEST_TOTAL_LEVEL,       /* op 9  */
    CS1VM_HOST_REQUEST_RUNENERGY,         /* op 11 */
    CS1VM_HOST_REQUEST_RUNWEIGHT,         /* op 12 */
    CS1VM_HOST_REQUEST_COORD_X,           /* op 18 */
    CS1VM_HOST_REQUEST_COORD_Z,           /* op 19 */
};

struct CS1VM_HostRequest_Stat
{
    int skill;
};

struct CS1VM_HostRequest_Inv
{
    int component_id;
    int obj_id;
};

struct CS1VM_HostRequest_Varp
{
    int varp_id;
};

struct CS1VM_HostRequest_Varbit
{
    int varbit_id;
};

struct CS1VM_HostRequest
{
    enum CS1VM_HostRequestKind kind;

    /** Host writes the read value here before returning CS1VM_EXECNO_OK. */
    int out_value;

    union
    {
        struct CS1VM_HostRequest_Stat stat;
        struct CS1VM_HostRequest_Inv inv;
        struct CS1VM_HostRequest_Varp varp;
        struct CS1VM_HostRequest_Varbit varbit;
    } u;
};

struct CS1VM;

/**
 * Returns CS1VM_EXECNO_OK (out_value filled), CS1VM_EXECNO_YIELD (host staged a
 * pending load and left VM state untouched), or CS1VM_EXECNO_ERROR.
 */
typedef int (*CS1VM_HostExec_Fn)(
    struct CS1VM* vm,
    struct CS1VM_HostRequest* request);

#endif /* CS1VM_HOST_H */
