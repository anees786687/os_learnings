#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

/*
So far we have used if-else to separate the logic of the child process and the parent process.
We can use the exec function family for making the parent and the child do separate tasks.

In this program we take user input using cli args and pass the user input, which is an int number,
to the child process. The child process then counts to that number (see test_4_1.c).

Learnings:
    • exec replaces the program running in the calling process with a new program. The pid
      stays the same, but the code, data and stack are those of the new program.
    • Because of that, exec does not return on success - any line after it runs only if
      the exec failed.
    • fork() + exec() is the standard way of running another program: fork to get a new
      process, exec inside the child to load the other program into it.
    • Command line arguments are always strings, so the number is passed on as a string
      and the child program converts it to an integer itself.
*/

int main(int argc, char *argv[]){
    /*
    argc - Argument Count; integer; stores the number of cli args passed; always at least 1,
           as the name of the executable counts as the first arg

    argv - Argument Vector; array of character pointers aka array of strings, which contain the actual arguments.
        • argv[0] holds the program's name or file path.
        • argv[1] holds the first user-provided argument.
        • argv[argc - 1] holds the last argument.
        • argv[argc] is safely guaranteed to be a NULL pointer
    */

    if(argc < 2){
        printf("Kindly provide enough arguments; provide an integer!\n");
        return 1;
    }
    char *num_str_frmt = argv[argc - 1];

    pid_t pid = fork();
    if(pid < 0){
        perror("fork");
        return 1;
    }

    if(pid == 0){
        /*
        execlp(file, arg0, arg1, ..., (char *)NULL)
            • l - the arguments are given as a list, one after the other
            • p - the file is searched for in the PATH, unless it contains a '/' (like here),
                  in which case it is used as a path directly
            • arg0 becomes argv[0] of the new program, by convention the program's name
            • the list must end with a NULL pointer, so that exec knows where it stops

        "./test_4_1" is relative to the current directory, so run this program from the
        directory that contains the test_4_1 executable (cd standalone && ./test_4 5).
        */
        execlp("./test_4_1", "test_4_1", num_str_frmt, (char *)NULL);

        /* Reached only if exec failed, e.g. test_4_1 has not been compiled yet */
        perror("execlp");
        return 1;
    }

    wait(NULL); // let the child finish counting before the parent exits
    printf("Child process finished executing! Exiting parent process\n");
    return 0;
}
