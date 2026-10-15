/*
TCP server: sends one message to the TCP client (tcp_client.c).

Server workflow:  socket() -> bind() -> listen() -> accept() -> send() -> close()

The server opens a socket, attaches it to port 9002 on every local
interface, waits for one client, sends it a greeting and exits.
Run it first, then run ./tcp_client from another terminal.
*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>         // close()
#include <sys/types.h>
#include <sys/socket.h>     // socket(), bind(), listen(), accept(), send()

#include <netinet/in.h>     // struct sockaddr_in, htons(), INADDR_ANY


int main(){
    // The message for the client. The array is 256 bytes and the rest of
    // it is filled with '\0', so the text arrives null-terminated.
    char server_message[256] = "You have reached the server!\n";

    // 1. socket(): same call as in the client, an IPv4 TCP socket.
    // This is the "listening" socket: it is only used to accept connections,
    // never to exchange data.
    int server_socket = socket(AF_INET,SOCK_STREAM,0);

    // 2. The address the server will be reachable at.
    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(9002);          // port in network byte order
    server_address.sin_addr.s_addr = INADDR_ANY;    // 0.0.0.0: every local interface

    // 3. bind(): attach the socket to that IP and port. Without it the
    // socket has no address and clients would have nothing to connect to.
    // (A client does not need bind(): connect() picks a free port for it.)
    bind(server_socket,(struct sockaddr *)&server_address, sizeof(server_address));

    // 4. listen(): mark the socket as passive, ready to accept connections.
    // The 5 is the backlog: how many finished handshakes may wait in the
    // queue before accept() takes them.
    listen(server_socket, 5);

    // 5. accept(): block until a client connects, then take the connection
    // off the queue. It returns a NEW file descriptor for that one client;
    // server_socket stays open to accept further clients.
    // The client's address is written into client_address. client_len is a
    // pointer because it goes both ways: in, the size of the buffer; out,
    // the size of the address the kernel actually stored.
    // The connection is full duplex: both sides can send and receive on it.
    struct sockaddr_in client_address;
    socklen_t client_len;
    int client_socket = accept(server_socket, (struct sockaddr *)&client_address,&client_len);

    // 6. send(): write the message to the client's socket, not to the
    // listening socket. Arguments: socket, data, length, flags (0 = none).
    // sizeof sends all 256 bytes of the array, the '\0' padding included.
    send(client_socket, server_message, sizeof(server_message), 0);

    // 7. close(): release the listening socket. The process exits right
    // after, and the kernel closes client_socket at that point.
    close(server_socket);
    return 0;
}
