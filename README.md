# ctn_patch for OnePlus PLF110

为 OnePlus PLF110 / Ace 5 Race 补回可配置的 `/proc/game_opt/task_boost/critical_task_name`，并由 daemon 根据 COSA 云控配置自动选择游戏关键线程。

本项目是面向 **PLF110 / OP60F9L1 / Android 16 / MT6989** 的独立实现。目标与 [`Turbowfz/ctn_patch`](https://github.com/Turbowfz/ctn_patch) 相同，但内核适配和 v2.1.3 的 kprobe 实现是独立完成的，不依赖旧设备二进制。
在此鸣谢toolfor以及turbowfz的思路

> 当前验证环境：`6.1.157-android14-11-o-gc2dad16af736`，KernelSU，arm64-v8a。
> 内核模块与机型、内核版本强绑定，不能跨内核直接复用。

## 功能

- 创建 `/proc/game_opt/task_boost/critical_task_name`。
- 自动读取 `/data/user/0/com.oplus.cosa/databases/db_game_database` 中 `PackageConfigBean.game_config` 的 `ctn` 配置。
- 找到游戏进程里匹配名称的线程，并写入 `/proc/game_opt/rt_info` 与 `critical_task_name`。
- 根据 `ctb` 控制 `ct_enable`。
- 游戏退出、PID 切换或配置删除时恢复原值。
- 名称节点不存在时，用户态 daemon 可回退到现有 `es4g/critical_task` 节点。

## 安装

1. 在 ROOT 管理器中安装
2. 重启设备。
3. 可选：在 ROOT 模块页点击 Action 重启 daemon。

安装后检查：

```sh
cat /proc/modules | grep ctb_name_patch
cat /proc/game_opt/task_boost/critical_task_name
ps -A -o pid,args | grep plf110_ctnd
cat /data/adb/modules/plf110_ctnd/plf110_ctnd.log
```

正常空载示例：

```text
ctb_name_patch 24576 0 - Live ... (OE)
UnityMain UnityGfxDevice
... /data/adb/modules/plf110_ctnd/plf110_ctnd -d ... -r 15
```

模块页的 **Action** 会执行只读诊断，不会重启服务。输出包含当前游戏、目标 `ctn`、匹配 TID、节点值、`ct_enable` 和最终结论：

```text
ko=loaded
daemon=running pid=<pid>
result=EFFECTIVE
message=关键线程配置已生效
overall=已生效
```

启动带 `ctn` 配置的游戏后，日志应出现类似：

```text
applied com.tencent.mf.uam -> GameThread RHIThread (ctb=1, name node)
```

并可在终端看到：

```text
game_pid=<pid> child_num=<n>
rt_num=1 total_num=<n>
tgid:<pid> pid:<tid> comm:RHIThread
```

## 配置来源

daemon 查询的表和字段：

```sql
SELECT game_config
FROM PackageConfigBean
WHERE package_name = ?;
```

示例配置：

```json
{"ctb":1,"htb":1,"ctep":90,"ctn":"Thread- RenderThread"}
```

数据库使用临时副本读取，避免直接打开正在写入的 COSA WAL 数据库。读取完成后会清理 `-wal`、`-shm`、`-journal` 和临时目录。

## 构建

### 用户态 daemon

Termux 交叉编译：

```sh
./scripts/build-daemon.sh
```

或者直接：

```sh
aarch64-linux-android-clang -O2 -Os \
  -ffunction-sections -fdata-sections -fno-strict-aliasing \
  -Wl,--gc-sections -Wl,--build-id=none \
  -o prebuilt/plf110_ctnd plf110_ctnd.c -ldl
aarch64-linux-android-strip prebuilt/plf110_ctnd
```

### 内核模块

需要与目标内核完全匹配的已准备构建树：

```sh
KDIR=/path/to/out ./scripts/build-module.sh
```

或：

```sh
make KDIR=/path/to/prepared-kernel-tree
```

当前设备构建参数：

```text
Kernel: 6.1.157-android14-11-o-gc2dad16af736
Compiler: Ubuntu clang 18.1.3
ARCH: arm64
LLVM: 1
```

仓库附带 `Module.symvers.device` 和 `device_kernel.config`，用于核对构建环境。

### 重新打包 KernelSU 模块

```sh
./scripts/package-ksu.sh
```

## 校验

```sh
./scripts/verify-release.sh
```

预编译产物哈希见 [`SHA256SUMS`](SHA256SUMS)。

## 安全说明

- kprobe 接口依赖 `oplus_bsp_game_opt` 的符号布局，只适用于已验证内核。
- 更新内核后必须重新编译，不得直接安装旧 `.ko`。
- 模块会 `try_module_get()` 固定厂商模块，卸载时先移除节点和 kprobe，再释放引用。
- 如果内核符号、kprobe 注册或节点创建失败，模块初始化会直接返回错误，不会尝试危险的内存写入。
- 不要与其他同时改写 `critical_task` / `critical_task_name` 的模块并用。

## 文件结构

```text
ctb_name_patch.c           内核模块源码
plf110_ctnd.c              用户态 daemon 源码
Kbuild / Makefile          外置内核模块构建
ksu/                       KernelSU 模块文件
prebuilt/                  已验证的 arm64 产物
dist/                      KernelSU 安装包
docs/ROOT_CAUSE.md          v2.0 卡死事故分析
docs/TESTING.md            测试记录
scripts/                   构建、打包、校验脚本
```

## 许可证

GPL-2.0-only。详见 [LICENSE](LICENSE)。
