/*
 * silver-init -- PID 1 for SilverOS.
 *
 * Written from scratch. Responsibilities:
 *   1. mount the kernel pseudo filesystems
 *   2. apply hardening sysctls from /etc/sysctl.conf
 *   3. run /etc/rc.boot (networking, firewall, ssh, ...)
 *   4. supervise the services listed in /etc/inittab-silver
 *      ("respawn:<tty>:<command>") and restart them if they die
 *   5. reap orphaned zombies
 *   6. handle shutdown: SIGUSR1 = halt, SIGUSR2 = poweroff, SIGTERM = reboot
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MAX_SERVICES 16

struct service {
	char tty[400];
	char cmd[256];
	pid_t pid;
	time_t last_start;
};

static struct service services[MAX_SERVICES];
static int nservices;
static volatile sig_atomic_t shutdown_cmd;
static int quiet;   /* 'quiet' on the kernel command line: keep the screen clean */

static void say(const char *msg)
{
	if (quiet)
		return;
	int fd = open("/dev/console", O_WRONLY | O_NOCTTY);
	if (fd < 0)
		return;
	dprintf(fd, "\033[1;37m[silver-init]\033[0m %s\n", msg);
	close(fd);
}

static void mount_or_warn(const char *src, const char *dst, const char *type,
			  unsigned long flags, const char *opts)
{
	mkdir(dst, 0755);
	if (mount(src, dst, type, flags, opts) < 0 && errno != EBUSY) {
		char buf[160];
		snprintf(buf, sizeof buf, "mount %s failed: %s", dst, strerror(errno));
		say(buf);
	}
}

static void early_mounts(void)
{
	const unsigned long hard = MS_NOSUID | MS_NODEV | MS_NOEXEC;
	mount_or_warn("proc", "/proc", "proc", hard, "hidepid=2");
	mount_or_warn("sysfs", "/sys", "sysfs", hard, NULL);
	mount_or_warn("securityfs", "/sys/kernel/security", "securityfs", hard, NULL);
	mount_or_warn("devtmpfs", "/dev", "devtmpfs", MS_NOSUID | MS_NOEXEC, "mode=0755");
	mount_or_warn("devpts", "/dev/pts", "devpts", MS_NOSUID | MS_NOEXEC, "gid=5,mode=0620,ptmxmode=0666");
	mount_or_warn("tmpfs", "/dev/shm", "tmpfs", hard, "mode=1777");
	mount_or_warn("tmpfs", "/run", "tmpfs", MS_NOSUID | MS_NODEV, "mode=0755");
	mount_or_warn("tmpfs", "/tmp", "tmpfs", MS_NOSUID | MS_NODEV, "mode=1777");
}

/* Write "key = value" lines from /etc/sysctl.conf into /proc/sys. */
static void apply_sysctl(void)
{
	FILE *f = fopen("/etc/sysctl.conf", "r");
	char line[256];
	if (!f)
		return;
	while (fgets(line, sizeof line, f)) {
		char *key = line, *eq, *val, path[300], *p;
		while (*key == ' ' || *key == '\t')
			key++;
		if (*key == '#' || *key == '\n' || !(eq = strchr(key, '=')))
			continue;
		*eq = 0;
		val = eq + 1;
		for (p = eq - 1; p >= key && (*p == ' ' || *p == '\t'); p--)
			*p = 0;
		while (*val == ' ' || *val == '\t')
			val++;
		val[strcspn(val, "\n")] = 0;
		snprintf(path, sizeof path, "/proc/sys/%s", key);
		for (p = path + 10; *p; p++)
			if (*p == '.')
				*p = '/';
		int fd = open(path, O_WRONLY);
		if (fd >= 0) {
			if (write(fd, val, strlen(val)) < 0) { /* best effort */ }
			close(fd);
		}
	}
	fclose(f);
}

static void set_hostname(void)
{
	char name[64] = "silver";
	FILE *f = fopen("/etc/hostname", "r");
	if (f) {
		if (fgets(name, sizeof name, f))
			name[strcspn(name, "\n")] = 0;
		fclose(f);
	}
	if (sethostname(name, strlen(name)) < 0)
		say("sethostname failed");
}

