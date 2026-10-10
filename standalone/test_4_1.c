#include <stdio.h>
#include <stdlib.h>

/*
This is the program that test_4.c loads into its child process using exec.
It takes an integer as its last cli arg and counts from 1 to that integer.
It is a normal program on its own and can also be run directly: ./test_4_1 5

Learnings:
    • A program started through exec receives its arguments in argc/argv exactly like a
      program started from the shell.
    • cli args are strings, strtol is used to convert the string to a number.
    • strtol also tells us where it stopped reading, which is how invalid input is detected.
*/

int main(int argc, char *argv[]){
    if(argc < 2){
        printf("Kindly provide enough arguments; provide an integer!\n");
        return 1;
    }
    char *str_x = argv[argc - 1];
    char *endptr;

    /*
        strtol(char *str, char **endptr, int base) function to convert a string number to a long int
            • str    - the string to convert
            • endptr - is set to point at the first character that could not be converted
            • base   - base of the number in the string, 10 for decimal

        After the call:
            • endptr == str   -> not a single digit was read, there is no number at all
            • *endptr != '\0' -> a number was read, but it is followed by other characters ("12abc")
            • *endptr == '\0' -> the whole string was a number
    */
    long int x = strtol(str_x, &endptr, 10);
    if(endptr == str_x){
        printf("No integer found!\n");
        return 1;
    }
    if(*endptr != '\0'){
        printf("String is not purely numeric!\n");
    }

    printf("The integer provided is %ld\nCounting...\n", x);
    for(long int i = 1; i <= x; i++){
        printf("%ld\n", i);
    }

    return 0;
}
