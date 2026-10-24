#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${1:-$ROOT/dist/plf110_ctnd_kmod_v2.1.5.zip}
STAGE=$(mktemp -d "${TMPDIR:-/tmp}/plf110-ctn.XXXXXX")
trap 'rm -rf "$STAGE"' EXIT HUP INT TERM

cp "$ROOT"/ksu/* "$STAGE/"
cp "$ROOT/prebuilt/plf110_ctnd" "$STAGE/"
cp "$ROOT/prebuilt/ctb_name_patch.ko" "$STAGE/"
cp "$ROOT/README.md" "$STAGE/README.md"
chmod 0755 "$STAGE"/*.sh "$STAGE/plf110_ctnd"

rm -f "$OUT"
(cd "$STAGE" && zip -r9 -X "$OUT" .)
