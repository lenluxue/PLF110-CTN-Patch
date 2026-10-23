#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

sh -n ksu/*.sh scripts/*.sh
sha256sum -c SHA256SUMS
unzip -t dist/plf110_ctnd_kmod_v2.1.3.zip >/dev/null

EXPECTED=$(sha256sum prebuilt/ctb_name_patch.ko | awk '{print $1}')
ACTUAL=$(unzip -p dist/plf110_ctnd_kmod_v2.1.3.zip ctb_name_patch.ko | sha256sum | awk '{print $1}')
test "$EXPECTED" = "$ACTUAL"

strings prebuilt/ctb_name_patch.ko | grep -q '^vermagic=6.1.157-android14-11-o-gc2dad16af736 '
echo 'release verification passed'
