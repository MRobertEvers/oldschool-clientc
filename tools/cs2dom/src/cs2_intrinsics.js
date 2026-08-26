/*
 * The operations CS2 computes for itself, in JavaScript.
 *
 * Every function here is the exact behaviour of the matching `CS2VM2_Op_*`
 * handler in `src/cs2vm2/cs2vm2.c`, and that is the only reason they are worth
 * writing out rather than reaching for the obvious JS operator. CS2 arithmetic
 * is signed 32-bit and JavaScript's is double: `a * b` agrees with the client
 * for small values and silently disagrees the moment a product passes 2^31,
 * which is exactly the range colour and coordinate maths lives in. Division
 * truncates toward zero in C and does not in JS. `>> 32` is a no-op in both,
 * but for different reasons.
 *
 * So the rule for this module is: no operator that could differ appears in
 * generated code. The generator emits a call to a named intrinsic instead, the
 * intrinsic is checked against the C handler once, and both VM back ends share
 * it. Where the C handler treats a case as an error rather than a value —
 * division by zero — the intrinsic throws, because a script that divides by
 * zero has already gone wrong and the C VM aborts it.
 */

/** Raised where the C VM answers CS2VM_EXECNO_ERROR and aborts the script. */
export class CS2RuntimeError extends Error {
    constructor(message) {
        super(message);
        this.name = 'CS2RuntimeError';
    }
}

/* -------------------------------------------------------------------------
 * Integers
 * ---------------------------------------------------------------------- */

/** Truncate to signed 32 bits, the width every CS2 int has. */
export function i32(value) {
    return value | 0;
}

export function add(a, b) {
    return (a + b) | 0;
}

export function sub(a, b) {
    return (a - b) | 0;
}

/* `Math.imul`, not `*`: a product over 2^53 loses low bits as a double, and
 * one under 2^53 but over 2^31 keeps bits the client discarded. Both are wrong
 * in the direction that looks plausible. */
export function multiply(a, b) {
    return Math.imul(a, b);
}

/* C division truncates toward zero; JS `/` does not divide at all. `| 0`
 * truncates toward zero as well, so the pair agrees over the whole int range. */
export function div(a, b) {
    if( b === 0 ) throw new CS2RuntimeError('div by zero');
    return (a / b) | 0;
}

export function mod(a, b) {
    if( b === 0 ) throw new CS2RuntimeError('mod by zero');
    return (a % b) | 0;
}

/* CS2VM2_Op_Scale: the intermediate is 64-bit in C, so it must not round here.
 * A number is exact to 2^53, and a 32x32 product needs 62 bits, so the product
 * goes through BigInt rather than through a double that would quietly lose the
 * low bits of a large scale. */
export function scale(a, b, c) {
    if( b === 0 ) return 0;
    const product = BigInt(c) * BigInt(a);
    return Number(product / BigInt(b)) | 0;
}

/* `(int)pow(double, double)`, including its truncation toward zero. */
export function pow(a, b) {
    return Math.trunc(Math.pow(a, b)) | 0;
}

export function invpow(a, b) {
    if( a === 0 ) return 0;
    return Math.trunc(Math.log(b) / Math.log(a)) | 0;
}

export function min(a, b) {
    return a < b ? a : b;
}

export function max(a, b) {
    return a > b ? a : b;
}

/* CS2VM2_Op_Interpolate, error case included: a zero span answers `a` rather
 * than dividing. The two multiplications are 32-bit in C. */
export function interpolate(a, b, c, d, e) {
    const denom = (d - c) | 0;
    if( denom === 0 ) return a | 0;
    const mul = Math.imul((b - a) | 0, (e - c) | 0);
    return (a + ((mul / denom) | 0)) | 0;
}

export function addpercent(value, percent) {
    return (value + ((Math.imul(value, percent) / 100) | 0)) | 0;
}

export function abs(value) {
    return value < 0 ? (-value) | 0 : value | 0;
}

/* -------------------------------------------------------------------------
 * Bits
 * ---------------------------------------------------------------------- */

export function setbit(value, bit) {
    return value | (1 << bit);
}

export function clearbit(value, bit) {
    return value & ~(1 << bit);
}

export function togglebit(value, bit) {
    return value ^ (1 << bit);
}

export function testbit(value, bit) {
    return (value & (1 << bit)) !== 0 ? 1 : 0;
}

export function getbitRange(value, low, high) {
    return (value >> low) & ((1 << (high - low + 1)) - 1);
}

/* CS2VM2_Op_SetBitRangeValue: the new bits are CLAMPED to the range width, not
 * masked. Masking would wrap a too-large value to a small one; clamping
 * saturates, and the client saturates. */
