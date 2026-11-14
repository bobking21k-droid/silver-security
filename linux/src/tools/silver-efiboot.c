/*
 * silver-efiboot -- manage UEFI boot entries, written from scratch for SilverOS.
 *
 * Talks to the firmware through efivarfs (/sys/firmware/efi/efivars) and builds
 * EFI_LOAD_OPTION structures by hand (UEFI spec 2.10, section 3.1.3).
 *
 *   silver-efiboot list
 *   silver-efiboot create <disk> <partnum> <loader-path> <label>
 *        e.g. create /dev/nvme0n1 1 '\EFI\SilverOS\grubx64.efi' SilverOS
 *        Creates (or replaces) the entry with that label and puts it first in BootOrder.
 *   silver-efiboot remove <label>
 *   silver-efiboot first <label>       move an existing entry to the front of BootOrder
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define EFIVARS "/sys/firmware/efi/efivars"
#define GLOBAL_GUID "8be4df61-93ca-11d2-aa0d-00e098032b8c"
#define ATTR_NV_BS_RT 0x7u
#define LOAD_OPTION_ACTIVE 0x1u

static void die(const char *fmt, const char *arg)
{
	fprintf(stderr, "silver-efiboot: ");
	fprintf(stderr, fmt, arg);
	fprintf(stderr, "\n");
	exit(1);
}

/* ---------- efivarfs access ---------- */

static void varpath(char *out, size_t n, const char *name)
{
	snprintf(out, n, EFIVARS "/%s-" GLOBAL_GUID, name);
}

/* Returns malloc'd payload (without the 4-byte attribute header), or NULL. */
static uint8_t *var_read(const char *name, size_t *len)
{
	char path[256];
	uint8_t buf[65536];
	varpath(path, sizeof path, name);
	int fd = open(path, O_RDONLY);
	if (fd < 0)
		return NULL;
	ssize_t n = read(fd, buf, sizeof buf);
	close(fd);
	if (n < 4)
		return NULL;
	uint8_t *out = malloc(n - 4 + 1);
	memcpy(out, buf + 4, n - 4);
	*len = n - 4;
	return out;
}

/* efivarfs marks variables immutable; lift that before rewriting or deleting. */
static void make_mutable(const char *path)
{
	int fd = open(path, O_RDONLY);
	if (fd < 0)
		return;
	int flags;
	if (ioctl(fd, FS_IOC_GETFLAGS, &flags) == 0 && (flags & FS_IMMUTABLE_FL)) {
		flags &= ~FS_IMMUTABLE_FL;
		ioctl(fd, FS_IOC_SETFLAGS, &flags);
	}
	close(fd);
}

static void var_write(const char *name, const uint8_t *data, size_t len)
{
	char path[256];
	varpath(path, sizeof path, name);
	make_mutable(path);
	uint8_t *buf = malloc(len + 4);
	uint32_t attr = ATTR_NV_BS_RT;
	memcpy(buf, &attr, 4);
	memcpy(buf + 4, data, len);
	int fd = open(path, O_WRONLY | O_CREAT, 0644);
	if (fd < 0)
		die("cannot open %s (is efivarfs mounted read-write?)", path);
	/* efivarfs requires the whole variable in a single write */
	if (write(fd, buf, len + 4) != (ssize_t)(len + 4))
		die("firmware rejected write to %s", name);
	close(fd);
	free(buf);
}

static void var_delete(const char *name)
{
	char path[256];
	varpath(path, sizeof path, name);
	make_mutable(path);
	if (unlink(path) < 0 && errno != ENOENT)
		die("cannot delete %s", name);
}

/* ---------- UTF-16 helpers ---------- */

static size_t utf16_put(uint8_t *out, const char *s)
{
	size_t n = 0;
	for (; *s; s++) {
		out[n++] = (uint8_t)*s;
		out[n++] = 0;
	}
	out[n++] = 0;
	out[n++] = 0;
	return n;
}

static void utf16_get(char *out, size_t max, const uint8_t *in, size_t inlen)
{
	size_t i = 0;
	for (size_t j = 0; j + 1 < inlen && i + 1 < max; j += 2) {
		if (!in[j] && !in[j + 1])
			break;
		out[i++] = in[j + 1] ? '?' : (char)in[j];
	}
	out[i] = 0;
}

