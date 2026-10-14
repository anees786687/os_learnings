# Operating Systems Practice in C

C programs written while studying operating systems. The repository follows *Operating System Concepts* by Silberschatz, Galvin and Gagne, the text for the OS course at BITS: it holds solutions to the book's programming exercises, plus small standalone programs that each isolate one idea.

Every program carries a comment block explaining what it demonstrates and what was learned from it. Each chapter directory has its own README with the output of every exercise and the bugs hit along the way.

Planned next: the projects and labs from *Operating Systems: Three Easy Pieces* (OSTEP).

The programs use POSIX system calls, so they need Linux, macOS or WSL.

## Building and running

```sh
make            # builds every program, each next to its source file
cd standalone
./test_1
./test_4 5      # test_4 needs an integer argument
cd ..
make clean      # removes the executables
```

The Makefile picks up every `.c` file in this directory and up to two directories below it, so a new program only has to be dropped into a folder.

To build a single program by hand: `gcc -Wall -Wextra -o standalone/test_1 standalone/test_1.c`

## Layout

| Directory | Contents |
| --- | --- |
| [standalone/](standalone/) | Self-contained programs on process creation, waiting and `exec` |
| [standalone/shared_memory/](standalone/shared_memory/) | A producer and a consumer that communicate through POSIX shared memory. The lesson is in its [README](standalone/shared_memory/README.md) |
| [standalone/socket_programming/](standalone/socket_programming/) | A TCP server and client that talk over a socket on port 9002: `socket`, `bind`, `listen`, `accept`, `connect`, `send`, `recv`. The lesson is in its [README](standalone/socket_programming/README.md) |
| [chapter_3_processes/](chapter_3_processes/) | Silberschatz chapter 3 programming exercises: zombie process, pid manager, Collatz sequence in a child process, the same sequence passed to the parent through shared memory, and a quote-of-the-day client for a systemd service on port 17. Details in its [README](chapter_3_processes/README.md) |

Directories are named after the textbook chapter they belong to. The rest of this page covers the standalone programs and the concepts behind them.

## Standalone programs

Read them in order; each one builds on the previous.

| File | Topic | What it shows |
| --- | --- | --- |
| [test_1.c](standalone/test_1.c) | `fork()` basics | One call to `fork()` produces two processes, and the code after it runs in both. |
| [test_2.c](standalone/test_2.c) | Separate address spaces | A variable changed in the child keeps its old value in the parent. |
| [test_3.c](standalone/test_3.c) | `wait()`, `getpid()`, `getppid()` | The parent blocks until the child is done, so the child's output always comes first. |
| [test_4.c](standalone/test_4.c) | `fork()` + `exec`, command line arguments | The child is replaced by a different program and is handed an argument. |
| [test_4_1.c](standalone/test_4_1.c) | `strtol()`, input validation | The program that `test_4` runs in its child: it counts from 1 to the given number. |

### Sample output

`./test_1`: the parent prints the child's pid, the child prints 0.

```
process id 11047
process id 0
```

`./test_2`: the child's change to `x` is not visible in the parent.

```
Parent x = 10
Child x = 20
```

`./test_3`: the child's PPID equals the parent's PID, and the parent only starts after the child has finished.

```
Task to print even numbers
PID 11051, PPID 11050, i 0
PID 11051, PPID 11050, i 2
...
Task to print odd numbers
PID 11050, PPID 11018, i 1
PID 11050, PPID 11018, i 3
...
```

`./test_4 3`: the counting is done by `test_4_1`, running inside the child.

```
The integer provided is 3
Counting...
1
2
3
```

## Concepts

### Creating a process: `fork()`

`fork()` duplicates the calling process. The original is the parent, the copy is the child, and both continue from the line after the call. It is called once but returns twice:

| Return value | Where | Meaning |
| --- | --- | --- |
| `> 0` | parent | pid of the new child |
| `0` | child | "I am the child" |
| `-1` | parent | fork failed, no child exists |

This return value is the only way to tell the two processes apart, which gives the usual pattern:

```c
pid_t pid = fork();
if (pid < 0) {
    perror("fork");      /* failed */
} else if (pid == 0) {
    /* child's work */
} else {
    /* parent's work */
}
```

The scheduler decides which of the two runs first, so their output order is not fixed unless the parent waits.

### Separate address spaces

The child starts with a copy of the parent's memory. After the fork the two are independent: writing to a variable in one process does not affect the other. Processes that need to share data must use an explicit mechanism for it. One such mechanism is shared memory, where two processes map the same physical pages: see [standalone/shared_memory/](standalone/shared_memory/README.md). Another is message passing over sockets, which also works between processes on different machines: see [standalone/socket_programming/](standalone/socket_programming/README.md).

### Waiting for a child: `wait()`

`wait(NULL)` blocks the parent until one of its children terminates. It is how the parent imposes an order ("child first, then me"). The argument is an `int *` that receives the child's exit status; `NULL` means the status is not needed.

### Process IDs

Every process has a pid and knows its parent's pid. For a program started from a terminal, the parent is the shell.

### Running another program: the `exec` family

`exec` replaces the program running in the current process with a new one. The pid stays the same; the code, data and stack are replaced. Two consequences:

- `exec` does not return on success. Code after it runs only if the call failed.
- To run another program without losing the current one, `fork()` first and call `exec` in the child.

The variant used here is `execlp`:

