#!/system/bin/sh

MODDIR=${0%/*}
case "${1:-restart}" in
	stop)
		/system/bin/sh "$MODDIR/stop.sh"
		;;
	start)
		/system/bin/sh "$MODDIR/start.sh"
		;;
	*)
		/system/bin/sh "$MODDIR/stop.sh"
		sleep 1
		/system/bin/sh "$MODDIR/start.sh"
		;;
esac
