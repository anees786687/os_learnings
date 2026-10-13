# Chapter 3: Processes

Solutions to the programming exercises of the Processes chapter, with what each one taught.

## Programs

| File | Topic | What it shows |
| --- | --- | --- |
| [q_1.c](q_1.c) | Zombie processes | A child that has exited stays in the process table until its parent calls `wait()`. |
| [q_2.c](q_2.c) | Pid manager, bitmaps | Tracking which PIDs are in use with one bit per PID. |
| [q_3.c](q_3.c) | Collatz sequence, `fork()` + `wait()` | Work done in a child stays in the child's copy of the data, so the child has to print it. |
| [q_4.c](q_4.c) | Collatz sequence through shared memory | A child hands its results to the parent through a region both have mapped, and the parent checks how the child ended before using them. |

## q_1: creating a zombie process

**Task:** fork a child that becomes a zombie and stays one for at least 10 seconds, then confirm it with `ps -l`.

**How it works:** the child prints a few numbers and exits straight away. The parent sleeps for 10 seconds and never calls `wait()`, so for those 10 seconds nobody collects the child's exit status.

### Running it

```sh
make            # from the repository root, or: gcc -Wall -Wextra -o q_1 q_1.c
./q_1 &         # background, so the shell gives the prompt back
ps -l           # within 10 seconds, in the same terminal
```

### Output

![Output of q_1 followed by ps -l, showing the child as a zombie](images/q_1.png)

Reading the `ps -l` table:

| PID | PPID | S | CMD | What it is |
| --- | --- | --- | --- | --- |
| 15793 | 8742 | `S` | `q_1` | The parent, asleep in `sleep(10)`. Its parent is the shell. |
| 15795 | 15793 | `Z` | `q_1 <defunct>` | The child. It has exited and is a zombie; its PPID is the parent's PID. |

### Learnings

- **What a zombie is.** When a process terminates, its memory is released but its entry in the process table stays, because that entry holds the exit status the parent is expected to read. Between the child's exit and the parent's `wait()`, the child is a zombie.
- **How to make one.** Let the child finish first and keep the parent alive without calling `wait()`.
- **How to recognise one.** In `ps -l` the `S` column shows `Z` and the command is marked `<defunct>`. `SZ` is 0, since the process no longer holds any memory.
- **`ps -l` is per terminal.** It lists only the processes attached to the terminal it is run in. From a different terminal the zombie does not show up; use `ps -el` there to list every process on the system.
- **Run it with `&`.** In the foreground the shell only returns the prompt after the parent has exited, and by then the zombie is gone.
- **A zombie cannot be killed.** It is already dead, so signals sent to it do nothing. It disappears in one of two ways:
  - the parent calls `wait()`, or
  - the parent terminates. The zombie is then adopted by `init` (`systemd`), which calls `wait()` for it.
- **Cleaning up.** `kill <parent pid>` removes the zombie for the second reason. In this program the parent also exits on its own after 10 seconds.
- **Why zombies matter.** Each one occupies a process table entry and a PID. A long-running parent that never waits for its children can eventually use up the table.

## q_2: a pid manager

**Task:** implement the part of the operating system that hands out process identifiers in the range 300 to 5000 and takes them back:

| Function | Does | Returns |
| --- | --- | --- |
| `int allocate_map(void)` | Creates the data structure that tracks the PIDs | 1 if successful, -1 if not |
| `int allocate_pid(void)` | Allocates a PID | the PID, or -1 if all are in use |
| `void release_pid(int pid)` | Releases a PID | nothing |

No processes are created. The program plays the role of the code that sits behind `fork()`, not of a program that calls it.

**How it works:** a bitmap in which every PID owns one bit, 0 for free and 1 for in use. The bits are packed 32 to a `uint32_t`:

```
pid_bank[0]    -> PIDs  300 ..  331
pid_bank[1]    -> PIDs  332 ..  363
...
pid_bank[146]  -> PIDs 4972 .. 5000   (3 bits left over)
```

`allocate_pid()` scans for the first 0 bit, sets it and returns the PID it stands for, so the lowest free PID is always handed out first. `release_pid()` clears the bit.

### Running it

```sh
make            # from the repository root, or: gcc -Wall -Wextra -o q_2 q_2.c
./q_2
```

### Output

![Output of q_2: 15 PIDs allocated, two released, and the lower one allocated again first](images/q_2.png)

