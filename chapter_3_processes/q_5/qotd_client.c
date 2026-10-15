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


QOTD client: the counterpart of qotd_server.c, playing the part of the
date client in Figure 3.22. It connects to port 6017 on this machine,
receives one quote and prints it.

Changing QOTD_PORT to 17 makes the same program a client for the systemd
QOTD service set up in README.md: the client only knows the protocol,
not what kind of server is behind the port.

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
    // 6017 is where qotd_server listens; 17 is the well-known QOTD port.
    const uint16_t QOTD_PORT = 6017;
    // The question limits a quote to fewer than 512 characters.
    char qotd[512];

    // 1. socket(): an IPv4 (AF_INET), TCP (SOCK_STREAM) endpoint, the same
    // kind of socket the server listens with.
    int qotd_socket = socket(AF_INET, SOCK_STREAM, 0);

    // 2. The address of the QOTD server.
    // INADDR_ANY (0.0.0.0) as a destination means this machine on Linux.
    // htons() puts the port into network byte order (big-endian).
    struct sockaddr_in qotd_address;
    qotd_address.sin_family = AF_INET;
    qotd_address.sin_port = htons(QOTD_PORT);
    qotd_address.sin_addr.s_addr = INADDR_ANY;

    // 3. connect(): TCP handshake with the server on port 6017. The
    // connection waits in the server's listen() queue until its accept()
    // takes it. Fails with "Connection refused" if no server is running.
    int connection_status = connect(qotd_socket,(struct sockaddr *)&qotd_address, sizeof(qotd_address));

    if(connection_status < 0){
        perror("connect");
        return 1;
    }

    // 4. recv(): read up to 512 bytes of the quote into the buffer. It
    // blocks until the server sends. Per RFC 865 the client sends nothing;
    // the server sends one quote.
    recv(qotd_socket,&qotd,sizeof(qotd),0);
    printf("Received message: %s", qotd);

    // 5. close(): release the descriptor.
    close(qotd_socket);

    return 0;
}
