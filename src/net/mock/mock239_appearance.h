#ifndef SRC_NET_MOCK_MOCK239_APPEARANCE_H
#define SRC_NET_MOCK_MOCK239_APPEARANCE_H

/* Rev 239 writes one overhead-icon archive index in an appearance block;
 * generic server/content state uses the older bitmask vocabulary.
 *
 * 31 bits, not 8. The eight came from the classic wire field being one byte,
 * but this is the index side of the conversion, not the mask side — and
 * `headicons_prayer` already has 24 frames in the base cache and 30 with the
 * Ancient Curses lane's own icons appended. At 8 the six curse overheads
 * (24..29) were unreachable: the mask carried the bit, this loop never looked
 * at it, and the appearance went out as 255 "no icon". */
static inline int
mock239_appearance_headicon_index(int mask)
{
    for( int icon = 0; icon < 31; icon++ )
        if( mask & (1 << icon) )
            return icon;
    return -1;
}

#endif