| Lines | What they show |
| --- | --- |
| `No PID bitmap allocated` | `allocate_pid()` returns -1 when called before `allocate_map()`. |
| `PID bitmap already allocated!` | A second `allocate_map()` is refused instead of replacing the bitmap. |
| `300` to `314` | PIDs are handed out in increasing order from `MIN_PID`. |
| Release 306 and 313, then `306` | Released PIDs are reused, lowest first. |
| `No process associated with PID 313`, then `313` | Releasing a PID that is already free changes nothing. |

The other end of the range was checked during development with a loop of 4701 + 15 calls: the last PID handed out is 5000, and every call after it returns -1.

### Learnings

**Bitmaps**

- **Size.** 5000 - 300 + 1 = 4701 PIDs need 4701 bits, which is 147 `uint32_t` or 588 bytes. An `int` array with one element per PID would take 4701 × 4 = 18,804 bytes.
- **Rounding up.** `(n + 32 - 1) / 32` is integer division that rounds up. It gives the number of 32-bit elements needed for `n` bits.
- **Finding a bit.** For a PID: `i = pid - MIN_PID`, then `idx = i / 32` is the array element and `pos = i % 32` is the bit inside it. PID 370 is bit index 70, which is `pid_bank[2]`, bit 6.
- **The three operations**, with `mask = 1U << pos`:

  | Operation | Code |
  | --- | --- |
  | Set the bit | `word \|= mask` |
  | Clear the bit | `word &= ~mask` |
  | Test the bit | `word & mask` |

- **Clearing with `& ~`, not `^`.** `^` flips the bit, so releasing a PID that is already free would mark it as in use. `& ~mask` gives 0 either way.

**C details that caused bugs**

- **`&=` is not `&`.** Testing a bit with `word &= mask` writes the result back and wipes the other 31 bits of the word. The symptom was the output 300, 301, 300, 301: every check erased the PIDs allocated before it.
- **Unsigned for bit work.** In a signed `int`, bit 31 is the sign bit. The elements are `uint32_t` (from `<stdint.h>`) and the literal is `1U`, because `1 << pos` is evaluated as a signed `int` regardless of the type it is stored into.
- **Spare bits.** 147 × 32 = 4704 bits exist but only 4701 are PIDs. Looping over every bit in memory handed out 5001, 5002 and 5003. The PID has to be checked against `MAX_PID` before its bit is set.
- **Only 0 is false.** `if (allocate_map())` is true for -1 as well, so the failure branch could never run. The return value has to be compared explicitly: `allocate_map() > 0`.
- **Macros are pasted as text.** Without outer parentheses, `100 / BOXES` expands to `100 / (4732) / 32`. Any macro containing an operator gets wrapped in parentheses.
- **Named constants everywhere.** A `300` or `5000` written directly in a function goes wrong the day `MIN_PID` or `MAX_PID` is changed.

**Memory**

- **`calloc()` zeroes the memory**, so every PID starts free. `malloc()` gives no such guarantee.
- **`free()` what was allocated.** Before the `free` was added, valgrind reported `in use at exit: 588 bytes in 1 blocks`, exactly the bitmap.
- **Set the pointer to `NULL` after `free()`.** Otherwise it is a dangling pointer, and using it is a use-after-free. With `NULL`, the guards at the top of each function catch the mistake.
- **Guard against wrong call order.** Each function checks `pid_bank` first: allocating or releasing without a bitmap is refused, and so is creating a second bitmap, which would leak the first and forget every PID in use.

**Testing**

- **`allocate_pid()` takes no argument**, so the test does not choose PIDs. With a "lowest free PID first" strategy every result can be predicted exactly, which makes a wrong one obvious.
- **Go past the limit.** A loop of exactly 4701 calls ended at 5000 and looked correct. Both bugs at the end of the range only appeared when the loop asked for more PIDs than exist.
- **Check return values in the test.** A test that relies on a message printed inside the function stops testing anything when that message is removed.
- **Do not check what cannot fail.** Testing a bit right after clearing it adds a branch that can never be taken.

## q_3: the Collatz sequence in a child process

**Task:** take a positive integer `n` from the command line and repeatedly apply

```
n = n / 2        if n is even
n = 3 * n + 1    if n is odd
```

until `n` is 1. The Collatz conjecture states that every positive integer gets there. The sequence has to be generated by a child process created with `fork()`, the parent has to `wait()` for it, and anything that is not a positive integer has to be rejected.

**How it works:** the program first checks `argc`, so that exactly one argument was given. That argument is converted with `strtol()` and checked. Only a valid number reaches `fork()`, and the return value of `fork()` is checked for failure. The child runs the loop and prints each term as it is computed; the parent sits in `wait(NULL)` until the child has terminated, prints one line and exits.

### Running it

