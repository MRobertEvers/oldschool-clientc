# Repository Windows toolchains

The two Windows build lanes use pinned WinLibs/MinGW-w64 archives committed
through Git LFS under `lib/`. The PowerShell wrappers validate the compiler
triple and extract the required archive on demand into the shared
`toolchains/` directory alongside the Java toolchain. The directory is not
ignored, so all repository toolchain material is visible to Git in one place.

| Lane | Canonical archive | Required triple | Contents | Repository SHA-256 |
|---|---|---|---|---|
| Windows XP | `lib/mingw32-win32-toolchain.zip` | `i686-w64-mingw32` | WinLibs GCC 15.2.0, MinGW-w64 13.0.0, MSVCRT, POSIX threads | `51bf4318564efe225afb32715431e68137f3301ca5c055eb916969ea565bb616` |
| Modern Windows | `lib/mingw64-win64-toolchain.zip` | `x86_64-w64-mingw32` | WinLibs GCC 16.1.0, MinGW-w64 14.0.0, MSVCRT, POSIX/SEH | `cbacca0307e6bffd8a989727f517d1572b4c860d5ac5a2568e20dd7786cf8e68` |

The modern archive is a repository repack of the `mingw64/` tree from the
[WinLibs 16.1.0/MSVCRT r4 release](https://github.com/brechtsanders/winlibs_mingw/releases/tag/16.1.0posix-14.0.0-msvcrt-r4), so its repository checksum is intentionally
the checksum above rather than the checksum of the upstream ZIP. The archives
retain the toolchain's component notices and licenses under their `share/`
trees.

After cloning, materialize the archives before building:

```powershell
git lfs pull --include="lib/mingw32-win32-toolchain.zip,lib/mingw64-win64-toolchain.zip"
```

Then use the lane wrapper from the repository root:

```powershell
.\build_winxp.ps1 -Opt
.\build_windows.ps1 -Opt
```

Both wrappers accept `-Toolchain` for a deliberate local override, but still
reject the wrong target triple. `src/makefile` has POSIX-shell recipes, so Git
for Windows (or another `sh.exe` already on `PATH`) remains a host prerequisite;
the compiler archives do not contain a shell.

Do not run the two wrappers concurrently. Their platform object directories
and final executables are distinct, but a few generated host-tool outputs and
`src/.last_flavor` are still shared.

## Stylizer model archive

The same `lib/` + Git LFS pattern also carries the pre-trained ML checkpoints
for the OSRS stylizer pipeline:

| Archive | Contents | Repository SHA-256 |
|---|---|---|
| `lib/osrs-stylizer-models.zip` | `models/osrs_classifier.pt`, `models/osrs_engine_judge.pt`, `models/content_preserver.pt`, `requirements.txt`, `MODELS.md` | `f29e2eb29fc03918f5c7cadcb7b9fca1f9d049182a47111fc0ebd08768cbcd24` |

To use the trained judges without retraining:

```powershell
git lfs pull --include="lib/osrs-stylizer-models.zip"
Expand-Archive lib\osrs-stylizer-models.zip -DestinationPath tools\osrs_stylizer\ -Force
pip install -r tools\osrs_stylizer\requirements.txt
```

That lands the checkpoints at `tools/osrs_stylizer/models/*.pt` — the default
paths the scorers expect. `MODELS.md` inside the archive documents each
checkpoint's accuracy and calibration; the full pipeline documentation is
[`tools/osrs_stylizer/README.md`](../osrs_stylizer/README.md).

When refreshing either compiler archive:

1. preserve the top-level `mingw32/` or `mingw64/` directory expected by
   `scripts/windows_toolchain.ps1`;
2. update the version, exact triple, and SHA-256 in this file;
3. run the matching wrapper and its post-link artifact check; and
4. confirm `git check-attr filter -- <archive>` reports `lfs`.
