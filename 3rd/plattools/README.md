# plattools

Small C utilities whose behavior is defined across every client platform.

## Full paths

```c
#include <stdlib.h>

#include "plattools.h"

char *full = plattools_full_path("assets/title.dat");
if (full) {
    /* use full */
    free(full);
}
```

`plattools_full_path` returns a `malloc`-allocated UTF-8 path and does not
require the target to exist. It resolves `.` and `..` lexically.

- Win32 uses the native Windows resolver and returns native separators.
- Linux and macOS return normalized absolute POSIX paths.
- Web returns a normalized absolute path in Emscripten's virtual filesystem.
  A browser cannot expose the corresponding host filesystem path.
- Web also accepts `idb://key`, `localstorage://key`, and
  `sessionstorage://key`. These are storage-qualified key paths rather than
  URLs or mounted directories. Scheme names are case-insensitive on input and
  canonicalized to lowercase; `.` and `..` are normalized without allowing a
  key to escape its storage root.

Compile `plattools.c` into the consuming target, or build the static library
and run its standalone checks:

```sh
make -C 3rd/plattools
make -C 3rd/plattools test
```
