/*
 * Cachepack config text markers -- the JS half of tools/config_text.py.
 *
 * Rank 0 and every new record state every key of the type. A field the record
 * does not set is written `key=default` (ABSENT: behave exactly as if the line
 * were missing), a list key with no entries `key=empty` (present, zero entries),
 * and a string that genuinely reads "default"/"empty" is escaped `key=\default`.
 * The test is on the raw text after `=`, before any unescaping -- the same test
 * cachepack makes (cp_value_is_default / cp_value_is_empty).
 */

export const DEFAULT_TEXT = 'default';
export const EMPTY_TEXT = 'empty';

/** 'default', 'empty', or null for the raw text after `=` (line ending ignored). */
export function markerOf(raw) {
    const text = raw.replace(/[\r\n]+$/, '');
    if( text === DEFAULT_TEXT ) return DEFAULT_TEXT;
    if( text === EMPTY_TEXT ) return EMPTY_TEXT;
    return null;
}

/** Undo only the marker escape (`\default` -> `default`); call after markerOf() said null. */
export function unmark(raw) {
    if( markerOf(raw) !== null ) throw new Error(`unmark() of a marker: ${raw}`);
    if( raw === `\\${DEFAULT_TEXT}` ) return DEFAULT_TEXT;
    if( raw === `\\${EMPTY_TEXT}` ) return EMPTY_TEXT;
    return raw;
}