/* Description of a Boot#### payload. */
static void option_label(const uint8_t *opt, size_t len, char *out, size_t max)
{
	if (len < 6) {
		*out = 0;
		return;
	}
	utf16_get(out, max, opt + 6, len - 6);
}

/* ---------- GPT: find the partition's start, size and unique GUID ---------- */

struct gpt_part {
	uint64_t first_lba, last_lba;
	uint8_t guid[16]; /* on-disk (mixed-endian) form == device path form */
};

static void gpt_lookup(const char *disk, unsigned partnum, struct gpt_part *p)
{
	int fd = open(disk, O_RDONLY);
	if (fd < 0)
		die("cannot open disk %s", disk);
	int ssz = 512;
	ioctl(fd, BLKSSZGET, &ssz);

	uint8_t hdr[512];
	if (pread(fd, hdr, sizeof hdr, ssz) != (ssize_t)sizeof hdr || memcmp(hdr, "EFI PART", 8))
		die("%s has no GPT partition table", disk);
	uint64_t entries_lba;
	uint32_t nentries, esize;
	memcpy(&entries_lba, hdr + 72, 8);
	memcpy(&nentries, hdr + 80, 4);
	memcpy(&esize, hdr + 84, 4);
	if (partnum < 1 || partnum > nentries || esize < 128)
		die("partition number out of range on %s", disk);

	uint8_t e[128];
	off_t off = (off_t)entries_lba * ssz + (off_t)(partnum - 1) * esize;
	if (pread(fd, e, sizeof e, off) != (ssize_t)sizeof e)
		die("cannot read partition entry on %s", disk);
	close(fd);
	static const uint8_t zero[16];
	if (!memcmp(e, zero, 16))
		die("partition slot is empty on %s", disk);
	memcpy(p->guid, e + 16, 16);
	memcpy(&p->first_lba, e + 32, 8);
	memcpy(&p->last_lba, e + 40, 8);
}

/* ---------- BootOrder ---------- */

static size_t bootorder_get(uint16_t *order, size_t max)
{
	size_t len = 0;
	uint8_t *d = var_read("BootOrder", &len);
	if (!d)
		return 0;
	size_t n = len / 2;
	if (n > max)
		n = max;
	memcpy(order, d, n * 2);
	free(d);
	return n;
}

static void bootorder_set(uint16_t first, int remove_only)
{
	uint16_t order[512], out[513];
	size_t n = bootorder_get(order, 512), m = 0;
	if (!remove_only)
		out[m++] = first;
	for (size_t i = 0; i < n; i++)
		if (order[i] != first)
			out[m++] = order[i];
	var_write("BootOrder", (uint8_t *)out, m * 2);
}

/* Find Boot#### whose description equals label; returns number or -1. */
static int find_label(const char *label)
{
	DIR *d = opendir(EFIVARS);
	struct dirent *de;
	int found = -1;
	if (!d)
		die("%s not available: boot SilverOS in UEFI mode", EFIVARS);
	while ((de = readdir(d))) {
		unsigned num;
		char tail[64];
		if (sscanf(de->d_name, "Boot%4x-%63s", &num, tail) != 2 || strlen(de->d_name) != 45
		    || strcmp(tail, GLOBAL_GUID))
			continue;
		char name[16], desc[128];
		size_t len;
		snprintf(name, sizeof name, "Boot%04X", num);
		uint8_t *opt = var_read(name, &len);
		if (!opt)
			continue;
		option_label(opt, len, desc, sizeof desc);
		free(opt);
		if (!strcmp(desc, label)) {
			found = (int)num;
			break;
		}
	}
	closedir(d);
	return found;
}

static int free_slot(void)
{
	char path[256], name[16];
	for (int i = 0; i < 0xffff; i++) {
		snprintf(name, sizeof name, "Boot%04X", i);
		varpath(path, sizeof path, name);
		if (access(path, F_OK) < 0)
			return i;
	}
	die("no free boot entry slots%s", "");
	return -1;
}

/* ---------- commands ---------- */

