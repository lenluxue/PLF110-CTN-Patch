# Testing

## 设备

- OnePlus PLF110 / Ace 5 Race
- Device codename: OP60F9L1
- Android 16
- Kernel: `6.1.157-android14-11-o-gc2dad16af736`
- ABI: arm64-v8a

## v2.1.2 checks

- KO vermagic matches the running kernel.
- `ctb_name_patch` loads and creates `critical_task_name`.
- Node accepts two whitespace-separated names, each shorter than `TASK_COMM_LEN`.
- A controlled PID registered through `rt_info` exercises the kprobe hot path.
- KO unload removes the proc node and releases the vendor module reference.
- KernelSU boot started the daemon after reapplying execute permission.
- Game test: `com.tencent.mf.uam`, config `GameThread RHIThread`, `ctb=1`.
- Observed result: node switched to `GameThread RHIThread`, `ct_enable=1`, `RHIThread` registered in `rt_info`.
- Database copy cleanup test leaves zero `plf110_ctnd_db_*` directories after a successful or unmatched lookup.
