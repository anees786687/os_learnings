/*
TCP client: the basics of the sockets API.

Client workflow:  socket() -> connect() -> recv() -> close()

The client creates a socket, connects it to the server listening on
port 9002 of this machine, receives one message and prints it.
Start ./tcp_server first, otherwise connect() fails with
"Connection refused".
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>         // close()

#include <sys/types.h>
#include <sys/socket.h>     // socket(), connect(), recv(), struct sockaddr

// struct sockaddr_in (an IPv4 address + port), htons(), INADDR_ANY
#include <netinet/in.h>

int main(){

    // 1. socket(): ask the kernel for a new communication endpoint.
    //    AF_INET     -> address family: IPv4
    //    SOCK_STREAM -> stream socket: reliable, ordered byte stream, i.e. TCP
    //    0           -> protocol: let the kernel pick the default for the
    //                   family/type pair (TCP for AF_INET + SOCK_STREAM)
    // The return value is a file descriptor, just like the one open() or
    // shm_open() return, so it is closed with the ordinary close().
    int nw_socket;
    nw_socket = socket(AF_INET, SOCK_STREAM, 0);

    // 2. Describe the address of the server we want to reach.
    // struct sockaddr_in is the IPv4-specific address structure:
    //   sin_family      -> must match the socket's family (AF_INET)
    //   sin_port        -> port number, in network byte order
    //   sin_addr.s_addr -> IPv4 address, in network byte order
    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    // htons() = "host to network short". Networks send multi-byte numbers
    // big-endian (most significant byte first), while x86 stores them
    // little-endian. htons() swaps the two bytes of the 16-bit port when the
    // host is little-endian, and does nothing on a big-endian host.
    server_address.sin_port = htons(9002);
    // INADDR_ANY is 0.0.0.0. On a server it means "every local interface";
    // when connecting, Linux treats it as this machine, so the client reaches
    // a server on the same computer.
    server_address.sin_addr.s_addr = INADDR_ANY;

    // 3. connect(): start the TCP three-way handshake with the server.
    // connect() takes the generic struct sockaddr *, so the IPv4 structure is
    // cast to it; the size tells the kernel which kind of address it really is.
    // Returns 0 on success, -1 on failure (errno says why).
    int connection_status = connect(nw_socket,(struct sockaddr *) &server_address, sizeof(server_address));
    if(connection_status < 0){
        perror("connect");
        printf("\nThere was an error making a connection to the remote socket \n\n");
        return 1;
    }

    // 4. recv(): read bytes the server sent on the connection into a buffer.
    // It blocks until data arrives (or the server closes the connection).
    // Arguments: socket, buffer, maximum bytes to read, flags (0 = none).
    // Returns the number of bytes read, 0 if the peer closed, -1 on error.
    char recv_info[256]; // buffer for the received message
    recv(nw_socket,&recv_info,sizeof(recv_info),0);

    // The server sends its whole 256-byte array, including the '\0' after
    // the text, so the buffer can be printed as a string.
    printf("Received data: %s", recv_info);

    // 5. close(): release the descriptor and end the connection.
    close(nw_socket);

    return 0;
}
