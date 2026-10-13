# POSIX Shared Memory

Two programs that pass a message through a piece of memory they both have mapped: a producer that writes it and a consumer that reads it. This page is the lesson that goes with them: what shared memory is, what each call does, and how file descriptors and virtual memory make it work.

The code follows the producer/consumer example in chapter 3 of Silberschatz.

## Programs

| File | Role | What it does |
| --- | --- | --- |
| [producer.c](producer.c) | Writer | Creates the shared memory object `"OS"`, sizes it to 4096 bytes, maps it and writes `Hello` and `World!\n` into it. |
| [consumer.c](consumer.c) | Reader | Opens the same object by name, maps it read-only, prints what is in it and removes the name. |

The two are separate programs with no parent/child relation. The only thing they have in common is the name `"OS"`.

## Running it

```sh
make            # from the repository root
cd standalone/shared_memory
./producer      # must run first
./consumer
```

By hand: `gcc -Wall -Wextra -o producer producer.c` and the same for `consumer`. On glibc older than 2.34, add `-lrt` at the end; that is where `shm_open()` used to live.

### Output

```
$ ./producer
$ ./consumer
HelloWorld!
```

The producer prints nothing. It has already exited by the time the consumer starts, and the message is still there: a shared memory object outlives the process that created it.

**Order matters.** The consumer removes the name when it is done, so it can be run once per run of the producer. Running `./consumer` before `./producer`, or a second time, finds no object: the consumer prints an error and exits with status 1. Before its return values were checked, the same run ended in a segmentation fault (see [Error checking](#error-checking)).

## The idea

Normally the operating system keeps every process's memory private: a variable changed in one process is not seen by any other (see `test_2.c`). Shared memory is a deliberate exception that both processes ask for. It is one of the two models of **inter-process communication (IPC)**; the other is message passing.

POSIX shared memory combines three ideas:

| Idea | Its job here |
| --- | --- |
| IPC | The goal: let two processes exchange data. |
| File descriptor | A handle that identifies the shared object while it is being set up. |
| Virtual memory | The mechanism: each process's page table leads to the same physical pages. |

System calls are needed only to set the region up and to take it down. In between, reading and writing are ordinary pointer operations that do not go through the kernel. That is why shared memory is the fastest form of IPC, and also why the kernel cannot protect the two processes from each other's mistakes.

## Background: virtual memory

### Addresses, bytes and words

- Memory is **byte-addressable**: every byte has its own address.
- An address is a position, like a locker number. It is not stored with the byte.
- With 32-bit addresses there are 2³² addresses, so 2³² bytes can be addressed.
- A **word** is a chunk of data. A 32-bit word is 4 bytes, so the same memory holds 2³² ÷ 2² = 2³⁰ words. A word is referred to by the address of its first byte: 0, 4, 8, 12, ...
- A word can hold an address as its value. That is a **pointer**. The word and the address it holds are still two different things.

### Page tables

- Memory is divided into fixed-size **pages**. On this machine a page is 4096 bytes (`getconf PAGESIZE`), which is why `SIZE` is 4096: the region is exactly one page.
- The addresses a program uses are **virtual**. Every process has its own **page table**, which translates a virtual page number into a physical **frame** number. An entry `64 → 0` means virtual page 64 is stored in physical frame 0.
- Because each process has its own table, the same virtual address in two processes normally leads to different physical memory. With shared memory, entries in two different tables lead to the **same** frame.

### Demand paging

- Physical memory is handed out one page at a time, on the **first access** to that page, not when the memory is requested.
- The first access to a page with no frame behind it causes a **page fault**. The kernel assigns a frame, fills in the page table entry and lets the instruction run again.
- A fresh page reads as zeros.

## The file descriptor

`shm_open()` returns a file descriptor: a small integer that means "that shared object" inside this process.

- It is used **only during setup**, to tell `ftruncate()` and `mmap()` which object is meant.
- It is not an address and it is not what gets mapped. The **object** is mapped; the descriptor only identifies it.
- Once `mmap()` has returned, the descriptor can be closed with `close()` and the mapping keeps working.
- Descriptor numbers are private to each process. The producer's might be 3 and the consumer's 4.
- What the two processes share is the object's **name**. That is how the second process finds what the first one created.

## Step by step

### Producer

| Step | Call | What it does | Physical memory used? |
| --- | --- | --- | --- |
| 1 | `shm_fd = shm_open(name, O_CREAT \| O_RDWR, 0666);` | Creates the object `"OS"` if it does not exist and returns a descriptor for it. | No, the object has size 0. |
| 2 | `ftruncate(shm_fd, SIZE);` | Sets the object's size to 4096 bytes. | No, only the size is recorded. |
| 3 | `ptr = mmap(0, SIZE, PROT_WRITE, MAP_SHARED, shm_fd, 0);` | Reserves a range of this process's addresses and ties it to the object. Returns a pointer to the start of the range. | No. |
| 4 | `sprintf(ptr, "%s", msg_1);` | First write to the page: page fault, a frame is assigned. | Yes, one page. |
| 5 | `ptr += strlen(msg_1);` then the same for `msg_2` | Moves the pointer past what was written, so the next write continues from there. | No more, it is the same page. |

The arguments:

- **`O_CREAT`** creates the object if it is not there yet. **`O_RDWR`** opens it for reading and writing.
- **`0666`** is the permission requested for a newly created object: read and write for owner, group and others. The process's umask is subtracted from it, so with the usual umask of `0002` the object ends up as `rw-rw-r--`.
- `mmap(addr, length, prot, flags, fd, offset)`:
  - `addr = 0`: let the kernel pick where in the address space the range goes.
  - `length = SIZE`: how many bytes to map.
  - `prot`: what the process may do with the pages, here `PROT_WRITE`.
  - **`MAP_SHARED`**: writes go to the object itself and are seen by every process that maps it. The alternative, `MAP_PRIVATE`, gives the process a private copy and would defeat the purpose.
  - `offset = 0`: start at the beginning of the object.

### Consumer

| Step | Call | What it does |
| --- | --- | --- |
| 1 | `shm_fd = shm_open(name, O_RDONLY, 0666);` | Opens the **existing** object. There is no `O_CREAT`, so nothing is created and the `0666` is ignored. |
| 2 | `ptr = mmap(0, SIZE, PROT_READ, MAP_SHARED, shm_fd, 0);` | Reserves a range of its own addresses, read-only, tied to the same object. |
| 3 | `printf("%s", (char *)ptr);` | First read of the page: page fault, and the kernel points this process's page table entry at the frame the producer wrote to. |
| 4 | `shm_unlink(name);` | Removes the name `"OS"`. |

The consumer does not call `ftruncate()`. The size belongs to the object, and the producer has already set it.

## What `mmap()` really does

`mmap()` connects a range of the process's addresses to an object and returns a pointer to the start of that range. It does not hand out physical memory, and it does not give the descriptor an address.

It helps to see it as three moments:

1. **At the call.** The kernel reserves a range of virtual addresses and records "this range belongs to object `OS`". It returns `ptr`. The page table entries for the range are still empty.
2. **At the first touch of a page.** A page fault happens. The kernel looks up which object the address belongs to, finds (or allocates) the frame that holds that part of the object, and fills in the page table entry.
3. **At every later touch.** The CPU translates the address through the page table by itself. The kernel is not involved.

With two processes, each calls `mmap()` for itself and gets its own pointer. The values differ; the frame behind them is the same:

```
   producer                                      consumer
   ptr = 0x7f12a000                              ptr = 0x7f88c000

   page table                                    page table
   +---------------+                             +---------------+
   | 0x7f12a -> 57 |--------+           +--------| 0x7f88c -> 57 |
   +---------------+        |           |        +---------------+
                            v           v
                      +---------------------+
                      |  physical frame 57  |
                      |  H e l l o W o r l d ! \n \0 ...
                      +---------------------+
                                 ^
                                 |
                    shared memory object "OS"
```

One consequence: a pointer value cannot be passed to the other process through shared memory. An address only means something inside the process that got it from `mmap()`. To refer to a place inside the region, store an offset from its start.

### When physical memory is actually used

| After this step | What exists | RAM used? |
| --- | --- | --- |
| `shm_open()` | A named object and a descriptor | No, the size is 0 |
| `ftruncate()` | An object with a size. A limit, not a reservation | No |
| `mmap()` | A pointer and a reserved address range with empty page table entries | No |
| First access to a page | Page fault, frame assigned, entry filled | Yes, one page at a time |

## Seeing the object from the shell

On Linux, POSIX shared memory objects are files in `/dev/shm`, a file system that lives in RAM. After `./producer` and before `./consumer`:

```
$ ls -l /dev/shm/OS
-rw-rw-r-- 1 anees anees 4096 Oct 13 14:34 /dev/shm/OS

$ od -c /dev/shm/OS | head -3
0000000   H   e   l   l   o   W   o   r   l   d   !  \n  \0  \0  \0  \0
0000020  \0  \0  \0  \0  \0  \0  \0  \0  \0  \0  \0  \0  \0  \0  \0  \0
*
```

- The size is the 4096 set by `ftruncate()`.
- The permissions are `0666` with the umask applied.
- The first 12 bytes are the message, newline included, and everything after it is zero.
- After `./consumer` the file is gone, because of `shm_unlink()`.

This also explains why `shm_open()` returns a file descriptor and why the size is set with `ftruncate()`, a call meant for files: to the kernel, the object is a file that happens to be kept in memory.

## Lifetime and cleanup

A shared memory object has **kernel persistence**: it exists until it is unlinked or the machine reboots. It does not disappear when the process that created it exits. This is what lets the producer finish completely before the consumer starts.

| Call | Removes | Notes |
| --- | --- | --- |
| `close(fd)` | The descriptor | The mapping keeps working. |
| `munmap(ptr, SIZE)` | This process's mapping | Other processes' mappings are unaffected. |
| `shm_unlink(name)` | The name | The memory is freed once no process has the object open or mapped any more. |

When a process exits, its mappings and descriptors are released automatically. The name is not: without `shm_unlink()` the object stays in `/dev/shm`, and the next run of the producer finds the old contents. A leftover object can be removed by hand with `rm /dev/shm/OS`.

## Details worth noticing in the code

- **Two `sprintf()` calls, one string.** `sprintf()` writes the characters and then a terminating `'\0'`. `ptr += strlen(msg_1)` moves the pointer by 5, onto that terminator, so the second `sprintf()` overwrites it with the `W` of `World!`. The result is a single string, `HelloWorld!\n`, with no space between the two words, which is exactly what the consumer prints.
- **The producer's `ptr` no longer points to the start.** After the two `ptr +=` lines it points 12 bytes in. Anything that needs the start of the region later, `munmap()` for example, needs a copy of the original pointer.
- **Arithmetic on a `void *`.** Standard C does not define `ptr += n` for a `void *`, since the size of what it points to is unknown. GCC allows it as an extension and counts in bytes. A `char *` does the same thing portably.
- **The producer maps with `PROT_WRITE` only**, where the textbook example uses `PROT_READ | PROT_WRITE`. On x86 a writable page is readable as well, so both behave the same here. The descriptor's access mode has to cover the protection asked for: a writable shared mapping needs `O_RDWR`, and the consumer's `O_RDONLY` goes with `PROT_READ`.
- **The name.** POSIX specifies the portable form as a leading slash followed by characters that are not slashes: `"/OS"`. Linux accepts `"OS"` as well.
- **Headers.** `shm_open()`, `mmap()` and the `PROT_`/`MAP_` constants come from `<sys/mman.h>`, the `O_` constants from `<fcntl.h>`, the mode constants from `<sys/stat.h>` and `ftruncate()` from `<unistd.h>`. `<sys/shm.h>` belongs to the older System V API (`shmget()`, `shmat()`), which is a different interface to the same idea.

## Common confusions

| Wrong idea | Correct idea |
| --- | --- |
| `mmap()` gives an address to the descriptor. | `mmap()` maps the **object** into the **process**. The descriptor only says which object. |
| `ftruncate()` allocates memory. | `ftruncate()` only sets the size. RAM arrives later, on first touch. |
| `mmap()` fills in the page table straight away. | `mmap()` reserves the range. Entries are filled page by page on first access. |
| Both processes share the same pointer. | Each has its own pointer. They share the frames and the name. |
| The data is gone when the producer exits. | The object lives until `shm_unlink()` or a reboot. |
| A word is an address. | A word is data. An address is a location. |

## Error checking

Every call used here can fail, and none of them stops the program when it does. A failed call returns a special value and execution carries on with the next line.

### Whether it failed, and why

Two separate questions with two separate sources:

| Question | Answered by |
| --- | --- |
| Did the call fail? | Its **return value** |
| Why did it fail? | **`errno`**, which `perror()` turns into text |

```c
int shm_fd = shm_open(name, O_RDONLY, 0666);
if (shm_fd == -1) {
    perror("shm_open");
    return 1;
}

void *ptr = mmap(0, SIZE, PROT_READ, MAP_SHARED, shm_fd, 0);
if (ptr == MAP_FAILED) {
    perror("mmap");
    return 1;
}
```

- **Test the return value, not a particular `errno` code.** `shm_open()` can fail for many reasons: `ENOENT` (no such object), `EACCES` (no permission), `EMFILE` (too many open descriptors) and more. A test like `errno == ENOENT` handles one of them and lets every other failure through. The return value is -1 for all of them.
- **`errno = 0` is not needed here.** That reset belongs to `strtol()`, where every return value is also a legitimate result and `errno` is the only signal. `shm_open()` and `mmap()` have a return value that can mean nothing else.
- **`MAP_FAILED` is not `NULL`.** It is `(void *) -1`, the highest possible address. `if (ptr == NULL)` or `if (!ptr)` never fires for a failed `mmap()`.
- **`return` after `perror()`.** `perror()` only prints. Without the `return`, the program reports the error and then carries on with the bad value anyway.
- **`perror()` adds the colon itself.** It prints its argument, then `": "`, then the reason. `perror("shm_open")` gives `shm_open: No such file or directory`; a label that already ends in a colon gives two.

### How a missing check becomes a segmentation fault

With no checks at all, running the consumer when the object does not exist went like this:

| Line | What happened | Crash? |
| --- | --- | --- |
| `shm_open(name, O_RDONLY, 0666)` | No object called `"OS"`. Returns -1, `errno` = `ENOENT`. | No |
| `mmap(0, SIZE, PROT_READ, MAP_SHARED, -1, 0)` | -1 is not a descriptor. Returns `MAP_FAILED`, `errno` = `EBADF`. | No |
| `printf("%s", (char *)ptr)` | Tries to read a string at address `(void *) -1`. | **Segmentation fault** |

Two things to take from the table:

- The crash is two lines away from the cause. A segmentation fault says where a bad value was **used**, not where it was produced.
- A failure that is not caught at its source gets a different description further down. The real problem is "no such object", but by the time `mmap()` fails the reason has become "Bad file descriptor", which says nothing about a missing producer. Each call needs its own check, directly after it.

### Making the failures happen

A check that has never fired is untested. These runs trigger each one:

| To make this fail | Do this | Reason reported |
| --- | --- | --- |
| `shm_open()` in the consumer | Run `./consumer` without running `./producer` first | No such file or directory |
| `shm_open()`, a second reason | `./producer`, then `chmod 000 /dev/shm/OS`, then `./consumer` | Permission denied |
| `mmap()` in the consumer | `./producer`, then temporarily ask for `PROT_WRITE` on the `O_RDONLY` descriptor | Permission denied |

Remove the leftover object afterwards with `rm /dev/shm/OS`.

### Where the programs stand

| Call | consumer.c | producer.c |
| --- | --- | --- |
| `shm_open()` | check written, but its condition never fires yet | not checked |
| `ftruncate()` | not used | not checked |
| `mmap()` | checked | not checked |
| `shm_unlink()` | not checked | not used |

## Limits of these programs

- **The producer checks nothing yet.** If its `shm_open()` fails, for example because an object with that name exists and is not writable, it ends in a segmentation fault at the first `sprintf()`.
- **Nothing is unmapped or closed.** The kernel releases both at exit, so nothing leaks, but the calls are missing.
- **No synchronisation.** The operating system does not control what goes into shared memory or when. Here the two processes never run at the same time, so the question does not come up. As soon as they do, the consumer could read a half-written message, and it is up to the programs to prevent that. Chapter 6 of the book is about exactly this problem.

## Function reference

| Function | Header | Purpose | Returns on failure |
| --- | --- | --- | --- |
| `shm_open(name, oflag, mode)` | `<sys/mman.h>` | Create or open a shared memory object | `-1` |
| `ftruncate(fd, length)` | `<unistd.h>` | Set the size of the object | `-1` |
| `mmap(addr, length, prot, flags, fd, offset)` | `<sys/mman.h>` | Map the object into the address space | `MAP_FAILED` |
| `munmap(addr, length)` | `<sys/mman.h>` | Remove a mapping | `-1` |
| `shm_unlink(name)` | `<sys/mman.h>` | Remove the object's name | `-1` |
| `close(fd)` | `<unistd.h>` | Close the descriptor | `-1` |

All of them set `errno` on failure, so `perror()` can say why. `man 7 shm_overview` describes how the calls fit together.

## Summary

- **IPC** is the goal: two processes exchange data through one region of physical memory.
- The **file descriptor** is a handle used only during setup. The object's **name** is what the two processes have in common.
- **Virtual memory** makes it work. `mmap()` reserves an address range in each process and records which object it belongs to. On the first touch of each page, a page fault fills that process's page table entry with the frame that holds the object's data. Both processes' entries lead to the same frames, so a write by one is seen by the other.
- In one line: **`shm_open()` names it, `ftruncate()` sizes it, `mmap()` reserves addresses for it, and physical pages are linked in on first touch.**