```sh
make            # from the repository root, or: gcc -Wall -Wextra -o q_3 q_3.c
./q_3 35
```

### Output

```
$ ./q_3 35
Entering child process.
Number n is valid, n = 35
106, 53, 160, 80, 40, 20, 10, 5, 16, 8, 4, 2, 1, 
Child process complete! Exiting...
```

The starting number is shown on the `n =` line and the line below it holds the terms that follow. The parent's line is always last, because of the `wait()`.

Invalid input is rejected before any process is created, with exit status 1:

| Command | Output | Check that caught it |
| --- | --- | --- |
| `./q_3` | `Usage: ./q_3 <positive integer>` | `argc != 2`: no argument |
| `./q_3 3 8` | `Usage: ./q_3 <positive integer>` | `argc != 2`: too many arguments |
| `./q_3 abc` | `No numbers found in the string!` | `endptr == param_str`: no digits were read |
| `./q_3 12abc` | `String is alphanumeric` | `*endptr != '\0'`: characters are left over after the number |
| `./q_3 -5` | `Number n entered must be positive!` | `number <= 0` |
| `./q_3 0` | `Number n entered must be positive!` | `number <= 0` |

A failed `fork()` was tested by limiting the number of processes the program may own:

```
$ prlimit --nproc=1 ./q_3 8
fork: Resource temporarily unavailable
```

The program exits with status 1 and the parent's "complete" line is not printed, since no child ever existed.

### Learnings

