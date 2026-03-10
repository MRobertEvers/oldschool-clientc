#include "windows_cp1252.h"

// Table for 0x80 to 0x9F. 0 represents unmapped/null characters.
/**
 * Table for CP1252 indices 0x80 to 0x9F (128-159).
 * 0x0000 represents unmapped/undefined control characters in this set.
 *
 * This array specifically fills the "gap" in ISO-8859-1 (where control characters usually live)
 * with the Windows-1252 printable symbols like the Euro sign.
 */
const uint16_t CP1252_SPECIAL_CHARS[32] = {
    0x20AC, // € (Euro Sign)
    0x0000, // [Undefined]
    0x201A, // ‚ (Single Low-9 Quotation Mark)
    0x0192, // ƒ (Latin Small Letter F with Hook / Florin)
    0x201E, // „ (Double Low-9 Quotation Mark)
    0x2026, // … (Horizontal Ellipsis)
    0x2020, // † (Dagger)
    0x2021, // ‡ (Double Dagger)
    0x02C6, // ˆ (Modifier Letter Circumflex Accent)
    0x2030, // ‰ (Per Mille Sign)
    0x0160, // Š (Latin Capital Letter S with Caron)
    0x2039, // ‹ (Single Left-Pointing Angle Quotation Mark)
    0x0152, // Œ (Latin Capital Ligature OE)
    0x0000, // [Undefined]
    0x017D, // Ž (Latin Capital Letter Z with Caron)
    0x0000, // [Undefined]
    0x0000, // [Undefined]
    0x2018, // ‘ (Left Single Quotation Mark)
    0x2019, // ’ (Right Single Quotation Mark)
    0x201C, // “ (Left Double Quotation Mark)
    0x201D, // ” (Right Double Quotation Mark)
    0x2022, // • (Bullet)
    0x2013, // – (En Dash)
    0x2014, // — (Em Dash)
    0x02DC, // ˜ (Small Tilde)
    0x2122, // ™ (Trade Mark Sign)
    0x0161, // š (Latin Small Letter S with Caron)
    0x203A, // › (Single Right-Pointing Angle Quotation Mark)
    0x0153, // œ (Latin Small Ligature OE)
    0x0000, // [Undefined]
    0x017E, // ž (Latin Small Letter Z with Caron)
    0x0178  // Ÿ (Latin Capital Letter Y with Diaeresis)
};
uint8_t
cp1252_encode_from_utf16(uint16_t c)
{
    if( (c > 0 && c < 128) || (c >= 160 && c <= 255) )
    {
        return (uint8_t)c;
    }

    // Manual mapping for special characters to their CP1252 unsigned byte values
    switch( c )
    {
    case 0x20AC:
        return 0x80; // 128: €
    case 0x201A:
        return 0x82; // 130: ‚
    case 0x0192:
        return 0x83; // 131: ƒ
    case 0x201E:
        return 0x84; // 132: „
    case 0x2026:
        return 0x85; // 133: …
    case 0x2020:
        return 0x86; // 134: †
    case 0x2021:
        return 0x87; // 135: ‡
    case 0x02C6:
        return 0x88; // 136: ˆ
    case 0x2030:
        return 0x89; // 137: ‰
    case 0x0160:
        return 0x8A; // 138: Š
    case 0x2039:
        return 0x8B; // 139: ‹
    case 0x0152:
        return 0x8C; // 140: Œ
    case 0x017D:
        return 0x8E; // 142: Ž
    case 0x2018:
        return 0x91; // 145: ‘
    case 0x2019:
        return 0x92; // 146: ’
    case 0x201C:
        return 0x93; // 147: “
    case 0x201D:
        return 0x94; // 148: ”
    case 0x2022:
        return 0x95; // 149: •
    case 0x2013:
        return 0x96; // 150: –
    case 0x2014:
        return 0x97; // 151: —
    case 0x02DC:
        return 0x98; // 152: ˜
    case 0x2122:
        return 0x99; // 153: ™
    case 0x0161:
        return 0x9A; // 154: š
    case 0x203A:
        return 0x9B; // 155: ›
    case 0x0153:
        return 0x9C; // 156: œ
    case 0x017E:
        return 0x9E; // 158: ž
    case 0x0178:
        return 0x9F; // 159: Ÿ
    default:
        return 0x3F; // 63: '?'
    }
}

bool
cp1252_inrange(uint16_t c)
{
    if( (c >= 0x20 && c < 127) || (c > 160 && c <= 255) )
    {
        return true;
    }
    if( c != 0 )
    {
        for( int i = 0; i < 32; i++ )
        {
            if( c == CP1252_SPECIAL_CHARS[i] )
                return true;
        }
    }
    return false;
}

uint16_t
cp1252_decode_to_utf16(uint8_t b)
{
    if( b < 128 )
        return (uint16_t)b;
    if( b >= 160 )
        return (uint16_t)b;

    uint16_t special = CP1252_SPECIAL_CHARS[b - 128];
    return (special == 0) ? (uint16_t)'?' : special;
}

// Simple encoder: Note that this assumes input is a basic char array (Latin-1/ASCII)
// For a full UTF-8 to CP1252 conversion, you would need a UTF-8 decoder here.
uint32_t
cp1252_encode_string(
    const char* src,
    uint8_t* dst,
    uint32_t dst_offset,
    uint32_t len)
{
    for( uint32_t i = 0; i < len; i++ )
    {
        dst[dst_offset + i] = (uint8_t)cp1252_encode_from_utf16((uint16_t)src[i]);
    }

    return len;
}