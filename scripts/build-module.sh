#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
: "${KDIR:?Set KDIR to a prepared matching kernel build tree}"

make -C "$KDIR" M="$ROOT" ARCH=arm64 LLVM=1 modules
cp "$ROOT/ctb_name_patch.ko" "$ROOT/prebuilt/ctb_name_patch.ko"
