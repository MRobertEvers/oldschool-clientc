#ifndef RSPROT_TABLES_DEF_H
#define RSPROT_TABLES_DEF_H

#include "../include/rsprot_rev.h"
#include "rsprot_buf.h"

#include <string.h>

/*
 * Shared types for the generated per-revision prot tables in 3rd/rsprot/gen/,
 * which tools/rsprot_gen.py writes from spec/tables.
 */

#define RSPROT_VARU8 (-1)  /* Prot.VAR_BYTE  */
#define RSPROT_VARU16 (-2) /* Prot.VAR_SHORT */

typedef struct RsprotProtDef {
	const char *name; /* RSProt's own prot name, e.g. "IF_OPENTOP" */
	int code;         /* wire opcode; -1 if the prot has no top-level opcode
	                    * (UPDATE_ZONE_PARTIAL_ENCLOSED sub-packets only) */
	int size;          /* payload byte count, or RSPROT_VARU8/RSPROT_VARU16 */
	/* The version of the packet this row calls -- its codec's registry name,
	 * or its stream's -- where that is not the row's own name: at 223-225
	 * RSProt calls a prot X / X_OLD whose codecs and streams are filed as
	 * X_V2 / X_V1. NULL everywhere else: the row calls `name`. */
	const char *codec;
} RsprotProtDef;

/** The packet version `def` calls: rsprot_registry_resolve_def's first try,
 *  and the name a stream reader is chosen by. */
static inline const char *
rsprot_prot_version(const RsprotProtDef *def)
{
	return def->codec ? def->codec : def->name;
}

typedef struct RsprotRevTable {
	RsprotRev rev;
	const RsprotProtDef *server;
	int server_count;
	const RsprotProtDef *client;
	int client_count;
	const char *const *zone_order; /* UPDATE_ZONE_PARTIAL_ENCLOSED sub-packet
	                                 * index -> RSProt prot name, in ordinal order */
	int zone_count;
} RsprotRevTable;

/* Linear scan by prot name — tables are a few hundred rows and this runs at
 * boot/codegen time, not per packet, so a hash map buys nothing here. */
static inline const RsprotProtDef *rsprot_find_server_prot(const RsprotRevTable *t, const char *name)
{
	int i;
	for (i = 0; i < t->server_count; i++)
		if (strcmp(t->server[i].name, name) == 0)
			return &t->server[i];
	return 0;
}

static inline const RsprotProtDef *rsprot_find_client_prot(const RsprotRevTable *t, const char *name)
{
	int i;
	for (i = 0; i < t->client_count; i++)
		if (strcmp(t->client[i].name, name) == 0)
			return &t->client[i];
	return 0;
}

static inline const RsprotProtDef *rsprot_find_server_by_opcode(const RsprotRevTable *t, int opcode)
{
	int i;
	for (i = 0; i < t->server_count; i++)
		if (t->server[i].code == opcode)
			return &t->server[i];
	return 0;
}

static inline const RsprotProtDef *rsprot_find_client_by_opcode(const RsprotRevTable *t, int opcode)
{
	int i;
	for (i = 0; i < t->client_count; i++)
		if (t->client[i].code == opcode)
			return &t->client[i];
	return 0;
}

/* UPDATE_ZONE_PARTIAL_ENCLOSED sub-packet ordinal -> RSProt prot name, or NULL
 * if out of range. */
static inline const char *rsprot_zone_prot_name(const RsprotRevTable *t, int ordinal)
{
	if (ordinal < 0 || ordinal >= t->zone_count)
		return 0;
	return t->zone_order[ordinal];
}

/*
 * UPDATE_ZONE_PARTIAL_ENCLOSED's payload is its sub-packets back to back, each
 * an ordinal byte (the revision's [zone_order]) and that prot's payload. This
 * reads the next ordinal and returns its prot, leaving `buf` at the sub-packet's
 * payload for that prot's codec. NULL at the end of `buf`, and for an ordinal
 * the table has no prot for (`*ordinal` says which): the walk cannot step past
 * a sub-packet it cannot size.
 */
static inline const char *rsprot_zone_enclosed_next(RsprotBuf *buf, const RsprotRevTable *t, int *ordinal)
{
	*ordinal = -1;
	if (rsprot_buf_readable(buf) <= 0 || buf->err)
		return 0;
	*ordinal = rsprot_g1(buf);
	return rsprot_zone_prot_name(t, *ordinal);
}

#endif /* RSPROT_TABLES_DEF_H */
