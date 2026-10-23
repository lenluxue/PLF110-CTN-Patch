#!/system/bin/sh

MODDIR=${0%/*}
/system/bin/sh "$MODDIR/stop.sh"
if grep -q '^ctb_name_patch ' /proc/modules; then
	rmmod ctb_name_patch 2>/dev/null || true
fi
