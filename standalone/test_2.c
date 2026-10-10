#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>

/*
In this program we see that the child process and the parent process have different
address spaces. Any modification done in a child/parent process is limited to that process
and is not propagated to the parent/child process.

Learnings:
    • At the time of fork() the child gets its own copy of the parent's memory, so both
      start with x = 10.
    • After the fork the two copies are independent: the child sets its x to 20, but the
      parent still sees x = 10.
    • The return value of fork() is what lets us tell the two processes apart:
      pid == 0 means "I am the child", anything else (> 0) means "I am the parent".
*/
int main(){
    int x = 10;
    pid_t pid = fork();

    if(pid < 0){
        perror("fork");
        return 1;
    }

    if(pid == 0){
        x = 20; // changes only the child's copy of x
        printf("Child x = %d\n", x);
    }
    else{
        printf("Parent x = %d\n", x); // still 10
    }

    return 0;
}
