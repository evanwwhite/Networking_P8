#include "libc.h"

#define CHECK(cond) check((cond), #cond, __FILE__, __LINE__)

int main() {
    //test 1: opening an existing file
    int fd = open("file1");
    //should return lowest possible fd for an existing file (3)
    /*
    Do not forget to account for fds 0 (stdin) 1 (stdout) and 2 (stderr) which should be 
    autopopulated for a process
    */
    if(fd == 3){
        printf("*** first fd returned correctly as 3\n");
    }else{
        printf("*** first fd returned incorrectly as %d\n",fd);
    }

    //test 2: opening a second file should now return the next lowest fd (4)
    int fd2 = open("file2");
    if(fd2 == 4){
        printf("*** second fd returned correctly as 4\n");
    }else{
        printf("*** second fd returned incorrectly as %d\n",fd2);
    }
    
    //test 3: opening a non-existent file should return -1
    int fd3 = open("dne.txt");
    if(fd3 == -1){
        printf("*** third fd returned correctly as -1\n");
    }else{
        printf("*** third fd returned incorrectly as %d\n",fd3);
    }

    //test 4: closing first file should return success! (0)
    int close_result = close(3);
    if(close_result == 0){
        printf("*** successfully closed first file\n");
    }else{
        printf("*** failed to close first file\n");
    }

    //test 5: should now return the newly available fd that was opened up by our close if we open a new file (3)
    int fd4 = open("file3");
    if(fd4 == 3){
        printf("*** successfully reused fd number 3\n");
    }else{
        printf("*** failed to reuse closed fd number 3 instead used fd #: %d\n",fd4);
    }

    //program should exit by itself
}
