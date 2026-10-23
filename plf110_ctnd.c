/* PLF110 CTN daemon: follows game_opt/game_pid and configures critical names. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define GAME_PID "/proc/game_opt/game_pid"
#define RT_INFO "/proc/game_opt/rt_info"
#define NODE "/proc/game_opt/task_boost/critical_task_name"
#define ES4G_CRITICAL "/proc/game_opt/es4g/critical_task"
#define CT_ENABLE "/proc/game_opt/task_boost/ct_enable"
#define DB "/data/user/0/com.oplus.cosa/databases/db_game_database"
#define DB2 "/data/data/com.oplus.cosa/databases/db_game_database"
#define POLL_MS 500
#define GONE_POLLS 6
#define MAX_NAME 100
#define MAX_CFG 4096
#define MAX_TIDS 256

typedef struct sqlite3 sqlite3;
typedef struct sqlite3_stmt sqlite3_stmt;
static void *sql_lib;
static int (*sql_open)(const char *, sqlite3 **, int, const char *);
static int (*sql_prepare)(sqlite3 *, const char *, int, sqlite3_stmt **, const char **);
static int (*sql_bind)(sqlite3_stmt *, int, const char *, int, void (*)(void *));
static int (*sql_step)(sqlite3_stmt *);
static const unsigned char *(*sql_col)(sqlite3_stmt *, int);
static int (*sql_finalize)(sqlite3_stmt *);
static int (*sql_close)(sqlite3 *);
#define SQLITE_OK 0
#define SQLITE_ROW 100
#define SQLITE_READONLY 1

static volatile sig_atomic_t stop_flag;
static int lock_fd = -1;
static char original_names[2 * MAX_NAME];
static char original_enable[32];
static char original_rt_info[4096];
static bool original_enable_valid;
static bool original_rt_info_valid;
static bool changed;
static bool enable_changed;
static bool rt_info_changed;
static bool using_es4g;
static int es4g_prio = 1;
static int current_pid = -1;
static char current_pkg[256];
static char current_names[2 * MAX_NAME];
static int current_ctb = -2;
static time_t last_db_check;
static int interval_s = 15;
static int pid_override = -1;
static const char *db_override;
static bool manage_enable;

static void on_signal(int sig) { (void)sig; stop_flag = 1; }

static bool read_file(const char *path, char *out, size_t cap)
{

	int fd = open(path, O_RDONLY | O_CLOEXEC);
	ssize_t n;
	if (fd < 0) return false;
	n = read(fd, out, cap - 1);
	close(fd);
	if (n <= 0) return false;
	out[n] = 0;
	return true;
}

static bool write_file(const char *path, const char *value)
{
	int fd = open(path, O_WRONLY | O_CLOEXEC);
	ssize_t n;
	if (fd < 0) return false;
	n = write(fd, value, strlen(value));
	close(fd);
	return n == (ssize_t)strlen(value);
}

static void log_line(const char *fmt, ...)
{
	va_list ap;
	char stamp[32];
	time_t now = time(NULL);
	struct tm tm;
	localtime_r(&now, &tm);
	strftime(stamp, sizeof(stamp), "%F %T", &tm);
	fprintf(stderr, "[%s] ", stamp);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	fflush(stderr);
}

static bool load_sqlite(void)
{
	if (sql_lib) return true;
	sql_lib = dlopen("/system/lib64/libsqlite.so", RTLD_NOW | RTLD_LOCAL);
	if (!sql_lib) return false;
	#define BIND_VAR(var, symbol) do { *(void **)(&var) = dlsym(sql_lib, symbol); if (!var) return false; } while (0)
	BIND_VAR(sql_open, "sqlite3_open_v2");
	BIND_VAR(sql_prepare, "sqlite3_prepare_v2");
	BIND_VAR(sql_bind, "sqlite3_bind_text");
	BIND_VAR(sql_step, "sqlite3_step");
	BIND_VAR(sql_col, "sqlite3_column_text");
	BIND_VAR(sql_finalize, "sqlite3_finalize");
	BIND_VAR(sql_close, "sqlite3_close");
	#undef BIND_VAR
	return true;
}

static const char *find_db(void)
{
	if (db_override && access(db_override, R_OK) == 0) return db_override;
	if (access(DB, R_OK) == 0) return DB;
	if (access(DB2, R_OK) == 0) return DB2;
	return NULL;
}

static bool copy_database(const char *src, char *dst, size_t cap)
{
	char dir[] = "/data/local/tmp/plf110_ctnd_db_XXXXXX";
	char buf[65536], wal_src[512], wal_dst[512];
	int in, out;
	ssize_t got;
	if (!mkdtemp(dir)) return false;
	snprintf(dst, cap, "%s/db", dir);
	in = open(src, O_RDONLY | O_CLOEXEC);
	out = open(dst, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
	if (in < 0 || out < 0) { if (in >= 0) close(in); if (out >= 0) close(out); return false; }
	while ((got = read(in, buf, sizeof(buf))) > 0) {
		if (write(out, buf, got) != got) { close(in); close(out); return false; }
	}
	close(in); close(out);
	snprintf(wal_src, sizeof(wal_src), "%s-wal", src);
	snprintf(wal_dst, sizeof(wal_dst), "%s-wal", dst);
	in = open(wal_src, O_RDONLY | O_CLOEXEC);
	if (in >= 0) {
		out = open(wal_dst, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
		if (out >= 0) {
			while ((got = read(in, buf, sizeof(buf))) > 0) (void)write(out, buf, got);
			close(out);
		}
		close(in);
	}
	return true;
}

static void cleanup_database(const char *dbpath)
{
	char dir[512];
	char *slash;
	if (!dbpath) return;
	unlink(dbpath);
	{ char p[512]; snprintf(p, sizeof(p), "%s-wal", dbpath); unlink(p); }
	snprintf(dir, sizeof(dir), "%s", dbpath);
	slash = strrchr(dir, '/');
	if (slash) { *slash = 0; rmdir(dir); }
}

static bool json_value(const char *json, const char *key, char *out, size_t cap)
{
	char needle[80];
	const char *p, *q;
	snprintf(needle, sizeof(needle), "\"%s\"", key);
	p = json;
	while ((p = strstr(p, needle))) {
		q = p + strlen(needle);
		while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r') q++;
		if (*q++ != ':') { p += strlen(needle); continue; }
		while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r') q++;
		if (*q != '"') return false;
		q++;
		p = q;
		while (*q && *q != '"') { if (*q == '\\' && q[1]) q++; q++; }
		if (*q != '"') return false;
		if ((size_t)(q - p) >= cap) return false;
		memcpy(out, p, (size_t)(q - p)); out[q - p] = 0;
		return true;
	}
	return false;
}

static bool json_int(const char *json, const char *key, int *value)
{
	char needle[80]; const char *p; char *end;
	snprintf(needle, sizeof(needle), "\"%s\"", key);
	p = strstr(json, needle); if (!p) return false;
	p += strlen(needle); while (*p && *p != ':') p++; if (!*p) return false;
	*value = (int)strtol(p + 1, &end, 10); return end != p + 1;
}

static bool lookup_names(const char *pkg, char *names, size_t cap, int *ctb)
{
	const char *source = find_db();
	char dbpath[512], cfg[MAX_CFG];
	sqlite3 *db = NULL; sqlite3_stmt *st = NULL; const unsigned char *txt;
	int rc; bool ok = false;
	*ctb = -1; names[0] = 0;
	if (!source || !load_sqlite() || !copy_database(source, dbpath, sizeof(dbpath))) return false;
	rc = sql_open(dbpath, &db, SQLITE_READONLY, NULL);
	if (rc != SQLITE_OK) goto out;
	rc = sql_prepare(db, "SELECT game_config FROM PackageConfigBean WHERE package_name=?1", -1, &st, NULL);
	if (rc != SQLITE_OK) goto out;
	sql_bind(st, 1, pkg, -1, NULL);
	rc = sql_step(st);
	if (rc != SQLITE_ROW) goto out;
	txt = sql_col(st, 0);
	if (!txt || !*txt) { ok = true; goto out; }
	snprintf(cfg, sizeof(cfg), "%s", txt);
	if (json_value(cfg, "ctn", names, cap)) { (void)json_int(cfg, "ctb", ctb); ok = true; }
	else { ok = true; names[0] = 0; }
out:
	if (st) sql_finalize(st); if (db) sql_close(db); cleanup_database(dbpath); return ok;
}

static int parse_pid(const char *s)
{
	const char *p = strstr(s, "game_pid="); return p ? atoi(p + 9) : -1;
}

static bool process_package(int pid, char *pkg, size_t cap)
{
	char path[64], buf[512]; int fd; ssize_t n;
	snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
	fd = open(path, O_RDONLY | O_CLOEXEC); if (fd < 0) return false;
	n = read(fd, buf, sizeof(buf) - 1); close(fd); if (n <= 0) return false; buf[n] = 0;
	if (!memchr(buf, 0, (size_t)n)) buf[n] = 0;
	snprintf(pkg, cap, "%s", buf); return pkg[0] != 0;
}

static bool thread_name(pid_t tid, char *name, size_t cap)
{
	char path[80]; int fd; ssize_t n;
	snprintf(path, sizeof(path), "/proc/%d/comm", (int)tid);
	fd = open(path, O_RDONLY | O_CLOEXEC); if (fd < 0) return false;
	n = read(fd, name, cap - 1); close(fd); if (n <= 0) return false; name[n] = 0;
	name[strcspn(name, "\n")] = 0; return name[0] != 0;
}

static int collect_tids(int pid, const char *want1, const char *want2, pid_t *out, int max)
{
	char path[96], name[64]; DIR *dir; struct dirent *ent; int n = 0;
	snprintf(path, sizeof(path), "/proc/%d/task", pid); dir = opendir(path); if (!dir) return 0;
	while ((ent = readdir(dir)) && n < max) {
		pid_t tid = (pid_t)atoi(ent->d_name);
		if (tid <= 0 || !thread_name(tid, name, sizeof(name))) continue;
		if (!strncmp(name, want1, strlen(want1)) || !strncmp(name, want2, strlen(want2))) out[n++] = tid;
	}
	closedir(dir); return n;
}

static pid_t es4g_tids[MAX_TIDS];
static int es4g_tid_count;

static void clear_es4g_tids(void)
{
	char line[64];
	int i;
	for (i = 0; i < es4g_tid_count; i++) {
		snprintf(line, sizeof(line), "%d -1\n", (int)es4g_tids[i]);
		(void)write_file(ES4G_CRITICAL, line);
	}
	es4g_tid_count = 0;
	using_es4g = false;
}

static bool add_es4g_tids(const pid_t *tids, int count)
{
	char line[64];
	int i;
	clear_es4g_tids();
	for (i = 0; i < count; i++) {
		snprintf(line, sizeof(line), "%d %d\n", (int)tids[i], es4g_prio);
		if (!write_file(ES4G_CRITICAL, line)) {
			log_line("es4g critical_task: failed to add tid=%d prio=%d (errno=%d)",
				 (int)tids[i], es4g_prio, errno);
			clear_es4g_tids();
			return false;
		}
		es4g_tids[es4g_tid_count++] = tids[i];
	}
	using_es4g = true;
	return true;
}

static bool apply_names(int pid, const char *names, int ctb)
{
	char first[MAX_NAME], second[MAX_NAME], copy[2 * MAX_NAME];
	char *a, *b, *save; pid_t tids[MAX_TIDS]; int n, fd; bool rt_ok = true;
	snprintf(copy, sizeof(copy), "%s", names);
	a = strtok_r(copy, " \t\r\n", &save); b = strtok_r(NULL, " \t\r\n", &save);
	if (!a) return false; if (!b) b = a;
	snprintf(first, sizeof(first), "%s", a); snprintf(second, sizeof(second), "%s", b);
	n = collect_tids(pid, first, second, tids, MAX_TIDS);
	if (n > 0) {
		fd = open(RT_INFO, O_WRONLY | O_CLOEXEC);
		if (fd >= 0) {
			char line[4096]; size_t off = 0; int i;
			for (i = 0; i < n; i++) off += (size_t)snprintf(line + off, sizeof(line) - off, "%s%d", i ? " " : "", tids[i]);
			line[off++] = '\n';
			if (write(fd, line, off) != (ssize_t)off) rt_ok = false;
			close(fd);
			if (rt_ok) rt_info_changed = true;
		} else rt_ok = false;
	} else rt_ok = false;
	if (!rt_ok) log_line("rt_info: no matching thread or write failed for pid=%d names=%s", pid, names);
	if (!rt_ok) return false;
	if (access(NODE, F_OK) != 0) {
		if (access(ES4G_CRITICAL, F_OK) != 0) {
			log_line("no critical-task node: %s and %s are both missing", NODE, ES4G_CRITICAL);
			(void)write_file(RT_INFO, original_rt_info_valid ? original_rt_info : "\n");
			rt_info_changed = false;
			return false;
		}
		if (!add_es4g_tids(tids, n)) return false;
		log_line("using es4g critical_task fallback (%d tids, prio=%d)", n, es4g_prio);
		return true;
	}
	clear_es4g_tids();
	if (!write_file(NODE, names)) {
		log_line("critical_task_name: write failed for %s (errno=%d)", NODE, errno);
		(void)write_file(RT_INFO, original_rt_info_valid ? original_rt_info : "\n");
		rt_info_changed = false;
		return false;
	}
	if (manage_enable || ctb > 0) {
		if (write_file(CT_ENABLE, "1\n")) enable_changed = true;
	} else if (ctb == 0) {
		if (write_file(CT_ENABLE, "0\n")) enable_changed = true;
	}
	return rt_ok;
}

static void remember_original(void)
{
	char raw[2 * MAX_NAME + 32], *a, *b, *comma, *save;
	if (read_file(NODE, raw, sizeof(raw))) {
		comma = strchr(raw, ',');
		if (comma) {
			*comma = 0; a = raw; b = strchr(comma + 1, ':');
			if (b) *b = 0;
			b = strchr(a, ':');
			if (b) *b = 0;
			if (*a && *(comma + 1)) snprintf(original_names, sizeof(original_names), "%s %s", a, comma + 1);
		} else {
			a = strtok_r(raw, " \t\r\n", &save);
			b = strtok_r(NULL, " \t\r\n", &save);
			if (a && b) snprintf(original_names, sizeof(original_names), "%s %s", a, b);
		}
	}
	if (read_file(CT_ENABLE, original_enable, sizeof(original_enable))) original_enable_valid = true;
	if (read_file(RT_INFO, original_rt_info, sizeof(original_rt_info))) original_rt_info_valid = true;
}

static void restore(void)
{
	clear_es4g_tids();
	if (changed && original_names[0]) (void)write_file(NODE, original_names);
	if (rt_info_changed) (void)write_file(RT_INFO, original_rt_info_valid ? original_rt_info : "\n");
	if (enable_changed && original_enable_valid) (void)write_file(CT_ENABLE, original_enable);
	changed = false; enable_changed = false; rt_info_changed = false;
}

static int acquire_lock(const char *exe)
{
	char path[512]; const char *slash = strrchr(exe, '/');
	snprintf(path, sizeof(path), "%.*s.plf110_ctnd.lock", slash ? (int)(slash - exe + 1) : 2, slash ? exe : "./");
	lock_fd = open(path, O_CREAT | O_RDWR | O_CLOEXEC, 0600); if (lock_fd < 0) return 0;
	return flock(lock_fd, LOCK_EX | LOCK_NB) == 0;
}

int main(int argc, char **argv)
{
	char buf[128], pkg[256], names[2 * MAX_NAME]; int gone = 0, ctb = -1, i;
	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-d") && i + 1 < argc) db_override = argv[++i];
		else if (!strcmp(argv[i], "-r") && i + 1 < argc) interval_s = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-p") && i + 1 < argc) pid_override = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-e")) manage_enable = true;
		else if (!strcmp(argv[i], "-h")) { printf("plf110_ctnd [-d db] [-r seconds] [-p pid] [-e]\n"); return 0; }
	}
	if (interval_s < 5) interval_s = 5;
	signal(SIGTERM, on_signal); signal(SIGINT, on_signal); signal(SIGHUP, SIG_IGN);
	if (!acquire_lock(argv[0])) return 3;
	remember_original();
	log_line("PLF110 daemon started; node=%s fallback=%s db=%s", NODE, ES4G_CRITICAL,
		 find_db() ? find_db() : "missing");
	while (!stop_flag) {
		int pid = -1;
		if (pid_override > 0) pid = pid_override;
		else if (read_file(GAME_PID, buf, sizeof(buf))) pid = parse_pid(buf);
		if (pid > 0 && pid != current_pid) {
			if (current_pid > 0) restore();
			current_pid = pid; gone = 0; current_pkg[0] = 0; current_names[0] = 0; current_ctb = -2; last_db_check = 0;
			if (process_package(pid, pkg, sizeof(pkg))) { snprintf(current_pkg, sizeof(current_pkg), "%s", pkg); log_line("game pid=%d package=%s", pid, pkg); }
		}
		if (current_pid > 0 && !current_pkg[0]) {
			if (process_package(current_pid, pkg, sizeof(pkg))) { snprintf(current_pkg, sizeof(current_pkg), "%s", pkg); log_line("game pid=%d package=%s", current_pid, pkg); }
		}
		if (current_pid > 0 && pid == current_pid) {
			if (!current_names[0] || time(NULL) - last_db_check >= interval_s) {
				last_db_check = time(NULL);
				if (current_pkg[0] && lookup_names(current_pkg, names, sizeof(names), &ctb)) {
					bool node_ready = access(NODE, F_OK) == 0;
					if (names[0] && (strcmp(names, current_names) || ctb != current_ctb ||
					    (using_es4g && node_ready))) {
						if (apply_names(current_pid, names, ctb)) {
							snprintf(current_names, sizeof(current_names), "%s", names);
							current_ctb = ctb;
							changed = node_ready;
							log_line("applied %s -> %s (ctb=%d, %s)", current_pkg, names, ctb,
								 node_ready ? "name node" : "es4g fallback");
						}
					} else if (!names[0] && current_names[0]) {
						log_line("configuration removed for %s; restoring", current_pkg); restore(); current_names[0] = 0; current_ctb = -2;
					}
				}
			}
		}
		if (pid <= 0 && current_pid > 0) { if (++gone >= GONE_POLLS) { log_line("game exit; restoring"); restore(); current_pid = -1; current_pkg[0] = 0; current_names[0] = 0; current_ctb = -2; gone = 0; } }
		else if (pid > 0) gone = 0;
		usleep(POLL_MS * 1000);
	}
	restore(); if (lock_fd >= 0) close(lock_fd); log_line("PLF110 daemon stopped"); return 0;
}
