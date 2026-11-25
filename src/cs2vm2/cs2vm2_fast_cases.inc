/*
 * The FAST instructions (tools/cs2_gen_opcodes/fast_instructions.py; the
 * range [CS2_OP_FAST_BEGIN, CS2_OP_PLAIN_COUNT) of cs2_opcode.h), one case each and no other:
 * tools/cs2_gen_opcodes/check_loop_arms.py fails on a difference. Included by
 * the interpreter's one loop (cs2vm2_run_script_body in cs2vm2.c). Not a
 * header: it is the inside of the loop's switch on the instruction it runs,
 * and reads the loop's locals by name -- `operand` (args[pc]), `stack`,
 * `top`, `pc`, `locals`, `locals_dirty`, `frame`. Each case either completes
 * its instruction (pc moved on, `break`) or changes nothing and does
 * `goto stop`, which gives the instruction (opcodes[pc]) to its handler.
 */
        case CS2_OP_PUSH_CONSTANT_INT:
            if( top >= CS2VM_STACK_MAX )
                goto stop;
            stack[top++] = operand;
            pc += 1;
            break;
        case CS2_OP_PUSH_INT_LOCAL:
            if( top >= CS2VM_STACK_MAX || operand < 0 || operand >= CS2VM_MAX_LOCALS )
                goto stop;
            stack[top++] = operand < locals_dirty ? locals[operand] : 0;
            pc += 1;
            break;
        case CS2_OP_POP_INT_LOCAL:
            if( top <= 0 || operand < 0 || operand >= CS2VM_MAX_LOCALS )
                goto stop;
            if( operand < locals_dirty )
            {
                locals[operand] = stack[--top];
            }
            else
            {
                /* First write to this slot: the frame grows its buffer and
                 * raises its mark, and both are read again. */
                *cs2vm2_frame_int_local_ref(frame, operand) = stack[--top];
                locals = frame->int_locals;
                locals_dirty = frame->int_locals_dirty;
            }
            pc += 1;
            break;

        case CS2_OP_BRANCH:
            pc += 1 + operand;
            break;
        case CS2_OP_BRANCH_EQUALS:
            CS2VM2_FAST_BRANCH_IF(==);
        case CS2_OP_BRANCH_NOT:
            CS2VM2_FAST_BRANCH_IF(!=);
        case CS2_OP_BRANCH_LESS_THAN:
            CS2VM2_FAST_BRANCH_IF(<);
        case CS2_OP_BRANCH_GREATER_THAN:
            CS2VM2_FAST_BRANCH_IF(>);
        case CS2_OP_BRANCH_LESS_THAN_OR_EQUALS:
            CS2VM2_FAST_BRANCH_IF(<=);
        case CS2_OP_BRANCH_GREATER_THAN_OR_EQUALS:
            CS2VM2_FAST_BRANCH_IF(>=);

        case CS2_OP_ADD:
            CS2VM2_FAST_BINARY(cs2_math_add(a, b));
        case CS2_OP_SUB:
            CS2VM2_FAST_BINARY(cs2_math_sub(a, b));
        case CS2_OP_MULTIPLY:
            CS2VM2_FAST_BINARY(cs2_math_mul(a, b));
        case CS2_OP_DIVIDE:
            CS2VM2_FAST_BINARY_CHECKED(cs2_math_div(a, b, &out));
        case CS2_OP_MODULO:
            CS2VM2_FAST_BINARY_CHECKED(cs2_math_mod(a, b, &out));
        case CS2_OP_AND:
            CS2VM2_FAST_BINARY(cs2_math_and(a, b));
        case CS2_OP_OR:
            CS2VM2_FAST_BINARY(cs2_math_or(a, b));
        case CS2_OP_MIN:
            CS2VM2_FAST_BINARY(cs2_math_min(a, b));
        case CS2_OP_MAX:
            CS2VM2_FAST_BINARY(cs2_math_max(a, b));
        case CS2_OP_ADDPERCENT:
            CS2VM2_FAST_BINARY(cs2_math_addpercent(a, b));
        case CS2_OP_ABS:
            CS2VM2_FAST_UNARY(cs2_math_abs(a));
        case CS2_OP_BITCOUNT:
            CS2VM2_FAST_UNARY(cs2_math_bitcount(a));
        case CS2_OP_SETBIT:
            CS2VM2_FAST_BINARY(cs2_math_setbit(a, b));
        case CS2_OP_CLEARBIT:
            CS2VM2_FAST_BINARY(cs2_math_clearbit(a, b));
        case CS2_OP_TESTBIT:
            CS2VM2_FAST_BINARY(cs2_math_testbit(a, b));
        case CS2_OP_TOGGLEBIT:
            CS2VM2_FAST_BINARY(cs2_math_togglebit(a, b));
        case CS2_OP_SCALE:
        {
            if( top < 3 )
                goto stop;
            stack[top - 3] = cs2_math_scale(stack[top - 3], stack[top - 2], stack[top - 1]);
            top -= 2;
            pc += 1;
            break;
        }
        case CS2_OP_SETBIT_RANGE:
            CS2VM2_FAST_TERNARY_CHECKED(cs2_math_setbit_range(a, b, c, &out));
        case CS2_OP_CLEARBIT_RANGE:
            CS2VM2_FAST_TERNARY_CHECKED(cs2_math_clearbit_range(a, b, c, &out));
        case CS2_OP_GETBIT_RANGE:
        {
            if( top < 3 )
                goto stop;
            stack[top - 3] = cs2_math_getbit_range(stack[top - 3], stack[top - 2], stack[top - 1]);
            top -= 2;
            pc += 1;
            break;
        }
        case CS2_OP_SETBIT_RANGE_VALUE:
        {
            /* Stack (top first): high, low, new bits, value. */
            int out;
            if( top < 4 )
                goto stop;
            if( !cs2_math_setbit_range_value(
                    stack[top - 4], stack[top - 3], stack[top - 2], stack[top - 1], &out) )
                goto stop;
            stack[top - 4] = out;
            top -= 3;
            pc += 1;
            break;
        }
