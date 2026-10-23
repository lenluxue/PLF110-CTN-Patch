# Changelog

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
