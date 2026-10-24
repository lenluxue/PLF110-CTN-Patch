# Changelog

## v2.1.5 - 2026-10-24

- 修正 KernelSU 打包内容：安装包不再包含 `.c` 源码。
- 模块只保留运行所需二进制、KO、脚本和说明，源码仅发布在 Git 仓库中。

## v2.1.4 - 2026-10-24

- 修复 COSA `ctn` 只有一个名称时的兼容性：自动复制为两个槽位后再写节点。
- 修复单名称配置反复收到 `EINVAL` 且无法生效的问题。
- 已验证 `ctn:"sleep"` 会写入 `sleep sleep` 并登记匹配 TID。

## v2.1.3 - 2026-10-24

- KernelSU Action 改为只读生效诊断，不再自动重启 daemon。
- 显示当前游戏、`ctn`、匹配 TID、节点值、`rt_num`、`ct_enable` 和最终结论。
- 新增 `plf110_ctnd --check` 调试接口。

## v2.1.2 - 2026-10-24

- 修复 COSA 数据库临时目录泄漏：清理 SQLite 生成的 `-shm` 和 `-journal` 文件。
- 修复数据库复制失败时遗留临时目录的问题。
- 已清理设备上 14 个遗留目录。

## v2.1.1 - 2026-10-24

- 修复 KernelSU 安装后清掉 daemon 可执行位的问题；`start.sh` 会自动补 `0755`。
- 安装包只保留最新构建。
- 已在 PLF110 实机完成重启、KO 加载、节点创建、daemon 启动和游戏配置应用验证。

## v2.1.0 - 2026-10-24

- 删除 `set_memory_rw/ro` 和厂商 `.rodata` 指针改写。
- 改用 `get_critical_task_state()` kprobe 覆盖，避免 v2.0 的 EL1 data abort 与 mrdump 卡死。
- 保留兼容的 `critical_task_name` proc 写格式。

## v2.0.0 - 2026-10-24

- 初版 KernelSU 整合模块。
- 已验证存在内核 Oops，禁止使用。
