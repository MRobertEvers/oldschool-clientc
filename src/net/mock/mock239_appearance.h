#ifndef SRC_NET_MOCK_MOCK239_APPEARANCE_H
#define SRC_NET_MOCK_MOCK239_APPEARANCE_H

/* Rev 239 writes one overhead-icon archive index in an appearance block;
 * generic server/content state uses the older bitmask vocabulary. */
static inline int
mock239_appearance_headicon_index(int mask)
{
    for( int icon = 0; icon < 8; icon++ )
        if( mask & (1 << icon) )
            return icon;
    return -1;
}

#endif
