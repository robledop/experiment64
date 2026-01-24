#!/bin/sh
set -eu

ROOT_DIR=$(cd "$(dirname "$0")/../.." && pwd)
DOOMGENERIC_ROOT=${DOOMGENERIC_ROOT:-"$ROOT_DIR/third_party/doomgeneric"}
UPSTREAM_URL="https://github.com/ozkl/doomgeneric"
UPSTREAM_COMMIT="fc601639494e089702a1ada082eb51aaafc03722"
#PATCH_DIR="$ROOT_DIR/user/doom/patches"

mkdir -p "$(dirname "$DOOMGENERIC_ROOT")"

if [ ! -d "$DOOMGENERIC_ROOT/.git" ]; then
  git clone "$UPSTREAM_URL" "$DOOMGENERIC_ROOT"
fi

if ! git -C "$DOOMGENERIC_ROOT" cat-file -e "${UPSTREAM_COMMIT}^{commit}" 2>/dev/null; then
  git -C "$DOOMGENERIC_ROOT" fetch --depth 1 origin "$UPSTREAM_COMMIT"
fi

git -C "$DOOMGENERIC_ROOT" checkout -q "$UPSTREAM_COMMIT"

if [ ! -d "$DOOMGENERIC_ROOT/doomgeneric" ]; then
  echo "doomgeneric source directory missing: $DOOMGENERIC_ROOT/doomgeneric" >&2
  exit 1
fi

#for patch in "$PATCH_DIR"/*.patch; do
#  if [ ! -f "$patch" ]; then
#    continue
#  fi
#  if git -C "$DOOMGENERIC_ROOT" apply --reverse --check "$patch" >/dev/null 2>&1; then
#    continue
#  fi
#  git -C "$DOOMGENERIC_ROOT" apply "$patch"
#done
