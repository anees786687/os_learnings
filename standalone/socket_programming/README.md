# TCP Sockets

A TCP server and a TCP client on the same machine. The server waits on port 9002 and the client connects to it. The server sends one line of text, and the client prints it. This page has two parts:

1. **The sockets API**, using these two programs: what each call does, the order the calls go in, and why the server needs more steps than the client.
2. **[Writing a server and client, step by step](#writing-a-server-and-client-step-by-step)**: a guide to building a complete, robust server and client from an empty file, with a test plan and a checklist. It uses the quote-of-the-day server from chapter 3 as the worked example.

Shared memory (see [../shared_memory/](../shared_memory/README.md)) only works between processes on one machine. Sockets are the other main form of inter-process communication, and they work between processes on the same machine or on different ones.

## Programs

| File | Role | What it does |
| --- | --- | --- |
| [tcp_server.c](tcp_server.c) | Server | Creates a socket and binds it to port 9002 on every local interface. Listens, accepts one client, sends it `You have reached the server!` and exits. |
| [tcp_client.c](tcp_client.c) | Client | Creates a socket and connects it to port 9002 on this machine. Receives the message, prints it and exits. |

## Running it

```sh
make                # from the repository root
cd standalone/socket_programming
./tcp_server        # terminal 1: blocks in accept(), waiting for a client
./tcp_client        # terminal 2
```

To build by hand: `gcc -Wall -Wextra -o tcp_server tcp_server.c`, and the same for `tcp_client`.

### Output

```
$ ./tcp_client
Received data: You have reached the server!
```

The server prints nothing. It exits as soon as it has sent the message.

**Order matters.** If nothing is listening on port 9002, the kernel rejects the connection attempt and `connect()` fails:

```
$ ./tcp_client
connect: Connection refused

There was an error making a connection to the remote socket
```

The client then exits with status 1. This also happens on a second run of the client, because the server only serves one client.

## The idea

A **socket** is an endpoint for communication. A connection is defined by a pair of sockets, one at each end, and each socket is identified by an **IP address and a port**:

- The **IP address** names the machine. `INADDR_ANY` (`0.0.0.0`) on the server means "every interface of this machine".
- The **port** names one service on that machine, here `9002`. Ports below 1024 are reserved for well-known services and need root. Any free port above that works for practice.

The two sides do different jobs. This is the **client-server model**:

- The **server** is passive. It claims a known address and waits.
- The **client** is active. It knows the server's address and starts the connection.

In the kernel a socket is an open file. `socket()` returns a **file descriptor**, the same kind of small integer that `open()` and `shm_open()` return, and `close()` releases it.

## The workflow

```
        SERVER                                CLIENT

       socket()                              socket()
          |                                     |
        bind()      port 9002 claimed           |
          |                                     |
       listen()     ready for clients           |
          |                                     |
       accept()  <------ handshake --------  connect()
          |          (blocks until a            |
          |           client arrives)           |
        send()   -------- message -------->   recv()
          |                                     |
       close()                               close()
```

The client makes 3 calls before it has data and the server makes 4. The extra server steps exist because the server has to be findable **before** anyone tries to connect: it needs a fixed address (`bind`), it has to declare that it accepts connections (`listen`), and it has to take one (`accept`). The client needs none of these. It does not need a fixed address because nobody connects to it, so `connect()` gives it a free temporary port automatically.

### 1. `socket()`: create the endpoint (both sides)

```c
int s = socket(AF_INET, SOCK_STREAM, 0);
```

| Argument | Value | Meaning |
| --- | --- | --- |
| domain | `AF_INET` | IPv4 addresses (`AF_INET6` for IPv6, `AF_UNIX` for sockets that are files on the same machine) |
| type | `SOCK_STREAM` | Reliable, ordered, connection-based byte stream: **TCP**. `SOCK_DGRAM` would be UDP: separate datagrams, no connection, no delivery guarantee |
| protocol | `0` | The default protocol for that domain and type, which is TCP here |

Returns a file descriptor, or `-1` on failure. The new socket has no address and no connection yet.

### 2. Filling in the address: `struct sockaddr_in`

Both programs describe the server's address the same way:

```c
struct sockaddr_in server_address;
server_address.sin_family      = AF_INET;      // must match the socket
server_address.sin_port        = htons(9002);  // port, in network byte order
server_address.sin_addr.s_addr = INADDR_ANY;   // IP address, 0.0.0.0
```

**Why `htons()`.** A port is a 16-bit number, made of two bytes. Machines disagree on the order to store those bytes in:

- **Big-endian** stores the most significant byte first. This is the **network byte order**, which every protocol header uses.
- **Little-endian** stores the least significant byte first. This is what x86 and most ARM machines do.

`htons` means *host to network short*. It puts the bytes of a 16-bit value into network order: it swaps them on a little-endian host and does nothing on a big-endian one. 9002 is `0x232A`. Without `htons`, an x86 machine would send the bytes `2A 23`, and the kernel would read them as port `0x2A23` = 10787.

| Function | Converts |
| --- | --- |
| `htons()` / `ntohs()` | 16-bit values (ports), host ↔ network |
| `htonl()` / `ntohl()` | 32-bit values (IPv4 addresses), host ↔ network |

`INADDR_ANY` is 0, and 0 looks the same in either byte order, so the code works without `htonl()`. A real address such as `127.0.0.1` would have to be converted, normally with `inet_pton()` from `<arpa/inet.h>`.

**Why the cast `(struct sockaddr *)`.** `bind()`, `connect()` and `accept()` were written to work with every address family. So they take a pointer to the generic `struct sockaddr`, plus a length. The program fills in the IPv4-specific `struct sockaddr_in` and casts its address. The kernel reads `sin_family` to find out what kind of address it really is.

### 3. `bind()`: claim the address (server only)

```c
bind(server_socket, (struct sockaddr *)&server_address, sizeof(server_address));
```

`bind()` attaches the socket to IP `0.0.0.0` and port 9002. From then on, the kernel delivers connections to port 9002 to this socket. Only one socket can be bound to a given address and port. If the port is taken, `bind()` returns `-1` and sets `errno` to `EADDRINUSE`.

### 4. `listen()`: become passive (server only)

```c
listen(server_socket, 5);
```

`listen()` turns the socket into a **listening socket**: it will accept connections but never carry data itself. The **backlog** (`5`) is the length of the queue of connections that have finished the handshake and are waiting for `accept()`.

### 5. `connect()`: start the connection (client only)

```c
int connection_status = connect(nw_socket, (struct sockaddr *)&server_address, sizeof(server_address));
```

`connect()` runs TCP's **three-way handshake** with the server: SYN, SYN-ACK, ACK. It returns `0` once the connection is established. It returns `-1` if not, and `errno` says why: `ECONNREFUSED` when nothing is listening on the port. This is the only return value the code checks, which is why a missing server gives a clean error message instead of odd behaviour.

The kernel completes the handshake by itself, and the connection waits in the server's backlog queue. So the client's `connect()` can succeed before the server's `accept()` runs.

### 6. `accept()`: take a connection (server only)

```c
struct sockaddr_in client_address;
socklen_t client_len = sizeof(client_address);
int client_socket = accept(server_socket, (struct sockaddr *)&client_address, &client_len);
```

`accept()` blocks until a connection is in the queue, then removes it. It returns a **new** file descriptor:

| Descriptor | Purpose |
| --- | --- |
| `server_socket` | The listening socket. It keeps listening and could accept more clients. It never carries data. |
| `client_socket` | The connected socket for this one client. All data with this client goes through it. |

This split is what lets a server handle many clients: one listening socket, and one connected socket per client.

The last two arguments are where the kernel writes **who connected**: the client's IP address and temporary port. The length is passed as a **pointer** (`socklen_t *`) because it goes both ways. Going in, it holds the size of the buffer you provide. Coming out, the kernel overwrites it with the size of the address it actually stored. This is why passing `sizeof(client_address)` directly gave the compiler warning `makes pointer from integer without a cast`. Compare this with `bind()` and `connect()`, which only *read* the address. There the length is a plain `socklen_t` passed by value.

### 7. `send()` and `recv()`: exchange data

```c
send(client_socket, server_message, sizeof(server_message), 0);   // server
recv(nw_socket, &recv_info, sizeof(recv_info), 0);                // client
```

| Call | Arguments | Returns |
| --- | --- | --- |
| `send(sock, buf, len, flags)` | socket, data, number of bytes, flags | bytes sent, `-1` on error |
| `recv(sock, buf, len, flags)` | socket, buffer, buffer size, flags | bytes received, `0` if the other side closed, `-1` on error |

With `flags = 0` they behave like `write()` and `read()` on the same descriptor.

- The server sends on `client_socket`, **not** `server_socket`.
- The connection is **full duplex**: both sides can send and receive at the same time. Here only the server sends.
- `recv()` **blocks** until data arrives.
- `server_message` is a 256-byte array. Bytes after the text are filled with `'\0'`, and `sizeof` sends all 256, so the client gets a null-terminated string and can print it with `%s`.

**TCP is a byte stream, not a message stream.** It guarantees that bytes arrive in order, but not that one `send()` arrives as one `recv()`. A single `recv()` may return only part of what was sent. With 256 bytes on the local machine it always arrives in one piece, but larger or networked transfers have to loop on `recv()` until all the data has arrived.

### 8. `close()`: release the socket (both sides)

`close()` releases the file descriptor. On a connected socket it also starts TCP's connection teardown. When a process exits, the kernel closes all its descriptors, so the descriptors the code does not close explicitly are still released.

## Writing a server and client, step by step

The sections above explain each call. This part puts them together: it builds a complete server and client from an empty file, one step at a time, using the quote-of-the-day exercise ([chapter_3_processes/q_5](../../chapter_3_processes/q_5/)) as the worked example. Each step has the code from [qotd_server.c](../../chapter_3_processes/q_5/qotd_server.c) or [qotd_client.c](../../chapter_3_processes/q_5/qotd_client.c), what it does, and what goes wrong without it. Most of the "what goes wrong" notes are bugs that actually happened while writing these programs.

The same skeleton was reused for the haiku server ([chapter_3_processes/q_6](../../chapter_3_processes/q_6/)): only step 7, where the message comes from, changed. The [chapter 3 README](../../chapter_3_processes/README.md) has the testing and the bugs of both exercises.

### The plan

```
        SERVER                                   CLIENT

  1  socket()                              1  socket()
  2  fill in sockaddr_in (port 6017)       2  fill in sockaddr_in (port 6017)
  3  setsockopt(SO_REUSEADDR)
  4  bind()
     loop, once per client:
  5    listen()
  6    accept()  <------- handshake ----   3  connect()
  7    produce the message
  8    send()    -------- message ----->   4  recv(), print
  9    clean up: pclose(), close(client)   5  close()
 10  close(listening socket)
```

Write the server first. Without a server, the client can only get `Connection refused`.

---

### Part 1: the server

#### Step 0: headers and constants

```c
#include <stdio.h>          // popen(), pclose(), fgets(), printf(), perror()
#include <stdlib.h>
#include <unistd.h>         // close()
#include <sys/types.h>
#include <sys/socket.h>     // socket(), bind(), listen(), accept(), send()
#include <netinet/in.h>     // struct sockaddr_in, htons(), INADDR_ANY
#include <string.h>         // strlen(), strcat(), strcpy(), memset()

#define MSG_SIZE 512        // a quote must be shorter than 512 characters
#define MAX_QUOTES 3        // clients served before the server exits
```

- If a header is missing, you get `implicit declaration of function 'close'` and similar warnings. `man 2 <function>` lists the header in its SYNOPSIS.
- Put limits in named constants. Every buffer size and check then follows from one place.

#### Step 1: create the listening socket

```c
int qotd_server_socket = socket(AF_INET, SOCK_STREAM, 0);
```

`AF_INET` means IPv4, `SOCK_STREAM` means TCP, and `0` picks the default protocol. The result is a file descriptor. This socket only **accepts** connections. It never carries a message.

#### Step 2: describe the address to listen on

```c
const int qotd_port = 6017;
struct sockaddr_in qotd_server_address;
qotd_server_address.sin_family = AF_INET;
qotd_server_address.sin_port = htons(qotd_port);      // network byte order
qotd_server_address.sin_addr.s_addr = INADDR_ANY;     // every local interface
```

- Always wrap the port in `htons()`. Without it, an x86 machine listens on a different port than you asked for: 6017 becomes 33047.
- The port must be 1024 or higher. Lower ports need root, which is why the exercise uses 6017 and not 17.

#### Step 3: allow a quick restart

```c
int opt = 1;
if (setsockopt(qotd_server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    perror("setsockopt");
    return 1;
}
```

After the server stops, its old connections stay in `TIME_WAIT` for about 60 seconds. Without this option, restarting during that time fails with `bind: Address already in use`. Set it **before** `bind()`, the call it affects.

#### Step 4: claim the port

```c
if (bind(qotd_server_socket, (struct sockaddr *)&qotd_server_address,
         sizeof(qotd_server_address)) < 0) {
    perror("bind");
    return 1;
}
```

Bind **once**, outside the loop. **Always check the return value.** If `bind()` failed and the program carried on, `listen()` would quietly give the socket a random port, and the server would wait where no client looks. This actually happened during testing.

#### Step 5: the loop, then `listen()`

```c
int i = 0;
while (i++ < MAX_QUOTES) {
    if (listen(qotd_server_socket, 5) < 0) {
        perror("listen");
        return 1;
    }
    ...
}
```

`listen()` makes the socket accept connections. The `5` is how many connections may wait in the queue. It only needs to run once, and calling it again just updates the queue size. The book's date server loops forever with `while (1)`. This one stops after `MAX_QUOTES` clients so that it ends cleanly.

**Rule for the loop body:** anything opened for one client must be closed before the next iteration, **on every path** through the loop, including `continue` and early `return`s.

#### Step 6: wait for a client

```c
struct sockaddr_in client_address;
socklen_t client_len = sizeof(client_address);
int client_socket = accept(qotd_server_socket,
                           (struct sockaddr *)&client_address, &client_len);
if (client_socket < 0) {
    perror("accept");
    return 1;
}
```

- `accept()` blocks until a client connects. It returns a **new** socket for that one client. The message goes to `client_socket`, never to `qotd_server_socket`.
- The length is passed as a **pointer** (`&client_len`) because the kernel writes the real address size back into it. Passing `sizeof(...)` directly gives `makes pointer from integer without a cast`.
- **Initialise `client_len` to `sizeof(client_address)`, inside the loop, before every `accept()`.** `accept()` reads it first ("how big is the buffer?") and then overwrites it with the real size. Left uninitialised, it holds whatever was on the stack: the program can work for days and then fail with `accept: Invalid argument` after an unrelated edit. That is what happened in the haiku server.

#### Step 7: produce the message

This is the only step that's specific to QOTD. Here the quote comes from running `fortune`:

```c
fp = popen("fortune", "r");             // fortune's stdout, readable as a FILE *
if (fp == NULL) {
    printf("Failed to run command!");
    return 1;
}

memset(buffer, 0, sizeof(buffer));      // start every client with empty buffers
memset(msg, 0, sizeof(msg));

while (fgets(buffer, sizeof(buffer), fp) != NULL) {
    if (strlen(buffer) + strlen(msg) < MSG_SIZE - 1) {
        strcat(msg, buffer);            // append this piece of the quote
    } else {
        overflow_flag = 1;              // quote too long for the limit
        break;
    }
}
```

- **Clear `msg` at the top of the loop**, before it's used. Code at the bottom can be skipped by a `continue`, and then the next quote is appended to the old one.
- **Check the length before every `strcat()`.** `strcat()` doesn't know the size of `msg`, and writing past the end is a buffer overflow.
- `popen()` starts a child process. Every `popen()` needs a `pclose()` (step 9), or the child stays a **zombie**.

For a different server, replace this step only. For example, you could pick a string from an array or read one from a file.

#### Step 8: send the message

```c
if (overflow_flag) {                                // the error path
    overflow_flag = 0;
    memset(msg, 0, sizeof(msg));
    strcpy(msg, "Error! Quote too long, try again!...");   // not msg = "..."
    if (send(client_socket, msg, sizeof(msg), 0) < 0) { ... }
    pclose(fp);
    close(client_socket);
    continue;
}

ssize_t send_conf = send(client_socket, msg, sizeof(msg), 0);   // the normal path
if (send_conf < 0) {
    perror("send");
    pclose(fp);
    return 1;
}
```

- The client should **always** get a reply. If it gets none, it sits in `recv()` forever.
- To put text into an array, use `strcpy()`. `msg = "..."` doesn't compile, and `*msg = "..."` stores part of an address in one `char`.
- `sizeof(msg)` sends all 512 bytes, so the `'\0'` padding after the text goes too. That's what tells the client where the quote ends.

#### Step 9: clean up after this client

```c
    pclose(fp);              // collect the fortune child: no zombie
    close(client_socket);    // end this client's connection: no descriptor leak
}                            // end of the loop
```

Every path through the loop body must reach both calls. Check this with the tests in Part 3. Close `client_socket` here, **not** `qotd_server_socket`. Closing the listening socket inside the loop makes the next `accept()` fail with `Bad file descriptor`.

#### Step 10: shut down

```c
close(qotd_server_socket);
printf("Enough quotes for the day! Goodbye...\n");
return 0;
```

Close the listening socket once, after the loop, when no more clients will be served.

---

### Part 2: the client

The client is short. It's the same for any server that sends one reply and hangs up: only the port changes.

#### Step 1: create a socket

```c
const uint16_t QOTD_PORT = 6017;
char qotd[512];

int qotd_socket = socket(AF_INET, SOCK_STREAM, 0);
```

Same call as the server. It must be the same kind of socket, TCP.

#### Step 2: describe the server's address

```c
struct sockaddr_in qotd_address;
qotd_address.sin_family = AF_INET;
qotd_address.sin_port = htons(QOTD_PORT);
qotd_address.sin_addr.s_addr = INADDR_ANY;     // this machine (Linux)
```

The port must match the server's port. To reach another machine, use its address, converted with `inet_pton()`.

#### Step 3: connect

```c
if (connect(qotd_socket, (struct sockaddr *)&qotd_address, sizeof(qotd_address)) < 0) {
    perror("connect");
    return 1;
}
```

`connect()` performs the TCP handshake. The client doesn't call `bind()`: the kernel gives it a free temporary port. `Connection refused` means nothing is listening on that port.

#### Step 4: receive and print

```c
recv(qotd_socket, &qotd, sizeof(qotd), 0);
printf("Received message: %s", qotd);
```

`recv()` blocks until the server sends. Printing with `%s` relies on a `'\0'` being in the buffer. Here the server guarantees one by sending all 512 bytes. A server that sent only the text would not, which is question 2 in the chapter README.

#### Step 5: close

```c
close(qotd_socket);
return 0;
```

---

### Part 3: build and test

```sh
gcc -Wall -Wextra -o qotd_server qotd_server.c
gcc -Wall -Wextra -o qotd_client qotd_client.c
```

Fix every warning before running. Two of the bugs in this exercise were first reported as warnings.

| Test | How | Expected |
| --- | --- | --- |
| Normal use | `./qotd_server` in one terminal, `./qotd_client` three times in another | three quotes, then the server says goodbye |
| Any client works | `nc localhost 6017` | a quote, same as the client |
| No server | run `./qotd_client` with the server stopped | `connect: Connection refused`, exit status 1 |
| Restart | Ctrl+C the server after a client, start it again straight away | it starts (needs step 3) |
| Port taken | start a second server while one runs | `bind: Address already in use` |
| No zombies | `ps --ppid $(pgrep qotd_server) -o pid,stat,cmd` between clients | no line with `Z` |
| No leaked sockets | `ls -l /proc/$(pgrep qotd_server)/fd \| grep -c socket` between clients | `1` (only the listening socket) every time |
| Error path | make step 7 fail, e.g. a fake `fortune` earlier in `PATH` that prints a long text | the client gets the error message and does not hang |

The last three tests matter most. A server can look fine for three clients and still leak one socket and one zombie per client.

### Checklist for the next server

- [ ] Port at least 1024, wrapped in `htons()`, the same in server and client
- [ ] `setsockopt(SO_REUSEADDR)` before `bind()`
- [ ] Return value of `socket`, `setsockopt`, `bind`, `listen`, `accept`, `send` checked
- [ ] `bind()` outside the loop, `accept()` inside
- [ ] Buffers cleared at the **top** of the loop
- [ ] Message length checked before it's built or sent
- [ ] The client gets a reply on every path, even on errors
- [ ] `send()` and `close()` on `client_socket`, never on the listening socket
- [ ] One `pclose()` per `popen()`, and one `close(client_socket)` per `accept()`, on every path
- [ ] Listening socket closed once, after the loop
- [ ] Compiles with no warnings under `-Wall -Wextra`

## Function reference

| Function | Header | Side | Purpose |
| --- | --- | --- | --- |
| `socket(domain, type, protocol)` | `<sys/socket.h>` | both | Create a socket, get a file descriptor |
| `bind(sock, addr, len)` | `<sys/socket.h>` | server | Attach the socket to an IP address and port |
| `listen(sock, backlog)` | `<sys/socket.h>` | server | Accept connections, queue up to `backlog` |
| `accept(sock, addr, &len)` | `<sys/socket.h>` | server | Wait for a client, return a new connected socket |
| `connect(sock, addr, len)` | `<sys/socket.h>` | client | Connect to a server (TCP handshake) |
| `send(sock, buf, len, flags)` | `<sys/socket.h>` | both | Send bytes on a connected socket |
| `recv(sock, buf, len, flags)` | `<sys/socket.h>` | both | Receive bytes from a connected socket |
| `close(fd)` | `<unistd.h>` | both | Release the descriptor, end the connection |
| `htons(x)` | `<netinet/in.h>` | both | 16-bit host → network byte order |

`struct sockaddr_in`, `INADDR_ANY` and `AF_INET` come from `<netinet/in.h>`, `socklen_t` from `<sys/socket.h>`.

## Bugs hit along the way

**`passing argument 3 of 'accept' makes pointer from integer without a cast`.** The call passed `sizeof(client_address)`, a number, where `accept()` expects a `socklen_t *`. The kernel would have treated that number as an address and written to it. The fix is a `socklen_t` variable passed as `&client_len`. Section 6 explains why the length is a pointer.

**`implicit declaration of function 'close'`.** `close()` is declared in `<unistd.h>`, not `<sys/socket.h>`. Without the prototype, the compiler guesses how to call it. The fix is to include `<unistd.h>`. `man 2 close` lists the header in its SYNOPSIS.

**`client_len` was never given a value.** After the first fix it was declared as `socklen_t client_len;` and passed straight to `accept()`. `accept()` reads it as the size of the buffer, so it saw whatever was left on the stack. Here the leftover value happened to be acceptable, so the program worked. The same line in the haiku server worked for a while, then failed with `accept: Invalid argument` (`EINVAL`) after unrelated edits changed the stack layout. The fix, now in all three servers, is `socklen_t client_len = sizeof(client_address);` before every `accept()`.

## Questions to explore next

The programs work for the normal case. These questions point at what happens when something goes wrong. Work through them before reading the man pages for the answers.

Questions 1 to 4 and 6 are answered by the [step-by-step guide](#writing-a-server-and-client-step-by-step), which was written after these programs. Try them on `tcp_server.c` first.

1. Only `connect()`'s return value is checked. What does each of `socket()`, `bind()`, `listen()`, `accept()`, `send()` and `recv()` return on failure? Which failure would be the most confusing to debug if it went unnoticed?
2. Start `./tcp_server` twice, in two terminals. What happens to the second one, and how would the program find out?
3. Run the server, the client, and then the server again straight away. Does `bind()` still succeed? Look up the `TIME_WAIT` state and the `SO_REUSEADDR` socket option.
4. The server closes `server_socket`. Which descriptor does it never close itself, and why does the program still work?
5. If the server sent only `strlen(server_message)` bytes instead of `sizeof`, what would the client's `printf("%s")` print, and why? What should the client do with `recv()`'s return value to be safe either way?
6. How would the server change to serve clients one after another without exiting? And to serve several at once (hint: `fork()` after `accept()`)?
