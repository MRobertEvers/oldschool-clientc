

PT_STATE_BEGIN(dat1_test)
int i;
PT_STATE_END(dat1_test)

PT_THREAD(dat1_test)

PT_BEGIN();

for( i = 0; i < 10; i++ )
{
    printf("i: %d\n", i);
    // Build IO Request
}
PT_YIELD();

PT_END();