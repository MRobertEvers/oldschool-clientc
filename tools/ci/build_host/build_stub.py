#!/usr/bin/env python3
"""Cross-platform stub build: writes CI_OUT_ZIP with run.bat (expects max-runtime seconds as %1)."""
import os
import zipfile

# run.bat: first argument is max runtime seconds (passed by xp_listener); stub only echoes it.
_RUN_BAT = (
    b"@echo off\r\n"
    b"echo ci_run max_runtime_seconds=%~1\r\n"
    b'if "%~1"=="" (\r\n'
    b"  echo ERROR missing run time arg 1>&2\r\n"
    b"  exit /b 1\r\n"
    b")\r\n"
    b"echo stub CI run succeeded.\r\n"
)


def main():
    out = os.environ.get("CI_OUT_ZIP")
    if not out:
        raise SystemExit("CI_OUT_ZIP is required")
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as zf:
        zf.writestr("run.bat", _RUN_BAT)


if __name__ == "__main__":
    main()
