/*
 * This file tests read and lseek, as well as keeping file descriptors separate.
 * This tests makes two entries for the same file and performs different operations on them.
 * This tests the ability to correctly read a file and maintain descriptor offset. 
 */

#include "libc.h"

int main() {
    int fd1 = open("Hello");
    char buf1[6];
    int numRead1 = read(fd1, buf1, 6);
    if (numRead1 == 6) {
        write(1, "*** PASS 1: read returned correct number of bytes\n", 50);
    } else if (numRead1 < 0) {
        write(1, "*** FAIL 1: read returned error\n", 32);
    } else {
        write(1, "*** FAIL 1: Didn't read right number of bytes\n", 46);
    }

    int fd2 = open("Hello");
    char buf2[2];
    
    // sets the offset to 16
    lseek(fd2, 16, 0); // SEEK_SET

    int numRead2 = read(fd2, buf2, 2);
    if (numRead2 == 2) {
        write(1, "*** PASS 2: read returned correct number of bytes\n", 50);
    } else if (numRead2 < 0) {
        write(1, "*** FAIL 2: read returned error\n", 32);
    } else {
        write(1, "*** FAIL 2: Didn't read right number of bytes\n", 46);
    }

    //sets the offset to 5 from the end of the file
    lseek(fd2, -5, 2); // SEEK_END

    char buf3[3];
    int numRead3 = read(fd2, buf3, 3);
    if (numRead3 == 3) {
        write(1, "*** PASS 3: read returned correct number of bytes\n", 50);
    } else if (numRead3 < 0) {
        write(1, "*** FAIL 3: read returned error\n", 32);
    } else {
        write(1, "*** FAIL 3: Didn't read right number of bytes\n", 46);
    }

    //sets the offset to its current offset + 15
    lseek(fd1, 15, 1); // SEEK_CUR

    char buf4[2];
    int numRead4 = read(fd1, buf4, 2);
    if (numRead4 == 2) {
        write(1, "*** PASS 4: read returned correct number of bytes\n", 50);
    } else if (numRead4 < 0) {
        write(1, "*** FAIL 4: read returned error\n", 32);
    } else {
        write(1, "*** FAIL 4: Didn't read right number of bytes\n", 46);
    }

    //This is needed to display output
    write(1, "*** ", 4);

    write(1, buf1, 6);
    write(1, buf2, 2);
    write(1, buf3, 3);
    write(1, buf4, 2);

}