#!/system/bin/sh

MODDIR=${0%/*}
KO="$MODDIR/ctb_name_patch.ko"
LOG="$MODDIR/plf110_ctnd.log"

log() {
	printf '[%s] %s\n' "$(date '+%F %T')" "$*" >> "$LOG"
}

log "KernelSU service start kernel=$(uname -r)"

i=0
while [ "$i" -lt 90 ]; do
	grep -q '^oplus_bsp_game_opt ' /proc/modules && [ -d /proc/game_opt/task_boost ] && break
	sleep 1
	i=$((i + 1))
done
if ! grep -q '^oplus_bsp_game_opt ' /proc/modules; then
	log "oplus_bsp_game_opt not loaded; abort"
	exit 0
fi
if [ ! -d /proc/game_opt/task_boost ]; then
	log "task_boost directory missing; abort"
	exit 0
fi

if grep -q '^ctb_name_patch ' /proc/modules; then
	log "ctb_name_patch already loaded"
else
	if insmod "$KO" >> "$LOG" 2>&1; then
		log "ctb_name_patch insmod ok"
	else
		rc=$?
		log "ctb_name_patch insmod failed rc=$rc"
		dmesg | tail -n 30 >> "$LOG" 2>/dev/null
		exit 0
	fi
fi

if [ -e /proc/game_opt/task_boost/critical_task_name ]; then
	log "critical_task_name ready: $(cat /proc/game_opt/task_boost/critical_task_name 2>/dev/null)"
	/system/bin/sh "$MODDIR/start.sh"
else
	log "critical_task_name missing after insmod; daemon will use es4g fallback"
	/system/bin/sh "$MODDIR/start.sh"
fi
