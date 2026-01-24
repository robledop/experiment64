# Doom (doomgeneric)

The Doom port builds against upstream doomgeneric at a pinned commit. The build
clones the upstream repository into `third_party/doomgeneric` and applies the
patches in `user/doom/patches/` before compiling.

Pinned upstream commit: `fc601639494e089702a1ada082eb51aaafc03722`.

## Setup

- Run `make doom` or `make image.hdd` to fetch, patch, and build.
- If you want to manage the source manually, run `user/doom/prepare_doomgeneric.sh`.
- To override the clone location, set `DOOMGENERIC_ROOT=/path` before building.