export function setbitRangeValue(value, newBits, low, high) {
    const maxValue = (1 << (high - low + 1)) - 1;
    const clamped = newBits > maxValue ? maxValue : newBits;
    return (value & ~bitRangeMask(low, high)) | (clamped << low);
}

function bitRangeMask(low, high) {
    return (((1 << (high - low + 1)) - 1) << low) | 0;
}

export function bitcount(value) {
    let count = 0;
    let bits = value | 0;
    while( bits !== 0 )
    {
        bits &= bits - 1;
        count++;
    }
    return count;
}

export function and(a, b) {
    return a & b;
}

export function or(a, b) {
    return a | b;
}

/* -------------------------------------------------------------------------
 * Strings
 *
 * A CS2 string is a byte string, and the runtime holds it as a JS string of
 * the same characters (the cache's windows-1252 bytes are decoded once, at
 * import). Length and indexing are therefore in characters, which matches the
 * C handlers because every byte of windows-1252 is one character.
 * ---------------------------------------------------------------------- */

export function append(a, b) {
    return `${a ?? ''}${b ?? ''}`;
}

export function appendNum(text, value) {
    return `${text ?? ''}${value | 0}`;
}

export function appendChar(text, code) {
    return `${text ?? ''}${String.fromCharCode(code & 0xff)}`;
}

export function tostring(value) {
    return String(value | 0);
}

export function stringLength(text) {
    return text ? text.length : 0;
}

/* CS2VM2_Op_Compare: the sign of strcmp, normalised to -1/0/1, with a null
 * string ordering before every real one. */
export function compare(a, b) {
    if( a === b ) return 0;
    if( a === null || a === undefined ) return b === null || b === undefined ? 0 : -1;
    if( b === null || b === undefined ) return 1;
    /* Codepoint order, which is what strcmp gives for these bytes; the default
     * JS comparison is already codepoint order for BMP characters. */
    return a < b ? -1 : a > b ? 1 : 0;
}

export function substring(text, start, end) {
    const source = text ?? '';
    const from = Math.max(0, Math.min(source.length, start | 0));
    const to = Math.max(from, Math.min(source.length, end | 0));
    return source.slice(from, to);
}

export function stringIndexofChar(text, code) {
    return (text ?? '').indexOf(String.fromCharCode(code & 0xff));
}

export function stringIndexofString(haystack, needle, from) {
    return (haystack ?? '').indexOf(needle ?? '', Math.max(0, from | 0));
}

export function lowercase(text) {
    /* ASCII only: the C handler walks bytes with tolower() in the C locale, so
     * a locale-aware JS toLowerCase() would additionally fold accented
     * windows-1252 characters the client leaves alone. */
    return (text ?? '').replace(/[A-Z]/g, (ch) => ch.toLowerCase());
}

export function uppercase(text) {
    return (text ?? '').replace(/[a-z]/g, (ch) => ch.toUpperCase());
}

export function removetags(text) {
    const source = text ?? '';
    let out = '';
    let inTag = false;
    for( let i = 0; i < source.length; i++ )
    {
        const ch = source[i];
        if( ch === '<' ) { inTag = true; continue; }
        if( ch === '>' ) { inTag = false; continue; }
        if( !inTag ) out += ch;
    }
    return out;
}

/*
 * ESCAPE emits `<lt>` / `<gt>`, NOT the HTML entities.
 *
 * Those are the escapes this tree's font decoder turns back into `<` and `>`
 * (3rd/toridraw/toridraw_font.c). The xrsps reference escapes to `&lt;`, which
 * is right for a DOM renderer and wrong here — our renderer would draw the
 * entity literally. Match the renderer, not the reference.
 */
export function escape(text) {
    /* ONE pass. Escaping `<` first and `>` second rewrites the `>` that the
     * first replacement just inserted: "<col=f>" becomes "<lt<gt>col=f<gt>".
     * A single alternation cannot revisit its own output. */
    return (text ?? '').replace(/[<>]/g, (ch) => (ch === '<' ? '<lt>' : '<gt>'));
}

export function charIsalphanumeric(code) {
    const ch = code & 0xff;
    return (ch >= 48 && ch <= 57) || (ch >= 65 && ch <= 90) || (ch >= 97 && ch <= 122) ? 1 : 0;
}

export function charIsnumeric(code) {
    const ch = code & 0xff;
    return ch >= 48 && ch <= 57 ? 1 : 0;
}

export function charIsprintable(code) {
    const ch = code & 0xff;
    return ch >= 32 && ch !== 127 ? 1 : 0;
}

export function stringToInt(text) {
    const parsed = Number.parseInt(text ?? '', 10);
    return Number.isNaN(parsed) ? 0 : parsed | 0;
}

