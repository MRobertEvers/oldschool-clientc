@echo off
setlocal
rem ---------------------------------------------------------------------------
rem  Record this machine's memory and CPU speed under a label, for before/after
rem  comparison across a hardware change.
rem
rem  Why this exists
rem  ---------------
rem  The XP box lost about a third of its frame rate between two benchmark
rem  sessions, and the binaries are not the cause: torirs-batch8.exe, the exact
rem  build that recorded 67.3 fps, measures ~40 fps today in its own recorded
rem  configuration on a freshly booted machine. What DID change is the shape of
rem  the slowdown:
rem
rem      divprobe  (register-bound)   1.24x slower than the archived run
rem      raster    (memory-bound)     ~1.6x slower
rem
rem  Something that hurts memory much more than registers is a bandwidth loss,
rem  and the usual cause on a board like this is dual-channel having quietly
rem  fallen back to single -- which is what mismatched sticks, or a stick the
rem  board would not train, will do.
rem
rem  How to use it
rem  -------------
rem      membench.cmd before-anything
rem      ... change ONE thing (see below) ...
rem      membench.cmd matched-pair-slots-1-3
rem
rem  Everything is appended to membench.log with its label, so the runs can be
rem  read side by side afterwards.
rem
rem  The number that decides it is section A of membw: "read ... GB/s".
rem  Single-channel DDR400 on a P4 lands near 2.8 GB/s, which is what this box
rem  measures now. Dual channel should be markedly higher. If a change roughly
rem  doubles that number, the channel configuration was the problem.
rem
rem  What to change, one at a time
rem  ----------------------------
rem    1. BIOS first, it costs nothing: find the memory page and read off the
rem       frequency and whether it says Dual Channel / Interleaved. Also look
rem       for a per-slot SPD listing -- two different speeds there answers the
rem       question outright, with no screwdriver.
rem    2. Leave only a MATCHED PAIR installed, in the two slots the board pairs
rem       (usually colour-coded, often 1+3 rather than 1+2 -- the manual says).
rem       Re-run. This is the single most informative test.
rem    3. If you have two pairs, try each pair on its own. A pair that measures
rem       low, or refuses to POST, is the suspect.
rem
rem  This does NOT validate the memory. If the board sometimes refuses to POST
rem  until sticks are moved, run memtest86+ from a CD or USB -- a pattern test
rem  from inside Windows cannot see kernel memory and cannot catch a module
rem  that only fails when hot.
rem ---------------------------------------------------------------------------

set LABEL=%1
if "%LABEL%"=="" set LABEL=unlabelled

set LOG=%~dp0membench.log

echo. >> "%LOG%"
echo ============================================================ >> "%LOG%"
echo LABEL: %LABEL% >> "%LOG%"
echo DATE : %DATE% %TIME% >> "%LOG%"
echo ============================================================ >> "%LOG%"

echo --- what Windows thinks is installed --- >> "%LOG%"
wmic path Win32_PhysicalMemory get BankLabel,DeviceLocator,Capacity,Speed,PartNumber /format:list >> "%LOG%" 2>&1
wmic computersystem get TotalPhysicalMemory /format:list >> "%LOG%" 2>&1
wmic cpu get Name,CurrentClockSpeed,MaxClockSpeed /format:list >> "%LOG%" 2>&1

echo --- membw (section A "read GB/s" is the number that matters) --- >> "%LOG%"
if exist "%~dp0membw.exe" (
  "%~dp0membw.exe" >> "%LOG%" 2>&1
) else (
  if exist membw.exe ( membw.exe >> "%LOG%" 2>&1 ) else ( echo membw.exe not found >> "%LOG%" )
)

echo --- divprobe (register-bound control: should NOT move much) --- >> "%LOG%"
if exist "%~dp0divprobe.exe" (
  "%~dp0divprobe.exe" >> "%LOG%" 2>&1
) else (
  if exist divprobe.exe ( divprobe.exe >> "%LOG%" 2>&1 ) else ( echo divprobe.exe not found >> "%LOG%" )
)

echo. >> "%LOG%"
echo Wrote results for "%LABEL%" to %LOG%
echo.
echo Compare the "read ... GB/s" line in section A between labels.
echo divprobe is the control: if it moves too, the change was not memory-only.
endlocal
