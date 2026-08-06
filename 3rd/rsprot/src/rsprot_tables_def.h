#ifndef RSPROT_TABLES_DEF_H
#define RSPROT_TABLES_DEF_H

#include <string.h>

/*
 * Shared types for the generated per-revision prot tables in 3rd/rsprot/gen/.
 * See tools/rsprot_gen_tables.py, which is the only thing that should ever
 * write a file in that directory.
 */

#define RSPROT_VARU8 (-1)  /* Prot.VAR_BYTE  */
#define RSPROT_VARU16 (-2) /* Prot.VAR_SHORT */

typedef struct RsprotProtDef {
	const char *name; /* RSProt's own prot name, e.g. "IF_OPENTOP" */
	int code;         /* wire opcode; -1 if the prot has no top-level opcode
	                    * (UPDATE_ZONE_PARTIAL_ENCLOSED sub-packets only) */
	int size;          /* payload byte count, or RSPROT_VARU8/RSPROT_VARU16 */
} RsprotProtDef;

typedef struct RsprotRevTable {
	int revision;
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

#endif /* RSPROT_TABLES_DEF_H */
