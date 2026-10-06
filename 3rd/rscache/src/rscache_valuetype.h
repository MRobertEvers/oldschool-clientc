#ifndef RSCACHE_VALUETYPE_H
#define RSCACHE_VALUETYPE_H

/*
 * ScriptVarType: the one table of value types.
 *
 * A config value's type is stated three ways depending on where it is declared:
 * a dbtable column stores the type's numeric id (22 for a coord), a param record
 * and an enum store its character ('c'), and config text spells it as a word
 * (`coord`). Every reader of any of the three -- cachepack's text codecs, the
 * param decoder, the game server -- maps between them through this table, so
 * there is one answer to "what is type 'c'".
 *
 * The rows are Jagex's ScriptVarType table as cache2 publishes it (id, char, jag
 * name), with LostCity's word where LostCity has the type (`int`, `coord`,
 * because the authored grammar wins). Rows with no id are types a param or enum
 * states by character that no dbtable has (`area`, `maparea`, `interface`).
 * `varbit` is this server's own: no published ScriptVarType names one, and its
 * character is 0x81, a byte windows-1252 leaves undefined, so it cannot collide
 * with a type Jagex adds later.
 *
 * How a value of each type is spelled in text, and which namespace a name
 * resolves in, is the text codec's (cachepack's cp_value.c), not this table's.
 */

struct RSCache_ValueType
{
    /** The ScriptVarType id a dbtable column stores, or -1 when none is published. */
    int id;
    /** The type character (a windows-1252 byte) a param or an enum stores. */
    int ch;
    /** The word config text spells it with. */
    const char* word;
};

int
RSCache_ValueTypeCount(void);

const struct RSCache_ValueType*
RSCache_ValueTypeAt(int index);

/** The type with ScriptVarType id `id`, or NULL. */
const struct RSCache_ValueType*
RSCache_ValueTypeOfId(int id);

/** The type with character `ch` (a byte; 0 is no type), or NULL. */
const struct RSCache_ValueType*
RSCache_ValueTypeOfChar(int ch);

/** The type config text spells `word`, or NULL. */
const struct RSCache_ValueType*
RSCache_ValueTypeNamed(const char* word);

#endif
