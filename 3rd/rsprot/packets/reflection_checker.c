/* REFLECTION_CHECKER. Hand-written -- see reflection_checker.h. */
#include "reflection_checker.h"

/*
 * v1 -- revs 223-241.
 * g1 checksCount, g4 id, then per check g1 opcode and its arm.
 */
void
packet_reflection_checker_v1_out(RsprotExec *x, MsgReflectionChecker *m)
{
    RSPROT_COUNT(x, m->checks_count, MSG_REFLECTION_CHECKER_MAX_CHECKS);
    RSPROT_U4(x, m->id);
    for (int32_t i = 0; i < m->checks_count; i++) {
        MsgReflectionCheck *c = &m->checks[i];
        int32_t op;

        RSPROT_U1(x, c->opcode);
        op = RSPROT_BRANCH(x, c->opcode);
        if (op < 0 || op > 4) {
            rsprot_exec_fail(x, "REFLECTION_CHECKER: unknown check opcode");
            return;
        }
        RSPROT_JSTR(x, c->class_name);
        RSPROT_JSTR(x, c->member);
        if (op == 1)
            RSPROT_U4(x, c->value);
        if (op == 3 || op == 4) {
            RSPROT_COUNT(x, c->param_count, MSG_REFLECTION_CHECKER_MAX_PARAMS);
            for (int32_t j = 0; j < c->param_count; j++)
                RSPROT_JSTR(x, c->param_classes[j]);
            RSPROT_JSTR(x, c->return_class);
        }
        if (op == 3) {
            for (int32_t j = 0; j < c->param_count; j++) {
                RSPROT_U4(x, c->param_sizes[j]);
                RSPROT_DATA(x, c->param_values[j], c->param_sizes[j]);
            }
        }
    }
}

const RsprotVersionRange rsprot_reflection_checker_out[] = {
    { 223, 241, (RsprotCodecFn)packet_reflection_checker_v1_out },
};

const int rsprot_reflection_checker_out_count =
    (int)(sizeof(rsprot_reflection_checker_out) / sizeof(rsprot_reflection_checker_out[0]));
