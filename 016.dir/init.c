#include "libc.h"

// I wrote this with the help of ChatGPT, specifically to generate the idea for this test case
// It will test the bundle of syscalls such as open, read, write, close, and exit

// This exist because libc standard is not available, I cannot make the init file and Chat GPT help me with this part
int puts(const char *s) {
    long n = 0;
    while (s[n] != 0) {
        n++;
    }
    write(1, s, n);
    write(1, "\n", 1);
    return 0;
}

// helper method to read one line from a file descriptor
static int read_line(int fd, char *buf, int max_len) {
    int i = 0; // current index in buffer
    while (i < max_len - 1) { // max_len - 1 so we can have space for null terminator
        char ch;
        long n = read(fd, &ch, 1); // read one byte which can test repeat syscall and offset correctness
        if (n <= 0) { 
            break; // stop if EOF or error
        }
        buf[i++] = ch; // store character in buffer
        if (ch == '\n') { 
            break; // stop at new line, which reading 1 line is complete
        }
    }
    buf[i] = 0; // Null terminate string
    return i; // return the number of bytes read
}

int main() {
    char line[256]; // buffer to store each line read from file

    // try to open the massage file
    int fd = open("message", 0);
    if (fd < 0) {
        printf("*** open file failed\n");
        return 1;
    }

    printf("*** open file success\n");

    // read and print the first line
    int n = read_line(fd, line, sizeof(line));
    if (n > 0) { // there is something that was read
        if (line[n - 1] == '\n') {
            printf("*** %s", line); 
        } else {
            printf("*** %s\n", line); // this is just to add new line manually
        }
    } else {
        printf("*** failed reading first line\n");
        close(fd);
        return 1;
    }

    // skip the second line
    if (read_line(fd, line, sizeof(line)) <= 0) {
        printf("*** failed while skipping intro line\n");
        close(fd);
        return 1;
    }

    // read the poem line by line and print each line with 
    while (1) {
        n = read_line(fd, line, sizeof(line));
        if (n <= 0) { // stop on EOF or error
            break;
        }
        // file can be read so print it, either add new line manually or no need too
        if (line[n - 1] == '\n') {
            printf("*** %s", line);
        } else {
            printf("*** %s\n", line);
        }
    }

    // try to close the file descriptor
    if (close(fd) == 0) {
        printf("*** close ok\n");
    } else {
        printf("*** close failed\n");
        return 1;
    }

    return 0; // exit 
}