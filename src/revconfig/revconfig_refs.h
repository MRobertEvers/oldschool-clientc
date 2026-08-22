#ifndef REVCONFIG_REFS_H
#define REVCONFIG_REFS_H

#include "revconfig.h"

/*
 * The resolved `[script:…]` / `[iface:…]` / `[varbit:…]` / `[varp:…]` /
 * `[seq:…]` / `[setting:…]` / `[font:…]` bindings of one boot, kept for the
 * life of the client.
 *
 * Why this exists separately from UIBuilderManifest: the manifest is built
 * inside Task_UITreeBuild and freed with it, but these ids are read all the way
 * through a session — the CS2 host wants its settings scripts at Init, the
 * overlay pass wants a font id every frame, a plugin wants a varbit whenever
 * the player toggles a row. So the App parses the same RevConfig sources once
 * into this table and holds it.
 *
 * Everything here answers "which id is that, on THIS cache". What a thing is
 * FOR stays in C, as the symbolic name at the call site.
 */

#define REVCONFIG_REFS_KIND_LEN 16
#define REVCONFIG_REFS_NAME_LEN 64

struct RevConfigRefEntry
{
    char kind[REVCONFIG_REFS_KIND_LEN];
    char name[REVCONFIG_REFS_NAME_LEN];

    /** The id C asks for. Fonts: the dat2 fonts-table archive id. */
    int id;

    /** Fonts only: the dat1 scene slot (RevConfig cache_font_id). -1 elsewhere. */
    int alt_id;
};

struct RevConfigRefs
{
    struct RevConfigRefEntry* entries;
    int count;
    int capacity;
};

void
RevConfigRefs_Init(struct RevConfigRefs* refs);

void
RevConfigRefs_Free(struct RevConfigRefs* refs);

/**
 * Ingest every ref-bearing item in `items`: RCITEM_CACHE_REF under its own
 * section kind, and RCITEM_CACHE_FONT under kind "font".
 *
 * A name declared twice replaces the earlier entry, so a boot manifest's inline
 * sections override the shared profile files it also names — the same
 * later-wins rule the UI manifest uses for its records.
 */
void
RevConfigRefs_AddItems(
    struct RevConfigRefs* refs,
    struct RevConfigItemBuffer const* items);

/**
 * Parse the three RevConfig sources a boot manifest can name, in load order.
 * Any of them may be NULL or "". `inline_ini` is read under the `revconfig`
 * section prefix, i.e. it is the boot manifest itself.
 *
 * Returns the number of entries the table holds afterwards.
 */
int
RevConfigRefs_LoadSources(
    struct RevConfigRefs* refs,
    char const* ui_ini,
    char const* cache_ini,
    char const* inline_ini);

/**
 * The declared id for `kind`/`name`, or -1.
 *
 * -1 means the running revision does not have that thing, and every caller
 * treats it that way: the feature is off, not defaulted. Do not substitute a
 * literal — a literal is what this table exists to delete.
 */
int
RevConfigRefs_Get(
    struct RevConfigRefs const* refs,
    char const* kind,
    char const* name);

/**
 * Cache font id for a `[font:<name>]` section: the dat1 scene slot when `dat1`,
 * otherwise the dat2 fonts-table archive id. -1 when undeclared.
 *
 * The two are different numbers for the same font (dat1 b12 is slot 2, dat2 b12
 * is archive 496), which is exactly why the caller states which it wants
 * instead of the table guessing from what happens to be filled in.
 */
int
RevConfigRefs_FontCacheId(
    struct RevConfigRefs const* refs,
    char const* name,
    int dat1);

#endif
