
/*

This test case specifically tests the ELF loader.

It tests if it correctly loads all of the necessary
segments of the program into memory and allows the program to access it.

*/

#include "libc.h"

// Here is some R/W data.

// You can read AND write to this data.

int x = 42;

int array[4] = {1, 2, 3, 4};

// Here is some R-only data.

// For example, read-only data includes string literals. 

const char *empty_string = "";
const char *small_string = "small sized string";
const char *medium_string = "this is a medium sized string";
const char *large_string = "this is a slightly larger sized string";
const char *odd_string = "+h1s 1s @n 0dd $+r!n9";

// Read-only data also includes constants. 

const int const_x = 4;
const int const_y = 42;
const int const_z = 420;

const int const_array_small[] = {1};
const int const_array_medium[] = {1, 2, 3, 4, 5};
const int const_array_large[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

// This data is left uninitialized (should be 0-initialized). 

// Should be 0-initialized.
int uninitialized_x;
int uninitialized_y;
int uninitialized_z;

// Should be ALL 0-initialized. 
int uninitialized_array_small[4];
int uninitialized_array_medium[42];
int uninitialized_array_large[420];


int main() {

    printf("*** test start%s\n", "");

    printf("*** beware of a MISMATCH%s\n", "");

    // NOTE: I focused on checking phase 3 here. You can add more checks
    // for the other phases as needed.

    //------------------------------------------------

    // Phase 1


    
    //------------------------------------------------

    // Phase 2



    //------------------------------------------------

    // Phase 3

    if (uninitialized_x != 0){
        printf("*** MISMATCH: uninitialized_x should be 0 but it's %d\n", uninitialized_x);
    }
    if (uninitialized_y != 0){
        printf("*** MISMATCH: uninitialized_y should be 0 but it's %d\n", uninitialized_y);
    }
    if (uninitialized_z != 0){
        printf("*** MISMATCH: uninitialized_z should be 0 but it's %d\n", uninitialized_z);
    }


    for (int i = 0; i < 4; i++){
        if (uninitialized_array_small[i] != 0){
            printf("*** MISMATCH: uninitialized_array_small should be all 0's but found a %d\n", uninitialized_array_small[i]);
        }
    }
    for (int i = 0; i < 42; i++){
        if (uninitialized_array_medium[i] != 0){
            printf("*** MISMATCH: uninitialized_array_medium should be all 0's but found a %d\n", uninitialized_array_medium[i]);
        }
    }
    for (int i = 0; i < 420; i++){
        if (uninitialized_array_large[i] != 0){
            printf("*** MISMATCH: uninitialized_array_large should be all 0's but found a %d\n", uninitialized_array_large[i]);
        }
    }


    //------------------------------------------------

    printf("*** test finish%s\n", "");



    return 0;
}
