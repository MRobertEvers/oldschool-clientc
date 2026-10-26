#ifndef RSPROT_REV_H
#define RSPROT_REV_H

/*
 * A protocol revision: its family and its number.
 *
 * RSProt speaks two protocol families, and their revision numbers overlap:
 *
 *   osrs<N>     Old School, 221 onwards (osrs239)
 *   rs<N>[.M]   the 2004-era RS2 builds LostCity serves (rs245.2, rs254, rs289)
 *
 * Old School will reach 245 too, so a bare number cannot say which protocol it
 * means. Every table, codec range, stream lookup and registry query takes an
 * RsprotRev instead, and it is a struct, not an integer: a bare int passed where
 * a revision belongs does not compile.
 *
 * The id is family << 24 | major << 8 | minor, so ids of one family order by
 * revision and a range [lo, hi] never spans two families.
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum RsprotFamily
{
    RSPROT_FAMILY_NONE = 0,
    RSPROT_FAMILY_OSRS = 1,
    RSPROT_FAMILY_RS = 2,
} RsprotFamily;

typedef struct RsprotRev
{
    uint32_t id;
} RsprotRev;

#define RSPROT_REV_ID(family, major, minor)                                                        \
    (((uint32_t)(family) << 24) | ((uint32_t)(major) << 8) | (uint32_t)(minor))
#define RSPROT_OSRS_ID(n) RSPROT_REV_ID(RSPROT_FAMILY_OSRS, (n), 0)
#define RSPROT_RS_ID(n, minor) RSPROT_REV_ID(RSPROT_FAMILY_RS, (n), (minor))

static inline RsprotRev
rsprot_osrs(int n)
{
    RsprotRev r = { RSPROT_OSRS_ID(n) };
    assert(n > 0 && n < 0x10000);
    return r;
}

static inline RsprotRev
rsprot_rs(int n, int minor)
{
    RsprotRev r = { RSPROT_RS_ID(n, minor) };
    assert(n > 0 && n < 0x10000);
    assert(minor >= 0 && minor < 0x100);
    return r;
}

static inline RsprotFamily
rsprot_rev_family(RsprotRev r)
{
    return (RsprotFamily)(r.id >> 24);
}

/** The major number: 239 for osrs239, 245 for rs245.2. */
static inline int
rsprot_rev_number(RsprotRev r)
{
    return (int)((r.id >> 8) & 0xFFFFu);
}

/** The minor number: 2 for rs245.2, 0 otherwise. */
static inline int
rsprot_rev_minor(RsprotRev r)
{
    return (int)(r.id & 0xFFu);
}

static inline int
rsprot_rev_eq(RsprotRev a, RsprotRev b)
{
    return a.id == b.id;
}

/**
 * "osrs239", "rs254", "rs245.2" -> the revision; id 0 (family NONE) when `text`
 * names none.
 */
static inline RsprotRev
rsprot_rev_parse(const char *text)
{
    RsprotRev none = { 0 };
    RsprotFamily family;
    char *end;
    long major;
    long minor = 0;

    assert(text);
    if (strncmp(text, "osrs", 4) == 0) {
        family = RSPROT_FAMILY_OSRS;
        text += 4;
    } else if (strncmp(text, "rs", 2) == 0) {
        family = RSPROT_FAMILY_RS;
        text += 2;
    } else {
        return none;
    }
    major = strtol(text, &end, 10);
    if (end == text || major <= 0 || major >= 0x10000)
        return none;
    if (*end == '.') {
        text = end + 1;
        minor = strtol(text, &end, 10);
        if (end == text || minor < 0 || minor >= 0x100)
            return none;
    }
    if (*end != '\0')
        return none;
    {
        RsprotRev r = { RSPROT_REV_ID(family, major, minor) };
        return r;
    }
}

/** The revision as rsprot_rev_parse reads it: "osrs239", "rs245.2". */
static inline const char *
rsprot_rev_name(RsprotRev r, char *buf, size_t cap)
{
    const char *family = rsprot_rev_family(r) == RSPROT_FAMILY_OSRS ? "osrs" : "rs";

    assert(buf);
    assert(rsprot_rev_family(r) != RSPROT_FAMILY_NONE);
    if (rsprot_rev_minor(r))
        snprintf(buf, cap, "%s%d.%d", family, rsprot_rev_number(r), rsprot_rev_minor(r));
    else
        snprintf(buf, cap, "%s%d", family, rsprot_rev_number(r));
    return buf;
}

#endif
