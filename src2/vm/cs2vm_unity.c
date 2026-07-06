/*
 * CS2 VM unity build — compile this single file to get the full CS2
 * bytecode interpreter, decoded-script helpers, trigger-arg parsing, and
 * the concrete UI host wiring.
 *
 * Usage in another project:
 *   1. Add -I<path>/src2 to your include path.
 *   2. Compile cs2vm_unity.c once (e.g. cc -c -I<path>/src2 vm/cs2vm_unity.c).
 *   3. #include "vm/cs2vm.h" (and cs2_host_ui.h, etc.) where you need the API.
 */

#include "cs2_opcode_meta.c"
#include "cs2_script.c"
#include "cs2_trigger_args.c"
#include "cs2vm.c"
#include "cs2_host_ui.c"
