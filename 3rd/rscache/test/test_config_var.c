/*
 * Unit tests for the varbit, varplayer, varclient and inv config codecs.
 *
 * The round-trip suite already runs these over ~190,000 real records at 100%
 * byte-exactness, so this file deliberately covers only what a real cache *cannot*
 * exercise:
 *
 *   - the unknown-opcode stop, which is the mechanism that turns an unrecognised
 *     field into a visible `_consumed` shortfall instead of a silent misparse. No
 *     record in the corpus contains an unknown opcode — that is exactly why the
 *     safety net needs its own test.
 *   - the cursor-based decoders walking back-to-back records out of one buffer,
 *     which is the dat1 `varbit.dat` shape. The corpus only exercises the dat2
 *     one-file-per-record form.
 *   - fields whose "absent" and "zero" cases the corpus happens not to distinguish.
 */

#include "rscache_test.h"

#include <rscache.h>

int
main(void)
{
    RSCACHE_TEST_GROUP("varbit — the settled wire format");
    {
        /* opcode 1: u16 basevar, u8 startbit, u8 endbit. The literal shape of every
         * one of the 21112 distinct records in the corpus. */
        const uint8_t record[] = { 0x01, 0x01, 0x3e, 0x02, 0x05, 0x00 };
        struct RSCache_Dat2ConfigVarbit vb;
        memset(&vb, 0, sizeof(vb));
        RSCache_Dat2ConfigVarbitDecodeInplace(&vb, record, (int)sizeof(record));

        RSCACHE_CHECK(vb.basevar == 0x013e); /* basevar 318 */
        RSCACHE_CHECK(vb.startbit == 2); /* startbit 2 */
        RSCACHE_CHECK(vb.endbit == 5); /* endbit 5 */
        RSCACHE_CHECK(vb._consumed == (int)sizeof(record)); /* consumed exactly */

        uint8_t out[32];
        uint32_t written = RSCache_Dat2ConfigVarbitEncode(&vb, out, sizeof(out));
        RSCACHE_CHECK(written == sizeof(record)); /* re-encode same length */
        RSCACHE_CHECK(memcmp(out, record, sizeof(record)) == 0); /* re-encode byte-exact */
        RSCache_Dat2ConfigVarbitFreeInplace(&vb);
    }

    RSCACHE_TEST_GROUP("varbit — absent base variable is -1, not 0");
    {
        /* A bare terminator. -1 matters: varplayer 0 is a real variable, so a varbit
         * that named none must not look like one pointing at it. */
        const uint8_t record[] = { 0x00 };
        struct RSCache_Dat2ConfigVarbit vb;
        memset(&vb, 0, sizeof(vb));
        RSCache_Dat2ConfigVarbitDecodeInplace(&vb, record, 1);

        RSCACHE_CHECK(vb.basevar == -1); /* basevar -1 when opcode 1 absent */
        RSCACHE_CHECK(vb._consumed == 1); /* consumed the terminator */

        uint8_t out[32];
        uint32_t written = RSCache_Dat2ConfigVarbitEncode(&vb, out, sizeof(out));
        RSCACHE_CHECK(written == 1 && out[0] == 0); /* re-encodes to a bare terminator */
        RSCache_Dat2ConfigVarbitFreeInplace(&vb);
    }

    RSCACHE_TEST_GROUP("varbit — opcode 10 debugname (dat1 caches carry it)");
    {
        const uint8_t record[] = { 0x01, 0x00, 0x2a, 0x00, 0x03, 0x0a, 'q', 'p', 0x00, 0x00 };
        struct RSCache_Dat2ConfigVarbit vb;
        memset(&vb, 0, sizeof(vb));
        RSCache_Dat2ConfigVarbitDecodeInplace(&vb, record, (int)sizeof(record));

        RSCACHE_CHECK(vb.basevar == 42); /* basevar 42 */
        RSCACHE_CHECK(vb.debugname && strcmp(vb.debugname, "qp") == 0); /* debugname decoded */
        RSCACHE_CHECK(vb._consumed == (int)sizeof(record)); /* consumed exactly */

        uint8_t out[64];
        uint32_t written = RSCache_Dat2ConfigVarbitEncode(&vb, out, sizeof(out));
        RSCACHE_CHECK(written == sizeof(record) && memcmp(out, record, sizeof(record)) == 0); /* debugname re-encodes byte-exact */
        RSCache_Dat2ConfigVarbitFreeInplace(&vb);
    }

    RSCACHE_TEST_GROUP("unknown opcode stops the decode and shows in _consumed");
    {
        /*
         * The safety net. Opcode 7 is not implemented for dat2 varbit; the decoder
         * must stop rather than guess an operand width, leaving `_consumed` short of
         * the record so the round-trip harness fails the record instead of silently
         * misaligning everything after it.
         */
        const uint8_t record[] = { 0x07, 0xde, 0xad, 0x00 };
        struct RSCache_Dat2ConfigVarbit vb;
        memset(&vb, 0, sizeof(vb));
        RSCache_Dat2ConfigVarbitDecodeInplace(&vb, record, (int)sizeof(record));

        RSCACHE_CHECK(vb._consumed < (int)sizeof(record)); /* consumed short of the record */
        RSCACHE_CHECK(vb.basevar == -1); /* no field invented from the unknown opcode */
        RSCache_Dat2ConfigVarbitFreeInplace(&vb);

        /* Same for inv, so the mechanism is not varbit-only. */
        const uint8_t inv_record[] = { 0x09, 0x01, 0x00 };
        struct RSCache_Dat2ConfigInv inv;
        memset(&inv, 0, sizeof(inv));
        RSCache_Dat2ConfigInvDecodeInplace(&inv, inv_record, (int)sizeof(inv_record));
        RSCACHE_CHECK(inv._consumed < (int)sizeof(inv_record)); /* inv stops too */
        RSCACHE_CHECK(inv.size == 0); /* inv size untouched */
    }

    RSCACHE_TEST_GROUP("varbit — one codec serves both eras");
    {
        /*
         * dat1 stores varbits as a single `varbit.dat` blob: a u16 count then that
         * many terminator-delimited records back to back. dat2 splits them into one
         * file per id. The per-record shape is identical, so the cursor form of the
         * decoder walks the dat1 blob directly — which is the whole point of taking a
         * cursor rather than a (data, size) pair.
         */
        const uint8_t blob[] = {
            /* Each record is opcode 1, u16 basevar, u8 startbit, u8 endbit, then its
             * own terminator — the terminator is what separates them, so leaving one
             * out makes the next record's first byte look like a field. */
            0x01, 0x00, 0x05, 0x00, 0x00, 0x00, /* record 0: basevar 5, bits 0..0 */
            0x01, 0x00, 0x05, 0x01, 0x03, 0x00, /* record 1: basevar 5, bits 1..3 */
            0x00,                               /* record 2: empty */
            0x01, 0x00, 0x09, 0x00, 0x07, 0x00, /* record 3: basevar 9, bits 0..7 */
        };

        struct RSCache_Buffer cursor;
        RSCache_BufferInit(&cursor, (uint8_t*)blob, (uint32_t)sizeof(blob));

        struct RSCache_Dat2ConfigVarbit records[4];
        for( int i = 0; i < 4; i++ )
        {
            memset(&records[i], 0, sizeof(records[i]));
            RSCache_Dat2ConfigVarbitDecode(&records[i], &cursor);
        }

        RSCACHE_CHECK(cursor.position == sizeof(blob)); /* walked the whole blob exactly */
        RSCACHE_CHECK(records[0].basevar == 5 && records[0].endbit == 0); /* record 0 */
        RSCACHE_CHECK(records[1].basevar == 5 && records[1].startbit == 1 && records[1].endbit == 3); /* record 1 */
        RSCACHE_CHECK(records[2].basevar == -1); /* record 2 is empty */
        RSCACHE_CHECK(records[3].basevar == 9 && records[3].endbit == 7); /* record 3 */

        for( int i = 0; i < 4; i++ )
            RSCache_Dat2ConfigVarbitFreeInplace(&records[i]);
    }

    RSCACHE_TEST_GROUP("inv — opcode 2 is the slot count");
    {
        const uint8_t record[] = { 0x02, 0x00, 0x1c, 0x00 };
        struct RSCache_Dat2ConfigInv inv;
        memset(&inv, 0, sizeof(inv));
        RSCache_Dat2ConfigInvDecodeInplace(&inv, record, (int)sizeof(record));

        RSCACHE_CHECK(inv.size == 28); /* 28 slots, the player inventory */
        RSCACHE_CHECK(inv._consumed == (int)sizeof(record)); /* consumed exactly */

        uint8_t out[16];
        uint32_t written = RSCache_Dat2ConfigInvEncode(&inv, out, sizeof(out));
        RSCACHE_CHECK(written == sizeof(record) && memcmp(out, record, sizeof(record)) == 0); /* byte-exact */
    }

    RSCACHE_TEST_GROUP("varplayer — opcode 5 clientcode");
    {
        const uint8_t record[] = { 0x05, 0x00, 0x11, 0x00 };
        struct RSCache_Dat2ConfigVarplayer vp;
        memset(&vp, 0, sizeof(vp));
        RSCache_Dat2ConfigVarplayerDecodeInplace(&vp, record, (int)sizeof(record));

        RSCACHE_CHECK(vp.clientcode == 17); /* clientcode 17 */
        RSCACHE_CHECK(vp._consumed == (int)sizeof(record)); /* consumed exactly */

        uint8_t out[16];
        uint32_t written = RSCache_Dat2ConfigVarplayerEncode(&vp, out, sizeof(out));
        RSCACHE_CHECK(written == sizeof(record) && memcmp(out, record, sizeof(record)) == 0); /* byte-exact */

        /* The overwhelmingly common case: a bare terminator, no clientcode. */
        const uint8_t empty[] = { 0x00 };
        memset(&vp, 0, sizeof(vp));
        RSCache_Dat2ConfigVarplayerDecodeInplace(&vp, empty, 1);
        RSCACHE_CHECK(vp.clientcode == 0 && vp._consumed == 1); /* empty record */
        written = RSCache_Dat2ConfigVarplayerEncode(&vp, out, sizeof(out));
        RSCACHE_CHECK(written == 1 && out[0] == 0); /* empty re-encodes to a terminator */
    }

    RSCACHE_TEST_GROUP("varclient — presence of opcode 3 is distinct from its value");
    {
        /*
         * 40 records in the corpus carry opcode 3 with an explicit zero. Keying the
         * encoder on the value rather than on presence would drop the opcode and
         * shorten the record, so `has_opcode_3` has to be tracked separately.
         */
        const uint8_t with_zero[] = { 0x02, 0x03, 0x00, 0x00, 0x00 };
        struct RSCache_Dat2ConfigVarclient vc;
        memset(&vc, 0, sizeof(vc));
        RSCache_Dat2ConfigVarclientDecodeInplace(&vc, with_zero, (int)sizeof(with_zero));

        RSCACHE_CHECK(vc.persist == 1); /* opcode 2 sets persist */
        RSCACHE_CHECK(vc.has_opcode_3); /* opcode 3 recorded as present */
        RSCACHE_CHECK(vc.opcode_3 == 0); /* its value is zero */
        RSCACHE_CHECK(vc._consumed == (int)sizeof(with_zero)); /* consumed exactly */

        uint8_t out[16];
        uint32_t written = RSCache_Dat2ConfigVarclientEncode(&vc, out, sizeof(out));
        RSCACHE_CHECK(written == sizeof(with_zero) && memcmp(out, with_zero, sizeof(with_zero)) == 0); /* explicit zero survives the round trip */

        /* persist alone, the second most common shape. */
        const uint8_t persist_only[] = { 0x02, 0x00 };
        memset(&vc, 0, sizeof(vc));
        RSCache_Dat2ConfigVarclientDecodeInplace(&vc, persist_only, 2);
        RSCACHE_CHECK(vc.persist == 1 && !vc.has_opcode_3); /* persist without opcode 3 */
        written = RSCache_Dat2ConfigVarclientEncode(&vc, out, sizeof(out));
        RSCACHE_CHECK(written == 2 && memcmp(out, persist_only, 2) == 0); /* persist-only byte-exact */
    }

    return rscache_test_report("config_var");
}