/** JOIN_STRING: concatenate the parts in push order. */
export function join(...parts) {
    let out = '';
    for( const part of parts ) out += part ?? '';
    return out;
}

/* -------------------------------------------------------------------------
 * Arrays
 *
 * A CS2 array is a handle held in a string local (see the rev-239 encoding
 * note in the VM's README): the program passes it around by reference, so a JS
 * array with the same identity semantics is the exact model. `define_array`
 * makes a new one; assigning the handle elsewhere shares it, as in the client.
 * ---------------------------------------------------------------------- */

/** The reference's `define_array` ceiling; a bound, not an allocation. */
export const ARRAY_CAPACITY_MAX = 5000;

/**
 * A fresh array. Int cells are **-1, not 0**, and that is not cosmetic.
 *
 * -1 is `null` for every reference-typed RuneScript base type, so a script
 * that fills an array and guards each slot with `= null` is testing a
 * sentinel — and 0 is a perfectly good dbrow, component or obj id.
 * `[clientscript,script1090]`, the builder behind all three of Slayer
 * Rewards' catalogue tabs, is the witness and it fails CLOSED: with zeroed
 * cells the first slot compares unequal to null, the script takes its error
 * branch on iteration zero and returns, and three tabs of a panel that
 * mounted perfectly draw nothing — with nothing logged.
 */
export function defineArray(size, stackType) {
    const count = size | 0;
    if( count < 0 || count > ARRAY_CAPACITY_MAX )
        throw new CS2RuntimeError(`define_array size ${count} outside 0..${ARRAY_CAPACITY_MAX}`);
    return new Array(count).fill(stackType === 'string' ? '' : -1);
}

/*
 * An out-of-range index is a read of 0 / a dropped write, not a throw.
 *
 * This is not leniency: `CS2VM2_ARRAY_CAPACITY` used to be 256 against the
 * reference's 5000, and the music list's 852 rows silently lost their tail
 * exactly this way — so the behaviour past the end is observable, was matched
 * to the client deliberately, and must not become an exception now.
 */
export function arrayGet(array, index) {
    const at = index | 0;
    if( at < 0 || at >= array.length ) return 0;
    return array[at];
}

export function arraySet(array, index, value) {
    const at = index | 0;
    if( at < 0 || at >= array.length ) return;
    array[at] = value;
}

export function arrayLength(array) {
    return array.length;
}

export function arraySetlength(array, length) {
    const want = Math.max(0, Math.min(ARRAY_CAPACITY_MAX, length | 0));
    while( array.length < want ) array.push(0);
    array.length = want;
    return array;
}

/* The third argument is the element's base-type selector, which the wire
 * carries so the VM knows which stack the value came off. The value has
 * already been popped by the time it reaches here, so the selector says
 * nothing this array does not already know. */
export function arrayAppend(array, value, _baseType) {
    if( array.length >= ARRAY_CAPACITY_MAX )
        throw new CS2RuntimeError(`array_append past ${ARRAY_CAPACITY_MAX}`);
    array.push(value);
    return array;
}

/*
 * ARRAY_SORT_ALL: sort `primary` ascending and permute `secondary` in lockstep.
 *
 * Two handles, and the second moves with the first — the quest list sorts
 * names while carrying their ids alongside. Sorting the two independently
 * would leave every row labelled with someone else's id, which is the failure
 * that is hardest to see because both lists are individually correct.
 */
export function arraySortAll(primary, secondary) {
    const order = primary.map((_, index) => index);
    const numeric = primary.every((value) => typeof value === 'number');
    order.sort((a, b) => {
        const left = primary[a];
        const right = primary[b];
        if( numeric ) return left - right;
        return compare(left, right);
    });
    const sortedPrimary = order.map((index) => primary[index]);
    for( let i = 0; i < primary.length; i++ ) primary[i] = sortedPrimary[i];
    if( secondary )
    {
        const sortedSecondary = order.map((index) => secondary[index]);
        for( let i = 0; i < secondary.length && i < sortedSecondary.length; i++ )
            secondary[i] = sortedSecondary[i];
    }
}

/* -------------------------------------------------------------------------
 * Hook bindings
 * ---------------------------------------------------------------------- */

/**
 * One `if_seton*` registration.
 *
 * The source dialect spells this as a quoted string holding the callee, its
 * arguments and its trigger list; that is a spelling, and this is the record.
 * `scriptId === -1` clears the slot, which is why the arguments may be empty
 * without the binding being absent.
 */
export function hook(scriptId, args, triggers) {
    return scriptId === -1 ? null : { scriptId, args, triggers };
}
