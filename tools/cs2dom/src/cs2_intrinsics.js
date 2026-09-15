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
/*
 * `POP_INT_LOCAL` — and it POPS THE INT STACK.
 *
 * A string reaching an int slot means the value went to the other bank, so the
 * int stack is empty and the reference's VM fails the op and unwinds the whole
 * call chain (`Task_CS2Run: script N failed at opcode 34`). Coercing instead
 * lets a script run on past a point the client stops at, which is a difference
 * in what gets BUILT, not just in what a number reads as.
 */
export function popInt(value) {
    if( typeof value === 'string' )
        throw new CS2RuntimeError('pop_int_local: the value is on the string stack');
    /*
     * A pop takes the TOP of the stack, so several values arriving where one
     * slot waits means the earlier ones LEAK — which is what the reference
     * does, harmlessly, because the frame ends.
     *
     * `db_getfield` is where this shows: its arity is the row's, and the
     * decompiler types it from the table. A quest row's `requirement_stats`
     * pushes two ints into an assignment typed for one, and the value the
     * script goes on to use is the SECOND. `sailing_bt_selection` reads item
     * 35 out of the pair (8, 35) and draws it; taking the pair, or its first
     * element, drew nothing.
     */
    if( Array.isArray(value) && !isArrayHandle(value) )
        return value.length > 0 ? value[value.length - 1] : -1;
    return value;
}

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

/*
 * `(int)pow(double, double)`, including its truncation toward zero AND its
 * saturation: converting an out-of-range double to int on this target clamps
 * to INT_MIN/INT_MAX, where `| 0` would wrap it. pow(10, 10) is 2147483647,
 * not 1410065408.
 */
export function pow(a, b) {
    return doubleToInt(Math.pow(a, b));
}

function doubleToInt(value) {
    if( Number.isNaN(value) ) return 0;
    if( value >= 2147483647 ) return 2147483647;
    if( value <= -2147483648 ) return -2147483648;
    return Math.trunc(value) | 0;
}

/*
 * `invpow(base, exponent)` — the exponent-th integer ROOT of base, not a
 * power and not a logarithm. `CS2VM2_Op_InvPow` special-cases the first five
 * exponents so the common roots come out of `sqrt`/`cbrt` rather than `pow`,
 * and the truncation is part of the answer.
 *
 * It read as a logarithm here, which is 0 for every argument these scripts
 * pass: `script5147` eases a model's position with `invpow(height - 334, 2)`,
 * so the seed in `pog_tele_seed_breaking` sat 24 pixels above where the
 * reference draws it, in three interfaces.
 */
