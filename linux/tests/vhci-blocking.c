/* LD_PRELOAD shim for BlueZ's btvirt on newer kernels: /dev/vhci answers the "create controller"
 * request a moment after the write, so btvirt's non-blocking read right after it fails. Opening
 * /dev/vhci blocking makes that first read wait for the answer. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <stdarg.h>
#include <string.h>
int open(const char *path, int flags, ...)
{
	static int (*real)(const char *, int, ...);
	if (!real) real = dlsym(RTLD_NEXT, "open");
	if (path && !strcmp(path, "/dev/vhci")) flags &= ~O_NONBLOCK;
	va_list ap; va_start(ap, flags); int mode = va_arg(ap, int); va_end(ap);
	return real(path, flags, mode);
}
int open64(const char *path, int flags, ...)
{
	va_list ap; va_start(ap, flags); int mode = va_arg(ap, int); va_end(ap);
	return open(path, flags, mode);
}
int __open_2(const char *path, int flags) { return open(path, flags, 0); }
int __open64_2(const char *path, int flags) { return open(path, flags, 0); }
