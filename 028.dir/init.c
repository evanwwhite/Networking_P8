#include "libc.h"

#define ALPHABET_SIZE 26

// Function Forwarding
int get_uppercase_char(int char_fd, char *character, char c);
int get_lowercase_char(int char_fd, char *character, char c);
int get_number_char(int num_fd, char *number, char c);
int get_special_char(int spec_fd, char *special, char c);
int pprint(char *string); // must be null terminated

int is_uppercase(char c);
int is_lowercase(char c);
int is_digit(char c);
int is_whitespace(char c);
int is_newline(char c);
int is_special(char c);

static const char special_list[] = "!@#$%^&*()_+{}|:\"<>?~`[];,./";

// Defines for just parsing and printing characters
#define GET_UPPERCASE(c)                           \
    do {                                           \
        get_uppercase_char(char_fd, character, c); \
        putchar(character[0]);                     \
    } while (0)

#define GET_LOWERCASE(c)                           \
    do {                                           \
        get_lowercase_char(char_fd, character, c); \
        putchar(character[0]);                     \
    } while (0)

#define GET_NUMBER(c)                       \
    do {                                    \
        get_number_char(num_fd, number, c); \
        putchar(number[0]);                 \
    } while (0)

#define GET_SPECIAL(c)                         \
    do {                                       \
        get_special_char(spec_fd, special, c); \
        putchar(special[0]);                   \
    } while (0)

#define GET_NEWLINE()                   \
    do {                                \
        lseek(newline_fd, 0, SEEK_SET); \
        read(newline_fd, newline, 1);   \
        putchar(newline[0]);            \
    } while (0)

#define GET_SPACE()                   \
    do {                              \
        lseek(space_fd, 0, SEEK_SET); \
        read(space_fd, space, 1);     \
        putchar(space[0]);            \
    } while (0)

// These were given to from the man page of lseek
enum seek_type {
    SEEK_SET, // 0
    SEEK_CUR, // 1
    SEEK_END  // 2
};

// One char for each type
static char character[1];
static char number[1];
static char special[1];
static char newline[1];
static char space[1];

// File descriptors for each type
static int char_fd;
static int num_fd;
static int spec_fd;
static int newline_fd;
static int space_fd;

int main() {
    // Opening a file for each type of character
    char_fd = open("alphabet.txt", 0, 0);
    num_fd = open("numbers.txt", 0, 0);
    spec_fd = open("special.txt", 0, 0);
    newline_fd = open("newline.txt", 0, 0);
    space_fd = open("space.txt", 0, 0);

    // Double checking that none of the opened files are not in a fd
    if (char_fd == -1) {
        printf("*** char_fd is not a valid file descriptor (%d)\n", char_fd);
        return -1;
    }
    if (num_fd == -1) {
        printf("*** num_fd is not a valid file descriptor (%d)\n", num_fd);
        return -1;
    }
    if (spec_fd == -1) {
        printf("*** spec_fd is not a valid file descriptor (%d)\n", spec_fd);
        return -1;
    }
    if (newline_fd == -1) {
        printf("*** newline_fd is not a valid file descriptor (%d)\n", newline_fd);
        return -1;
    }
    if (space_fd == -1) {
        printf("*** space_fd is not a valid file descriptor (%d)\n", space_fd);
        return -1;
    }

    // Testing the syscalls
    pprint((char *)"*** ::::::::::: INTRODUCTION :::::::::::\n");
    pprint((char *)"*** This is some code that I made that tests the following:\n");
    pprint((char *)"*** 1. read\n");
    pprint((char *)"*** 2. write\n");
    pprint((char *)"*** 3. open\n");
    pprint((char *)"*** 4. close\n");
    pprint((char *)"*** 5. lseek\n");
    pprint((char *)"*** I implemented a version of print that reads through different\n");
    pprint((char *)"*** files at different seek numbers. Code that works should be\n");
    pprint((char *)"*** to print out this documentation without any issues. If for some\n");
    pprint((char *)"*** you are not able to see this documentation, your code does not work\n");
    pprint((char *)"*** as intended.\n");
    pprint((char *)"*** ::::::::::: WHAT IT DOES :::::::::::\n");
    pprint((char *)"*** What this code does is first it sets up global varialbes above the\n");
    pprint((char *)"*** main function. These are going to just help with getting references to\n");
    pprint((char *)"*** the file descriptor numbers for each file.\n");
    pprint((char *)"*** At the beginning of main, there are lots of open files and as you may\n");
    pprint((char *)"*** see, there are lots of open files. These files all hold a map of the\n");
    pprint((char *)"*** character map that this code mainly runs on to print.\n");
    pprint((char *)"*** \"pprint\"(char *string) is a custom function that is custom made to use\n");
    pprint((char *)"*** these file maps. Now, the reason this is a good test for lseek is because\n");
    pprint((char *)"*** while you are printing this documentation out, you are constantly using\n");
    pprint((char *)"*** lseek to jump around the open file descriptions.\n");
    pprint((char *)"*** Lets have some fun with this new pprint to test your edge cases shall we?\n");
    pprint((char *)"*** ::::::::::: TESTING YOUR LSEEK IMPLEMENTATION :::::::::::\n");
    pprint((char *)"*** Capital Letters: ABCDEFGHIJKLMNOPQRSTUVWXYZ\n");
    pprint((char *)"*** Passed Capital Letters\n");
    pprint((char *)"*** Lowercase Letters: abcdefghijklmnopqrstuvwxyz\n");
    pprint((char *)"*** Passed Lowercase Letters\n");
    pprint((char *)"*** Digits: 0123456789\n");
    pprint((char *)"*** Passed Digits\n");
    pprint((char *)"*** Specials: !@#$%^&*()_+{}|:\"<>?~`[];,./\n");
    pprint((char *)"*** Passed Specials\n");

    // Close all open fd's
    int char_close = close(char_fd);
    int num_close = close(num_fd);
    int spec_close = close(spec_fd);
    int newline_close = close(newline_fd);
    int space_close = close(space_fd);

    // Make sure that all closes return
    if (char_close != 0) {
        printf("*** char_close did not close properly (%d)\n", char_close);
        return -1;
    }
    if (num_close != 0) {
        printf("*** num_close did not close properly (%d)\n", num_close);
        return -1;
    }
    if (spec_close != 0) {
        printf("*** spec_close did not close properly (%d)\n", spec_close);
        return -1;
    }
    if (newline_close != 0) {
        printf("*** newline_close did not close properly (%d)\n", newline_close);
        return -1;
    }
    if (space_close != 0) {
        printf("*** space_close did not close properly (%d)\n", space_close);
        return -1;
    }

    return 0;
}