- **Why the child prints.** After `fork()` the child has its own copy of `number`. The loop changes the child's copy only; the parent's `number` still holds the starting value when `wait()` returns. A result computed in a child is invisible to the parent unless the child outputs it or the two share it through an explicit mechanism.
- **Validate before `fork()`.** Checking the argument first means a bad input never creates a child. After the fork, both processes would run the checks and the error would be printed twice.
- **`strtol()` over `atoi()`.** `atoi("abc")` returns 0 and so does `atoi("0")`, with no way to tell them apart. `strtol()` reports through `endptr` where it stopped, which separates "no number", "number followed by junk" and "a whole number".
- **A minus sign is part of a valid number.** `strtol()` converts `"-5"` without complaint, so "is it a number" and "is it positive" are two separate checks.
- **`wait()` gives the order.** Without it the scheduler decides which process runs first, and the parent's message could land in the middle of the sequence. It also collects the child's exit status, so the child does not become a zombie as in [q_1](#q_1-creating-a-zombie-process).
- **The sequence rises before it falls.** 27 takes 111 steps and climbs to 9232 before it reaches 1. The terms are held in a `long` for that reason.

**Bugs hit along the way**

- **`argv[argc - 1]` hides a missing argument.** With no argument `argc` is 1, so `argv[argc - 1]` is `argv[0]`, the program name. `./q_3` was rejected with "No numbers found", but only because `"./q_3"` happens not to be a number. It also meant `./q_3 3 8` silently used the 8.
- **`argv[1]` without a check crashes.** After switching to `argv[1]`, running `./q_3` gave a segmentation fault: `argv[1]` is then `argv[argc]`, the `NULL` pointer, and `strtol()` reads through it. The fix is `if (argc != 2)` at the top of `main`, before `argv[1]` is touched.
- **The compiler pointed at it.** `-Wall -Wextra` reported `unused parameter 'argc'` for as long as the check was missing.
- **`if` / `if` / `else` is not `if` / `else if` / `else`.** The first fork check was a separate `if (pid < 0)` followed by `if (pid == 0) ... else ...`. The `else` belongs to the second `if` only, so after a failed fork `perror()` ran and then the parent branch ran as well: `wait()` with no child, followed by "Child process complete!".
- **A failure needs a non-zero exit status.** With the `else if` in place the failure branch still fell through to `return 0`, reporting success. It now returns 1, like the validation branches.
- **Making `fork()` fail on purpose.** A failure branch that has never run is untested. `prlimit --nproc=1 ./q_3 8` (or `ulimit -u` in a subshell) makes `fork()` return -1 with `EAGAIN`, which `perror()` prints as "Resource temporarily unavailable".

## q_4: the Collatz sequence through shared memory

**Task:** the same sequence as in q_3, but printed by the **parent**. In q_3 the child had to print it, because the two processes have separate copies of the data. Here the parent creates a shared memory object, the child writes the sequence into it, and the parent outputs it after the child has finished. The steps the book lays out:

1. Establish the shared memory object (`shm_open()`, `ftruncate()`, `mmap()`).
2. Create the child process and wait for it to terminate.
3. Output the contents of shared memory.
4. Remove the shared memory object.

The background on shared memory itself, what each call does and how the page tables make it work, is in [standalone/shared_memory/](../standalone/shared_memory/README.md). This section covers what is specific to the exercise.

**How it works:**

| Order | Process | Step |
| --- | --- | --- |
| 1 | parent | Validates the argument, exactly as in q_3. |
| 2 | parent | Creates the object `"COMMON"`, sizes it to 4096 bytes and maps it. |
| 3 | parent | Stores the starting number in the first slot. |
| 4 | parent | Calls `fork()`. The child inherits the mapping. |
| 5 | child | Computes each term and stores it in the next slot. The last one stored is 1. |
| 6 | parent | `wait(&child_stat)` blocks until the child has terminated. |
| 7 | parent | `shm_unlink()` removes the name. |
| 8 | parent | Checks how the child ended. Only after a clean exit, prints the slots up to and including the 1. |

The region is used as an array of `long int`:

```
long int * shared = ptr;

 shared[0]  shared[1]  shared[2]  shared[3]  shared[4]       ...    shared[511]
+----------+----------+----------+----------+----------+----------+----------+
|    8     |    4     |    2     |    1     |    0     |   ...    |    0     |
+----------+----------+----------+----------+----------+----------+----------+
 parent     child      child      child      untouched, still zero
 byte 0     byte 8     byte 16    byte 24                           byte 4088
```

4096 bytes ÷ 8 bytes per `long` gives 512 slots, which the program computes as `MAX_NUMS = SIZE / sizeof(long int)`.

### Running it

```sh
make            # from the repository root, or: gcc -Wall -Wextra -o q_4 q_4.c
./q_4 35
```

### Output

```
$ ./q_4 35
35 106 53 160 80 40 20 10 5 16 8 4 2 1 
Exiting Parent process
```

Both lines are printed by the parent. The child prints nothing unless something goes wrong.

Invalid input is rejected before the object is created, with exit status 1. The checks are the ones from q_3, plus one for a number that does not fit in a `long`:

| Command | Output | Check that caught it |
| --- | --- | --- |
| `./q_4` or `./q_4 3 8` | `Usage: ./q_4 <positive integer>` | `argc != 2` |
| `./q_4 abc` | `No numbers found in the string` | `num_str == endptr` |
| `./q_4 12abc` | `String is alphanumeric` | `*endptr != '\0'` |
| `./q_4 -5` | `Number must be positive!` | `num <= 0` |
| `./q_4 99999999999999999999` | `strtol: Numerical result out of range` | `errno == ERANGE` |

Failures that happen after the object exists:

| Command | Output | What happened |
| --- | --- | --- |
| `./q_4 9223372036854775806` | `Overflow possible! Exiting...` then `Overflow occurred!` | The second term is odd and too large for `3 * num + 1`. The child returns 1; the parent reads that status and does not print the region. |
| `./q_4 63728127` | `Shared memory consumed entirely!` | The sequence has 950 terms and the region holds 512. The child returns 2. |
| `prlimit --nproc=1 ./q_4 8` | `Failed to fork!` | `fork()` returned -1. No child exists, so the parent unlinks and exits. |

All three exit with status 1, and after every one of them `ls /dev/shm` shows no `COMMON` left behind. The fourth branch, a child killed by a signal, exists in the code but was not triggered in testing.

### Learnings

**Sharing across `fork()`**

- **What the child inherits.** `fork()` copies the parent's address space, mappings included. A mapping made with `MAP_SHARED` stays shared in the copy: both processes' page tables lead to the same frame. The child therefore uses the pointer it inherited and never calls `shm_open()` or `mmap()` itself.
- **What stays private.** Everything else. `num` and `i` are changed in the child and the parent's copies do not move, exactly as in q_3. Only what is written through `shared` crosses over.
- **The name is barely needed.** A name exists so that an unrelated process can find the object with `shm_open()`, as the consumer does in the standalone example. Between a parent and its child the inherited mapping is enough.

**Raw data in shared memory**

- **Writer and reader must agree on the format.** The standalone example stores text and reads it back as text. This program stores the 8 bytes of each `long` as they are and reads them back the same way. Either works; mixing them does not.
- **Giving the region a type.** `mmap()` returns a `void *`, an address with no type that cannot be dereferenced. Assigning it to a `long int *` tells the compiler what lives there: `*shared = num` stores a `long`, and `shared[i]` is the i-th one.
- **Pointer arithmetic counts elements.** `shared + 1` moves 8 bytes, one `long`. `shared + sizeof(long)` would move 8 elements, 64 bytes. On a `void *`, GCC counts in bytes, which is why the same `+= 8` means different things on the two pointers.
- **Index, do not move the pointer.** `shared[i]` leaves `shared` pointing at the start, so the reader can find the beginning without a saved copy.
- **Marking the end.** There is no `'\0'` in an array of numbers, and the child's counter `i` is private to the child. The sequence always ends in 1, so the 1 serves as the terminator and the parent reads until it sees it.
- **Looking at it from outside.** `od -t d8 /dev/shm/COMMON` prints the object as 8-byte integers, independent of the program's own printing code. (The name has to exist for that, so the `shm_unlink()` has to be commented out for the experiment.)

**Bounds**

- **Nothing guards the edge of a mapping.** Writing `shared[512]` is a write outside the region. The index is tested against `MAX_NUMS` before every write.
- **Derive the limit, do not type it.** `MAX_NUMS` comes from `SIZE`. A literal `512` in the loop would be silently wrong after a change to `SIZE`.
- **A bigger region costs nothing until it is used.** `ftruncate()` and `mmap()` only record a size and reserve addresses; frames are assigned page by page on first touch. `SIZE` could be far larger without using more RAM for a short sequence. The bound check is needed either way, since no maximum length of a Collatz sequence is known.

**Finding out how the child ended**

- **`wait(NULL)` throws the answer away.** `wait(&status)` stores how the child ended in `status`, a packed value decoded with macros from `<sys/wait.h>`:

  | Macro | Meaning |
  | --- | --- |
  | `WIFEXITED(status)` | The child ended by returning from `main` or calling `exit()` |
  | `WEXITSTATUS(status)` | The value it returned. Only valid when `WIFEXITED` is true |
  | `WIFSIGNALED(status)` | The child was killed by a signal, such as a segmentation fault |

- **`wait()` returning -1 is something else.** It means `wait()` itself failed, for example because there is no child. A child that failed is still a successful `wait()`.
- **Exit codes as a small protocol.** The child returns 0 for a complete sequence, 1 for arithmetic overflow and 2 for a full region, and the parent prints a different message for each.
- **Why it matters here.** The parent's read loop stops at a 1. If the child stopped early, there is no 1 in the region, and the loop would run past the end of the mapping. The exit status is what tells the parent that the terminator is really there.

**Cleaning up**

- **Unlink in the process that outlives the other.** The parent created the object and is still running after the child is gone, so it does the unlinking. The child has three ways to exit, and unlinking there meant one call per exit.
- **One call after `wait()`** covers the success path and all three failure branches, because every one of them comes after that line.
- **Unlinking does not take the data away.** `shm_unlink()` removes the name; the memory is freed when the last mapping is gone. The parent unlinks first and reads afterwards.
- **Every early return needs it too.** Once `shm_open()` has succeeded the object exists, so the `ftruncate()`, `mmap()` and `fork()` failure branches each unlink before returning.

**Bugs hit along the way**

- **52 for an input of 4.** The first version wrote with `sprintf(ptr, "%ld", num)` and read with `*(long int *)ptr`. `sprintf()` stores the character `'4'`, which is the byte 52, and the read interpreted that byte as a number. `35` came out as 13619: `'3'` is 0x33 and `'5'` is 0x35, and on a little-endian machine the two bytes read as 0x3533.
- **An endless row of 3s.** The loop tested and computed from `num` but stored the result in a second variable, `term`, and never updated `num`. Every pass computed 6 / 2 = 3 again and the loop could not reach 1.
- **Garbage after the 3s.** With no bound, the child filled all 512 slots and wrote on past the mapping until it was killed by a segmentation fault. Nothing reported it, because the parent called `wait(NULL)`. The parent then read past the region as well, printing whatever followed in memory until some unrelated value happened to be 1.
- **The starting number disappeared.** The parent stored it in `shared[0]` and the child's index also started at 0, overwriting it with the second term. The child now starts at 1.
- **`0 -2147483648` for 8589934592.** The parent read each `long` into an `int` and printed it with `%d`, which keeps only the low 32 bits. The first two terms, 4294967296 and 2147483648, do not fit.
- **Garbage when the child gave up.** After an overflow the child returned 1 without writing a terminating 1, and the parent printed the region regardless. This is what the status check after `wait()` fixes.
- **Objects left in `/dev/shm`.** The child unlinked at the end of its normal path only, so its early returns and a failed `fork()` each left `COMMON` behind. Moving the cleanup to the parent fixed all of them at once.
- **The compiler pointed at one more.** After `MAX_NUMS` was introduced, `-Wall -Wextra` reported it as an unused variable: the loop was still comparing against the literal `512`.
