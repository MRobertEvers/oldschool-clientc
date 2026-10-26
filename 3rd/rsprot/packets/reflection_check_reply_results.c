/* REFLECTION_CHECK_REPLY's results. Hand-written; see reflection_check_reply_results.h. */
#include "reflection_check_reply_results.h"

/* REFLECTION_CHECKER's opcodes. */
enum
{
    CHECK_GET_FIELD_VALUE = 0,
    CHECK_SET_FIELD_VALUE = 1,
    CHECK_GET_FIELD_MODIFIERS = 2,
    CHECK_INVOKE_METHOD = 3,
    CHECK_GET_METHOD_MODIFIERS = 4,
};

void
packet_reflection_check_reply_results_in(RsprotExec *x, MsgReflectionCheckReplyResults *m)
{
    if (m->check_count < 0 || m->check_count > MSG_REFLECTION_CHECKER_MAX_CHECKS) {
        rsprot_exec_fail(x, "REFLECTION_CHECK_REPLY: more checks than a request carries");
        return;
    }
    RSPROT_U4_1234(x, m->id);
    for (int32_t i = 0; i < m->check_count; i++) {
        MsgReflectionCheckResult *r = &m->results[i];
        int32_t op = m->check_ops[i];

        RSPROT_I1(x, r->result);
        if (RSPROT_BRANCH(x, r->result) < 0)
            continue; /* the check threw */
        if (op == CHECK_GET_FIELD_VALUE || op == CHECK_GET_FIELD_MODIFIERS ||
            op == CHECK_GET_METHOD_MODIFIERS) {
            RSPROT_U4_1234(x, r->value);
        } else if (op == CHECK_INVOKE_METHOD) {
            if (r->result == 1)
                RSPROT_U8_12345678(x, r->number);
            else if (r->result == 2)
                RSPROT_JSTR(x, r->text);
            else if (r->result != 0 && r->result != 4)
                rsprot_exec_fail(x, "REFLECTION_CHECK_REPLY: unknown method invocation result");
        } else if (op != CHECK_SET_FIELD_VALUE) {
            rsprot_exec_fail(x, "REFLECTION_CHECK_REPLY: unknown check");
        }
    }
    RSPROT_REST_BUT(x, m->rest, m->rest_length, 4);
    RSPROT_U4_1234(x, m->crc);
}
