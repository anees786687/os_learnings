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


QOTD server on port 6017.

Server workflow:  socket() -> bind() -> listen() -> accept() -> send()
                                         ^                         |
                                         +-------- next client ----+

For each client the quote is produced by running the fortune program with
popen() and reading its output. A quote of 512 characters or more is not
sent; the client gets an error message instead. Every path through the
loop closes the client's socket and pclose()s fortune, so no descriptors
leak and no zombies are left. After MAX_QUOTES clients the server stops.
Test it with ./qotd_client or nc localhost 6017.
*/
#include <stdio.h>          // popen(), pclose(), fgets(), printf(), perror()
#include <stdlib.h>
#include <unistd.h>         // close()
#include <sys/types.h>
#include <sys/socket.h>     // socket(), bind(), listen(), accept(), send()
#include <netinet/in.h>     // struct sockaddr_in, htons(), INADDR_ANY
#include <string.h>         // strlen(), strcat(), memset()

#define MSG_SIZE 512        // a quote must be shorter than 512 characters
#define MAX_QUOTES 3        // clients served before the server exits
int main(){
    // 6017 instead of 17: ports below 1024 need root to bind.
    const int qotd_port = 6017;
    // fortune's output is read in pieces of at most 63 characters + '\0'.
    const int buffer_size = 64;
    char buffer[buffer_size];       // one piece of fortune's output
    char msg[MSG_SIZE] = {0};       // the whole quote, built up piece by piece
    FILE * fp;                      // fortune's stdout, as returned by popen()


    // 1. socket(): an IPv4 TCP socket. This is the listening socket; it is
    // never used to send data.
    int qotd_server_socket = socket(AF_INET,SOCK_STREAM,0);

    // 2. The address to listen on: every local interface, port 6017
    // (htons() converts the port to network byte order).
    struct sockaddr_in qotd_server_address;
    qotd_server_address.sin_family = AF_INET;
    qotd_server_address.sin_port = htons(qotd_port);
    qotd_server_address.sin_addr.s_addr = INADDR_ANY;

    // SO_REUSEADDR: allow bind() to port 6017 even while connections from
    // an earlier run of the server are still in TIME_WAIT (about 60 s after
    // they close). Without it, restarting the server right away fails with
    // "bind: Address already in use". It must be set before bind(), the
    // call it affects. It does not let two running servers share the port.
    // optval is a pointer to the value (1 = on), optlen its size.
    int opt = 1;
    int setsockopt_stat = setsockopt(qotd_server_socket,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));
    if(setsockopt_stat < 0){
        perror("setsockopt");
        return 1;
    }
    // 3. bind(): claim port 6017. Done once, before the loop: the port is
    // claimed for the lifetime of the server, not per client.
    // Checked: if it failed unnoticed, listen() would give the socket a
    // random port and the server would wait where no client looks.
    int bind_status =bind(qotd_server_socket,(struct sockaddr *) &qotd_server_address, sizeof(qotd_server_address));
    if(bind_status < 0){
        perror("bind");
        return 1;
    }

    int i = 0;
    int overflow_flag = 0;          // set when a quote does not fit in msg
    // The date server in the book loops forever; this one serves
    // MAX_QUOTES clients and then exits.
    while(i++ < MAX_QUOTES){
        // 4. listen(): mark the socket as passive, with a queue of 5
        // waiting connections. Calling it again on a socket that is already
        // listening only updates the backlog.
        int listen_stat = listen(qotd_server_socket, 5);
        if(listen_stat < 0){
            perror("listen");
            return 1;
        }
        // 5. accept(): block until a client connects. Returns a new socket
        // for this one client; qotd_server_socket keeps listening.
        struct sockaddr_in client_address;
        socklen_t client_len;
        int client_socket = accept(qotd_server_socket,(struct sockaddr *)&client_address,&client_len);

        if(client_socket < 0){
            perror("accept");
            return 1;
        }

        // 6. popen(): run "fortune" through the shell (/bin/sh -c fortune)
        // in a child process, and return a FILE * connected by a pipe to its
        // stdout. Internally: pipe() + fork() + exec(), as in the standalone
        // programs. "r" means this process reads what the child writes.
        // Returns NULL if the pipe or the fork fails.
        fp = popen("fortune", "r");
        if(fp == NULL){
            printf("Failed to run command!");
            return 1;
        }

        // Start every client with empty buffers. Done at the top of the
        // loop, so no path (including the overflow continue) can skip it
        // and leave the previous quote in msg.
        memset(buffer,0, sizeof(buffer));
        memset(msg, 0, sizeof(msg));

        // 7. Read the quote line by line (fgets() stops at '\n' or after 63
        // characters) and append each piece to msg, as long as the result
        // stays below MSG_SIZE - 1 characters. fgets() returns NULL at the
        // end of the output, when fortune has exited.
        while(fgets(buffer,sizeof(buffer),fp) != NULL){
            // check that the quote stays under 512 bytes, '\0' included
            if(strlen(buffer) + strlen(msg) < MSG_SIZE - 1){
                strcat(msg,buffer); // add the contents of buffer to the msg string
            }
            else{
                // the quote is too long for the limit in the question
                printf("Buffer overflow occurred while fetching QOTD! Fetching new QOTD...\n");
                overflow_flag = 1;
                break;
            }
        }

        // A quote that is too long is not sent. The client gets an error
        // message instead, so its recv() returns instead of waiting. strcpy()
        // copies the text into msg: an array cannot be assigned with =.
        // Then the same cleanup as the normal path: pclose() collects the
        // fortune child (else it stays a zombie) and close() ends this
        // client's connection, before continue goes on to the next accept().
        if(overflow_flag){
            overflow_flag = 0;
            memset(msg,0,sizeof(msg));
            strcpy(msg,"Error! Quote too long, try again!...");
            ssize_t send_conf  = send(client_socket,msg,sizeof(msg),0);
            if(send_conf < 0){
                perror("send");
                pclose(fp);
                
                return 1;
            }
            pclose(fp);
            close(client_socket);
            continue;
        }

        // 8. send(): write the quote to the client's socket. All MSG_SIZE
        // bytes of msg are sent; the '\0' bytes after the text tell the
        // client where the quote ends.
        ssize_t send_conf  = send(client_socket,msg,sizeof(msg),0);
        if(send_conf < 0){
            perror("send");
            pclose(fp);

            return 1;
        }

        
        // 9. Clean up after this client.
        // pclose(): wait for the fortune child and close the pipe. One
        // pclose() per popen(), or each finished child stays a zombie.
        // close(client_socket): end this client's connection and free its
        // descriptor. The listening socket stays open for the next client.
        pclose(fp);
        close(client_socket);
        
    }
    // All clients served: close the listening socket, so no further
    // clients can connect.
    close(qotd_server_socket);

    printf("Enough quotes for the day! Goodbye...\n");
    return 0;



}
