# q_7: an echo server

**Task (exercise 3.25):** write an echo server. It waits for a client with `accept()`, then loops: read data from the socket into a buffer, write the buffer back to the client. It stops only when the client has closed the connection. The data may be binary, so it must not be handled as text. (The book asks for Java and `InputStream`; this is the same server in C.)

| File | Role | What it does |
| --- | --- | --- |
| [hello_server.c](hello_server.c) | Server | Listens on port 5575. For each client it loops `recv()` → `send()` the same bytes until `recv()` returns 0, then closes that client and waits for the next one. |
| [hello_client.c](hello_client.c) | Client | Sends each command line argument followed by a space, calls `shutdown(SHUT_WR)`, then prints the echo until the server closes. |

**How it works:**

| Order | Process | Step |
| --- | --- | --- |
| 1 | server | `socket()`, `setsockopt(SO_REUSEADDR)`, `bind()` to port 5575, once. |
| 2 | server | `listen()`, then `accept()` waits for a client. |
| 3 | client | `connect()`, then one `send()` per word. |
| 4 | client | `shutdown(SHUT_WR)`: done sending, still receiving. |
| 5 | server | `recv()` up to 512 bytes, `send()` back exactly that many (looping on partial sends), repeat. |
| 6 | server | `recv()` returns 0: closes the client's socket, back to 2. |
| 7 | client | `recv()` and print until it returns 0, then `close()`. |

## Running it

```sh
make                                   # from the repository root
cd chapter_3_processes/q_7
./hello_server                         # terminal 1
./hello_client haa hee hoo hum         # terminal 2
head -c 500000000 /dev/zero | nc -N localhost 5575 | wc -c    # binary data, 500 MB
```

## Output

```
$ ./hello_client haa hee hoo hum
haa 
hee hoo hum 
$ head -c 500000000 /dev/zero | nc -N localhost 5575 | wc -c
500000000
```

The client prints a `\n` after every `recv()`. Here the four words came back in two pieces, though they were sent as four: TCP keeps no message boundaries. In other runs all four arrive in one piece.

Half a gigabyte of zero bytes went through a 512-byte buffer and came back complete.

## Is there a limit on how much the client can send?

**No.** TCP is a byte stream with no message size. The 512-byte buffer only limits how much one `recv()` takes; the loop collects the rest. What *is* limited is the kernel's per-socket buffers, and when they fill up, the sender waits rather than losing data.

### Experiment: flow control

With a `sleep(30)` after `accept()`, so the server reads nothing, `nc` sent 50 MB. During the sleep `ss -tn` showed:

| Socket | Recv-Q | Send-Q |
| --- | --- | --- |
| server's end, `127.0.0.1:5575` | 123,584 | 0 |
| client's end, `127.0.0.1:44450` | 0 | 1,727,808 |

About 1.85 MB was in the kernels and the other 48 MB had not left `head`. The chain stalled from the slow end backwards:

1. The server's **receive buffer** filled up. Its kernel advertised a receive window of 0, so the client's kernel stopped sending.
2. The client's **send buffer** filled up, so `nc`'s `write()` **blocked**, just as `read()` blocks when no data has arrived.
3. `nc` stopped reading its pipe, so `head`'s `write()` blocked too.

Nothing was dropped or overwritten. This is **backpressure**: everything slows to the speed of the slowest reader. Recv-Q and Send-Q show how many bytes are in the buffer *now*, not the buffer size; the limits are in `/proc/sys/net/ipv4/tcp_rmem` and `tcp_wmem`.

## Bugs hit along the way

**One `recv()` per client.** The first server did `recv()`, `send()`, `close()`. With 50 MB coming in it read 511 bytes and closed the socket with the rest unread. Closing a socket with unread data makes the kernel send a **reset** (RST) instead of a normal close, so the client saw `Connection reset by peer` and `wc -c` printed `0`. The fix is the loop the exercise describes.

**An empty `Data from client:`.** The server printed the data with `%s`. Bytes from `/dev/zero` are all `'\0'`, which `%s` reads as the end of the string, so it printed nothing although 511 bytes had arrived. This is why the exercise says not to use a character reader: binary data can contain any byte, including `0`.

**`send(..., sizeof(buffer), ...)`.** This always sent 512 bytes: for `hi` it sent `h`, `i` and 510 bytes of old data or zeros. The echo must send back the count `recv()` returned.

**`return 1` on a client error.** A failed `recv()` or `send()` on one client shut down the whole server. Per-client errors now `break`, and the server goes back to `accept()`. Errors from `socket`, `bind`, `listen` and `accept` still end the program.

