#!/system/bin/sh

MODDIR=${0%/*}
DAEMON="$MODDIR/plf110_ctnd"
PIDFILE="$MODDIR/plf110_ctnd.pid"
LOG="$MODDIR/plf110_ctnd.log"
DB=/data/user/0/com.oplus.cosa/databases/db_game_database
INTERVAL=${PLF110_CTND_INTERVAL:-15}

log() {
	printf '[%s] %s\n' "$(date '+%F %T')" "$*" >> "$LOG"
}

if [ "$(id -u)" != "0" ]; then
	log "not root; refusing to start"
	exit 1
fi
chmod 0755 "$DAEMON" 2>/dev/null || true
if [ ! -x "$DAEMON" ]; then
	log "missing executable: $DAEMON"
	exit 1
fi

if [ -s "$PIDFILE" ]; then
	oldpid=$(tr -dc '0-9' < "$PIDFILE" 2>/dev/null)
	if [ -n "$oldpid" ] && kill -0 "$oldpid" 2>/dev/null; then
		oldcmd=$(tr '\0' ' ' < "/proc/$oldpid/cmdline" 2>/dev/null)
		case "$oldcmd" in
			*plf110_ctnd*) log "daemon already running pid=$oldpid"; exit 0 ;;
		esac
	fi
	rm -f "$PIDFILE"
fi

enable_arg=
if [ "${PLF110_CTND_ENABLE:-0}" = "1" ]; then
	enable_arg=-e
fi

nohup "$DAEMON" -d "$DB" -r "$INTERVAL" $enable_arg >> "$LOG" 2>&1 < /dev/null &
pid=$!
echo "$pid" > "$PIDFILE"
sleep 1
if ! kill -0 "$pid" 2>/dev/null; then
	rm -f "$PIDFILE"
	log "daemon failed to start"
	exit 1
fi
log "daemon started pid=$pid db=$DB interval=$INTERVAL"
