/* macOS SDL installs signal handlers during startup.  Let those handlers own
 * their signals rather than asking ASan to interpose them while its runtime is
 * still initialising. */
#if defined(__APPLE__) && defined(TORIRS_ENABLE_ASAN)

const char*
__asan_default_options(void)
{
    return "handle_segv=0:handle_sigbus=0:handle_sigill=0";
}

#endif
