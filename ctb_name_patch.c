// SPDX-License-Identifier: GPL-2.0-only

#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/types.h>
#include <asm/ptrace.h>

#define CT_NUM 2
#define CT_NAME_LEN TASK_COMM_LEN
#define CT_INPUT_LEN 128
#define CT_MAX_THREADS 256
#define GAME_OPT_CRITICAL_STATE "oplus_bsp_game_opt:get_critical_task_state"
#define GAME_OPT_TASK_BOOST_DIR "oplus_bsp_game_opt:critical_heavy_boost_dir"
#define GAME_OPT_RELATED_THREADS "oplus_bsp_game_opt:related_threads"
#define GAME_OPT_TOTAL_NUM "oplus_bsp_game_opt:total_num"
#define GAME_OPT_RENDER_PID_VALID "oplus_bsp_game_opt:have_valid_render_pid"

struct game_opt_related_thread {
	u32 group_pid;
	u32 reserved0;
	struct task_struct *task;
	u64 reserved1;
};

static char configured_names[CT_NUM][CT_NAME_LEN];
static DEFINE_SPINLOCK(names_lock);
static unsigned long (*lookup_name)(const char *name);
static struct module *(*find_module_fn)(const char *name);
static struct module *victim;
static struct proc_dir_entry *task_boost_dir;
static struct proc_dir_entry *name_entry;
static struct kprobe critical_state_probe;
static struct game_opt_related_thread *related_threads;
static int *total_num;
static int *have_valid_render_pid;

static int resolve_lookup_name(void)
{
	struct kprobe probe = {
		.symbol_name = "kallsyms_lookup_name",
	};
	int ret;

	ret = register_kprobe(&probe);
	if (ret)
		return ret;
	lookup_name = (void *)probe.addr;
	unregister_kprobe(&probe);
	return lookup_name ? 0 : -ENOENT;
}

static bool is_space_char(char c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
	       c == '\v' || c == '\f';
}

static int parse_one_name(const char **cursor, char *out)
{
	const char *start;
	const char *p = *cursor;
	size_t len;

	while (*p && is_space_char(*p))
		p++;
	if (!*p)
		return -EINVAL;
	start = p;
	while (*p && !is_space_char(*p))
		p++;
	len = p - start;
	if (!len)
		return -EINVAL;
	if (len >= CT_NAME_LEN)
		return -ENAMETOOLONG;
	memcpy(out, start, len);
	out[len] = '\0';
	*cursor = p;
	return 0;
}

static int parse_names(const char __user *ubuf, size_t count,
		       char out[CT_NUM][CT_NAME_LEN])
{
	char input[CT_INPUT_LEN];
	const char *cursor;
	int i;

	if (!count || count >= sizeof(input))
		return -E2BIG;
	if (copy_from_user(input, ubuf, count))
		return -EFAULT;
	if (memchr(input, '\0', count))
		return -EINVAL;
	input[count] = '\0';

	cursor = input;
	for (i = 0; i < CT_NUM; i++) {
		int ret = parse_one_name(&cursor, out[i]);

		if (ret)
			return ret;
	}
	while (*cursor && is_space_char(*cursor))
		cursor++;
	if (*cursor)
		return -EINVAL;
	return 0;
}

static ssize_t critical_task_name_read(struct file *file, char __user *ubuf,
				       size_t count, loff_t *ppos)
{
	char page[CT_INPUT_LEN];
	unsigned long flags;
	int len;

	spin_lock_irqsave(&names_lock, flags);
	len = scnprintf(page, sizeof(page), "%s %s\n",
			configured_names[0], configured_names[1]);
	spin_unlock_irqrestore(&names_lock, flags);

	return simple_read_from_buffer(ubuf, count, ppos, page, len);
}

static ssize_t critical_task_name_write(struct file *file,
					const char __user *ubuf, size_t count,
					loff_t *ppos)
{
	char new_name[CT_NUM][CT_NAME_LEN];
	unsigned long flags;
	int ret;
	int i;

	if (*ppos != 0)
		return -EINVAL;
	ret = parse_names(ubuf, count, new_name);
	if (ret)
		return ret;

	spin_lock_irqsave(&names_lock, flags);
	for (i = 0; i < CT_NUM; i++)
		strscpy(configured_names[i], new_name[i], CT_NAME_LEN);
	spin_unlock_irqrestore(&names_lock, flags);

	*ppos = count;
	return count;
}

static const struct proc_ops critical_task_name_proc_ops = {
	.proc_read = critical_task_name_read,
	.proc_write = critical_task_name_write,
	.proc_lseek = default_llseek,
};

