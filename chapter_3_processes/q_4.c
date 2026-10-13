#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>

#define RED "\033[0;31m"
#define GREEN "\033[0;32m"
#define RESET "\033[0m"

/*
In this program we generate the Collatz sequence of a number given on the command line,
as in q_3, but this time the parent prints it. The child still does the computing.
After fork() the two processes have separate copies of their variables, so the child
cannot hand the sequence back through an ordinary array. It writes the terms into a
POSIX shared memory object that both processes have mapped, and the parent reads them
from there once the child has finished.

The steps:
    parent    shm_open(), ftruncate(), mmap()   create the object and map it
    parent    shared[0] = starting number
    parent    fork()                            the child inherits the mapping
    child     shared[1], shared[2], ...         one term per slot, the last one is 1
    parent    wait()                            until the child has written everything
    parent    shm_unlink()                      remove the object's name
    parent    print shared[0], shared[1], ...   up to and including the 1

Learnings:
    • A mapping made with MAP_SHARED before fork() stays shared afterwards. The child
      inherits the pointer and the mapping, so it does not call shm_open() or mmap() again.
      Ordinary variables (num, i) are still private copies, as in q_3.
    • The region holds raw long ints, not text. Writer and reader have to agree on the
      format: writing with sprintf() and reading with *(long *) printed the ASCII code of
      the first digit (52 for 4) and not the number.
    • mmap() returns a void *, an address without a type. Assigning it to a long int *
      gives the region a type: shared[i] is the i-th long, and shared + 1 moves by
      sizeof(long) = 8 bytes, not by 1.
    • The region is 4096 bytes, which is room for 4096 / 8 = 512 terms. Nothing stops a
      write to shared[512] except a check, so the index is tested before every write.
    • The count of terms is not passed to the parent. The sequence always ends in 1, so
      the 1 itself marks the end and the parent reads until it sees it.
    • That only works if the child got as far as writing the 1. wait(&status) tells the
      parent how the child ended: WIFEXITED / WEXITSTATUS give the value it returned,
      WIFSIGNALED tells that it was killed. The parent prints only after a clean exit.
    • wait() returning -1 means wait() itself failed. It says nothing about the child.
    • shm_unlink() removes only the name. The parent can still read through its mapping
      afterwards, the memory is freed when the last mapping is gone.
    • Every return after shm_open() has to leave no object behind in /dev/shm.
      The cleanup is done by the parent, which is the process that outlives the other.
*/
int main(int argc, char ** argv){
    // exactly one argument is expected: the starting number
    if(argc != 2){
        printf(RED "Usage: %s <positive integer> \n" RESET, argv[0]);
        return 1;
    }

    char * num_str = argv[1];
    char * endptr; // set by strtol() to the first character it did not convert

    // strtol() reports a number too large for a long only through errno,
    // so errno is cleared first and tested straight after the call
    errno = 0;
    long int num = strtol(num_str, &endptr,10);
    if(errno == ERANGE){
        perror("strtol");
        return 1;
    }
    // no digits were read
    else if(num_str == endptr){
        printf(RED "No numbers found in the string\n" RESET);
        return 1;
    }
    // digits were read, but other characters follow them
    else if(*endptr != '\0'){
        printf(RED "String is alphanumeric\n" RESET);
        return 1;
    }
    // a whole number, but 0 and negatives are not part of the conjecture
    else if(num <= 0){
        printf(RED "Number must be positive!\n" RESET);
        return 1;
    }

    // the shared memory object
    const int SIZE = 4096;                          // size in bytes: one page
    const int MAX_NUMS = SIZE / sizeof(long int);   // long ints that fit in it: 512
    const char * name = "COMMON";

    // create the object and get a descriptor for it. A new object has size 0

    int shm_fd = shm_open(name,O_CREAT | O_RDWR,0666);
    if(shm_fd < 0){
        perror("shm_open");
        return 1;
    }

    // set the size. From here on the object exists, so every failure below
    // removes it again before returning
    int trunc_stat = ftruncate(shm_fd,SIZE);
    if(trunc_stat < 0){
        perror("ftruncate");
        shm_unlink(name);
        return 1;
    }

    // map the object into this process. This is done before fork(), so the child
    // inherits the mapping and both processes reach the same physical page
    void * ptr = mmap(0,SIZE,PROT_READ | PROT_WRITE,MAP_SHARED,shm_fd,0);
    if(ptr == MAP_FAILED){
        perror("mmap");
        shm_unlink(name);
        return 1;
    }

    // treat the region as an array of long ints. The starting number is the first term
    long int * shared = ptr;
    *shared = num;

    // from here on two processes run this code. num and every other variable is copied,
    // only the memory behind 'shared' is the same in both
    pid_t pid = fork();
    if(pid < 0){
        printf(RED "Failed to fork!\n" RESET);
        shm_unlink(name);
        return 1;
    }
    else if(pid == 0){
        // child process: computes the terms and stores them, prints nothing but errors.
        // Slot 0 already holds the starting number, so the first new term goes into slot 1
        int i = 1;
        while (num != 1) {
            // the region is full and the sequence has not reached 1.
            // Exit status 2 tells the parent why there is no complete sequence
            if(i == MAX_NUMS){
                return 2;
            }
            // long int temp = 0
            if(num % 2 == 0){
                num = num / 2;
            }
            else {
                // 3 * num + 1 would not fit in a long. This is the condition
                // 3 * num + 1 > LONG_MAX rearranged, so that the test itself cannot
                // overflow. Exit status 1 reports it to the parent
                if(num > (LONG_MAX - 1) / 3){
                    printf(RED "Overflow possible! Exiting...\n" RESET);
                    return 1;
                }

                num = 3 * num + 1;
            }
            // same as shared[i] = num. The last value stored is the 1 that ends the loop
            *(shared + i) = num;
            i++;
        }
        
    }
    else{
        // parent process: blocks until the child has terminated, and so has finished writing.
        // child_stat receives how the child ended, to be decoded with the W... macros
        int child_stat;
        wait(&child_stat);

        // the name is not needed any more. The mapping stays valid, so the region can
        // still be read below. Done before the checks so that every path cleans up
        shm_unlink(name);

        // the child returned from main: its return value says whether the sequence is complete
        if(WIFEXITED(child_stat)){
            if(WEXITSTATUS(child_stat) == 1){
                printf(RED "Overflow occurred!\n" RESET);
                return 1;
            }
            else if(WEXITSTATUS(child_stat) == 2){
                printf(RED "Shared memory consumed entirely!\n" RESET);
                return 1;
            }
        }
        // the child did not return at all, it was killed (by a segmentation fault, for example)
        else if(WIFSIGNALED(child_stat)){
            printf(RED "Child process killed by a signal!\n" RESET);
            return 1;
        }

        // the child exited with 0, so the region holds a complete sequence ending in 1.
        // Print from slot 0 up to and including that 1
        long int term = 0;
        int i = 0;
        while(term != 1){
            term = shared[i++];
            printf("%ld ", term);
        }
        printf("\nExiting Parent process\n");
    }

    return 0;
}