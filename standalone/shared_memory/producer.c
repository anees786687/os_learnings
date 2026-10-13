#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>

/*
In this program we create a POSIX shared memory object and write a message into it.
It is the first half of a pair: consumer.c opens the same object by its name and prints
the message. This program has to run first, and it prints nothing itself.

The steps:
    shm_open()      creates the object "OS" and returns a file descriptor for it
    ftruncate()     sets the size of the object, a new object has size 0
    mmap()          maps the object into this process's address space and returns a pointer
    sprintf()       writes the message through that pointer, no system call involved

Learnings:
    • Shared memory is a way for two processes to exchange data: both map the same
      physical pages, so what one writes the other can read.
    • System calls are only needed for the setup. After mmap() the region is used like
      any other memory, which is what makes shared memory fast.
    • None of the three setup calls uses physical memory. shm_open() creates an empty object,
      ftruncate() only records a size and mmap() only reserves a range of addresses.
      A frame is assigned at the first write to the page, through a page fault.
    • The descriptor is only a handle for ftruncate() and mmap(). It is not an address.
    • The object is not removed when this program exits. It stays (on Linux as the file
      /dev/shm/OS) until some process calls shm_unlink(), which the consumer does.
    • sprintf() writes a terminating '\0' after the characters. Moving the pointer by
      strlen() puts it on that '\0', so the next sprintf() overwrites it and the two
      messages end up as one string.
*/
int main(){
    const int SIZE = 4096;      // size of the object in bytes: one page on this machine

    const char * name = "OS";   // the name the consumer uses to find the object
    const char * msg_1 = "Hello";
    const char * msg_2 = "World!\n";

    // create the object if it does not exist (O_CREAT) and open it for reading and writing.
    // 0666 is the permission asked for a new object: read and write for everyone, minus the umask.
    int shm_fd = shm_open(name,O_CREAT | O_RDWR, 0666);
    if (shm_fd < 0){
        perror("shm_open");
        return 1;
    }
    // set the size of the object. No memory is allocated yet, only the size is recorded
    ftruncate(shm_fd, SIZE);

    // map the object into this process and get a pointer to its first byte.
    // MAP_SHARED makes the writes go to the object itself, so other processes see them.
    // 0 as the address lets the kernel choose where the mapping goes.
    void * ptr = mmap(0, SIZE, PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if(ptr == MAP_FAILED){
        perror("mmap");
        return 1;
    }
    // write into the shared memory. The first write is the first access to the page.
    // After each write the pointer is moved past the characters written, so that the
    // next write continues there. ptr is a void *, moving it in bytes is a GCC extension.
    sprintf(ptr,"%s",msg_1);
    ptr += strlen(msg_1);
    sprintf(ptr,"%s",msg_2);
    ptr += strlen(msg_2);

    return 0;

}