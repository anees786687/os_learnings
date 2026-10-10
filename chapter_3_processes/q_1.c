#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>

#define RED     "\033[0;31m"
#define GREEN   "\033[0;32m"
#define RESET   "\033[0m"

/*
In this program we create a zombie process: a child that has terminated but whose
parent has not yet collected its exit status with 'wait()'.

Run it in the background and look at the process table while the parent is asleep:
    ./q_1 &
    ps -l

Learnings:
    • When a process terminates, its memory is released but its entry in the process table
      stays, because the entry holds the exit status that the parent is expected to read.
      Until the parent calls wait(), the child is a zombie.
    • The child here finishes almost immediately while the parent sleeps for 10 seconds
      without calling wait(), so the child is a zombie for those 10 seconds.
    • In the output of 'ps -l' a zombie has Z in the S column and <defunct> next to its name.
      Its PPID is the PID of the parent, and its SZ is 0 since it no longer holds any memory.
    • 'ps -l' only lists the processes of the terminal it is run in. Run from another
      terminal it does not show the zombie; use 'ps -el' there to list every process.
    • A zombie cannot be killed, it is already dead. It goes away when the parent calls wait(),
      or when the parent terminates: the zombie is then adopted by init (systemd), which
      calls wait() for it. That is why killing the parent removes the zombie.
    • The program has to be run with & so that the shell gives the prompt back
      while the parent is still asleep.
*/
int main(){
    pid_t pid, pid1;

    pid = fork();
    if(pid < 0){
        printf(RED "Forking failed! Exiting...\n" RESET);
        return 1;
    }

    if (pid == 0){
        printf(GREEN "Forked into child process, this process will become a zombie process\n" RESET);
        for(int i = 0; i < 10; i++){
            printf(GREEN "%d " RESET, i);
        }
        printf(GREEN "\nChild process exiting!\n" RESET);

        printf("\n");
    }
    else{
        /*Wait wont be called purposely to make the child process a zombie process once it is complete.
        The idea is to keep the exit status in the process table and remove it manually 
        */
        printf(RED "Parent process sleeping for 10 seconds...\n" RESET);
        sleep(10);
        printf(RED "Parent process exiting!\n" RESET);
    }


}