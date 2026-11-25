/*
 * The FAST instructions (tools/cs2_gen_opcodes/fast_instructions.py; the
 * range [0, CS2_OP_FAST_END) of cs2_opcode.h), one case each and no other:
 * tools/cs2_gen_opcodes/check_fast_arms.py fails on a difference. Included by
 * the interpreter's one loop (cs2vm2_run_script_body in cs2vm2.c), where a
 * script's plain codes and a jit overlay's plain codes meet the same text.
 * Not a header: it is the inside of a switch on a CS2VM2_HOT_* code, and
 * reads the loop's locals by name -- `operand` (args[pc]), `stack`, `top`,
 * `pc`, `locals`, `locals_dirty`, `frame`. Each case either completes its
 * instruction (pc moved on, `break`) or changes nothing and does `goto stop`,
 * which gives the instruction to its handler.
 */
        case CS2VM2_HOT_PUSH_CONSTANT_INT:
            if( top >= CS2VM_STACK_MAX )
                goto stop;
            stack[top++] = operand;
            pc += 1;
            break;
        case CS2VM2_HOT_PUSH_INT_LOCAL:
            if( top >= CS2VM_STACK_MAX || operand < 0 || operand >= CS2VM_MAX_LOCALS )
                goto stop;
            stack[top++] = operand < locals_dirty ? locals[operand] : 0;
            pc += 1;
            break;
        case CS2VM2_HOT_POP_INT_LOCAL:
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

        case CS2VM2_HOT_BRANCH:
            pc += 1 + operand;
            break;
        case CS2VM2_HOT_BRANCH_EQUALS:
            CS2VM2_HOT_BRANCH_IF(==);
        case CS2VM2_HOT_BRANCH_NOT:
            CS2VM2_HOT_BRANCH_IF(!=);
        case CS2VM2_HOT_BRANCH_LESS_THAN:
            CS2VM2_HOT_BRANCH_IF(<);
        case CS2VM2_HOT_BRANCH_GREATER_THAN:
            CS2VM2_HOT_BRANCH_IF(>);
        case CS2VM2_HOT_BRANCH_LESS_THAN_OR_EQUALS:
            CS2VM2_HOT_BRANCH_IF(<=);
        case CS2VM2_HOT_BRANCH_GREATER_THAN_OR_EQUALS:
            CS2VM2_HOT_BRANCH_IF(>=);

        case CS2VM2_HOT_ADD:
            CS2VM2_HOT_BINARY(cs2_math_add(a, b));
        case CS2VM2_HOT_SUB:
            CS2VM2_HOT_BINARY(cs2_math_sub(a, b));
        case CS2VM2_HOT_MULTIPLY:
            CS2VM2_HOT_BINARY(cs2_math_mul(a, b));
        case CS2VM2_HOT_DIVIDE:
            CS2VM2_HOT_BINARY_CHECKED(cs2_math_div(a, b, &out));
        case CS2VM2_HOT_MODULO:
            CS2VM2_HOT_BINARY_CHECKED(cs2_math_mod(a, b, &out));
        case CS2VM2_HOT_AND:
            CS2VM2_HOT_BINARY(cs2_math_and(a, b));
        case CS2VM2_HOT_OR:
            CS2VM2_HOT_BINARY(cs2_math_or(a, b));
        case CS2VM2_HOT_MIN:
            CS2VM2_HOT_BINARY(cs2_math_min(a, b));
        case CS2VM2_HOT_MAX:
            CS2VM2_HOT_BINARY(cs2_math_max(a, b));
        case CS2VM2_HOT_ADDPERCENT:
            CS2VM2_HOT_BINARY(cs2_math_addpercent(a, b));
        case CS2VM2_HOT_ABS:
            CS2VM2_HOT_UNARY(cs2_math_abs(a));
        case CS2VM2_HOT_BITCOUNT:
            CS2VM2_HOT_UNARY(cs2_math_bitcount(a));
        case CS2VM2_HOT_SETBIT:
            CS2VM2_HOT_BINARY(cs2_math_setbit(a, b));
        case CS2VM2_HOT_CLEARBIT:
            CS2VM2_HOT_BINARY(cs2_math_clearbit(a, b));
        case CS2VM2_HOT_TESTBIT:
            CS2VM2_HOT_BINARY(cs2_math_testbit(a, b));
        case CS2VM2_HOT_TOGGLEBIT:
            CS2VM2_HOT_BINARY(cs2_math_togglebit(a, b));
        case CS2VM2_HOT_SCALE:
        {
            if( top < 3 )
                goto stop;
            stack[top - 3] = cs2_math_scale(stack[top - 3], stack[top - 2], stack[top - 1]);
            top -= 2;
            pc += 1;
            break;
        }
        case CS2VM2_HOT_SETBIT_RANGE:
            CS2VM2_HOT_TERNARY_CHECKED(cs2_math_setbit_range(a, b, c, &out));
        case CS2VM2_HOT_CLEARBIT_RANGE:
            CS2VM2_HOT_TERNARY_CHECKED(cs2_math_clearbit_range(a, b, c, &out));
        case CS2VM2_HOT_GETBIT_RANGE:
        {
            if( top < 3 )
                goto stop;
            stack[top - 3] = cs2_math_getbit_range(stack[top - 3], stack[top - 2], stack[top - 1]);
            top -= 2;
            pc += 1;
            break;
        }
        case CS2VM2_HOT_SETBIT_RANGE_VALUE:
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
