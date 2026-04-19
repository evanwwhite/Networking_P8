
#include "libc.h"
// used claude with idea formulation and how to connect all the pieces together ( especially with getting it to run)
int main()
{
    printf("*** start%s\n", "");
    int data = open("/congrats.txt");
    char buf[20];
    long n = read(data, buf, 16); // read 16 and move offset
    printf("*** parent read1 n=%d first=%c\n", (int)n, buf[0]);

    int pid = fork();
    if (pid == 0)
    {
        char cbuf[20];
        long c1 = read(data, cbuf, 16);
        printf("*** child read1 n=%d first=%c\n", (int)c1, cbuf[0]); // Child reads 16 bytes and move offset
        long c2 = read(data, cbuf, 20);
        printf("*** child read2 n=%d first=%c\n", (int)c2, cbuf[0]); // Child asks for 20 bytes but 16 left
        long c3 = read(data, cbuf, 5);
        printf("*** child read3 n=%d\n", (int)c3); // ask for 5 shouge get 0
        exit(0);
    }
    waitpid(pid, 0, 0);

    long pn = read(data, buf, 5);
    printf("*** parent read2 n=%d\n", (int)pn);
    close(data);
    printf("*** done%s\n", "");
    return 0;
}
