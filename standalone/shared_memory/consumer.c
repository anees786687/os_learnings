#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

/*
In this program we read a message from a POSIX shared memory object and print it.
It is the second half of a pair: producer.c creates the object and writes the message,
this program opens the same object by its name and reads it. The two are separate programs,
the only thing they have in common is the name "OS".

The steps:
    shm_open()      opens the existing object and returns a file descriptor for it
    mmap()          maps the object into this process's address space and returns a pointer
    printf()        reads the message straight through that pointer, no system call involved
    shm_unlink()    removes the object's name, so the memory can be freed

Learnings:
    • The descriptor is only a handle used during setup. What gets mapped is the object,
      and after mmap() all reading goes through the pointer.
    • This process has its own pointer, different from the producer's. Both lead to the
      same physical page, which is why the producer's bytes are visible here.
    • The object outlives the producer: it stays until shm_unlink() or a reboot, so the
      producer can have exited long before this program starts.
    • There is no ftruncate() here. The size belongs to the object and the producer set it.
    • Whether a call failed is told by its return value, why it failed is told by errno.
      perror() prints the reason that belongs to the current value of errno.
    • The two calls report failure differently: shm_open() returns -1,
      mmap() returns MAP_FAILED, which is (void *) -1 and not NULL.
    • A failed call does not crash the program. Without a check the bad value is passed on,
      and the crash comes later at the first line that uses it (here the printf()).
    • shm_unlink() makes this program runnable once per run of the producer:
      a second run finds no object.
*/
int main(){
    const int SIZE = 4096;      // bytes to map: one page, the size the producer gave the object
    const char * name = "OS";   // must be the same name the producer used

    // open the existing object for reading. There is no O_CREAT, so nothing is created
    // and the mode (0666) is ignored. If the producer has not run, there is nothing to open.
    int shm_fd = shm_open(name,O_RDONLY,0666);

    // shm_open() returns -1 on failure and sets errno to the reason
    if (shm_fd < 0){
        perror("shm_open");
        return 1;
    }

    // map the object into this process: read-only (PROT_READ, matching O_RDONLY above)
    // and shared (MAP_SHARED), so that this mapping shows what other processes wrote.
    // 0 as the address lets the kernel choose where the mapping goes.
    void * ptr = mmap(0,SIZE,PROT_READ,MAP_SHARED,shm_fd, 0);
    // mmap() returns MAP_FAILED on failure, not NULL
    if(ptr == MAP_FAILED){
        perror("mmap");
        return 1;
    }

    // the region holds a C string, so it is read like any other string.
    // This is the first access to the page: it is linked to the producer's frame here.
    printf("%s", (char *)ptr);

    // remove the name. The memory is freed once no process has the object mapped or open
    shm_unlink(name);

    return 0;
}