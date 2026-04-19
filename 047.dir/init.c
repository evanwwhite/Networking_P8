#include "libc.h"
 
int main() {
    int fd = open("/hello", 0); // O_RDONLY = 0
    if (fd < 0) {
        printf("*** open failed\n");
        return 1;
    }
 
    printf("*** open succeeded\n");
 
    char buf[128];
    int n = read(fd, buf, sizeof(buf) - 1);
    if (n < 0) {
        printf("*** read failed\n");
        close(fd);
        return 1;
    }
 
    buf[n] = '\0';
    printf("*** read %d bytes: %s\n", n, buf);
 
    close(fd);
    return 0;
}
 