**The partial-send loop, three attempts.** `send()` returns how many bytes it actually sent, which can be fewer than asked (for example when a signal arrives during the call).
- First attempt: `remain = bytes_recv - send_stat` was kept for the *next* `recv()`, which overwrote the unsent bytes, and then `send(buffer + remain, bytes_recv)` read past the end of the buffer.
- Second attempt: `while(!remain)`. When everything was sent, `remain` was 0 and the loop re-sent the same buffer forever. After a partial send it stopped.
- Third attempt: right start, wrong length. `send(buffer + sent, bytes_recv)` asks for 512 bytes from position 200, which ends at byte 711 of a 512-byte buffer.

The working version: `sent = 0` for each `recv()`, loop `while (sent < bytes_recv)`, send from `buffer + sent` with length `bytes_recv - sent`, add the result to `sent`. Start and length move together and always cover exactly the unsent part.

**`break` leaves only the inner loop.** A failed `send()` broke out of the send loop, and the echo loop went on to the next `recv()` on a broken socket. A `stop_recv` flag checked right after the send loop fixes it. (`continue` would have been worse: `recv()` on the broken socket again, forever.)

**`strcat(argv[i], " ")` corrupted the other arguments.** `./hello_client haa hee hoo hum` printed `haa`, an empty word, `hoo`, an empty word. The arguments are packed next to each other:

```
before:  h a a \0 h e e \0 h o o \0 h u m \0
after:   h a a ␣  \0 e e \0 ...       argv[2] now points at "\0ee"
```

`argv[1]` owns only 4 bytes. `strcat()` does not check space, so the space overwrote the `'\0'` and the new `'\0'` landed on `argv[2]`. The word is now copied into `buffer` first. (`strcpy(buffer, " ")` was tried on the way: it *replaces* the buffer, it does not append.)

**An off-by-two length check.** The word, the space and the `'\0'` must fit, so the condition to reject is `strlen(argv[i]) + 2 > BUFFER_SIZE`. `> BUFFER_SIZE + 2` let 511 characters through, which needs 513 bytes. Test with 510 (must pass) and 511 (must fail).

**Deadlock: the echo appeared only after Ctrl+C.** After sending, the client waited for `recv()` to return 0 (the server closes), and the server waited for `recv()` to return 0 (the client closes). Neither moved. Killing the client closed its socket, which released the server. `shutdown(SHUT_WR)` tells the server "no more data" while keeping the socket open for reading the echo. `nc` without `-N` hangs in the same way, for the same reason.

**Output that did not appear.** `printf("%s", buffer)` without `\n` stays in `stdout`'s buffer, which on a terminal is flushed on a newline, `fflush(stdout)` or a normal exit, and is lost if the process is killed.

**Testing an old binary.** A 500 MB test passed once against an executable that was older than the source (`ls -l` shows the times). Save, compile, *then* run.

## Learnings

- **TCP is a byte stream.** One `send()` may arrive as several `recv()`s, and several sends as one. A protocol that needs message boundaries has to add them itself (a delimiter or a length).
- **The buffer limits one call, not the message.** Anything that reads from a socket needs a loop that ends on `recv() == 0`.
- **`recv()` returns three kinds of answer:** `> 0` bytes received, `0` the peer has finished sending, `-1` an error.
- **`send()` may send less than asked.** Loop until everything is sent, moving the start pointer and shrinking the length.
- **Use the count, not the size or `strlen()`.** `sizeof(buffer)` sends stale bytes; `strlen()` and `%s` stop at the first `'\0'`, so they break on binary data.
- **Flow control and backpressure.** A full buffer blocks the writer; TCP never drops or overwrites data that was accepted.
- **Close cleanly.** `shutdown(SHUT_WR)` ends the sending direction only. `close()` with unread data resets the connection.
- **One client's error is not the server's error.** `break` out of the client loop; `return` only when the server itself cannot run.
- **C does not check bounds.** `strcpy`, `strcat` and `send` write or read exactly where they are told. Check sizes before the call, and test checks at the exact boundary.
- **Trace a loop by hand with small numbers** ("512 received, 200 sent") before running it. Each partial-send bug showed up in one such trace.

## Questions to explore next

1. The client's `recv()` can fill all 512 bytes of `buffer`, leaving no `'\0'`. What does `printf("%s\n", buffer)` then read? How could the client print exactly `recv_stat` bytes instead (`fwrite()`)?
2. The client prints a `\n` after every piece, so the output depends on how TCP split the data. How would you print the echo exactly as it was sent?
3. The client's `send()` has no partial-send loop. Is it needed there too? Could the server's loop be moved into a `send_all()` function both programs use?
4. The server handles one client at a time. Start a client that sends slowly (`nc localhost 5575` and type), then a second client. What happens to the second one, and how would `fork()` after `accept()` change that?
5. `shutdown()`'s return value is not checked. When could it fail?
