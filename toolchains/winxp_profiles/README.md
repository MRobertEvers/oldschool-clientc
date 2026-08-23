# Windows XP profiling toolchain

Vendored binaries used for Windows XP client profiling.

## Very Sleepy 0.7

- Exact installer supplied for the XP profile: `verysleepy_0_7_setup.exe`
- Installer product version: 0.7
- Installer SHA-256: `BC2038E4F70A7B0E14BF4C0CA041FCA954913A11E1DC34DCDB08A1AE04C43572`
- Installed payload: `verysleepy_0_7_exact/`
- `sleepy.exe` SHA-256: `7E993EB45E7442F7135DA49AE3714D0758CFF94D8ABA3AFBCEC1A1071F7ADBCD`
- License: GPL; see `verysleepy_0_7_exact/license.txt`

This original 0.7 CLI accepts `/r`, `/i`, `/o`, `/t`, and `/q`. Its `/r`
value is one command-line string passed to `CreateProcess`. On the XP target,
where the paths contain no spaces, do not add nested quotes around the client
executable. The later `/f` and `/s` switches are not accepted by this build.

The following later archive is retained as a reference/fallback, but it was not
used for the final trace:

- Vendored release: 0.7.2 (the final 0.7-series archive)
- Directory: `verysleepy_0_7/`
- Upstream archive: https://raw.githubusercontent.com/mhoffesommer/Very-Sleepy/master/releases/sleepy-0.7.2.zip
- Archive SHA-256: `DB68365225F0FBB05BA3D386048D18076DE5997DAFE9D93331172A2CBAF66CCD`
- License: GPL, as provided in `verysleepy_0_7/license.rtf`

The upstream repository describes its base as Very Sleepy 0.7 from
2010-10-13. The 0.7.2 archive additionally accepts `/f` for its frame-pointer
stack walker and `/s` for a symbol-path override.

The release ZIP omitted `dbghelp.dll`, although the same revision's installer
expects the matching 32-bit DLL beside `sleepy.exe`. It is vendored from
`mhoffesommer/Very-Sleepy` at the same revision; SHA-256:
`C06430B8CB025BE506BE50A756488E1BCC3827C4F45158D93E4E3EEB98CE1E4F`.

## cv2pdb 0.54

- Directory: `cv2pdb-0.54/`
- Upstream archive: https://github.com/rainers/cv2pdb/releases/download/v0.54/cv2pdb-0.54.zip
- Archive SHA-256: `B2FC075B0B57FBF6D989BF380D91BA443BEC0110B6A3E5A8D4B95A078903E02C`
- License: Artistic License 2.0; see `cv2pdb-0.54/LICENSE`

Run `cv2pdb.exe program.exe` on the host before deployment. It rewrites the
executable to reference a sibling PDB and emits `program.pdb`. Keep the two
files together on the target so Very Sleepy can resolve GCC DWARF symbols.
