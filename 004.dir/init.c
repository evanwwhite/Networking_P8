#include "libc.h"

/*
    This entire test was inspired by Dr. Gheith's test and Claude also contributed a huge majority
    of this test case throughout the entire folder. 
*/

// some compile time constants 
static constexpr long HEAP_BYTES = 4096L;
static constexpr int CHILD_ONE_EXIT = 7;
static constexpr int CHILD_TWO_EXIT = 13;

/*
    This namespace implements a simple bump allocator using brk which controls the process
    heap boundary aka the program break and increasing it extends the heap region given
    by the OS. The allocator grabs a fixed heap block once and then uses it manually 
    using a linear bump pointer without freeing.
*/
namespace impl {
    // some macros related to the heap region like start, end and more
    static char *start_heap = nullptr;
    static char *end_heap = nullptr;
    static char *pointer = nullptr; // pointer to current alloc

    /*
        Initializes a fixed size heap region utilizing brk. Returns true if heap extension is
        successful, false otherwise. 

        @return boolean true/false based on the success of the heap extension
    */
    static bool init() {
        start_heap = static_cast<char *>(brk(nullptr)); // get current program break
        char *requested_size = start_heap + HEAP_BYTES; // amount of bytes needed for heap from OS
        if (static_cast<char *>(brk(requested_size)) < requested_size) {
            return false; // false if size requirements cannot be met
        }
        // reset end boundary and pointer to its proper place
        end_heap = requested_size;
        pointer  = start_heap;
        return true; // successful!
    }

    /*
        Allocates a memory block with 8-byte alignment with size n. 

        @param n the size of the requested block to be allocated
    */
    static void *alloc(long size) {
        long byte_aligned = (size + 7L) & ~7L; // round size up to proper aligned boundary
        // check if enough space remains, if not, return nullptr
        if (pointer + byte_aligned > end_heap) {
            return nullptr;
        }
        void *start_block = pointer; // save it before
        pointer += byte_aligned;
        return start_block; // return the start of this allocated block (saved before advancing pointer)
    }

    /*
        Allocate an array of objects from bump heap utilizing the above alloc method
        
        @param num_obj the number of objects in the array
    */
    template<typename T>
    static T *alloc_array(long num_obj) {
        // allocate enough space for all the objects of T type
        return static_cast<T *>(alloc(num_obj * static_cast<long>(sizeof(T))));
    }
}; /* namespace impl */

/*
    A struct for a fixed capacity string buffer. (no dynamic alloc, basic ops only)
    Capacity is the fixed total size of the internal buffer (includes null terminator)
*/
template<int CAPACITY>
struct StringBuffer {
    // capacity must be more than just null term/single char
    static_assert(CAPACITY > 1, "StrBuf capacity must be > 1");

    // the instance vars
    char buffer[CAPACITY];
    int length;

    /*
        Constructor that zeros out the buffer. Passes in 0 to len because there is nothing
        of note in the buffer yet. 
    */
    StringBuffer() : length(0) {
        for (int i = 0; i < CAPACITY; i++) {
            buffer[i] = '\0'; // zeroing out
        }
    }

    /*
        Appends a single character to the end of the string. 

        @param c the character you are appending over
    */
    StringBuffer &operator+=(char to_add) {
        // if there's enough spaceleft, then incr the length and add it at that spot
        if (length < CAPACITY - 1) {
            buffer[length++] = to_add; // equal that spot to the to_add var to add it to the end
        }
        return *this; // return the string itself
    }

    /*
        Append an entire string to the end of this string.

        @param str the string that is going to be added
    */
    StringBuffer &operator+=(const char *str) {
        // until the other string has characters in it, iterate through that string
        //  and add each character of that one by one
        while (*str && length < CAPACITY - 1) {
            buffer[length++] = *str++; 
        }
        return *this; // return the string itself
    }

    /*
        Trim the trailing whitepsace in place
    */
    void trim() {
        // continually make the last character the null term char until there are no trailing spaces
        while (length > 0 && (unsigned char)buffer[length - 1] <= ' '){
            buffer[--length] = '\0'; 
        }
    }

    /*
        returns null terminated C view of string
    */
    const char *view_string() {
        buffer[length] = '\0'; // null term the end
        return buffer; // return the char array
    }
};

/*
    File descriptor wrapper class - for read/write/lseek/close syscalls
*/
class FileDescr {
    int  file_descr; // file descriptor
    bool owned; // whether obj owns file descr or not

public:
    // default constructor with setting up the vars as if no file has opened yet
    FileDescr() : file_descr(-1), owned(false) {}

    /*
        Opens file immediately using open
        
        @param path filesystem path to open
        @param flags default read-only + open mode
    */
    explicit FileDescr(const char *file_sys_path, int flags = O_RDONLY)
        : file_descr(open(file_sys_path, flags, 0)), owned(true) {}

    /*
        Closes the file upon destruction of FileDescr object
    */
    ~FileDescr() {
        if (owned && file_descr >= 0) {
            close(file_descr); // close the file descr
        }
    }
    
