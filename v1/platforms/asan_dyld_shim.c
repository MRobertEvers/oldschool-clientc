/* macOS 26+ AddressSanitizer dyld deadlock workaround.
 * https://github.com/llvm/llvm-project/issues/182943
 *
 * dyld_shared_cache_iterate_text may allocate via _Block_copy before ASan's
 * malloc interceptor is ready, re-entering AsanInitFromRtl() and spinning.
 * Interpose the iterate call with a non-allocating _dyld_get_dyld_header path.
 */
#if defined(__APPLE__) && defined(TORIRS_ENABLE_ASAN)

#include <dlfcn.h>
#include <mach-o/loader.h>
#include <stdint.h>
#include <string.h>
#include <uuid/uuid.h>

extern const void* _dyld_get_shared_cache_range(size_t* length);

struct dyld_shared_cache_dylib_text_info {
    uint64_t version;
    uint64_t loadAddressUnslid;
    uint64_t textSegmentSize;
    uuid_t dylibUuid;
    const char* path;
    uint64_t textSegmentOffset;
};
typedef void (^dsc_callback)(const struct dyld_shared_cache_dylib_text_info*);

extern int dyld_shared_cache_iterate_text(const uuid_t, dsc_callback);

static int
shim_iterate(
    const uuid_t uuid,
    dsc_callback cb)
{
    (void)uuid;
    typedef const struct mach_header* (*get_hdr_fn)(void);
    get_hdr_fn get_hdr = (get_hdr_fn)dlsym(RTLD_DEFAULT, "_dyld_get_dyld_header");
    const struct mach_header* hdr = get_hdr ? get_hdr() : NULL;
    size_t len;
    const void* cacheStart = _dyld_get_shared_cache_range(&len);
    if( !hdr || !cacheStart || !cb )
        return -1;

    struct dyld_shared_cache_dylib_text_info info;
    memset(&info, 0, sizeof(info));
    info.version = 2;
    info.textSegmentOffset = (uint64_t)((uintptr_t)hdr - (uintptr_t)cacheStart);
    info.path = "/usr/lib/dyld";
    cb(&info);
    return 0;
}

__attribute__((used, section("__DATA,__interpose"))) static struct {
    const void* replacement;
    const void* original;
} interpose_tbl[] = {
    {(const void*)(uintptr_t)&shim_iterate,
     (const void*)(uintptr_t)&dyld_shared_cache_iterate_text},
};

#endif
