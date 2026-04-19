#include "libc.h"

//Used ChatGPT to help me break down the conversion for the actual file descriptior tables and provide a step by step breakdown of it. Also helped me understand critical issues in my syscall as I was getting confused with the registers.

//This testcase tests the basic functionality of you having open, read, write, and close working.


int main() {
    int fd = open("/sixseven",0); //check if open works
    if(fd < 0){
        printf("*** failed to open 67 file\n");
        return 1;
    }

    char buf[128];
    size_t sz = read(fd, buf, sizeof(buf) -1); //check if read works
    buf[sz] = '\0';
    close(fd);//close the file here
    printf("*** read from 67 file!:\n");
    printf("%s\n", buf);

    printf("***attack write from kernel pointer\n");
    char *kernel = (char *)0xffff800000000000ULL;//now lets try to write to the kernel ptr and see if the syscall setup blocking this.
    write(1, kernel, 1);//writes to the ptr now.
    printf("*** still alive!!!\n");

    return 0;
}
