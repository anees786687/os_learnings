# Chapter 3: Processes

Solutions to the programming exercises of the Processes chapter, with what each one taught.

## Programs

| File | Topic | What it shows |
| --- | --- | --- |
| [q_1.c](q_1.c) | Zombie processes | A child that has exited stays in the process table until its parent calls `wait()`. |
| [q_2.c](q_2.c) | Pid manager, bitmaps | Tracking which PIDs are in use with one bit per PID. |
| [q_3.c](q_3.c) | Collatz sequence, `fork()` + `wait()` | Work done in a child stays in the child's copy of the data, so the child has to print it. |

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