int pprint(char *buffer) {
    while (*buffer != '\0') {
        if (is_uppercase(*buffer)) {
            GET_UPPERCASE(*buffer);
        } else if (is_lowercase(*buffer)) {
            GET_LOWERCASE(*buffer);
        } else if (is_digit(*buffer)) {
            GET_NUMBER(*buffer);
        } else if (is_special(*buffer)) {
            GET_SPECIAL(*buffer);
        } else if (is_whitespace(*buffer)) {
            GET_SPACE();
        } else if (is_newline(*buffer)) {
            GET_NEWLINE();
        } else {
            return -1;
        }
        buffer++;
    }
    return 0;
}

int is_uppercase(char c) {
    return (c >= 'A') && (c <= 'Z');
}
int is_lowercase(char c) {
    return (c >= 'a') && (c <= 'z');
}
int is_digit(char c) {
    return (c >= '0') && (c <= '9');
}
int is_whitespace(char c) {
    return c == ' ';
}
int is_newline(char c) {
    return c == '\n';
}
int is_special(char c) {
    int i = 0;
    while (special_list[i] != '\0') {
        if (special_list[i] == c) {
            return 1;
        }
        i++;
    }
    return 0;
}

int get_uppercase_char(int char_fd, char *character, char c) {
    if (!is_uppercase(c)) {
        printf("*** Could not find uppercase for this char (%c)\n", c);
        return -1;
    }
    int new_offset = c - 'A';

    lseek(char_fd, new_offset, SEEK_SET);
    read(char_fd, character, 1);

    return 0;
}
int get_lowercase_char(int char_fd, char *character, char c) {
    if (!is_lowercase(c)) {
        printf("*** Could not find lowercase for this char (%c)\n", c);
        return -1;
    }
    int new_offset = c - 'a' + ALPHABET_SIZE;

    lseek(char_fd, new_offset, SEEK_SET);
    read(char_fd, character, 1);

    return 0;
}

int get_number_char(int num_fd, char *number, char c) {
    if (!is_digit(c)) {
        printf("*** Could not find number for this char (%c)\n", c);
        return -1;
    }
    int new_offset = c - '0';

    lseek(num_fd, new_offset, SEEK_SET);
    read(num_fd, number, 1);

    return 0;
}
int get_special_char(int spec_fd, char *special, char c) {
    int i = 0;
    while (special_list[i] != '\0') {
        if (special_list[i] == c) {
            break;
        }
        i++;
    }
    if (special_list[i] == '\0') {
        printf("*** Could not find special for this char (%c)\n", c);
        return -1;
    }

    lseek(spec_fd, i, SEEK_SET);
    read(spec_fd, special, 1);

    return 0;
}
