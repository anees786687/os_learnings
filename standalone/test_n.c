#include <stdio.h>
// #include <unistd.h>
// #include <sys/wait.h>
// #include <sys/types.h>
#include <pthread.h>

/*
    This program takes an array of 10 numbers as the input from CLI
    and returns the sum total of its elements. Rather than linearly going through
    the entire array, the array is split into two halves and each half is assigned to a thread
    which will calculate the sum of that half and write to a memory location.
    
    Finally, the respective memory locations will be read and the value obtained will be summed
    to get the final value.

    This example focuses on making a program multithreaded
*/

