/*
 * CS2 VM unity build — compile this single file to get the full CS2VMX
 * bytecode interpreter, decoded-script helpers, trigger-arg parsing, and
 * opcode metadata.
 *
 * Usage in another project:
 *   1. Add -I<path>/src2 to your include path.
 *   2. Compile cs2vm_unity.c once (e.g. cc -c -I<path>/src2 vm/cs2vm_unity.c).
 *   3. #include "vm/cs2vmx.h" where you need the API.
 *   4. Provide a CS2VMX_HostExec_Fn host (e.g. GameRunescape_CS2HostExec).
 */

#include "cs2_opcode_meta.c"
#include "cs2_script.c"
#include "cs2_trigger_args.c"
#include "cs2vmx.c"
#include <assert.h>
