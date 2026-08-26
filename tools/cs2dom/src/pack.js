/* Shared parsing for cachepack's `id=name [hash provenance]` symbol files. */

const HASH_SUFFIX = /^(.*?)(\s+hash(?:name|code)\s*\([\s\S]*\))\s*$/;

/** Split the canonical asset name from cachepack's optional hash annotation. */
export function splitPackName(value) {
    const text = String(value ?? '').trim();
    const match = HASH_SUFFIX.exec(text);
    return match
        ? { name: match[1].trim(), suffix: match[2] }
        : { name: text, suffix: '' };
}

/** Return only the canonical name used by source filenames and runtime lookup. */
export function packName(value) {
    return splitPackName(value).name;
}