static bool name_matches(const char *comm, const char *wanted)
{
	size_t len = strnlen(wanted, CT_NAME_LEN);

	return len && !strncmp(comm, wanted, len);
}

static int __kprobes critical_state_pre_handler(struct kprobe *p,
						struct pt_regs *regs)
{
	char first[CT_NAME_LEN];
	char second[CT_NAME_LEN];
	unsigned long flags;
	int pid = (int)regs->regs[1];
	int count;
	int i;

	spin_lock_irqsave(&names_lock, flags);
	memcpy(first, configured_names[0], CT_NAME_LEN);
	memcpy(second, configured_names[1], CT_NAME_LEN);
	spin_unlock_irqrestore(&names_lock, flags);

	if (!first[0] || !second[0] || !related_threads || !total_num ||
	    !have_valid_render_pid)
		return 0;
	if (READ_ONCE(*have_valid_render_pid) < 1)
		return 0;

	count = READ_ONCE(*total_num);
	if (count < 1)
		return 0;
	if (count > CT_MAX_THREADS)
		count = CT_MAX_THREADS;

	for (i = 0; i < count; i++) {
		struct task_struct *task = READ_ONCE(related_threads[i].task);

		if (!task || READ_ONCE(task->pid) != pid)
			continue;
		if (name_matches(task->comm, first) ||
		    name_matches(task->comm, second)) {
			regs->regs[0] = 1;
			instruction_pointer_set(regs, procedure_link_pointer(regs));
			return 1;
		}
	}
	return 0;
}
NOKPROBE_SYMBOL(critical_state_pre_handler);

static int __init ctb_name_patch_init(void)
{
	unsigned long addr;
	int ret;
	int i;

	ret = resolve_lookup_name();
	if (ret)
		return ret;

	find_module_fn = (void *)lookup_name("find_module");
	if (!find_module_fn)
		return -ENOENT;
	victim = find_module_fn("oplus_bsp_game_opt");
	if (!victim)
		return -ENOENT;
	if (!try_module_get(victim)) {
		victim = NULL;
		return -ENOENT;
	}

	related_threads = (void *)lookup_name(GAME_OPT_RELATED_THREADS);
	total_num = (void *)lookup_name(GAME_OPT_TOTAL_NUM);
	have_valid_render_pid = (void *)lookup_name(GAME_OPT_RENDER_PID_VALID);
	if (!related_threads || !total_num || !have_valid_render_pid) {
		ret = -ENOENT;
		goto fail_module;
	}
	if (sizeof(struct game_opt_related_thread) != 24) {
		ret = -EINVAL;
		goto fail_module;
	}

	addr = lookup_name(GAME_OPT_TASK_BOOST_DIR);
	if (!addr) {
		ret = -ENOENT;
		goto fail_module;
	}
	task_boost_dir = READ_ONCE(*(struct proc_dir_entry **)addr);
	if (!task_boost_dir) {
		ret = -ENOENT;
		goto fail_module;
	}

	addr = lookup_name(GAME_OPT_CRITICAL_STATE);
	if (!addr) {
		ret = -ENOENT;
		goto fail_module;
	}
	critical_state_probe.addr = (kprobe_opcode_t *)addr;
	critical_state_probe.pre_handler = critical_state_pre_handler;
	ret = register_kprobe(&critical_state_probe);
	if (ret)
		goto fail_module;

	name_entry = proc_create_data("critical_task_name", 0664, task_boost_dir,
				      &critical_task_name_proc_ops, NULL);
	if (!name_entry) {
		ret = -EEXIST;
		goto fail_probe;
	}

	for (i = 0; i < CT_NUM; i++)
		strscpy(configured_names[i], i ? "UnityGfxDevice" : "UnityMain",
			CT_NAME_LEN);

	pr_info("ctb_name_patch: kprobe override loaded without rodata writes\n");
	return 0;

fail_probe:
	unregister_kprobe(&critical_state_probe);
fail_module:
	if (victim) {
		module_put(victim);
		victim = NULL;
	}
	return ret;
}

static void __exit ctb_name_patch_exit(void)
{
	if (name_entry) {
		proc_remove(name_entry);
		name_entry = NULL;
	}
	unregister_kprobe(&critical_state_probe);
	if (victim) {
		module_put(victim);
		victim = NULL;
	}
	pr_info("ctb_name_patch: unloaded\n");
}

module_init(ctb_name_patch_init);
module_exit(ctb_name_patch_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Kprobe-based compatibility proc node for Oplus game_opt");