static void cmd_list(void)
{
	uint16_t order[512];
	size_t n = bootorder_get(order, 512);
	printf("BootOrder:");
	for (size_t i = 0; i < n; i++)
		printf(" %04X", order[i]);
	printf("\n");
	for (size_t i = 0; i < n; i++) {
		char name[16], desc[128];
		size_t len;
		snprintf(name, sizeof name, "Boot%04X", order[i]);
		uint8_t *opt = var_read(name, &len);
		if (!opt)
			continue;
		option_label(opt, len, desc, sizeof desc);
		uint32_t attr;
		memcpy(&attr, opt, 4);
		printf("%s%c %s\n", name, (attr & LOAD_OPTION_ACTIVE) ? '*' : ' ', desc);
		free(opt);
	}
}

static void cmd_create(const char *disk, unsigned partnum, const char *loader, const char *label)
{
	struct gpt_part p;
	gpt_lookup(disk, partnum, &p);

	uint8_t dp[1024], opt[2048];
	size_t n = 0;

	/* Media / Hard Drive device path node (type 4, subtype 1, 42 bytes) */
	uint64_t size = p.last_lba - p.first_lba + 1;
	dp[n++] = 4; dp[n++] = 1; dp[n++] = 42; dp[n++] = 0;
	memcpy(dp + n, &partnum, 4);      n += 4;
	memcpy(dp + n, &p.first_lba, 8);  n += 8;
	memcpy(dp + n, &size, 8);         n += 8;
	memcpy(dp + n, p.guid, 16);       n += 16;
	dp[n++] = 2;                      /* partition format: GPT */
	dp[n++] = 2;                      /* signature type: GUID */

	/* Media / File Path node (type 4, subtype 4) */
	uint8_t path16[512];
	size_t plen = utf16_put(path16, loader);
	uint16_t nodelen = (uint16_t)(4 + plen);
	dp[n++] = 4; dp[n++] = 4;
	memcpy(dp + n, &nodelen, 2);      n += 2;
	memcpy(dp + n, path16, plen);     n += plen;

	/* End of entire device path */
	dp[n++] = 0x7f; dp[n++] = 0xff; dp[n++] = 4; dp[n++] = 0;

	/* EFI_LOAD_OPTION */
	size_t o = 0;
	uint32_t attr = LOAD_OPTION_ACTIVE;
	uint16_t dplen = (uint16_t)n;
	memcpy(opt + o, &attr, 4);  o += 4;
	memcpy(opt + o, &dplen, 2); o += 2;
	o += utf16_put(opt + o, label);
	memcpy(opt + o, dp, n);     o += n;

	int num = find_label(label);
	if (num < 0)
		num = free_slot();
	char name[16];
	snprintf(name, sizeof name, "Boot%04X", num);
	var_write(name, opt, o);
	bootorder_set((uint16_t)num, 0);
	printf("%s \"%s\" -> %s partition %u %s (first in BootOrder)\n", name, label, disk, partnum, loader);
}

static void cmd_remove(const char *label)
{
	int num = find_label(label);
	if (num < 0)
		die("no boot entry labelled \"%s\"", label);
	char name[16];
	snprintf(name, sizeof name, "Boot%04X", num);
	bootorder_set((uint16_t)num, 1);
	var_delete(name);
	printf("removed %s \"%s\"\n", name, label);
}

static void cmd_first(const char *label)
{
	int num = find_label(label);
	if (num < 0)
		die("no boot entry labelled \"%s\"", label);
	bootorder_set((uint16_t)num, 0);
	printf("Boot%04X \"%s\" is now first\n", num, label);
}

int main(int argc, char **argv)
{
	if (argc >= 2 && !strcmp(argv[1], "list"))
		cmd_list();
	else if (argc == 6 && !strcmp(argv[1], "create"))
		cmd_create(argv[2], (unsigned)atoi(argv[3]), argv[4], argv[5]);
	else if (argc == 3 && !strcmp(argv[1], "remove"))
		cmd_remove(argv[2]);
	else if (argc == 3 && !strcmp(argv[1], "first"))
		cmd_first(argv[2]);
	else {
		fprintf(stderr, "usage: silver-efiboot list | create <disk> <partnum> <loader> <label>"
				" | remove <label> | first <label>\n");
		return 2;
	}
	return 0;
}
