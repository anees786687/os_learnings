#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#define BUFFER_SIZE 512
#define PORT 5576

int main(int argc, char ** argv){
    // check if program is started correctly 
    if(argc == 1){
        printf("Usage ./slow_client <message>");
        return 1;
    }
    // initialize the buffer
    char buffer[BUFFER_SIZE] = {0};

    // create the client socket
    int client_socket = socket(AF_INET,SOCK_STREAM,0);
    if(client_socket < 0){
        perror("socket");
        return 1;
    }

    struct sockaddr_in client_socket_addr;
    client_socket_addr.sin_family = AF_INET;
    client_socket_addr.sin_port = htons(PORT);
    client_socket_addr.sin_addr.s_addr = INADDR_ANY;

    int connect_status = connect(client_socket,(struct sockaddr * )&client_socket_addr, sizeof(client_socket_addr));
    if(connect_status < 0){
        perror("connect");
        return 1;
    }
    
    // loop through the argv vector and send the data to the server
    for(int i = 1; i < argc; i++){
        // check if current word, null terminator
        // and space will cause buffer overflow
        if(argv[i] + 2 > BUFFER_SIZE){
            printf("Buffer overflow! Terminating client...");
            return 1;
        }
        
        // copy the word to the buffer and add an 
        //  space at the end
        strcpy(buffer, argv[i]);
        strcat(buffer, " ");

        // send the contents of the buffer
        int send_status = send(client_socket,buffer,BUFFER_SIZE,0);
        if(send_status < 0){
            perror("send");
            return 1;
        }

        // clear the buffer
        memset(buffer,0,BUFFER_SIZE);
    }

    // inform the server that there are no more tranmissions
    // the recv() in the server will return a 0 
    shutdown(client_socket,SHUT_WR);

    // receiving data from the server
    ssize_t recv_bytes = 0;
    while(1){
        recv_bytes = recv(client_socket,buffer,BUFFER_SIZE, 0);

        if(recv_bytes == 0) break;

        if(recv_bytes < 0){
            perror("recv");
            return 1;
        }

        printf("%s\n", buffer);
        memset(buffer, 0, BUFFER_SIZE);
    }
}