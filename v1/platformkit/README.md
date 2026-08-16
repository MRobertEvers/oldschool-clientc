# TRSPK v1 snapshot (archived)

This directory preserves the predecessor retained-mode toolkit for historical
comparison. It is not linked by the current client and its backend layout must
not be used as the platform-extension template.

Current code and documentation live at:

- [`3rd/trspk/`](../../3rd/trspk/README.md) - sole current TRSPK implementation
- [`src/platform/`](../../src/platform/) - current host and renderer backends
- [Platform quirks and contracts](../../docs/platform_quirks.md#retired-stacks) -
  current lane ownership and retired-stack rules

The files under this `v1/` tree remain useful as historical evidence for the
OpenGL3, WebGL1, D3D9, and Soft3D transition. They are snapshots, not build
inputs, and may intentionally differ from current compatibility contracts.
