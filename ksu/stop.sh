#!/system/bin/sh

MODDIR=${0%/*}
PIDFILE="$MODDIR/plf110_ctnd.pid"
LOG="$MODDIR/plf110_ctnd.log"

log() {
	printf '[%s] %s\n' "$(date '+%F %T')" "$*" >> "$LOG"
}

[ -s "$PIDFILE" ] || exit 0
pid=$(tr -dc '0-9' < "$PIDFILE" 2>/dev/null)
cmd=$(tr '\0' ' ' < "/proc/$pid/cmdline" 2>/dev/null)
case "$cmd" in
	*plf110_ctnd*) ;;
	*) rm -f "$PIDFILE"; exit 0 ;;
esac

kill -TERM "$pid" 2>/dev/null || true
i=0
while [ "$i" -lt 10 ] && kill -0 "$pid" 2>/dev/null; do
	sleep 1
	i=$((i + 1))
done
if kill -0 "$pid" 2>/dev/null; then
	kill -KILL "$pid" 2>/dev/null || true
	log "daemon required SIGKILL pid=$pid"
else
	log "daemon stopped pid=$pid"
fi
rm -f "$PIDFILE"
