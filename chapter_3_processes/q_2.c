#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#define RED     "\033[0;31m"
#define GREEN   "\033[0;32m"
#define RESET   "\033[0m"
#define MIN_PID 300
#define MAX_PID 5000
#define BITMAP_SIZE (MAX_PID - MIN_PID + 1)                 // number of PIDs: 4701
#define WORD_SIZE 32                                        // bits in one element of the bitmap
#define BOXES ((BITMAP_SIZE + WORD_SIZE - 1) / WORD_SIZE)   // elements needed, rounded up: 147

/*
In this program we implement a pid manager: the part of the operating system that hands out
process identifiers and takes them back. No processes are created here, the program only
keeps track of which PIDs in the range [MIN_PID, MAX_PID] are in use.

The PIDs are tracked in a bitmap in which every PID owns one bit: 0 means the PID is free,
1 means it is in use. The bits are packed 32 to a uint32_t, so the PID is found with
    i   = pid - MIN_PID     position of the PID in the whole row of bits
    idx = i / WORD_SIZE     element of the array that holds the bit
    pos = i % WORD_SIZE     position of the bit inside that element

Learnings:
    • A bitmap needs 4701 bits = 147 uint32_t = 588 bytes. An int array with one element
      per PID would need 4701 * 4 = 18804 bytes.
    • (n + 32 - 1) / 32 is integer division that rounds up, used to get the number of elements.
    • With mask = 1U << pos:
          word |= mask     sets the bit
          word &= ~mask    clears the bit
          word & mask      tests the bit
      Testing with &= instead of & writes the result back and wipes the other 31 bits of the word.
    • The 1 has to be unsigned (1U) and the elements uint32_t: in a signed int bit 31 is the sign bit.
    • 147 * 32 = 4704 bits exist but only 4701 are PIDs. The 3 spare bits of the last element
      must never be handed out, which is why allocate_pid() checks pid <= MAX_PID.
    • In C only 0 is false, so 'if (allocate_map())' is also true for -1.
      The return value has to be compared explicitly.
    • A #define is pasted as text, so a macro containing an operator needs outer parentheses.
    • calloc() returns zeroed memory, so every PID starts free. The memory is given back with
      free(), and the pointer is set to NULL afterwards so that later use is caught by the
      NULL checks instead of touching freed memory.
*/
uint32_t * pid_bank = NULL;

/*
Creates the bitmap with every PID marked free.
Returns 1 if successful, -1 if the memory could not be allocated or a bitmap already exists.
*/
int allocate_map(void){
    if (pid_bank != NULL){
        printf(RED "PID bitmap already allocated!\n" RESET);
        return -1;
    }
    pid_bank = (uint32_t * )calloc(BOXES, sizeof(uint32_t));

    if (pid_bank == NULL){
        return -1;
    }

    return 1;
}

/*
Allocates the lowest free PID and returns it.
Returns -1 if all PIDs are in use or the bitmap has not been created.
*/
int allocate_pid(void){
    if(pid_bank == NULL){
        return -1;
    }

    int idx = 0, pos = 0, set = 0, pid;
    for(int i = 0; i < BOXES; i++){
        for(int j = 0; j < WORD_SIZE; j++){
            if((pid_bank[i] & (1U << j)) == 0){ // bit j of element i is free
                idx = i;
                pos = j;
                pid = MIN_PID + idx * WORD_SIZE + pos;
                // a spare bit of the last element is not a PID, so it is left untouched
                if (pid <= MAX_PID){
                    set = 1;
                    pid_bank[i] |= (1U << j);
                    break;
                }
            }
        }

        // the break above only leaves the inner loop
        if (set){
            break;
        }
    }

    // every bit that stands for a PID is taken
    if(!set){
        return -1;
    }

    return pid;
}

/*
Releases a PID so that it can be allocated again.
A PID outside the range, or one that is not in use, is reported and otherwise ignored.
*/
void release_pid(int pid){
    if(pid_bank == NULL){
        printf(RED "No PID bitmap allocated\n" RESET);
        return;
    }
    // must be checked before pid is turned into an index into the bitmap
    if(pid < MIN_PID || pid > MAX_PID){
        printf(RED "Invalid PID! PID must be between [300,5000]\n" RESET);
        return;
    }

    int i = pid - MIN_PID;
    int pid_idx = i / WORD_SIZE;
    uint32_t mask = 1U <<( i % WORD_SIZE);
    if((pid_bank[pid_idx] & mask) == 0){
        printf("No process associated with PID %d\n", pid);
        return;
    }

    pid_bank[pid_idx] &= ~mask;
    printf("Process decoupled from PID %d\n", pid);
}

int main(){
    // a PID cannot be allocated before the bitmap exists
    int pid = allocate_pid();
    if(pid < 0){
        printf(RED "No PID bitmap allocated\n" RESET);
    }

    if(allocate_map() > 0){
        printf(GREEN "PID bitmap allocated successfully!\n" RESET);
    }
    else{
        printf(RED "PID bitmap allocation failed!Exiting...\n" RESET);
        return 1;
    }
    // a second bitmap is refused
    allocate_map();

    // PIDs are handed out in increasing order starting from MIN_PID
    for(int i = 0; i < 15; i++){
        int pid = allocate_pid();

        if(pid == -1){
            printf("PID bitmap full!\n");
            break;
        }
        printf("PID allocated: %d\n",pid);
    }

    // of the two released PIDs, the lower one is allocated first
    release_pid(306);
    release_pid(313);
    printf("PID allocated: %d\n",allocate_pid());

    // releasing a PID that is already free changes nothing: 313 is still the next one
    release_pid(313);
    printf("PID allocated: %d\n",allocate_pid());

    free(pid_bank);
    pid_bank = NULL;
}
