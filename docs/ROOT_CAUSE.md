# v2.0 卡死根因

## 现象

安装 v2.0 后设备在启动阶段卡屏，KernelSU 随后把模块标记为禁用。

## 持久化证据

设备 `/data/persist_log/pstore/pstore_last_kmsg` 记录了这次异常：

```text
Unable to handle kernel unknown 63 at virtual address fedcba9876543210
Internal error: Oops: 000000009600003f [#1] PREEMPT SMP
pc : set_task_pointer+0x98/0x10c [ctb_name_patch]
lr : set_task_pointer+0x80/0x10c [ctb_name_patch]

Call trace:
 set_task_pointer+0x98/0x10c [ctb_name_patch]
 init_module+0x294/0xf6c [ctb_name_patch]
 do_one_initcall
 do_init_module
 load_module
```

随后 MTK mrdump 接管异常，用户界面无法继续启动。

## 原因

v2.0 在初始化阶段调用 `set_memory_rw()`，试图把厂商模块 `.rodata` 中的 `critical_task` 指针数组改成可写，写入新指针后再调用 `set_memory_ro()`。异常发生在 `set_task_pointer()` 的写入路径，与正常业务无关，是内核层面的内存访问故障。

## 修复

v2.1 不再修改任何厂商内存：

1. 注册 `get_critical_task_state()` kprobe。
2. 在函数入口读取目标 PID。
3. 从厂商 `related_threads`/`total_num` 中查找该 PID。
4. 仅当线程 `comm` 与 daemon 写入的名字匹配时，直接返回“关键线程”并跳过原函数。

修复版完成以下受控测试：

- `insmod` 成功。
- 创建并读写 `critical_task_name`。
- 写入真实 `game_pid` 和 `rt_info` 后运行热路径。
- `rmmod` 成功，节点消失，`rt_info` 恢复为空。
- 没有新增 Oops、BUG 或 WARNING。

## 发布规则

- 任何内核更新都必须重新编译 KO。
- `vermagic` 不匹配时不得安装。
- v2.0 和 `set_memory_rw/ro` 版本不得重新发布。
