#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>

#define RED     "\033[0;31m"
#define GREEN   "\033[0;32m"
#define RESET   "\033[0m"

/*
In this program we generate the Collatz sequence of a number given on the command line.
The rule is applied until the number reaches 1:
    n = n / 2        if n is even
    n = 3 * n + 1    if n is odd
The Collatz conjecture says that every positive integer reaches 1 this way.

The sequence is generated and printed by a child process created with fork(). The parent
does nothing except wait() for the child and then exit.

Learnings:
    • After fork() the child has its own copy of 'number'. Every change the loop makes to it
      happens in the child's copy only, the parent's 'number' still holds the starting value.
      This is why the child has to print the sequence itself: the parent never sees it.
    • The argument is validated before fork(), so no child is created for a bad input.
      Both processes would otherwise run the checks and print the error.
    • Command line arguments are strings. strtol() converts one to a long and, through endptr,
      reports where it stopped reading, which is what makes the validation possible:
          endptr == str      nothing was read, there is no number
          *endptr != '\0'    a number was read but other characters follow it ("12abc")
          *endptr == '\0'    the whole string was a number
    • strtol() accepts a leading minus sign, so "-5" is a valid number as far as the
      conversion goes. The sign has to be checked separately.
    • wait(NULL) blocks the parent until the child has terminated, so the parent's message
      always comes after the complete sequence. It also collects the child's exit status,
      so the child does not stay behind as a zombie (see q_1).
    • The sequence rises before it falls: 27 climbs to 9232 on its way to 1. The values are
      held in a long, which has more room for this than an int.
*/
int main(int argc, char * argv[]) {
    if(argc != 2){
        printf(RED "Usage: %s <positive integer>\n" RESET, argv[0]);
        return 1;
    }

    // exactly one argument was given, so argv[1] is the starting number
    char * param_str = argv[1];
    char * endptr; // set by strtol() to the first character it did not convert
    errno = 0;
    long int number = strtol(param_str,&endptr,10);
    if(errno == ERANGE){
        printf(RED "Number is too large!Exiting..\n" RESET);
        return 1;
    }
    // strtol() did not move past the start of the string: no digits were read
   else if(param_str == endptr) {
        printf(RED "No numbers found in the string!\n" RESET);
        return 1;
    }
    // digits were read, but the string does not end where they end
    else if(*endptr != '\0'){
        printf(RED "String is alphanumeric\n" RESET);
        return 1;
    }
    // a whole number, but 0 and negatives are not part of the conjecture
    else if(number <= 0){
        printf(RED "Number n entered must be positive!\n" RESET);
        return 1;
    }

    // from here on two processes run this code, each with its own copy of 'number'
    pid_t pid = fork();
    if(pid < 0){
        perror("fork");
        return 1;
    }
    else if(pid == 0){
        // child process: fork() returned 0

        printf(GREEN"Entering child process.\nNumber n is valid, n = %ld\n" RESET, number);

        // each pass computes the next term and prints it, until the term is 1
        while (number != 1) {
            if (number % 2 == 0){
                number = number / 2;
            }
            else {
                if(number > (LONG_MAX - 1) / 3){
                    printf(RED "Overflow possible! Exiting...\n" RESET);
                    return 1;
                }
)
                number = 3 * number + 1;
            }
            printf(GREEN "%ld, " RESET, number);
        }
        printf("\n");
    }
    else{
        // parent process: blocks here until the child has printed the whole sequence
        wait(NULL);
        printf(RED "Child process complete! Exiting...\n" RESET);
    }

    return 0;
}
