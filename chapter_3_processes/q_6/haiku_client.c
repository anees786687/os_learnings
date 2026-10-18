/**
 * A haiku is a three-line poem in which the first line contains five syllables,
 * the second line contains seven syllables, and the third line contains five
 * syllables. Write a haiku server that listens to port 5575. When a client
 * connects to this port, the server responds with a haiku. The date client
 * shown in Figure 3.22 can be used to read the quotes returned by your
 * haiku server.
 *
 *
 * Haiku client: asks the server for haiku until there are none left.
 *
 * Client workflow, once per haiku:
 *     socket() -> connect() -> recv() -> print -> close()
 *
 * The server sends one haiku per connection and then closes it, and a TCP
 * socket can only be connected once, so every haiku needs a new socket and a
 * new connection. The loop stops when the server answers "No more haikus",
 * or when nothing is listening on port 5575 any more.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>         // close(), sleep()
#include <sys/types.h>
#include <sys/socket.h>     // socket(), connect(), recv()
#include <netinet/in.h>     // struct sockaddr_in, htons(), INADDR_ANY
#include <string.h>         // memset(), strcmp()
#include <errno.h>          // errno, ECONNREFUSED
#define PORT 5575
#define HAIKU_SIZE 512
int main(){
    char haiku[HAIKU_SIZE] = {0};

    // A new socket is created on every pass of the loop (see below).
    int haiku_client_socket;

    // The server's address does not change between haiku, so it is filled
    // in once: this machine (INADDR_ANY), port 5575 in network byte order.
    struct sockaddr_in haiku_client_addr;
    haiku_client_addr.sin_family = AF_INET;
    haiku_client_addr.sin_port = htons(PORT);
    haiku_client_addr.sin_addr.s_addr = INADDR_ANY;

    int server_active  = 1;
    while(server_active){
        // 1. socket(): a new socket for this haiku. The previous one was
        // closed at the end of the last pass ("Bad file descriptor" when it
        // was reused), and a connected socket cannot be connected again
        // ("Transport endpoint is already connected").
        haiku_client_socket = socket(AF_INET,SOCK_STREAM,0);
        // 2. connect(). errno is cleared first, so that the check below sees
        // only what this call set. ECONNREFUSED means nothing is listening on
        // the port: the server has exited, so the client stops too.
        errno = 0;
        int connect_stat = connect(haiku_client_socket,(struct sockaddr *)&haiku_client_addr,sizeof(haiku_client_addr));
        if(errno == ECONNREFUSED){
            printf("Server port is closed! Exiting...");
            return 0;
        }


        // 3. recv(): one haiku per connection, at most 512 bytes. The server
        // pads it with '\0', so it can be printed as a string.
        ssize_t recv_stat = recv(haiku_client_socket,&haiku,HAIKU_SIZE,0);
        if(recv_stat < 0){
            perror("recv");
            return 1;
        }
        // The server's way of saying it is done: print it and stop.
        if(strcmp(haiku,"No more haikus\n") == 0) {
            printf("%s\n", haiku);
            break;
        }
        // 4. Print it. Each haiku already ends in '\n'; the extra '\n' leaves
        // one blank line between haiku.
        printf("Received haiku: %s\n", haiku);
        memset(haiku, 0, HAIKU_SIZE);
        sleep(2);
        // 5. close(): end this connection and free the descriptor before
        // the next pass creates a new socket.
        close(haiku_client_socket);
    }

    return 0;
}
