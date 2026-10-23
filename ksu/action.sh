#!/system/bin/sh

MODDIR=${0%/*}
BIN="$MODDIR/plf110_ctnd"
LOG="$MODDIR/plf110_ctnd.log"

if [ "${1:-check}" = "restart" ]; then
	/system/bin/sh "$MODDIR/stop.sh"
	/system/bin/sh "$MODDIR/start.sh"
fi

chmod 0755 "$BIN" 2>/dev/null || true

if grep -q '^ctb_name_patch ' /proc/modules 2>/dev/null; then
	ko_state=loaded
else
	ko_state=unloaded
fi

dpid=$(pidof plf110_ctnd 2>/dev/null | awk '{print $1}')
if [ -n "$dpid" ]; then
	daemon_state="running pid=$dpid"
else
	daemon_state=stopped
fi

check_output=$("$BIN" --check 2>&1)
result=$(printf '%s\n' "$check_output" | sed -n 's/^result=//p' | head -n 1)

case "$result" in
	EFFECTIVE)
		overall="已生效"
		;;
	READY_IDLE)
		overall="已就绪，等待游戏启动"
		;;
	NOT_CONFIGURED)
		overall="未生效：当前游戏没有 ctn 配置"
		;;
	WAITING_THREADS)
		overall="等待：游戏线程尚未创建"
		;;
	DB_ERROR)
		overall="异常：COSA 数据库不可读"
		;;
	*)
		overall="未生效"
		;;
esac

if [ "$ko_state" != "loaded" ] || [ -z "$dpid" ]; then
	overall="异常：KO 或 daemon 未运行"
fi

version=$(sed -n 's/^version=//p' "$MODDIR/module.prop" 2>/dev/null | head -n 1)
output=$(printf 'PLF110 CTN Action\nmodule=%s\nkernel=%s\nko=%s\ndaemon=%s\n%s\noverall=%s' \
	"$version" "$(uname -r)" "$ko_state" "$daemon_state" "$check_output" "$overall")

printf '%s\n' "$output"
printf '[%s] %s\n' "$(date '+%F %T')" "$output" >> "$LOG"
