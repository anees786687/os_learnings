/*
A haiku is a three-line poem in which the first line contains five syllables,
the second line contains seven syllables, and the third line contains five
syllables. Write a haiku server that listens to port 5575. When a client
connects to this port, the server responds with a haiku. The date client
shown in Figure 3.22 can be used to read the quotes returned by your
haiku server.


Haiku server on port 5575.

Server workflow:  socket() -> bind() -> listen() -> accept() -> send()
                                         ^                         |
                                         +-------- next client ----+

The haiku come from haikus.txt (opened relative to the current directory,
so start the server from this folder). In the file every haiku is a few
lines, and a blank line separates one haiku from the next. Each client gets
the next haiku in the file, in order. A haiku that does not fit in 512 bytes
is skipped and the client gets an error haiku instead. Once the file is used
up, the next client gets "No more haikus" and the server exits.
*/
#include <stdio.h>          // fopen(), fgets(), feof(), fclose(), perror()
#include <stdlib.h>
#include <unistd.h>         // close()
#include <sys/types.h>
#include <sys/socket.h>     // socket(), setsockopt(), bind(), listen(), accept(), send()
#include <netinet/in.h>     // struct sockaddr_in, htons(), INADDR_ANY
#include <string.h>         // strlen(), strcat(), strcpy(), memset()


#define PORT 5575
#define HAIKU_SIZE 512      // a whole haiku, '\0' included
#define BUFFER_SIZE 512     // one line of the file
int main(){
    char haiku[HAIKU_SIZE] = {0};   // the haiku being built, line by line
    char buffer[HAIKU_SIZE];        // the line fgets() just read
    // 1. socket(): the listening socket (IPv4, TCP). It only accepts
    // connections; haiku are sent on the per-client socket from accept().
    int haiku_server_socket = socket(AF_INET,SOCK_STREAM,0);

    // 2. The address to listen on: every local interface, port 5575.
    // htons() converts the port to network byte order; without it the
    // server would really listen on port 50965.
    struct sockaddr_in haiku_server_addr;
    haiku_server_addr.sin_family = AF_INET;
    haiku_server_addr.sin_port = htons(PORT);
    haiku_server_addr.sin_addr.s_addr = INADDR_ANY;

    // 3. SO_REUSEADDR: allow bind() even while connections from an earlier
    // run are still in TIME_WAIT, so the server can be restarted at once.
    // Must come before bind().
    int opt = 1;
    int setsockopt_stat = setsockopt(haiku_server_socket,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));
    if(setsockopt_stat < 0){
        perror("setsockopt");
        return 1;
    }

    // 4. bind(): claim port 5575, once, for the life of the server.
    int bind_stat = bind(haiku_server_socket,(struct sockaddr *)&haiku_server_addr,sizeof(haiku_server_addr));
    if(bind_stat < 0){
        perror("bind");
        return 1;
    }

    // 5. Open the haiku file. It stays open for the whole run: the FILE *
    // remembers how far it has been read, so each client continues where
    // the previous one stopped. perror() says why it failed, usually
    // "No such file or directory" when started from another folder.
    FILE * fp = fopen("haikus.txt", "r");
    if(fp == NULL){
        perror("fopen");
        return 1;
    }
    // Serve one client per pass, until the file is used up.
    while(1){
        // 6. listen(): accept connections, with up to 5 waiting in the queue.
        // Only needed once; calling it again just updates the queue length.

        int listen_stat = listen(haiku_server_socket, 5);
        if(listen_stat < 0){
            perror("listen");
            return 1;
        }

        // 7. accept(): wait for a client and get a new socket for it.
        // client_address_len must hold the size of client_address when the
        // call starts: accept() reads it, then overwrites it with the real
        // size. Left uninitialised it caused "accept: Invalid argument".
        struct sockaddr_in client_address;
        socklen_t client_address_len = sizeof(client_address);
        int client_socket = accept(haiku_server_socket,(struct sockaddr *)&client_address,&client_address_len);
        if(client_socket < 0){
            perror("accept");
            return 1;
        }
        // 8. The file was used up while serving the previous client: this
        // client gets "No more haikus" instead of a haiku, and the server
        // stops. This way the end is announced to a client rather than
        // discovered as "Connection refused".
        if(feof(fp)){
            strcpy(haiku,"No more haikus\n");
            ssize_t send_stat = send(client_socket,haiku,HAIKU_SIZE,0);
            if(send_stat < 0){
                perror("send");
                return 1;
            }
            close(client_socket);
            break;
        }
        // 9. Read the next haiku. fgets() returns one line; poem lines are
        // appended to haiku with strcat(), and the blank line that ends the
        // haiku triggers the send.
        while(fgets(buffer,BUFFER_SIZE,fp) != NULL){
            // Too long: only poem lines are checked (the blank line is never
            // appended). The condition decides whether strcat() may write
            // strlen(haiku) + strlen(buffer) characters plus a '\0' into
            // HAIKU_SIZE bytes (see README, q_6 question 1).
            if(buffer[0] != '\n' && strlen(buffer) + strlen(haiku) > HAIKU_SIZE){
                printf("Buffer overflow detected!\n");
                memset(buffer,0,BUFFER_SIZE);
                memset(haiku,0,HAIKU_SIZE);
                // The client still gets an answer (a haiku about the error),
                // so its recv() does not wait for nothing.
                strcpy(haiku,"Buffer overflow\nlost the haiku\nnew haiku inevitable\n");
                ssize_t send_stat = send(client_socket,&haiku,HAIKU_SIZE,0);
                if(send_stat < 0){
                    perror("send");
                    return 1;
                }
                memset(buffer,0,BUFFER_SIZE);
                memset(haiku,0,HAIKU_SIZE);
                // Skip the rest of the bad haiku. Clearing the arrays does
                // not move the file position: without this loop the next
                // client would get the leftover lines as if they were a haiku.
                while(fgets(buffer,BUFFER_SIZE,fp) != NULL)
                    if(buffer[0] == '\n' || feof(fp)) break;
                break;
            }
            if(buffer[0] != '\n') strcat(haiku,buffer);
            else{
                // Blank line: the haiku is complete. All HAIKU_SIZE bytes are
                // sent; the '\0' padding marks where the text ends.
                ssize_t send_stat = send(client_socket,haiku,HAIKU_SIZE,0);
                if(send_stat < 0){
                    perror("send");
                    return 1;
                }
                break;
            }
        }

        // 10. The last haiku in the file has no blank line after it: the
        // loop ended because fgets() hit the end of the file, with the
        // complete haiku still in haiku. Send it now.
        if(feof(fp)){
            ssize_t send_stat = send(client_socket,haiku, HAIKU_SIZE,0);
            if(send_stat < 0){
                perror("send");
                return 1;
            }
        }


        // 11. Clean up after this client: empty buffers for the next haiku,
        // and close this client's socket (once, on every path). The
        // listening socket stays open.
        memset(buffer,0,BUFFER_SIZE);
        memset(haiku,0,HAIKU_SIZE);
        close(client_socket);
    }
    // 12. Shut down: close the file and the listening socket, once.
    fclose(fp);
    close(haiku_server_socket);
    printf("No more haikus for you! Goodbye!\n");
    return 0;
}