static void load_services(void)
{
	FILE *f = fopen("/etc/inittab-silver", "r");
	char line[400];
	if (!f) {
		say("no /etc/inittab-silver, falling back to a console shell");
		strcpy(services[0].tty, "/dev/console");
		strcpy(services[0].cmd, "/bin/sh");
		nservices = 1;
		return;
	}
	while (fgets(line, sizeof line, f) && nservices < MAX_SERVICES) {
		char *tty, *cmd;
		line[strcspn(line, "\n")] = 0;
		if (strncmp(line, "respawn:", 8))
			continue;
		tty = line + 8;
		if (!(cmd = strchr(tty, ':')))
			continue;
		*cmd++ = 0;
		snprintf(services[nservices].tty, sizeof services[0].tty, "%s", tty);
		snprintf(services[nservices].cmd, sizeof services[0].cmd, "%s", cmd);
		nservices++;
	}
	fclose(f);
}

static pid_t spawn(const char *tty, const char *cmd)
{
	pid_t pid = fork();
	if (pid != 0)
		return pid;

	sigset_t all;
	sigemptyset(&all);
	sigprocmask(SIG_SETMASK, &all, NULL);
	setsid();
	int fd = open(tty, O_RDWR);
	if (fd >= 0) {
		dup2(fd, 0);
		dup2(fd, 1);
		dup2(fd, 2);
		if (fd > 2)
			close(fd);
	}
	char *const envp[] = { "PATH=/usr/sbin:/usr/bin:/sbin:/bin", "TERM=linux",
			       "HOME=/root", NULL };
	execle("/bin/sh", "sh", "-c", cmd, (char *)NULL, envp);
	_exit(127);
}

static void run_and_wait(const char *cmd)
{
	pid_t pid = spawn("/dev/console", cmd);
	int status;
	while (pid > 0 && waitpid(pid, &status, 0) < 0 && errno == EINTR)
		;
}

static void on_signal(int sig)
{
	shutdown_cmd = sig;
}

static void do_shutdown(int sig)
{
	int cmd = sig == SIGUSR1 ? RB_HALT_SYSTEM
		: sig == SIGUSR2 ? RB_POWER_OFF : RB_AUTOBOOT;
	say("shutting down");
	if (access("/etc/rc.shutdown", X_OK) == 0)
		run_and_wait("/etc/rc.shutdown");
	kill(-1, SIGTERM);
	sleep(2);
	kill(-1, SIGKILL);
	sync();
	/* installed systems: leave the root filesystem clean (no-op on the live USB) */
	mount(NULL, "/", NULL, MS_REMOUNT | MS_RDONLY, NULL);
	sync();
	reboot(cmd);
	for (;;)
		pause();
}

int main(void)
{
	if (getpid() != 1) {
		fprintf(stderr, "silver-init must run as PID 1\n");
		return 1;
	}
	umask(022);
	early_mounts();
	{
		char cmd[1024] = "";
		FILE *f = fopen("/proc/cmdline", "r");
		if (f) {
			if (fgets(cmd, sizeof cmd, f)) { /* read */ }
			fclose(f);
		}
		quiet = strstr(cmd, "quiet") != NULL;
	}
	say("SilverOS is waking up");
	apply_sysctl();
	set_hostname();
	if (access("/etc/rc.boot", X_OK) == 0)
		run_and_wait("/etc/rc.boot");

	struct sigaction sa = { .sa_handler = on_signal };
	sigemptyset(&sa.sa_mask);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGUSR1, &sa, NULL);
	sigaction(SIGUSR2, &sa, NULL);
	sigaction(SIGINT, &sa, NULL); /* ctrl-alt-del */
	reboot(RB_DISABLE_CAD);

	load_services();
	for (;;) {
		time_t now = time(NULL);
		for (int i = 0; i < nservices; i++) {
			/* throttle crash loops to one restart per 2 seconds */
			if (services[i].pid <= 0 && now - services[i].last_start >= 2) {
				services[i].pid = spawn(services[i].tty, services[i].cmd);
				services[i].last_start = now;
			}
		}
		if (shutdown_cmd)
			do_shutdown(shutdown_cmd == SIGINT ? SIGTERM : shutdown_cmd);

		int status;
		pid_t pid = waitpid(-1, &status, 0);
		if (pid < 0) {
			if (errno == ECHILD)
				sleep(1);
			continue;
		}
		for (int i = 0; i < nservices; i++)
			if (services[i].pid == pid)
				services[i].pid = 0;
	}
}
