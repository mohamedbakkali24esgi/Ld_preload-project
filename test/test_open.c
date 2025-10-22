// test/test_open.c
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

int main() {
    printf("=== LD_PRELOAD Test ===\n");
    
    // Test normal files
    int fd1 = open("test.txt", O_CREAT | O_WRONLY, 0644);
    printf("open(test.txt, CREATE) = %d\n", fd1);
    close(fd1);
    
    // Test sensitive files (should be blocked)
    int fd2 = open("/var/log/syslog", O_RDONLY);
    printf("open(/var/log/syslog, READ) = %d (should be -1)\n", fd2);
    
    FILE *f = fopen("/etc/passwd", "r");
    printf("fopen(/etc/passwd) = %p (should be NULL)\n", f);
    if (f) fclose(f);
    
    // Test normal file again
    FILE *f2 = fopen("normal.txt", "w");
    printf("fopen(normal.txt) = %p\n", f2);
    if (f2) {
        fprintf(f2, "test\n");
        fclose(f2);
    }
    
    printf("Test complete.\n");
    return 0;
}
