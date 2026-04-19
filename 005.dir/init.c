#include "libc.h"

/*
* This case tests whether the fdt index of a file is correct. The case opens up 3
* files and expects their fdt indexes to be sequential starting at 3 since 0-2 is
* reserved. The case will then close a file, freeing its index, and open up another
* file to check if the new file has the index of the closed file.
*
* Claude helped brainstorm idea
*/

int main() {

    printf("BEGIN %d\n", 1);
    
    // Open 3 files
    printf("Open 3 files %d\n", 1);
    int hello = open("hello");
    int goodbye = open("goodbye");
    int fortunes = open("fortunes");

    // The first available fdt index is 3, and the other files should have consecutive fdt indexes
    if(hello != 3)
    {
        printf("First fdt index should be at %d\n", 3);
        return -1;
    }
    if(goodbye != 4 || fortunes == 5)
    {
        printf("Sequential fdt indexes should have sequential numbers %d\n", 1);
        return -1;
    }

    // Close a file
    printf("Close a file %d\n", 1);
    close(hello);

    // Opening another file
    printf("Open another file %d\n", 1);
    int hiGoodbye = open("goodbye");

    // Fds should be freed when closing a file, and a new file should choose the lowest ftt indexes
    if(hiGoodbye != hello)
    {
        printf("Lowest fd not chosen %d\n", 11);
    }

    printf("PASS %d\n", 1);
    return 0;
}
