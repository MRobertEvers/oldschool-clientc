# Retired native/surface plugin chrome prototype

This directory preserves the native and retained-surface implementation that
was replaced by the shared WebView/DOM direction on 2026-09-02. It is reference
source only and is not part of any build.

The prototype provided:

- Android framework Views followed by a hybrid retained bitmap surface;
- an attached SDL surface for macOS/Linux;
- attached USER32/GDI controls and D3D9 game-rectangle isolation on Windows;
- one persistent multi-plugin rail with authored icons;
- semantic/custom panel nodes, generation fences, and retained dirty tracking.

`tracked-native-presenters.patch` is a binary-safe `git diff` against commit
`41997e27b` for the affected Android, platform, UI, plugin, app, manifest, and
script files. `snapshot/` holds files that were new/untracked at the point of
retirement. To study or revive it, use a disposable branch/worktree and apply
the patch there; do not add this directory to production include paths.

The platform-neutral `PluginHost` panel registry/API and selection lifecycle
are not conceptually retired. The WebView implementation may retain/evolve
those pieces while replacing only how the host presents them.
