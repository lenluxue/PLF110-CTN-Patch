#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CC=${CC:-aarch64-linux-android-clang}
STRIP=${STRIP:-aarch64-linux-android-strip}
OUT=${OUT:-$ROOT/prebuilt/plf110_ctnd}

"$CC" -O2 -Os -Wall -Wextra -Wno-unused-parameter \
	-Wno-format-truncation -ffunction-sections -fdata-sections \
	-fno-strict-aliasing -Wl,--gc-sections -Wl,--build-id=none \
	-o "$OUT" "$ROOT/plf110_ctnd.c" -ldl

if command -v "$STRIP" >/dev/null 2>&1; then
	"$STRIP" "$OUT"
fi
chmod 0755 "$OUT"
