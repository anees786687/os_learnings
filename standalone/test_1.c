#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>

/*
This program explores the functionality of fork.

Learnings:
    • fork() creates a new (child) process that is a copy of the calling (parent) process.
    • Both processes continue executing from the line right after fork(), so the code
      below the fork() call runs twice - once in the parent and once in the child.
    • fork() is called once but returns twice, with a different value in each process:
        - in the parent: the pid of the newly created child (> 0)
        - in the child : 0
        - on failure   : -1 (no child is created)
    • The order in which the parent and the child run is decided by the scheduler,
      so the two lines of output can appear in either order.
*/
int main(){
    pid_t pid = fork();

    if(pid < 0){
        perror("fork"); // perror prints the reason why the last system call failed
        return 1;
    }

    /* Runs in both processes: the parent prints the child's pid, the child prints 0 */
    printf("process id %d\n", pid);

    return 0;
}
