/*
 * The JIT section's instructions (tools/cs2_gen_opcodes/jit_instructions.py;
 * [CS2_OP_JIT_FIRST, CS2_OP_COUNT) of cs2_opcode.h), one case each and no
 * other: tools/cs2_gen_opcodes/check_loop_arms.py fails on a difference.
 * Included by the interpreter's one loop (cs2vm2_run_script_body in
 * cs2vm2.c), beside cs2vm2_fast_cases.inc; like it, not a header but the
 * inside of the loop's switch, reading the loop's locals by name.
 *
 * The declared-local forms and the fused instructions index `locals` without
 * the FAST cases' range and dirty-mark tests. That is sound for every frame:
 * an overlay names only locals below the script's declared local_int_count
 * (<= CS2VM_MAX_LOCALS), proven when it was built or attached, and
 * CS2VM2_PushCallScript sizes every frame's int locals to that count and
 * raises the mark over them. A FAST POP_INT_LOCAL above the declared count
 * still grows the buffer, and `locals` is re-read after it.
 *
 * Dead slots above `top` that an unfused run would have written in passing
 * (the pushes a compare-and-branch pops again) are left as they were. Nothing
 * reads above `top`.
 *
 * A case that cannot run as it stands changes nothing and does `goto stop`,
 * which gives the instruction at pc (opcodes[pc], the first of a fused group)
 * to its handler.
 */
            case CS2_OP_END_OF_SCRIPT:
                goto script_end;
            case CS2_OP_RUN_HANDLER:
                goto stop;


            case CS2_OP_PUSH_INT_LOCAL_DECLARED:
                if( top >= CS2VM_STACK_MAX )
                    goto stop;
                stack[top++] = locals[operand];
                pc += 1;
                break;
            case CS2_OP_POP_INT_LOCAL_DECLARED:
                if( top <= 0 )
                    goto stop;
                locals[operand] = stack[--top];
                pc += 1;
                break;
            case CS2_OP_PUSH_CONSTANT_STRING_PRESENT:
                /* The script's own constant, not a pool copy: nothing writes
                 * through a string it popped (UPPERCASE and LOWERCASE allocate). */
                if( vm->strs_stack_top >= CS2VM_STACK_MAX )
                    goto stop;
                vm->strs_stack[vm->strs_stack_top++] = cstrs[pc];
                pc += 1;
                break;

            case CS2_OP_PUSH_INT_LOCAL__PUSH_INT_LOCAL:
                CS2VM2_JIT_GUARD(CS2_OP_PUSH_INT_LOCAL__PUSH_INT_LOCAL);
                stack[top] = locals[operand];
                stack[top + 1] = locals[args[pc + 1]];
                top += 2;
                CS2VM2_JIT_DONE(CS2_OP_PUSH_INT_LOCAL__PUSH_INT_LOCAL);
            case CS2_OP_PUSH_INT_LOCAL__PUSH_CONSTANT_INT:
                CS2VM2_JIT_GUARD(CS2_OP_PUSH_INT_LOCAL__PUSH_CONSTANT_INT);
                stack[top] = locals[operand];
                stack[top + 1] = args[pc + 1];
                top += 2;
                CS2VM2_JIT_DONE(CS2_OP_PUSH_INT_LOCAL__PUSH_CONSTANT_INT);
            case CS2_OP_PUSH_CONSTANT_INT__PUSH_INT_LOCAL:
                CS2VM2_JIT_GUARD(CS2_OP_PUSH_CONSTANT_INT__PUSH_INT_LOCAL);
                stack[top] = operand;
                stack[top + 1] = locals[args[pc + 1]];
                top += 2;
                CS2VM2_JIT_DONE(CS2_OP_PUSH_CONSTANT_INT__PUSH_INT_LOCAL);
            case CS2_OP_PUSH_CONSTANT_INT__PUSH_CONSTANT_INT:
                CS2VM2_JIT_GUARD(CS2_OP_PUSH_CONSTANT_INT__PUSH_CONSTANT_INT);
                stack[top] = operand;
                stack[top + 1] = args[pc + 1];
                top += 2;
                CS2VM2_JIT_DONE(CS2_OP_PUSH_CONSTANT_INT__PUSH_CONSTANT_INT);
            case CS2_OP_PUSH_INT_LOCAL__PUSH_INT_LOCAL__PUSH_INT_LOCAL:
                CS2VM2_JIT_GUARD(CS2_OP_PUSH_INT_LOCAL__PUSH_INT_LOCAL__PUSH_INT_LOCAL);
                stack[top] = locals[operand];
                stack[top + 1] = locals[args[pc + 1]];
                stack[top + 2] = locals[args[pc + 2]];
                top += 3;
                CS2VM2_JIT_DONE(CS2_OP_PUSH_INT_LOCAL__PUSH_INT_LOCAL__PUSH_INT_LOCAL);
            case CS2_OP_PUSH_INT_LOCAL__PUSH_CONSTANT_INT__ADD__POP_INT_LOCAL:
                CS2VM2_JIT_GUARD(CS2_OP_PUSH_INT_LOCAL__PUSH_CONSTANT_INT__ADD__POP_INT_LOCAL);
                locals[args[pc + 3]] = cs2_math_add(locals[operand], args[pc + 1]);
                CS2VM2_JIT_DONE(CS2_OP_PUSH_INT_LOCAL__PUSH_CONSTANT_INT__ADD__POP_INT_LOCAL);
            case CS2_OP_PUSH_INT_LOCAL__PUSH_CONSTANT_INT__ADD:
                CS2VM2_JIT_GUARD(CS2_OP_PUSH_INT_LOCAL__PUSH_CONSTANT_INT__ADD);
                stack[top++] = cs2_math_add(locals[operand], args[pc + 1]);
                CS2VM2_JIT_DONE(CS2_OP_PUSH_INT_LOCAL__PUSH_CONSTANT_INT__ADD);
            case CS2_OP_PUSH_CONSTANT_INT__ADD:
                CS2VM2_JIT_GUARD(CS2_OP_PUSH_CONSTANT_INT__ADD);
                stack[top - 1] = cs2_math_add(stack[top - 1], operand);
                CS2VM2_JIT_DONE(CS2_OP_PUSH_CONSTANT_INT__ADD);
            case CS2_OP_ADD__POP_INT_LOCAL:
                CS2VM2_JIT_GUARD(CS2_OP_ADD__POP_INT_LOCAL);
                locals[args[pc + 1]] = cs2_math_add(stack[top - 2], stack[top - 1]);
                top -= 2;
                CS2VM2_JIT_DONE(CS2_OP_ADD__POP_INT_LOCAL);
            case CS2_OP_PUSH_CONSTANT_INT__POP_INT_LOCAL:
                CS2VM2_JIT_GUARD(CS2_OP_PUSH_CONSTANT_INT__POP_INT_LOCAL);
                locals[args[pc + 1]] = operand;
                CS2VM2_JIT_DONE(CS2_OP_PUSH_CONSTANT_INT__POP_INT_LOCAL);
            case CS2_OP_PUSH_INT_LOCAL__POP_INT_LOCAL:
                CS2VM2_JIT_GUARD(CS2_OP_PUSH_INT_LOCAL__POP_INT_LOCAL);
                locals[args[pc + 1]] = locals[operand];
                CS2VM2_JIT_DONE(CS2_OP_PUSH_INT_LOCAL__POP_INT_LOCAL);
            case CS2_OP_POP_INT_LOCAL__BRANCH:
                CS2VM2_JIT_GUARD(CS2_OP_POP_INT_LOCAL__BRANCH);
                {
                    int const off = args[pc + 1];
                    locals[operand] = stack[--top];
                    pc += off;
                }
                CS2VM2_JIT_DONE(CS2_OP_POP_INT_LOCAL__BRANCH);

            case CS2_OP_PUSH_CONSTANT_INT__BRANCH_EQUALS:
                CS2VM2_JIT_CBR(CS2_OP_PUSH_CONSTANT_INT__BRANCH_EQUALS, ==);
            case CS2_OP_PUSH_CONSTANT_INT__BRANCH_NOT:
                CS2VM2_JIT_CBR(CS2_OP_PUSH_CONSTANT_INT__BRANCH_NOT, !=);
            case CS2_OP_PUSH_CONSTANT_INT__BRANCH_LESS_THAN:
                CS2VM2_JIT_CBR(CS2_OP_PUSH_CONSTANT_INT__BRANCH_LESS_THAN, <);
            case CS2_OP_PUSH_CONSTANT_INT__BRANCH_GREATER_THAN:
                CS2VM2_JIT_CBR(CS2_OP_PUSH_CONSTANT_INT__BRANCH_GREATER_THAN, >);
            case CS2_OP_PUSH_INT_LOCAL__PUSH_CONSTANT_INT__BRANCH_EQUALS:
                CS2VM2_JIT_LBR(CS2_OP_PUSH_INT_LOCAL__PUSH_CONSTANT_INT__BRANCH_EQUALS, args[pc + 1], ==);
            case CS2_OP_PUSH_INT_LOCAL__PUSH_CONSTANT_INT__BRANCH_NOT:
                CS2VM2_JIT_LBR(CS2_OP_PUSH_INT_LOCAL__PUSH_CONSTANT_INT__BRANCH_NOT, args[pc + 1], !=);
            case CS2_OP_PUSH_INT_LOCAL__PUSH_CONSTANT_INT__BRANCH_LESS_THAN:
                CS2VM2_JIT_LBR(CS2_OP_PUSH_INT_LOCAL__PUSH_CONSTANT_INT__BRANCH_LESS_THAN, args[pc + 1], <);
            case CS2_OP_PUSH_INT_LOCAL__PUSH_CONSTANT_INT__BRANCH_GREATER_THAN:
                CS2VM2_JIT_LBR(CS2_OP_PUSH_INT_LOCAL__PUSH_CONSTANT_INT__BRANCH_GREATER_THAN, args[pc + 1], >);
            case CS2_OP_PUSH_INT_LOCAL__PUSH_INT_LOCAL__BRANCH_EQUALS:
                CS2VM2_JIT_LBR(CS2_OP_PUSH_INT_LOCAL__PUSH_INT_LOCAL__BRANCH_EQUALS, locals[args[pc + 1]], ==);
            case CS2_OP_PUSH_INT_LOCAL__PUSH_INT_LOCAL__BRANCH_NOT:
                CS2VM2_JIT_LBR(CS2_OP_PUSH_INT_LOCAL__PUSH_INT_LOCAL__BRANCH_NOT, locals[args[pc + 1]], !=);
            case CS2_OP_PUSH_INT_LOCAL__PUSH_INT_LOCAL__BRANCH_LESS_THAN:
                CS2VM2_JIT_LBR(CS2_OP_PUSH_INT_LOCAL__PUSH_INT_LOCAL__BRANCH_LESS_THAN, locals[args[pc + 1]], <);
            case CS2_OP_PUSH_INT_LOCAL__PUSH_INT_LOCAL__BRANCH_GREATER_THAN:
                CS2VM2_JIT_LBR(CS2_OP_PUSH_INT_LOCAL__PUSH_INT_LOCAL__BRANCH_GREATER_THAN, locals[args[pc + 1]], >);