    // no copying of it allowed to prevent issues for close
    FileDescr(const FileDescr &) = delete;
    FileDescr &operator=(const FileDescr &) = delete;

    /*
        Checks if file is valid and open

        @return true if valid
    */
    bool valid() const { 
        return file_descr >= 0; 
    }

    /*
        Reads from file descriptor 

        @param buffer the output buffer
        @param num_bytes the number of bytes to read
        @return the number of bytes succesfully read
    */
    long read(char *buffer, long num_bytes) const {
        return ::read(file_descr, buffer, num_bytes);
    }

    /*
        Moves the offset of the file

        @param offset position to move to
        @return resultant offset upon success
    */
    long seek(long offs, int whence = SEEK_SET) const {
        return lseek(file_descr, offs, whence);
    }
};

// generic min/max helpers here - pretty self explanatory
template<typename T>
static constexpr T min_helper(T a, T b) { return a < b ? a : b; }

template<typename T>
static constexpr T max_helper(T a, T b) { return a > b ? a : b; }

/*
    Increement semaphore and wake waiting process 

    @param semid the semaphore for which this operation is occuring for
*/
static inline void sem_up(int semid) {
    sembuf op{0, 1, 0};            
    semop(semid, &op, 1); // increment
}

/*
    Decrement semaphore 

    @param semid the semaphore for which this operation is occuring for
*/
static inline void sem_down(int semid) {
    sembuf op{0, -1, 0};          
    semop(semid, &op, 1); // decrement
}

int main() {
    // brk allocator test for init
    if (!impl::init()) {
        printf("brk initialization failed\n");
        return 1;
    }
    printf("brk ready\n");

    // more stuff to prove brk works 
    char *buffer = impl::alloc_array<char>(256);
    if (!buffer) {
        printf("more brk stuff failed\n");
        return 1;
    }
    // write canary pattern to heap buffer in order to verify it 
    for (int i = 0; i < 8; i++) {
        buffer[i] = (char)('A' + i);
    }
    buffer[8] = '\0'; // null term

    // semget test
    int semid = semget(IPC_PRIVATE, 1, 0600);
    if (semid < 0) {
        printf("semget failed\n");
        return 1;
    }

    // fork test
    printf("forking test starting\n");
    int process_id = fork();

    // child 1 process
    if (process_id == 0) {
        // open hello_world.txt, read the first 12 bytes and then send semop signal
        {
            FileDescr f{"/hello_world.txt"};
            if (!f.valid()) {
                printf("open failed\n");
                exit(1);
            }

            char reading_buffer[64];
            long n = f.read(reading_buffer, 12);
            reading_buffer[min_helper(n, 63L)] = '\0';

            StringBuffer<64> string_buffer;
            string_buffer += reading_buffer;

            printf("opened successfully\n");
            printf("child read successfully\n");
        } // file closes auto here

        // semop test
        printf("sent signal to parent\n");
        sem_up(semid);
        exit(CHILD_ONE_EXIT);
    }

    // block the parent until child is done/got the signal from the child
    sem_down(semid);
    printf("parent got child signal\n");
    // make parent do open and read as well pretty much self explanatory after above
    {
        FileDescr f{"/os.txt"};
        if (f.valid()) {
            char reading_buffer[128];
            long n = f.read(reading_buffer, 127);
            n = min_helper(n, 127L);
            reading_buffer[n] = '\0'; // null term for the read

            StringBuffer<128> str_buffer;
            str_buffer += reading_buffer;
            str_buffer.trim();

            printf("parent opened file and read: %s\n", str_buffer.view_string());
        }
    }

    // waitpid test
    int status1 = 0;
    waitpid(process_id, &status1, 0);
    int ec1 = (status1 >> 8) & 0xFF;
    printf("child 1 exited: %d\n", ec1);

    // schedule yield test
    printf("yielding...\n");
    for (int i = 0; i < 3; i++) {
        sched_yield();
    }

    // another fork in the road
    printf("forking 2\n");
    int proc_id_two = fork();

    if (proc_id_two == 0) {
        // open, lseek and read testing
        {
            FileDescr f{"/exact_block.txt"};
            if (!f.valid()) {
                printf("child2 cannot open file\n");
                exit(1);
            }

            // skip first 26 chars using lseek
            f.seek(26, SEEK_SET);

            // grab next 26 characters
            char reading_buffer[32];
            long n = f.read(reading_buffer, 26);
            reading_buffer[min_helper(n, 31L)] = '\0';

            printf("child2 opened file success!\n");
            printf("what was read: %s\n", reading_buffer);
            printf("child2 done now\n");
        }

        exit(CHILD_TWO_EXIT);
    }

    // waitpid test again
    int status2 = 0;
    waitpid(proc_id_two, &status2, 0);
    int ec2 = (status2 >> 8) & 0xFF;
    printf("child 2 exited: %d\n", ec2);

    // semctl test - should destroy semaphore
    semctl(semid, 0, IPC_RMID, 0L);
    printf("sema destroyed\n");

    printf("all done\n");
    return 0;
}