export function invpow(base, exponent) {
    if( base === 0 ) return 0;
    switch( exponent )
    {
    case 0: return 2147483647;
    case 1: return base | 0;
    case 2: return Math.trunc(Math.sqrt(base)) | 0;
    case 3: return Math.trunc(Math.cbrt(base)) | 0;
    case 4: return Math.trunc(Math.sqrt(Math.sqrt(base))) | 0;
    default: return Math.trunc(Math.pow(base, 1 / exponent)) | 0;
    }
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

/* The product is 64-bit on purpose — `CS2VM2_Op_AddPercent` casts to int64_t
 * so it cannot overflow, and `Math.imul` is exactly the wrap it avoids:
 * addpercent(200000000, 50) came out 214100654 instead of 300000000. */
export function addpercent(value, percent) {
    const scaled = Number(BigInt(value | 0) * BigInt(percent | 0) / 100n);
    return (value + (scaled | 0)) | 0;
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

/*
 * windows-1252, both directions.
 *
 * The strings this runtime carries are DECODED — the decompiler maps
 * byte 0x80 to U+20AC and so on — but
 * the C handlers work on the bytes. Anything that compares or searches by
 * ordinal has to go back through the table, and `& 0xff` is not that mapping:
 * U+20AC & 0xff is 0x14.
 */
const CP1252_C1 = Object.freeze([
    0x20ac, 0x0081, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021,
    0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008d, 0x017d, 0x008f,
    0x0090, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
    0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0x009d, 0x017e, 0x0178,
]);

/** The character a cp1252 BYTE decodes to. */
function cp1252Char(byte) {
    const value = byte & 0xff;
    return String.fromCharCode(value >= 0x80 && value < 0xa0
        ? CP1252_C1[value - 0x80] : value);
}

/** The cp1252 byte a character encodes to, or -1 when it has none. */
function cp1252Byte(code) {
    if( code < 0x80 || (code >= 0xa0 && code <= 0xff) ) return code;
    const index = CP1252_C1.indexOf(code);
    return index >= 0 ? 0x80 + index : -1;
}

export function appendChar(text, code) {
    /* `(char)chr` in the C: the low byte, then whatever cp1252 says it is. */
    return `${text ?? ''}${cp1252Char(code)}`;
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
    /*
     * BYTE order, not codepoint order.
     *
     * `strcmp` compares cp1252 bytes, and the two orders disagree across the
     * whole C1 range: byte 0x80 is the euro sign, which sorts BEFORE every
     * accented letter for the C client and after all of them by codepoint.
     * Sorted name lists are where it shows.
     */
    const length = Math.min(a.length, b.length);
    for( let i = 0; i < length; i++ )
    {
        const left = cp1252Byte(a.charCodeAt(i));
        const right = cp1252Byte(b.charCodeAt(i));
        if( left !== right ) return left < right ? -1 : 1;
    }
    if( a.length === b.length ) return 0;
    return a.length < b.length ? -1 : 1;
}

export function substring(text, start, end) {
    const source = text ?? '';
    const from = Math.max(0, Math.min(source.length, start | 0));
    const to = Math.max(from, Math.min(source.length, end | 0));
    return source.slice(from, to);
}

export function stringIndexofChar(text, code) {
    /* The C compares `(unsigned char)haystack[i]` against `(unsigned char)ch`,
     * so the needle is a cp1252 BYTE. */
    return (text ?? '').indexOf(cp1252Char(code));
}

export function stringIndexofString(haystack, needle, from) {
    /* An EMPTY needle is "not found", not "found at 0": the C guards the whole
     * search with `needle[0] != '\0'` and leaves the result at -1. Every
     * `= -1` not-found test inverts on a needle that happens to be empty. */
    if( !needle ) return -1;
    return (haystack ?? '').indexOf(needle, Math.max(0, from | 0));
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
        /* `>` closes a tag only when one is OPEN. The C guards its branch with
         * `in_tag`, so a bare `>` is ordinary text: removetags("5 > 3") keeps
         * its `>`. Dropping it unconditionally ate the character. */
        if( ch === '<' ) { inTag = true; continue; }
        if( inTag && ch === '>' ) { inTag = false; continue; }
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

/*
 * The character predicates take the code UNMASKED.
 *
 * `CS2VM2_Op_CharClass` tests `chr` itself, so 304 is not a digit however its
 * low byte reads; masking made it one.
 */
export function charIsalphanumeric(code) {
    return (code >= 48 && code <= 57) || (code >= 65 && code <= 90)
        || (code >= 97 && code <= 122) ? 1 : 0;
}

export function charIsnumeric(code) {
    return code >= 48 && code <= 57 ? 1 : 0;
}

/*
 * Printable is a TABLE, not a range: ASCII 32..126, latin-1 160..255, and five
 * scattered points the cp1252 C1 range decodes to. This filters keyboard
 * input, so a wrong answer silently eats or admits keystrokes — 127..159 were
 * printable here and are not, and the em dash was not and is.
 */
export function charIsprintable(code) {
    return (code >= 32 && code <= 126) || (code >= 160 && code <= 255)
        || code === 0x20ac || code === 0x0152 || code === 0x2014
        || code === 0x0153 || code === 0x0178 ? 1 : 0;
}

/*
 * STRING_TO_INT answers -1 for anything that is not a whole number, and -1 is
 * not a value `parseInt` can produce by accident: the C rejects an empty
 * string, a non-numeric one, AND a numeric prefix with trailing garbage
 * (`*end != '\0'`). Answering 0 for those made "not a number" read as zero,
 * and "12abc" read as twelve.
 */
export function stringToInt(text) {
    const source = text ?? '';
    if( source === '' || !/^[+-]?\d+$/.test(source) ) return -1;
    const parsed = Number.parseInt(source, 10);
    return Number.isNaN(parsed) ? -1 : parsed | 0;
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
/*
 * Which bank an array's cells live on, carried on the array itself.
 *
 * `arraySetlength` and an out-of-range `arrayGet` both have to answer with the
 * bank's empty value — `""` for strings, -1/0 for ints — and an empty array
 * cannot be asked what it holds. The C keeps `array->is_string` beside the
 * cells; this is the same field, hidden from iteration and from JSON.
 */
const STRING_BANK = Symbol('cs2ArrayIsString');

function isStringArray(array) {
    return array?.[STRING_BANK] === true;
}

/** An array made by `defineArray` — a HANDLE, not a tuple of stack slots. */
function isArrayHandle(value) {
    return Array.isArray(value) && STRING_BANK in value;
}

/*
 * One value, or the several a DYNAMIC-arity op pushed.
 *
 * `db_getfield` reads a whole column, and how many fields that is depends on
 * the ROW — a static lowering cannot know. So its result is spread through
 * this at the call site: a tuple contributes one argument per field, exactly
 * as the two stacks would, and a scalar contributes itself.
 *
 * An array HANDLE must never be spread: it is one value that happens to be an
 * array, and splicing it would hand the callee its elements instead.
 */
export function tuple(value) {
    return Array.isArray(value) && !isArrayHandle(value) ? value : [value];
}

export function defineArray(size, stackType) {
    /* CLAMPED, not rejected: `cs2vm2_array_begin` pins the size into
     * 0..CS2VM2_ARRAY_CAPACITY and the script runs on. Throwing here aborted a
     * script the reference would have carried through with a short array. */
    const count = Math.max(0, Math.min(ARRAY_CAPACITY_MAX, size | 0));
    const array = new Array(count).fill(stackType === 'string' ? '' : -1);
    Object.defineProperty(array, STRING_BANK, { value: stackType === 'string' });
    return array;
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
    /* Past the end reads the BANK's empty value: `CS2VM2_Op_ArrayGet` pushes
     * `CS2VM2_StrEmpty` for a string array, so a 0 here concatenated as "0". */
    if( at < 0 || at >= array.length ) return isStringArray(array) ? '' : 0;
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

/*
 * Growing fills with the bank's NULL, which for ints is -1 and not 0.
 *
 * Same sentinel argument as `defineArray`: -1 is `null` for every
 * reference-typed base type, so a script that grows an array and then guards
 * each slot with `= null` reads a zeroed cell as a real dbrow, component or
 * obj id. The C initialises both arms explicitly for exactly this reason.
 */
export function arraySetlength(array, length) {
    const want = Math.max(0, Math.min(ARRAY_CAPACITY_MAX, length | 0));
    const empty = isStringArray(array) ? '' : -1;
    while( array.length < want ) array.push(empty);
    array.length = want;
    return array;
}

/* The third argument is the element's base-type selector, which the wire
 * carries so the VM knows which stack the value came off. The value has
 * already been popped by the time it reaches here, so the selector says
 * nothing this array does not already know. */
export function arrayAppend(array, value, _baseType) {
    /* Past the ceiling is a silent no-op, not an abort: `cs2vm2_array_reserve`
     * fails and `CS2VM2_Op_ArrayAppend` still returns OK. */
    if( array.length >= ARRAY_CAPACITY_MAX ) return array;
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
    /*
     * The secondary moves only when it is at least as long as the primary.
     * `int const paired = secondary && secondary->size >= primary->size;` —
     * a shorter one is left ALONE, where permuting it by the primary's order
     * both reorders it wrongly and writes `undefined` into the cells whose
     * source index is past its end.
     */
    if( secondary && secondary.length >= primary.length )
    {
        const sortedSecondary = order.map((index) => secondary[index]);
        for( let i = 0; i < sortedSecondary.length; i++ )
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
