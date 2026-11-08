# q_8: a concurrent echo server that counts its clients

**Task:** the servers in q_5 to q_7 are *iterative*: they `accept()` one client, serve it completely, and only then accept the next, so one slow client holds up everyone behind it. This one is *concurrent*: it forks a child for every client, and the parent goes straight back to `accept()`. A count of clients served is kept in POSIX shared memory, and the server must not leave zombies behind. The full question is in [claude_practice_question.docx](claude_practice_question.docx).

| File | Role | What it does |
| --- | --- | --- |
| [concurrent_server.c](concurrent_server.c) | Server | Listens on port 5576. Forks a child per client; the child runs the q_7 echo loop, increments the shared counter and prints `Served Client #N (pid X)`. A `SIGCHLD` handler in the parent reaps children as soon as they exit. |
| [slow_client.c](slow_client.c) | Client | Not finished yet (see [Where we stopped](#where-we-stopped)). The tests below used `nc` instead. |

**How it works:**

| Order | Process | Step |
| --- | --- | --- |
| 1 | parent | Registers `reap_children()` for `SIGCHLD` with `sigaction()` and `SA_RESTART`. |
| 2 | parent | `socket()`, `setsockopt(SO_REUSEADDR)`, `bind()`, `listen()`, once each. |
| 3 | parent | `shm_open()`, `ftruncate()` to `sizeof(struct shared)` (a counter and a `sem_t`), `mmap()` with `MAP_SHARED`, counter set to 0, `sem_init(&lock, 1, 1)`. This happens **before** any `fork()`, so every child inherits the same mapping. |
| 4 | parent | `accept()` blocks until a client connects, then `fork()`. |
| 5 | parent | `close(client_socket)` right away and back to 4. The child has its own reference to the connection. |
| 6 | child | Echo loop: `recv()` → `send()` until `recv()` returns 0. Then `close(client_socket)`; `sem_wait()`, increment the counter, print, `sem_post()`; `return 0`. |
| 7 | kernel → parent | The child's exit sends `SIGCHLD`. The handler interrupts `accept()`, calls `waitpid(-1, &status, WNOHANG)` in a loop, prints how each child ended, and `accept()` restarts because of `SA_RESTART`. |

## Running it

```sh
gcc -Wall -Wextra -o concurrent_server concurrent_server.c
./concurrent_server                    # terminal 1
nc -N localhost 5576                   # terminal 2: type a line, Ctrl+D ends it
for i in $(seq 20); do echo hi | nc -N localhost 5576 > /dev/null & done; wait   # 20 clients at once
```

`nc` takes only a host and a port: `nc localhost 5576 hi hello` fails with `port number invalid: hi`. Without `-N`, `nc` does not `shutdown()` its side on Ctrl+D, so the server never sees EOF and both wait forever.

## Experiments

### 1. The parent kept every client's socket open

The first version did not close `client_socket` in the parent. A client that pressed Ctrl+D never got its prompt back, even with `nc -N`. The parent's open files, after four `nc` sessions of which three had already ended:

```
$ ls -l /proc/$(pgrep -o concurrent_serv)/fd
0 -> /dev/pts/0
1 -> /dev/pts/0
2 -> /dev/pts/0
3 -> socket:[1754677]        listening socket, created first
4 -> /dev/shm/COMMONs        shm_fd, still open after mmap()
5 -> socket:[1754678]        client 1   (finished)
6 -> socket:[1765795]        client 2   (finished)
7 -> socket:[1765818]        client 3   (finished)
8 -> socket:[1766038]        client 4   (still connected)
```

After `fork()` there are two descriptors for each connection, and the kernel keeps a reference count on the socket. The child's `close()` only takes it from 2 to 1, so the connection never closes and the client never sees EOF. Every client also costs the parent one descriptor forever, until `accept()` fails at the `ulimit -n` limit.

With `close(client_socket)` in the parent straight after `fork()`, the same listing taken while a client was connected showed only fd 3, and `nc -N` exited on its own after Ctrl+D.

`pgrep concurrent_server` found nothing: the kernel keeps only the first 15 characters of a process name, so it is stored as `concurrent_serv`.

### 2. Zombies, with no reaping

Three clients connected and disconnected:

```
$ ps -el | grep concurrent
F S   UID   PID  PPID ... WCHAN  ...  CMD
0 S  1000 57704 22474 ... inet_c ...  concurrent_serv
1 Z  1000 57723 57704 ...   -    ...  concurrent_serv <defunct>
1 Z  1000 57731 57704 ...   -    ...  concurrent_serv <defunct>
1 Z  1000 57747 57704 ...   -    ...  concurrent_serv <defunct>
```

- `Z` and `<defunct>`: the children have exited, but nobody has read their exit status (as in q_1).
- `WCHAN inet_c` (`inet_csk_accept`): the parent is asleep inside `accept()`, so it is not running any code that could reap them.
- `F 1`: forked but never called `exec`.

### 3. Reaping after `accept()`: one zombie is always left

The first fix was a reaping loop at the end of the parent's loop, after `close(client_socket)`:

```c
while (waitpid(-1, NULL, WNOHANG) > 0);
```

With clients connecting one after another, `ps` always showed **exactly one** zombie:

| Event | `accept()` returns, the loop runs | Zombie after that client leaves |
| --- | --- | --- |
| client 1 connects | nothing to reap | child 1 |
| client 2 connects | reaps child 1 | child 2 |
| client 3 connects | reaps child 2 | child 3 |

The loop only runs when `accept()` returns, which is when the next client arrives. If nobody connects for an hour, the last child stays a zombie for an hour. It has to be a `while` loop, because several children can exit between two connections and each `waitpid()` reaps only one. A zombie shows in `ps -f` as `[concurrent_serv]` in brackets: there is no memory left to read its command line from.

### 4. Reaping in a `SIGCHLD` handler

The kernel sends `SIGCHLD` to the parent whenever a child exits. The handler runs immediately, even while the parent is blocked in `accept()`, and reaps there:

```
Child process with pid 68338 exited with status 0
Child process with pid 68345 exited with status 0
Child process with pid 68363 exited with status 0
```

Each line appeared the moment its client disconnected, without another client connecting. `ps` showed only the parent (`inet_c`, in `accept()`) and the child of a client that was still connected (`wait_w`, blocked in `recv()`). No zombies.

### 5. 200 clients: the counter looked right

```
Served Client #1 (pid 70613)
...
Served Client #6 (pid 70627)
Served Client #7 (pid 70626)
...
Served Client #184 (pid 71161)
Served Client #183 (pid 71159)
...
Served Client #200 (pid 71201)
```

Every number from 1 to 200 appeared once. Two things to read from it:

- **`#N` is the order children reach the increment, not the order clients connected.** PIDs are handed out in order, so pid 70626 was forked before 70627, yet it got #7.
- **The clients barely overlapped.** `echo hi | nc` is so quick that almost every child finished before the next one started, so two children were almost never inside the increment at the same time.

### 6. Widening the window: lost updates

`*shared += 1` was split into its three steps with a sleep in the middle. It does the same thing as before, just slowly:

```c
long int temp = *shared;
usleep(1000);
*shared = temp + 1;
printf("Served Client #%ld (pid %d)\n", temp, getpid());
```

20 clients at once:

| Printed | Children |
| --- | --- |
| #0 | 7 |
| #1 | 5 |
| #2 | 5 |
| #3 | 3 |

20 clients served, counter at **4**. Seven children all read 0 into `temp` before any of them wrote back, and each then wrote 0 + 1 = 1 over the others' work. Six increments disappeared. The next groups did the same with 1, 2 and 3. (It printed `#0` rather than `#1` because it prints `temp`, the value from *before* the increment.)

### 7. `*shared += 1` in assembly

A one-line function, `void inc(long *shared) { *shared += 1; }`, compiled with `gcc -S`:

```
-O0:
movq  -8(%rbp), %rax      # load the pointer `shared`
movq  (%rax), %rax        # read: load the value it points to
leaq  1(%rax), %rdx       # add 1
movq  -8(%rbp), %rax      # load the pointer again
movq  %rdx, (%rax)        # write: store the result back

-O2:
addq  $1, (%rdi)
```

At `-O0` it is the same read, add and write as experiment 6, only with a gap of nanoseconds instead of 1 ms. The process can be switched out between any two instructions, and on several cores two children can run the read at the same moment. Even the single `-O2` instruction still reads, adds and writes memory inside the CPU; without the x86 `lock` prefix, two cores can interleave it.

**So experiment 5 was lucky, not correct.** The `usleep` did not add a bug; it made an existing one easy to see.

### 8. The fix: a semaphore in shared memory

The counter and a semaphore now live together in one struct in the mapped region:

```c
struct shared {
    long int counter;
    sem_t lock;
};
```

After `mmap()` and before the first `fork()`, the parent points `shared_struct` at the region, sets the counter to 0, and calls `sem_init(&shared_struct->lock, 1, 1)`. The second argument, `pshared = 1`, makes it shared between processes. The third argument, an initial value of 1, lets one process in at a time. Each child wraps the read, add, write and print in `sem_wait()` / `sem_post()`.

20 clients, **with the `usleep(1000)` still inside**:

```
Served Client #0 (pid 84713)
[Parent] Child 84713 exited with status 0
Served Client #1 (pid 84716)
...
Served Client #19 (pid 84762)
[Parent] Child 84762 exited with status 0
```

Twenty different numbers, each exactly once and in order. Experiment 6 gave four numbers for twenty clients. (It still prints `temp`, the value before the increment, so it counts from #0.)

Why each choice:
- **The `sem_t` must be inside the shared region.** A `sem_t` declared as a local in `main()` would be copied by `fork()`, so each child would lock its own copy (test_2 again).
- **A struct rather than `(char *)ptr + 8`.** `sizeof(*shared_struct)` gives the size, and the compiler handles offsets and alignment. `sizeof(sem_t)` is 32 bytes here, but that varies between platforms.
- **`sem_init()` after `mmap()`.** Its first argument is the semaphore's address, which does not exist until the region is mapped. It runs once, before any child exists.
- **`sizeof(*shared_struct)` is safe before `mmap()`.** `sizeof` never evaluates its operand; the compiler takes the size from the type. `shared_struct->counter = 0` at that point would write through an uninitialised pointer.
- **The `printf` is inside the lock**, so the output comes out in order. It does not have to be: `temp` is a local, so printing it outside would still give the right number, only possibly out of order. The smallest critical section is just the read-add-write; a slow terminal write inside it makes the other children wait.

## Bugs hit along the way

**`listen()` inside the loop.** `listen()` marks the socket as accepting connections. It belongs once, before the loop. It does not wait for a client; `accept()` does.

**`shm_unlink(shm_fd)`.** `shm_unlink()` takes the object's **name**, not a descriptor. `-Wall -Wextra` warned about it.

**`mmap(90, ...)`.** The first argument is a hint for where to place the mapping. Pass `NULL` to let the kernel choose.

**The parent never closed `client_socket`.** See experiment 1: clients hung after Ctrl+D and the parent leaked one descriptor per client.

**`return 0; // return back to parent`.** The child is running `main()`, so `return` ends the child process. Nothing goes back to the parent; the exit status waits in the process table, as a zombie, until the parent calls `waitpid()`.

**`stop_recv = 1; return 1;`.** The flag was set and then the function returned, so the check after the send loop could never see it. `break` makes the flag do its job.

**`sprintf(buffer, sizeof(buffer), ...)`.** That is `snprintf`'s argument list. `sprintf` took the number 128 as the address of the format string. The compiler said so: `passing argument 2 of 'sprintf' makes pointer from integer without a cast`. A file that compiles is not a file without warnings.

**`write(STDOUT_FILENO, buffer, sizeof(buffer))`.** Always wrote 128 bytes, most of them garbage after the message. `strlen(buffer)`, or `snprintf`'s return value, gives the real length.

**Raw `status` printed as the exit code.** `status` encodes how the child ended, not just its exit code. `WIFEXITED`/`WEXITSTATUS` and `WIFSIGNALED`/`WTERMSIG` take it apart.

**Unused parameter `sig`.** The handler must take an `int`; `(void) sig;` tells the compiler that ignoring it is deliberate.

**`%d` for a `long`.** `*shared` is a `long int`, so it needs `%ld`.

**Missing `\n`.** Without it every `Served Client` line ran straight into the next `[Parent] Child ... exited` line.

**Testing an unsaved file.** Twice the change was in the editor but not on disk, so the compiler still reported the old code. Save, compile, then run.

## Learnings

- **A concurrent server is `accept()` + `fork()`.** The parent only accepts; each child serves one client. Three slow clients no longer wait for each other.
- **`fork()` duplicates descriptors, and the kernel counts references.** A connection closes only when every descriptor for it is closed. The parent closes the client socket right after `fork()`; the child closes the listening socket it does not need.
- **Set up shared memory before `fork()`.** The child inherits the `MAP_SHARED` mapping, so both point to the same physical page. A normal variable would be copied (test_2).
- **`waitpid()` returns three kinds of answer:** a pid (one child reaped), `0` (with `WNOHANG`: children exist, none finished), `-1` (error, or no children left). Loop while it returns `> 0`.
- **Where you reap decides how long zombies live.** After `accept()` they live until the next client. In a `SIGCHLD` handler they are reaped as soon as they exit.
- **Signals of the same kind do not queue.** Three children exiting together can produce one `SIGCHLD`, so the handler must loop.
- **Use `sigaction()`, not `signal()`.** `signal()`'s behaviour differs between systems (`man 2 signal`, PORTABILITY). Register the handler once, before the first `fork()`.
- **A signal interrupts blocking calls.** Without `SA_RESTART`, `accept()` would return `-1` with `errno == EINTR` every time a child exits, and this server treats that as fatal.
- **Only some functions are safe inside a handler** (`man 7 signal-safety`). `write()` is; `printf()` is not, and strictly neither is `snprintf()`.
- **`x += 1` is read, add, write.** If two processes can do those steps at the same time, updates are lost: a **race condition**. The read-add-write is a **critical section**.
- **A lock shared by processes must itself be in shared memory.** `sem_init(&sem, 1, 1)`: `pshared` nonzero, initial value 1, with the `sem_t` inside the mapped region. Only one process at a time gets past `sem_wait()` until it calls `sem_post()`.
- **A passing test does not prove there is no race.** It shows the race did not happen that time. Widening the window (`usleep`) makes it visible; `gcc -S` shows the separate steps.
- **Debugging commands used:** `ls -l /proc/<pid>/fd` (what a process has open), `ps -el` (the `S`, `PPID`, `WCHAN` and `F` columns), `pgrep -o`, `gcc -S`.

## Where we stopped

The race is fixed (experiment 8). What is left is tidying up.

**Loose ends in the code as it is now:**

- Print `temp + 1` so the first client is #1, and remove the `usleep(1000)` now that the lock has been tested with it.
- The child does not yet `close(server_socket)`; there is only a comment for it.
- `sem_destroy()` and `shm_unlink()` are never called, because the server only stops on Ctrl+C. What would it take to clean up on Ctrl+C (`SIGINT`)?
- Try a `pthread_mutex_t` with `PTHREAD_PROCESS_SHARED` (`man pthread_mutexattr_setpshared`) in place of the semaphore, for comparison.
- `fork()` is not checked for `-1`.
- The commented-out loop on line 218 is `while(waitpid(-1,NULL,WNOHANG));`, without `> 0`. What happens when `waitpid()` returns `-1`?
- Does the parent still need `shm_fd` (fd 4) after `mmap()`? And who calls `shm_unlink()` when the server stops normally? Check `ls /dev/shm`.
- Optional: the handler's `snprintf` is not on the signal-safety list. How could the printing be done without it?

**Still to do from the question:**

- Finish `slow_client.c`: connect, send a line, `sleep(10)`, send another, `shutdown(SHUT_WR)`, print the echo. In the current draft, compare `argv[i] + 2 > BUFFER_SIZE` with the length check in q_7, and check how many bytes `send()` is given.
- Experiment 1 from the question: start 3 slow clients at once against this server and against q_7. Do they finish in about 10 s or about 30 s?
