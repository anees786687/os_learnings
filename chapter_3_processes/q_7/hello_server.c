/*
Question 3.25
An echo server echoes back whatever it receives from a client. For example,
if a client sends the server the string Hello there!, the server will respond
with Hello there! Write an echo server that waits for a client connection
using accept(). When a client connection is received, the server loops:
    - Read data from the socket into a buffer.
    - Write the contents of the buffer back to the client.
It breaks out of the loop only when it has determined that the client has
closed the connection. The data may be binary, not only text.
(The book asks for Java and InputStream; this is the same server in C.)


Echo server on port 5575.

Server workflow:  socket() -> bind() -> listen() -> accept() -> recv() -> send()
                                         ^                      ^           |
                                         |                      +- more ----+
                                         +---- client closed: close() -----+

The client can send any amount of data: TCP has no message size limit. The
512-byte buffer only limits how much one recv() takes, so the server loops
until recv() returns 0. Binary data is safe because the server never treats
the buffer as a string: it sends back exactly the number of bytes received.
*/
#include <stdio.h>          // perror()
#include <stdlib.h>
#include <unistd.h>         // close()
#include <sys/types.h>
#include <sys/socket.h>     // socket(), setsockopt(), bind(), listen(), accept(), recv(), send()
#include <netinet/in.h>     // struct sockaddr_in, htons(), INADDR_ANY
#include <string.h>         // memset()
#define PORT 5575
#define BUFFER_SIZE 512     // the most one recv() takes, not a limit on the message

int main(){
    char buffer[BUFFER_SIZE] = {0};
    // 1. socket(): the listening socket (IPv4, TCP). It only accepts
    // connections; data is echoed on the per-client socket from accept().
    int echo_server_socket = socket(AF_INET,SOCK_STREAM,0);
    if(echo_server_socket < 0){
        perror("socket");
        return 1;
    }

    // 2. The address to listen on: every local interface, port 5575,
    // converted to network byte order with htons().
    struct sockaddr_in echo_server_socket_addr;
    echo_server_socket_addr.sin_family = AF_INET;
    echo_server_socket_addr.sin_port = htons(PORT);
    echo_server_socket_addr.sin_addr.s_addr = INADDR_ANY;

    // 3. SO_REUSEADDR: allow bind() while connections from an earlier run
    // are still in TIME_WAIT, so the server can be restarted at once.
    int opt = 1;
    int setsockopt_stat = setsockopt(echo_server_socket,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));
    if(setsockopt_stat < 0){
        perror("setsockopt");
        return 1;
    }

    // 4. bind(): claim port 5575, once, for the life of the server.
    int bind_stat = bind(echo_server_socket,(struct sockaddr *)&echo_server_socket_addr,sizeof(echo_server_socket_addr));

    if(bind_stat < 0){
        perror("bind");
        return 1;
    }

    // Serve one client per pass, forever. Errors in setting up the server
    // end the program (return 1); errors with one client only end that
    // client (break), and the server goes back to accept().
    while(1){
        // 5. listen(): accept connections, with up to 5 waiting in the queue.
        int listen_stat = listen(echo_server_socket, 5);
        if(listen_stat < 0){
            perror("listen");
            return 1;
        }

        // 6. accept(): wait for a client and get a new socket for it.
        struct sockaddr_in echo_client_addr;
        socklen_t echo_client_addr_len = sizeof(echo_client_addr);

        int echo_client = accept(echo_server_socket,(struct sockaddr *)&echo_client_addr, &echo_client_addr_len);
        if(echo_client < 0){
            perror("accept");
            return 1;
        }
        ssize_t bytes_recv = 0, bytes_sent = 0;
        int stop_recv = 0; // flag to stop receiving if any error has occurred while sending

        // 7. The echo loop: one pass per recv(). A single recv() is not
        // enough: it takes at most BUFFER_SIZE bytes, and TCP is a byte
        // stream, so one send() by the client may arrive as several recv()s
        // (or several sends as one). Closing the socket with data still
        // unread makes the kernel reset the connection, and the client
        // loses the echo.
        while(1){
            ssize_t sent = 0; // counter to check how many bytes have been sent
            // receive data from client in 512 bytes chunk
            bytes_recv = recv(echo_client,buffer,BUFFER_SIZE,0);

            // 0 means the client closed its side (or called
            // shutdown(SHUT_WR)): it will send nothing more. Not an error.
            if(bytes_recv == 0){
                break;
            }
            if(bytes_recv < 0){
                perror("recv");
                break;
            }
            // 8. Send back exactly bytes_recv bytes, not sizeof(buffer): the
            // rest of the buffer is old data. send() may send fewer bytes
            // than asked (e.g. interrupted by a signal), so loop until all
            // of them are out. Each pass continues where the last stopped:
            // start at buffer + sent, length bytes_recv - sent.
            while(sent < bytes_recv){
                bytes_sent = send(echo_client,buffer + sent,bytes_recv - sent,0);
                if(bytes_sent < 0){
                    perror("send");
                    stop_recv = 1;
                    break;
                }
                sent += bytes_sent;
            }
            // break above only left the send loop; leave the echo loop too,
            // before the next recv() on a broken connection.
            if(stop_recv) break;

            memset(buffer,0,sizeof(buffer));
        }


        // 9. The client is done: close its socket and wait for the next
        // one. The listening socket stays open.
        close(echo_client);


    }
    close(echo_server_socket);
    return 0;
}
