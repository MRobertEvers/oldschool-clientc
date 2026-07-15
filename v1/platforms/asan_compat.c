/* macOS + AddressSanitizer: SDL2 installs its own signal handlers and thread
 * setup; ASan's default signal interception can deadlock or spin at startup.
 * See ASAN_OPTIONS=handle_segv=0:handle_sigbus=0:handle_sigill=0 */
#if defined(__APPLE__) && defined(TORIRS_ENABLE_ASAN)

const char*
__asan_default_options(void)
{
    return "handle_segv=0:handle_sigbus=0:handle_sigill=0";
}

#endif