```c
execlp("./test_4_1", "test_4_1", num_str, (char *)NULL);
```

- `l`: arguments are passed as a list, terminated by a `NULL` pointer.
- `p`: the file is looked up in `PATH`, unless the name contains a `/`.
- The second parameter becomes `argv[0]` of the new program, by convention its name.

### Command line arguments: `argc` and `argv`

`int main(int argc, char *argv[])`

- `argc` is the number of arguments, always at least 1.
- `argv[0]` is the program name, `argv[1]` the first user argument, `argv[argc - 1]` the last one.
- `argv[argc]` is guaranteed to be `NULL`.
- Arguments are always strings. Numbers have to be converted.

`argc` has to be checked before any `argv[i]` beyond `argv[0]` is used:

```c
if (argc != 2) {
    printf("Usage: %s <positive integer>\n", argv[0]);
    return 1;
}
```

- With no user argument `argc` is 1, so `argv[1]` is `argv[argc]`, the `NULL` pointer. Passing it to `strtol()` is a segmentation fault.
- `argc != 2` accepts exactly one user argument. `argc < 2` only catches too few and lets extra arguments through unnoticed.
- `argv[0]` in the message prints the name the program was run as, so the usage line stays right if the executable is renamed.
- The check goes at the top of `main`, above the first line that reads `argv[1]`.
- Compiling with `-Wall -Wextra` gives `unused parameter 'argc'` when a program reads `argv` without ever looking at `argc`, which is a sign the check is missing.

### Converting a string to a number: `strtol()`

`long strtol(const char *str, char **endptr, int base)` converts and also reports where it stopped, which makes validation possible:

| After the call | Meaning |
| --- | --- |
| `endptr == str` | no digits were read, there is no number |
| `*endptr != '\0'` | a number was read but is followed by other characters (`"12abc"`) |
| `*endptr == '\0'` | the whole string was a number |

`endptr` does not catch a number that is too large for a `long`. That is reported through `errno`, covered next.

### Detecting errors with `errno`

`errno` is a global `int` provided by `<errno.h>`. Library functions and system calls store an error code in it when they fail. It is not declared by the program, only used:

```c
errno = 0;                                   /* clear any old error */
long number = strtol(str, &endptr, 10);
if (errno == ERANGE) {
    /* the number does not fit in a long */
}
```

- **Why the return value is not enough.** On overflow `strtol()` returns `LONG_MAX` (or `LONG_MIN`), but `LONG_MAX` is also the correct result for the string `"9223372036854775807"`. Only `errno` tells the two apart.
- **Why it is set to 0 first.** A function writes to `errno` when it fails and leaves it alone when it succeeds. Without the reset, a value left behind by an earlier call would be mistaken for a failure of this one.
- **Check it straight away.** Any library call in between, a `printf` included, may change `errno`.
- **`ERANGE`** is the code for "result out of range". Codes are named constants from `<errno.h>`; another common one is `EAGAIN`, which a failed `fork()` sets.
- **`perror(msg)`** reads `errno` and prints `msg` followed by the matching description, e.g. `fork: Resource temporarily unavailable`.

## Function reference

| Function | Header | Purpose |
| --- | --- | --- |
| `fork()` | `<unistd.h>` | Create a child process |
| `getpid()` | `<unistd.h>` | Pid of the calling process |
| `getppid()` | `<unistd.h>` | Pid of the parent process |
| `sleep(seconds)` | `<unistd.h>` | Suspend the calling process |
| `execlp(file, arg0, ..., NULL)` | `<unistd.h>` | Replace the current program with another |
| `wait(&status)` | `<sys/wait.h>` | Block until a child terminates |
| `strtol(str, &endptr, base)` | `<stdlib.h>` | Convert a string to a `long` |
| `perror(msg)` | `<stdio.h>` | Print why the last system call failed |
| `errno` | `<errno.h>` | Error code of the last failed call; a variable, not a function |
| `close(fd)` | `<unistd.h>` | Release a file descriptor: a file, a shared memory object or a socket |

`pid_t`, the type used for process IDs, comes from `<sys/types.h>`. The socket calls (`socket`, `bind`, `listen`, `accept`, `connect`, `send`, `recv`) are listed in the [socket_programming README](standalone/socket_programming/README.md#function-reference).

## Things that are easy to get wrong

- Forgetting that code after `fork()` runs in both processes.
- Expecting a variable changed in the child to change in the parent.
- Leaving out the terminating `(char *)NULL` in `execlp`.
- Writing code after `exec` and expecting it to run on success.
- Running `test_4` from another directory (e.g. `./standalone/test_4 5`): it loads `./test_4_1`, a path relative to the current directory, so `cd standalone` first.
- Not checking the return value of `fork()` and `exec` for failure.
- Checking `errno` without setting it to 0 before the call, or after another library call has had the chance to change it.
- Testing `strtol()`'s return value against `LONG_MAX` to detect overflow: `LONG_MAX` is also a valid result.
- Passing a value where a system call expects a pointer, e.g. `sizeof(addr)` as the last argument of `accept()`. The compiler only warns (`makes pointer from integer without a cast`), so compile with `-Wall -Wextra` and read the warnings.
- Calling a function without including its header (`implicit declaration of function`), e.g. `close()` without `<unistd.h>`. `man 2 <name>` lists the header in its SYNOPSIS.
- Reading `argv[1]` without checking `argc` first: with no argument it is `NULL`, and the program crashes instead of printing an error.
