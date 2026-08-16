#include <stdio.h>
#include \"../../src2/platforms/torirs_memtrace.c\"
int main(){printf(\"record %zu header %zu mod %zu\n\", sizeof(struct TorIRS_MemTrace_EventRecord), sizeof(struct TorIRS_MemTrace_FileHeader), sizeof(struct TorIRS_MemTrace_ModEntry)); return 0;}
