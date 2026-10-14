/*
Section 3.6.1 describes port numbers below 1024 as being well known—
that is, they provide standard services. Port 17 is known as the quote-of-the-day service. 
When a client connects to port 17 on a server, the server responds with a quote for that day.
Modify the date server shown in Figure 3.21 so that it delivers a quote
of the day rather than the current date. The quotes should be printable
ASCII characters and should contain fewer than 512 characters, although
multiple lines are allowed. Since port 17 is well known and therefore
unavailable, have your server listen to port 6017. The date client shown
in Figure 3.22 can be used to read the quotes returned by your server.


What this program does instead:
Port 17 is not left unused on this machine. A real QOTD service is set up
on it with systemd socket activation: systemd listens on port 17 and, for
every connection, runs /usr/games/fortune with its output connected to the
socket (setup steps in README.md). So the server side is provided by the
system, and this program is the client: it connects to port 17, receives
the quote and prints it.

Client workflow:  socket() -> connect() -> recv() -> close()
*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>         // close()
#include <sys/types.h>
#include <sys/socket.h>     // socket(), connect(), recv()
#include <netinet/in.h>     // struct sockaddr_in, htons(), INADDR_ANY, uint16_t

int main(){
    // Ports are 16-bit unsigned numbers (0 to 65535), hence uint16_t.
    // 17 is the well-known port of the Quote of the Day protocol (RFC 865).
    const uint16_t QOTD_PORT = 17;
    // The question limits a quote to fewer than 512 characters.
    char qotd[512];

    // 1. socket(): an IPv4 (AF_INET), TCP (SOCK_STREAM) endpoint.
    // QOTD is defined over both TCP and UDP; the systemd unit listens with
    // ListenStream, which is TCP, so a stream socket is needed here.
    int qotd_socket = socket(AF_INET, SOCK_STREAM, 0);

    // 2. The address of the QOTD server.
    // INADDR_ANY (0.0.0.0) as a destination means this machine on Linux.
    // htons() puts the port into network byte order (big-endian).
    struct sockaddr_in qotd_address;
    qotd_address.sin_family = AF_INET;
    qotd_address.sin_port = htons(QOTD_PORT);
    qotd_address.sin_addr.s_addr = INADDR_ANY;

    // 3. connect(): TCP handshake with whatever is listening on port 17.
    // Here that is systemd. On accepting the connection it starts a fresh
    // qotd@.service instance, i.e. one fortune process, for this client.
    // Fails with "Connection refused" if qotd.socket is not running.
    int connection_status = connect(qotd_socket,(struct sockaddr *)&qotd_address, sizeof(qotd_address));

    if(connection_status < 0){
        perror("connect");
        return 1;
    }

    // 4. recv(): read up to 512 bytes of the quote into the buffer.
    // fortune writes the quote and exits, and systemd then closes the
    // connection: RFC 865 has the server send one quote and hang up, and
    // ignore anything the client sends.
    recv(qotd_socket,&qotd,sizeof(qotd),0);
    printf("Received message: %s", qotd);

    // 5. close(): release the descriptor.
    close(qotd_socket);

    return 0;
}