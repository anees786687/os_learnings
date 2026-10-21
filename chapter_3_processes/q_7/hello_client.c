/*
Client for the echo server of Question 3.25 (hello_server.c, port 5575).

Usage: ./hello_client <word> [word ...]

Client workflow:  socket() -> connect() -> send() each word -> shutdown(SHUT_WR)
                                                                   |
                  close() <- recv() and print until it returns 0 <-+

Each command line argument is sent followed by a space, so
./hello_client haa hee hoo hum gets back "haa hee hoo hum ".
*/
#include <stdio.h>          // printf(), perror()
#include <stdlib.h>
#include <unistd.h>         // close()
#include <sys/types.h>
#include <sys/socket.h>     // socket(), connect(), send(), shutdown(), recv()
#include <netinet/in.h>     // struct sockaddr_in, htons(), INADDR_ANY
#include <string.h>         // strlen(), strcpy(), strcat(), memset()
#define BUFFER_SIZE 512
#define PORT 5575

int main(int argc, char ** argv){
    char buffer[512]= {0};
    // 1. socket(): the client's socket (IPv4, TCP).
    int echo_client_socket = socket(AF_INET, SOCK_STREAM,0);
    if(echo_client_socket < 0){
        perror("sokcet");
        return 1;
    }

    // 2. The server's address: this machine, port 5575.
    struct sockaddr_in echo_client_sock_addr;
    echo_client_sock_addr.sin_family = AF_INET;
    echo_client_sock_addr.sin_port = htons(PORT);
    echo_client_sock_addr.sin_addr.s_addr = INADDR_ANY;

    // 3. connect(): fails with "Connection refused" if no server is running.
    int connect_stat = connect(echo_client_socket,(struct sockaddr *)&echo_client_sock_addr,sizeof(echo_client_sock_addr));
    if(connect_stat < 0){
        perror("connect");
        return 1;
    }

    // 4. Send each word followed by a space. The word is copied into buffer
    // first: strcat(argv[i], " ") would write past the end of argv[i],
    // whose memory holds only the word and its '\0'. The arguments sit
    // next to each other ("haa\0hee\0..."), so the new '\0' would land on
    // the first letter of the next argument and turn it into "".
    for(int i = 1; i < argc; i++){
        // The word, the space and the '\0' must fit: strlen + 2 <= BUFFER_SIZE.
        // (Check at the boundary: 510 passes, 511 is rejected.)
        if(strlen(argv[i]) + 2 > BUFFER_SIZE){
            printf("Buffer overflow possible! Terminating!");
            return 1;
        }
        strcpy(buffer,argv[i]);
        strcat(buffer," ");
        // Send strlen(buffer) bytes, not sizeof(buffer): only the text,
        // without the '\0' padding.
        int send_stat = send(echo_client_socket,buffer,strlen(buffer),0);
        if(send_stat < 0){
            perror("send");
            return 1;
        }
        // clear the buffer
        memset(buffer,0,BUFFER_SIZE);
    }
    // 5. shutdown(SHUT_WR): "done sending, still receiving". The server's
    // recv() then returns 0, it finishes echoing and closes, and the recv()
    // loop below gets its 0. Without it both sides wait for the other to
    // close first, and nothing happens until the client is killed.
    shutdown(echo_client_socket,SHUT_WR);



    // 6. Read the echo until the server closes the connection (recv()
    // returns 0). It may come back in a different number of pieces than it
    // was sent in. The '\n' in printf() also flushes stdout, which is
    // line-buffered on a terminal: text without one stays in memory.
    ssize_t recv_stat;
    while(1){

        recv_stat = recv(echo_client_socket,buffer,BUFFER_SIZE,0);
        if(recv_stat == 0)break;
        if(recv_stat < 0){
            perror("recv");
            return 1;
        }
        printf("%s\n", buffer);
        memset(buffer,0,sizeof(buffer));
    }

    close(echo_client_socket);
    return 0;
}
