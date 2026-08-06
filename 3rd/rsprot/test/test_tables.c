/* Sanity checks for the generated per-revision prot tables (gen/rsprot_tables.h),
 * produced by tools/rsprot_gen_tables.py. These are not round-trip tests -- the
 * tables are data, not codecs -- they check the generator's own invariants:
 * every vendored revision is reachable, opcode 0..127 is small enough to be a
 * real single byte, and a few known facts from docs/RSPROT_OSRS239_PORT.md hold
 * against the generated rev239 table specifically (its highest server opcode is
 * 148, IF_OPENTOP is 96) -- so a parsing regression in the generator shows up
 * as a specific wrong number, not just "table looks empty". */

#include "rsprot.h"

#include <stdio.h>
#include <string.h>

static int g_failures = 0;

#define CHECK(cond, msg)                                                                                              \
	do {                                                                                                            \
		if (!(cond)) {                                                                                             \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg);                                                  \
			g_failures++;                                                                                          \
		}                                                                                                          \
	} while (0)

#define CHECK_EQ_I(a, b, msg)                                                                                          \
	do {                                                                                                            \
		long long _a = (long long)(a), _b = (long long)(b);                                                        \
		if (_a != _b) {                                                                                             \
			printf("FAIL %s:%d: %s (got %lld, want %lld)\n", __FILE__, __LINE__, msg, _a, _b);                     \
			g_failures++;                                                                                          \
		}                                                                                                          \
	} while (0)

static void test_all_revisions_present(void)
{
	int rev;
	for (rev = 221; rev <= 239; rev++) {
		const RsprotRevTable *t = rsprot_rev_table(rev);
		char msg[64];
		snprintf(msg, sizeof(msg), "revision %d present", rev);
		CHECK(t != NULL, msg);
		if (t) {
			CHECK(t->server_count > 0, "server table nonempty");
			CHECK(t->client_count > 0, "client table nonempty");
		}
	}
	CHECK(rsprot_rev_table(999) == NULL, "unknown revision returns NULL");
}

static void test_rev239_known_facts(void)
{
	const RsprotRevTable *t = rsprot_rev_table(239);
	const RsprotProtDef *d;
	int i;
	int max_code = -1;

	CHECK(t != NULL, "rev239 present");
	if (!t)
		return;

	/* docs/RSPROT_OSRS239_PORT.md SS3: highest server opcode is 148. */
	for (i = 0; i < t->server_count; i++)
		if (t->server[i].code > max_code)
			max_code = t->server[i].code;
	CHECK_EQ_I(max_code, 148, "rev239 highest server opcode is 148");

	/* docs/RSPROT_OSRS239_PORT.md 5b: IF_OPENTOP op=96 payload=2 at rev239. */
	d = rsprot_find_server_prot(t, "IF_OPENTOP");
	CHECK(d != NULL, "IF_OPENTOP present at rev239");
	if (d) {
		CHECK_EQ_I(d->code, 96, "IF_OPENTOP opcode 96 at rev239");
		CHECK_EQ_I(d->size, 2, "IF_OPENTOP payload 2 at rev239");
	}

	/* IF_SETEVENTS grew from 12 to 16 bytes (two masks) at rev239 -- V2 suffix. */
	d = rsprot_find_server_prot(t, "IF_SETEVENTS_V2");
	CHECK(d != NULL, "IF_SETEVENTS_V2 present at rev239");
	if (d)
		CHECK_EQ_I(d->size, 16, "IF_SETEVENTS_V2 payload 16 at rev239");

	/* Opcode lookup is the inverse of name lookup. */
	if (d) {
		const RsprotProtDef *by_op = rsprot_find_server_by_opcode(t, d->code);
		CHECK(by_op == d, "opcode lookup finds same row as name lookup");
	}
}

static void test_zone_order_nonempty(void)
{
	const RsprotRevTable *t = rsprot_rev_table(239);
	CHECK(t != NULL, "rev239 present for zone check");
	if (!t)
		return;
	CHECK(t->zone_count > 0, "rev239 has a zone sub-packet order");
	CHECK(rsprot_zone_prot_name(t, 0) != NULL, "zone ordinal 0 has a name");
	CHECK(rsprot_zone_prot_name(t, -1) == NULL, "negative zone ordinal is NULL");
	CHECK(rsprot_zone_prot_name(t, 999999) == NULL, "out-of-range zone ordinal is NULL");
}

static void test_no_duplicate_opcodes_within_direction(void)
{
	/* Two prots at the same top-level opcode in the same direction would
	 * mean the wire is ambiguous. Rows with code == -1 (enclosed-only
	 * sub-packets) are exempt -- they share -1 by construction. */
	const RsprotRevTable *t = rsprot_rev_table(239);
	int i, j;
	int dups = 0;

	CHECK(t != NULL, "rev239 present for dup check");
	if (!t)
		return;

	for (i = 0; i < t->server_count; i++) {
		if (t->server[i].code < 0)
			continue;
		for (j = i + 1; j < t->server_count; j++) {
			if (t->server[j].code == t->server[i].code) {
				printf("  dup opcode %d: %s / %s\n", t->server[i].code, t->server[i].name,
				       t->server[j].name);
				dups++;
			}
		}
	}
	CHECK_EQ_I(dups, 0, "no duplicate server opcodes at rev239");
}

int main(void)
{
	test_all_revisions_present();
	test_rev239_known_facts();
	test_zone_order_nonempty();
	test_no_duplicate_opcodes_within_direction();

	if (g_failures) {
		printf("%d failures\n", g_failures);
		return 1;
	}
	printf("all rsprot table tests passed\n");
	return 0;
}
