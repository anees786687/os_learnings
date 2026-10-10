#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

/*
In this program we see how the parent can wait for the child process to finish its
execution, which is done using the 'wait()' function call.

In this program we also see how to get the pid of a process and the pid of its parent.

Learnings:
    • wait(NULL) blocks the parent until one of its children terminates. Because of it,
      all the even numbers (child) are printed before any odd number (parent).
      Without the wait() both loops would run at the same time and the output would interleave.
    • wait() takes a pointer to an int in which the child's exit status is stored;
      passing NULL means we are not interested in the status.
    • getpid() returns the pid of the calling process, getppid() returns the pid of its parent.
      The PPID printed by the child is the same as the PID printed by the parent.
      The PPID printed by the parent is the pid of the shell that started the program.
    • sleep(x) suspends the calling process for x seconds.
*/
int main(){
    pid_t pid = fork();

    if(pid < 0){
        perror("fork");
        return 1;
    }

    if(pid == 0){
        printf("Task to print even numbers\n");
        for(int i = 0; i < 10; i += 2){
            printf("PID %d, PPID %d, i %d\n", getpid(), getppid(), i);
            sleep(1); // use sleep to make the process sleep for x seconds
        }
    }
    else{
        wait(NULL); // parent does nothing until the child is done
        printf("Task to print odd numbers\n");
        for(int i = 1; i < 10; i += 2){
            printf("PID %d, PPID %d, i %d\n", getpid(), getppid(), i);
            sleep(1);
        }
    }

    return 0;
}
