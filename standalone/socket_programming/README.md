# TCP Sockets

A TCP server and a TCP client on the same machine. The server waits on port 9002 and the client connects to it. The server sends one line of text, and the client prints it. This page explains the sockets API the two programs use: what each call does, the order the calls go in, and why the server needs more steps than the client.

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
socklen_t client_len;
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

## Questions to explore next

The programs work for the normal case. These questions point at what happens when something goes wrong. Work through them before reading the man pages for the answers.

1. `client_len` is passed to `accept()` but never given a value. What does `man 2 accept` say `accept()` expects to find in it when the call starts? What could happen if it holds a random number?
2. Only `connect()`'s return value is checked. What does each of `socket()`, `bind()`, `listen()`, `accept()`, `send()` and `recv()` return on failure? Which failure would be the most confusing to debug if it went unnoticed?
3. Start `./tcp_server` twice, in two terminals. What happens to the second one, and how would the program find out?
4. Run the server, the client, and then the server again straight away. Does `bind()` still succeed? Look up the `TIME_WAIT` state and the `SO_REUSEADDR` socket option.
5. The server closes `server_socket`. Which descriptor does it never close itself, and why does the program still work?
6. If the server sent only `strlen(server_message)` bytes instead of `sizeof`, what would the client's `printf("%s")` print, and why? What should the client do with `recv()`'s return value to be safe either way?
7. How would the server change to serve clients one after another without exiting? And to serve several at once (hint: `fork()` after `accept()`)?
